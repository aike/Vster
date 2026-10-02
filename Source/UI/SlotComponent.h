#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"

// One chain slot: role label, plugin-name button (click = load/remove menu)
// and a button to open the plugin's editor.
class SlotComponent : public juce::Component
{
public:
    SlotComponent (int slotIndex, AudioEngine&, PluginHostManager&, PluginWindowManager&);

    void resized() override;
    void refresh();   // re-read slot state from the engine

    // Wired up by MainComponent, which owns the actual load logic.
    std::function<void (int slot, const juce::PluginDescription&)> onLoadRequested;
    std::function<void (int slot)> onClearRequested;

private:
    void showMenu();
    void browseForPlugin();
    void chooseFromFound (juce::Array<juce::PluginDescription> found);

    bool isInstrumentSlot() const { return slot == 0; }

    int slot;
    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;

    juce::Label roleLabel;
    juce::TextButton nameButton;
    juce::TextButton uiButton { "UI" };
    std::unique_ptr<juce::FileChooser> chooser;
};
