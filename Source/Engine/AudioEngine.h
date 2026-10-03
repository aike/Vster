#pragma once
#include <JuceHeader.h>
#include "PluginChain.h"
#include "HostPlayHead.h"
#include "LevelMeterSource.h"
#include "EngineCallback.h"

// Saved state for a slot whose plugin could not be found on session load.
// Kept verbatim so the next save never loses the user's settings.
struct MissingPlugin
{
    std::unique_ptr<juce::XmlElement> descriptionXml;
    juce::MemoryBlock state;
    juce::String name;
    int uniqueId = 0;
};

// Counts incoming hardware MIDI messages so the UI can show an activity
// lamp. handleIncomingMidiMessage runs on the system MIDI thread.
struct MidiActivityMonitor : public juce::MidiInputCallback
{
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (! m.isActiveSense() && ! m.isMidiClock())
            count.fetch_add (1, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> count { 0 };
};

// Owns the audio device, the chain and everything audible. All plugin
// lifecycle methods must be called on the message thread.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    void initialise (const juce::XmlElement* savedDeviceState);
    void shutdown();   // stop audio + clear slots; safe to call twice

    // Transport / master -----------------------------------------------------
    void setBpm (double b)             { playHead.setBpm (b); }
    double getBpm() const              { return playHead.getBpm(); }
    void setPlaying (bool p)           { playHead.setPlaying (p); }
    bool isTransportPlaying() const    { return playHead.isPlaying(); }
    void setMasterGainDb (float db)    { userGainDb = db; updateGain(); }
    float getMasterGainDb() const      { return userGainDb; }
    void setMuted (bool m)             { muted = m; updateGain(); }
    bool isMuted() const               { return muted; }

    // Plugins (message thread) ------------------------------------------------
    // Takes ownership; applies initialState (if given) before the plugin is
    // audible. Returns an error message, or an empty string on success.
    juce::String setPluginInSlot (int slot,
                                  std::unique_ptr<juce::AudioPluginInstance>,
                                  const juce::PluginDescription&,
                                  const juce::MemoryBlock* initialState = nullptr);
    void clearSlot (int slot);         // also discards any missing-plugin record

    int getTotalLatencySamples() const { return chain.getTotalLatencySamples(); }
    double getCurrentSampleRate() const { return chain.isPrepared() ? chain.getSampleRate() : 44100.0; }
    int getCurrentBlockSize() const     { return chain.isPrepared() ? chain.getBlockSize() : 512; }

    juce::AudioDeviceManager deviceManager;
    HostPlayHead playHead;
    PluginChain chain { playHead };
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;
    MidiActivityMonitor midiActivity;
    LevelMeterSource meter;
    std::array<std::unique_ptr<MissingPlugin>, PluginChain::numSlots> missing;

private:
    void updateGain();
    void enableNewMidiInputs();

    EngineCallback callback { chain, playHead, midiCollector, keyboardState, meter };
    juce::MidiDeviceListConnection midiListConnection;
    juce::StringArray knownMidiInputs;
    float userGainDb = 0.0f;
    bool muted = false;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
