#include "SlotComponent.h"

SlotComponent::SlotComponent (int slotIndex, AudioEngine& e, PluginHostManager& p, PluginWindowManager& w)
    : slot (slotIndex), engine (e), plugins (p), windows (w)
{
    roleLabel.setText (slot == 0 ? "INST" : "FX " + juce::String (slot), juce::dontSendNotification);
    roleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    roleLabel.setJustificationType (juce::Justification::centred);

    nameButton.onClick = [this] { showMenu(); };

    uiButton.onClick = [this]
    {
        if (auto* instance = engine.chain.getInstance (slot))
            windows.showEditorFor (slot, *instance, windows.getPosition (slot));
    };

    addAndMakeVisible (roleLabel);
    addAndMakeVisible (nameButton);
    addAndMakeVisible (uiButton);
    refresh();
}

void SlotComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    roleLabel.setBounds (area.removeFromLeft (52));
    area.removeFromLeft (4);
    uiButton.setBounds (area.removeFromRight (44));
    area.removeFromRight (4);
    nameButton.setBounds (area);
}

void SlotComponent::refresh()
{
    if (auto* desc = engine.chain.getDescription (slot))
    {
        nameButton.setButtonText (desc->name);
        uiButton.setEnabled (true);
    }
    else if (auto* m = engine.missing[(size_t) slot].get())
    {
        nameButton.setButtonText ("[!] Missing: " + m->name);
        uiButton.setEnabled (false);
    }
    else
    {
        nameButton.setButtonText ("(empty)");
        uiButton.setEnabled (false);
    }
}

void SlotComponent::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Browse for .vst3 file...");
    if (engine.chain.getInstance (slot) != nullptr || engine.missing[(size_t) slot] != nullptr)
        menu.addItem (2, "Remove plugin");
    menu.addSeparator();

    juce::Array<juce::PluginDescription> filtered;
    for (const auto& d : plugins.knownPlugins.getTypes())
        if (d.isInstrument == isInstrumentSlot())
            filtered.add (d);

    juce::KnownPluginList::addToMenu (menu, filtered, juce::KnownPluginList::sortByManufacturer);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton),
        [this, filtered] (int result)
        {
            if (result == 0)
                return;
            if (result == 1) { browseForPlugin(); return; }
            if (result == 2) { if (onClearRequested) onClearRequested (slot); return; }

            const int index = juce::KnownPluginList::getIndexChosenByMenu (filtered, result);
            if (index >= 0 && onLoadRequested)
                onLoadRequested (slot, filtered.getReference (index));
        });
}

void SlotComponent::browseForPlugin()
{
    const juce::File defaultDir ("C:\\Program Files\\Common Files\\VST3");
    chooser = std::make_unique<juce::FileChooser> ("Select a VST3 plugin",
                                                   defaultDir.isDirectory() ? defaultDir : juce::File(),
                                                   "*.vst3");
    // .vst3 can be a single file or a bundle directory.
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;

            auto found = plugins.findTypesInFile (file);

            // Keep only types that fit this slot's role.
            for (int i = found.size(); --i >= 0;)
                if (found.getReference (i).isInstrument != isInstrumentSlot())
                    found.remove (i);

            if (found.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "No plugin found",
                    isInstrumentSlot()
                        ? "No VST3 instrument was found in that file.\n\n"
                          "Note: Vster loads 64-bit VST3 plugins only — 32-bit plugins and VST2 (.dll) are not supported, "
                          "and effect plugins cannot go into the instrument slot."
                        : "No VST3 effect was found in that file.\n\n"
                          "Note: Vster loads 64-bit VST3 plugins only — 32-bit plugins and VST2 (.dll) are not supported, "
                          "and instruments cannot go into an FX slot.");
                return;
            }

            if (found.size() == 1)
            {
                if (onLoadRequested)
                    onLoadRequested (slot, found.getReference (0));
                return;
            }

            chooseFromFound (std::move (found));
        });
}

void SlotComponent::chooseFromFound (juce::Array<juce::PluginDescription> found)
{
    juce::PopupMenu menu;
    for (int i = 0; i < found.size(); ++i)
        menu.addItem (i + 1, found.getReference (i).name);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton),
        [this, found] (int result)
        {
            if (result > 0 && onLoadRequested)
                onLoadRequested (slot, found.getReference (result - 1));
        });
}
