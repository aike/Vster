#include "MainComponent.h"

MainComponent::MainComponent (AudioEngine& e, PluginHostManager& p, PluginWindowManager& w, SessionManager& s)
    : engine (e), plugins (p), windows (w), session (s)
{
    addAndMakeVisible (transport);

    for (int i = 0; i < PluginChain::numSlots; ++i)
    {
        auto slot = std::make_unique<SlotComponent> (i, engine, plugins, windows);
        slot->onLoadRequested = [this] (int index, const juce::PluginDescription& desc)
        {
            loadPluginIntoSlot (index, desc);
        };
        slot->onClearRequested = [this] (int index) { clearSlot (index); };
        addAndMakeVisible (*slot);
        slotComponents[(size_t) i] = std::move (slot);
    }

    volumeLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (volumeLabel);

    volumeSlider.setSliderStyle (juce::Slider::LinearVertical);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setValue (engine.getMasterGainDb(), juce::dontSendNotification);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.onValueChange = [this] { engine.setMasterGainDb ((float) volumeSlider.getValue()); };
    addAndMakeVisible (volumeSlider);

    addAndMakeVisible (meter);

    keyboard.setAvailableRange (24, 108);   // C1..C8
    keyboard.setOctaveForMiddleC (4);
    addAndMakeVisible (keyboard);

    session.onSessionChanged = [this] { syncControls(); };

    // Ctrl+S anywhere in the main window triggers Save; the menu bar shows
    // the shortcut because it watches this command manager.
    commandManager.registerAllCommandsForTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);
    setApplicationCommandManagerToWatch (&commandManager);

    setSize (860, 258);   // 40 transport + 128 slots/master + 90 keyboard
}

MainComponent::~MainComponent()
{
    session.onSessionChanged = nullptr;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Option" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0)
    {
        menu.addItem (menuFileNew,  "New");
        menu.addItem (menuFileLoad, "Load...");
        menu.addCommandItem (&commandManager, commandSave);   // "Save  Ctrl+S"
        menu.addItem (menuFileSaveAs, "Save As...");
    }
    else if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuOptionAudioSettings, "Audio Settings...");
        menu.addItem (menuOptionPluginManager, "Plugin Manager...");
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case menuFileNew:            session.newInteractive(); break;
        case menuFileLoad:           session.loadInteractive(); break;
        case menuFileSaveAs:         session.saveAsInteractive(); break;
        // commandSave arrives via the command manager, not here.
        case menuOptionAudioSettings: showAudioSettings(); break;
        case menuOptionPluginManager:
            if (pluginManagerWindow == nullptr)
                pluginManagerWindow = std::make_unique<PluginManagerWindow> (plugins);
            pluginManagerWindow->setVisible (true);
            pluginManagerWindow->toFront (true);
            break;
        default: break;
    }
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (commandSave);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    if (id == commandSave)
    {
        info.setInfo ("Save", "Saves the session to its file", "File", 0);
        info.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier);
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    if (info.commandID == commandSave)
    {
        session.saveInteractive();
        return true;
    }
    return false;
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.deviceManager,
        0, 0,      // no audio inputs
        2, 2,      // stereo output
        true,      // MIDI input selection
        false,     // no MIDI output
        true,      // channels as stereo pairs
        false);
    selector->setSize (520, 480);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio / MIDI Settings";
    options.componentToCentreAround = getTopLevelComponent();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transport.setBounds (area.removeFromTop (40));
    keyboard.setBounds (area.removeFromBottom (90).reduced (4, 0));
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) keyboard.getWidth() / 50.0f));

    auto master = area.removeFromRight (130).reduced (6);
    volumeLabel.setBounds (master.removeFromTop (16));
    meter.setBounds (master.removeFromRight (36));
    master.removeFromRight (6);
    volumeSlider.setBounds (master);

    // Fixed one-text-line slot rows; extra vertical space stays empty.
    auto slotsArea = area.reduced (6);
    for (auto& slot : slotComponents)
    {
        slot->setBounds (slotsArea.removeFromTop (26));
        slotsArea.removeFromTop (4);
    }
}

void MainComponent::loadPluginIntoSlot (int slot, const juce::PluginDescription& desc)
{
    if (session.isLoading())
        return;

    session.autosaveNow();   // crash insurance before instantiating a new plugin

    plugins.createInstance (desc, engine.getCurrentSampleRate(), engine.getCurrentBlockSize(),
        [this, slot, desc] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Plugin load failed", desc.name + "\n\n" + error);
                return;
            }

            windows.closeEditorFor (slot);   // old editor must die before its plugin

            // If this slot held a missing plugin's saved state and the user
            // just loaded that same plugin, restore the state.
            const juce::MemoryBlock* pendingState = nullptr;
            if (auto* m = engine.missing[(size_t) slot].get())
                if (m->uniqueId == desc.uniqueId)
                    pendingState = &m->state;

            const auto loadError = engine.setPluginInSlot (slot, std::move (instance), desc, pendingState);
            if (loadError.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Plugin load failed", loadError);
            else if (auto* loaded = engine.chain.getInstance (slot))
                windows.showEditorFor (slot, *loaded, windows.getPosition (slot));

            slotComponents[(size_t) slot]->refresh();
        });
}

void MainComponent::clearSlot (int slot)
{
    windows.closeEditorFor (slot);
    engine.clearSlot (slot);
    slotComponents[(size_t) slot]->refresh();
}

void MainComponent::syncControls()
{
    transport.syncFromEngine();   // also syncs the MUTE toggle
    volumeSlider.setValue (engine.getMasterGainDb(), juce::dontSendNotification);
    for (auto& slot : slotComponents)
        slot->refresh();
}
