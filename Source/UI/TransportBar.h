#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Session/SessionManager.h"

// Top strip: audio settings, plugin scan, BPM, play/stop, save/load, and a
// status readout (device, sample rate, buffer, summed plugin latency).
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar (AudioEngine&, PluginHostManager&, SessionManager&);

    void resized() override;
    void syncFromEngine();   // after a session load

private:
    void timerCallback() override;
    void showAudioSettings();

    AudioEngine& engine;
    PluginHostManager& plugins;
    SessionManager& session;

    juce::TextButton settingsButton { "Audio Settings..." };
    juce::TextButton scanButton { "Scan Plugins" };
    juce::Slider bpmSlider;
    juce::TextButton playButton { "Play" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::Label statusLabel;
};
