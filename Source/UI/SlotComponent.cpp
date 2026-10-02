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
    auto area = getLocalBounds();
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

    if (engine.chain.getInstance (slot) != nullptr || engine.missing[(size_t) slot] != nullptr)
    {
        menu.addItem (1, "Remove plugin");
        menu.addSeparator();
    }

    juce::Array<juce::PluginDescription> filtered;
    for (const auto& d : plugins.knownPlugins.getTypes())
        if (d.isInstrument == isInstrumentSlot())
            filtered.add (d);

    if (filtered.isEmpty())
    {
        menu.addItem (2, "No plugins found - use \"Scan Plugins\"", false);
    }
    else
    {
        // Both views share the same array, so the menu IDs (base + array
        // index) resolve identically whichever submenu the pick came from.
        juce::PopupMenu byManufacturer, byCategory;
        juce::KnownPluginList::addToMenu (byManufacturer, filtered, juce::KnownPluginList::sortByManufacturer);
        juce::KnownPluginList::addToMenu (byCategory, filtered, juce::KnownPluginList::sortByCategory);
        menu.addSubMenu ("By manufacturer", byManufacturer);
        menu.addSubMenu ("By category", byCategory);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton),
        [this, filtered] (int result)
        {
            if (result == 0)
                return;
            if (result == 1) { if (onClearRequested) onClearRequested (slot); return; }

            const int index = juce::KnownPluginList::getIndexChosenByMenu (filtered, result);
            if (index >= 0 && onLoadRequested)
                onLoadRequested (slot, filtered.getReference (index));
        });
}
