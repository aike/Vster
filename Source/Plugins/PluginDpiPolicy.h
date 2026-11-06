#pragma once
#include <JuceHeader.h>

// How a plugin's editor window is hosted on a display with >100 % scaling.
//
// A VST3 plugin that implements IPlugViewContentScaleSupport is told the
// scale and reports its size in physical pixels, so a normal DPI-aware window
// fits it. Older plugins (BIAS FX 2 for one) ignore that interface, report an
// unscaled size and then draw at the monitor's DPI on their own, so the
// DPI-aware window only shows their top-left corner. Those are hosted in a
// DPI-unaware window instead, which Windows bitmap-stretches as a whole.
enum class EditorScaling
{
    automatic,   // DPI-aware; switches to `bitmap` if the plugin turns out to draw oversized
    native,      // always DPI-aware (the plugin handles HiDPI itself)
    bitmap       // always DPI-unaware, stretched by the OS
};

// Per-plugin editor scaling preferences, persisted in the settings file and
// keyed by PluginDescription::createIdentifierString(). Message thread only.
namespace PluginDpiPolicy
{
    EditorScaling getUserChoice (juce::PropertiesFile&, const juce::PluginDescription&);
    void setUserChoice (juce::PropertiesFile&, const juce::PluginDescription&, EditorScaling);

    // Remembered outcome of the oversize check, so later windows for the
    // plugin open correctly the first time.
    bool wasDetectedOversized (juce::PropertiesFile&, const juce::PluginDescription&);
    void setDetectedOversized (juce::PropertiesFile&, const juce::PluginDescription&, bool);

    // The decision: an explicit user choice wins, otherwise the detection record.
    bool shouldHostDpiUnaware (juce::PropertiesFile&, const juce::PluginDescription&);
}
