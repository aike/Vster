#pragma once
#include <JuceHeader.h>

// One editor window per slot. Windows must always be closed BEFORE their
// plugin instance is destroyed — callers do that by calling closeEditorFor()
// (or closeAll()) before touching the slot.
class PluginWindowManager
{
public:
    PluginWindowManager();
    ~PluginWindowManager();   // defined in the .cpp where PluginWindow is complete

    void showEditorFor (int slot, juce::AudioPluginInstance&, juce::Point<int> position);
    void closeEditorFor (int slot);
    void closeAll();

    bool isOpen (int slot) const;
    juce::Point<int> getPosition (int slot) const;   // live position, or last known

private:
    class PluginWindow;
    std::array<std::unique_ptr<PluginWindow>, 4> windows;
    std::array<juce::Point<int>, 4> lastPositions { juce::Point<int> (120, 120), { 150, 150 },
                                                    { 180, 180 }, { 210, 210 } };

    JUCE_DECLARE_NON_COPYABLE (PluginWindowManager)
};
