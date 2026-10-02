#pragma once
#include <JuceHeader.h>
#include "HostPlayHead.h"

// A plugin fully configured and ready to be swapped into the chain.
struct LoadedPlugin
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    bool direct = true;          // can process straight into the host stereo buffer
    int totalIn = 0, totalOut = 2;
    juce::AudioBuffer<float> scratch;   // used when !direct, sized on the message thread
};

// Fixed serial chain: slot 0 = instrument (receives MIDI), slots 1..3 = FX.
// The audio thread only ever takes `lock` around process(); the message thread
// takes it for a pointer swap, so worst-case audio blocking is microseconds.
class PluginChain
{
public:
    static constexpr int numSlots = 4;

    explicit PluginChain (HostPlayHead& ph) : playHead (ph) {}

    // ---- message thread ----------------------------------------------------
    // Negotiates bus layouts, sets the play head, sizes the scratch buffer and
    // prepares the plugin if the device is running. Returns nullptr + error if
    // no workable layout exists.
    std::unique_ptr<LoadedPlugin> configure (std::unique_ptr<juce::AudioPluginInstance>,
                                             const juce::PluginDescription&,
                                             bool isInstrument,
                                             juce::String& error);

    // Swaps the slot contents under the lock and returns the previous plugin
    // so the caller can destroy it outside the lock, on the message thread.
    std::unique_ptr<LoadedPlugin> exchangeSlot (int index, std::unique_ptr<LoadedPlugin> newPlugin);

    juce::AudioPluginInstance* getInstance (int index) const;
    const juce::PluginDescription* getDescription (int index) const;
    int getTotalLatencySamples() const;

    // ---- device lifecycle (audio is stopped while these run) ---------------
    void prepareAll (double sampleRate, int blockSize);
    void releaseAll();
    bool isPrepared() const    { return prepared; }
    double getSampleRate() const { return sampleRate; }
    int getBlockSize() const   { return blockSize; }

    // ---- audio thread -------------------------------------------------------
    // `stereo` must have exactly 2 channels; it is cleared, the instrument
    // renders into it, then each FX processes it in place.
    void process (juce::AudioBuffer<float>& stereo, juce::MidiBuffer& midi);

private:
    void processOne (LoadedPlugin&, juce::AudioBuffer<float>& host, juce::MidiBuffer& midi);
    static void preparePlugin (LoadedPlugin&, double sampleRate, int blockSize);

    HostPlayHead& playHead;
    juce::CriticalSection lock;
    std::array<std::unique_ptr<LoadedPlugin>, numSlots> slots;
    juce::MidiBuffer fxMidi;   // always empty; FX don't receive MIDI in v1
    double sampleRate = 0.0;
    int blockSize = 0;
    bool prepared = false;
};
