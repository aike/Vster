#pragma once
#include <JuceHeader.h>
#include "PluginChain.h"
#include "HostPlayHead.h"
#include "LevelMeterSource.h"

// The real-time audio callback. Everything that runs on the device thread
// lives here so it stays auditable: MIDI gathering, the plugin chain, the
// click-free master gain and the meter capture.
class EngineCallback : public juce::AudioIODeviceCallback
{
public:
    EngineCallback (PluginChain& c, HostPlayHead& ph, juce::MidiMessageCollector& mc,
                    juce::MidiKeyboardState& ks, LevelMeterSource& m)
        : chain (c), playHead (ph), midiCollector (mc), keyboardState (ks), meter (m) {}

    // Message thread; the ramp in the callback makes the change click-free.
    void setTargetGain (float linearGain)  { targetGain.store (linearGain, std::memory_order_relaxed); }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;

private:
    PluginChain& chain;
    HostPlayHead& playHead;
    juce::MidiMessageCollector& midiCollector;
    juce::MidiKeyboardState& keyboardState;
    LevelMeterSource& meter;

    juce::AudioBuffer<float> work;   // stereo render buffer, sized at device start
    juce::MidiBuffer midi;
    std::atomic<float> targetGain { 1.0f };
    float currentGain = 1.0f;        // audio-thread state
    double sampleRate = 44100.0;
};
