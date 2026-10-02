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

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
    muteButton.onClick = [this] { engine.setMuted (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    addAndMakeVisible (meter);

    keyboard.setAvailableRange (24, 108);   // C1..C8
    keyboard.setOctaveForMiddleC (4);
    addAndMakeVisible (keyboard);

    session.onSessionChanged = [this] { syncControls(); };

    setSize (860, 560);
}

MainComponent::~MainComponent()
{
    session.onSessionChanged = nullptr;
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transport.setBounds (area.removeFromTop (40));
    keyboard.setBounds (area.removeFromBottom (90).reduced (4, 0));
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) keyboard.getWidth() / 50.0f));

    auto master = area.removeFromRight (130).reduced (6);
    volumeLabel.setBounds (master.removeFromTop (18));
    muteButton.setBounds (master.removeFromBottom (32));
    master.removeFromBottom (6);
    meter.setBounds (master.removeFromRight (36));
    master.removeFromRight (6);
    volumeSlider.setBounds (master);

    auto slotsArea = area.reduced (6);
    const int slotHeight = slotsArea.getHeight() / PluginChain::numSlots;
    for (auto& slot : slotComponents)
        slot->setBounds (slotsArea.removeFromTop (slotHeight).reduced (0, 4));
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
    transport.syncFromEngine();
    volumeSlider.setValue (engine.getMasterGainDb(), juce::dontSendNotification);
    muteButton.setToggleState (engine.isMuted(), juce::dontSendNotification);
    for (auto& slot : slotComponents)
        slot->refresh();
}
