#pragma once
#include <JuceHeader.h>
#include <array>
#include "PluginDpiPolicy.h"

// One editor window per slot. Windows must always be closed BEFORE their
// plugin instance is destroyed — callers do that by calling closeEditorFor()
// (or closeAll()) before touching the slot.
class PluginWindowManager
{
public:
    explicit PluginWindowManager (juce::PropertiesFile& settings);
    ~PluginWindowManager();   // defined in the .cpp where PluginWindow is complete

    void showEditorFor (int slot, juce::AudioPluginInstance&, juce::Point<int> position);
    void closeEditorFor (int slot);
    void closeAll();

    bool isOpen (int slot) const;
    juce::Point<int> getPosition (int slot) const;   // live position, or last known

    // HiDPI handling for one plugin (see PluginDpiPolicy). Changing it reopens
    // the slot's editor so the new mode is visible at once.
    EditorScaling getEditorScaling (const juce::PluginDescription&) const;
    void setEditorScaling (int slot, juce::AudioPluginInstance&, EditorScaling);

    // Called (message thread, asynchronously) when the hosting mode of a
    // slot's plugin changed: automatically because its editor turned out to
    // be cut off (`detected` true), or through setEditorScaling. The new mode
    // only takes effect once the plugin is loaded in a fresh process - a
    // plugin's module reads the display scale when it is first loaded and
    // stays loaded until the app exits - so the handler offers a restart.
    // Without a handler the window alone is reopened.
    std::function<void (int slot, bool detected)> onHostingModeChanged;

private:
    class PluginWindow;
    void reopen (int slot);

    juce::PropertiesFile& settings;
    std::array<std::unique_ptr<PluginWindow>, 4> windows;
    std::array<juce::Point<int>, 4> lastPositions { juce::Point<int> (120, 120), { 150, 150 },
                                                    { 180, 180 }, { 210, 210 } };

    JUCE_DECLARE_NON_COPYABLE (PluginWindowManager)
};
