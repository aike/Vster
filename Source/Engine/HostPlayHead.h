#pragma once
#include <JuceHeader.h>

// Host-side AudioPlayHead shared by every loaded plugin. The audio thread
// advances it once per block; the message thread sets BPM / play state.
// getPosition() is called by plugins from inside processBlock, so it must be
// allocation-free and lock-free.
class HostPlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        const double sr      = sampleRate.load (std::memory_order_relaxed);
        const double ppq     = ppqPosition.load (std::memory_order_relaxed);
        const auto   samples = timeInSamples.load (std::memory_order_relaxed);

        juce::AudioPlayHead::PositionInfo info;
        info.setBpm (bpm.load (std::memory_order_relaxed));
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (sr > 0.0 ? (double) samples / sr : 0.0);
        info.setPpqPosition (ppq);
        // Tempo-synced arps/LFOs locate bar boundaries from this; omitting it
        // makes them drift.
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        info.setBarCount ((juce::int64) std::floor (ppq / 4.0));
        info.setIsPlaying (playing.load (std::memory_order_relaxed));
        info.setIsRecording (false);
        info.setIsLooping (false);
        return info;
    }

    // Audio thread: call once per block, after processing, so plugins see the
    // position of the block that was just rendered. ppq accumulates
    // incrementally so a BPM change never makes it jump backwards.
    void advance (int numSamples)
    {
        if (! playing.load (std::memory_order_relaxed))
            return;

        const double sr = sampleRate.load (std::memory_order_relaxed);
        if (sr <= 0.0)
            return;

        timeInSamples.fetch_add (numSamples, std::memory_order_relaxed);
        const double dppq = (double) numSamples / sr * bpm.load (std::memory_order_relaxed) / 60.0;
        ppqPosition.store (ppqPosition.load (std::memory_order_relaxed) + dppq, std::memory_order_relaxed);
    }

    void setSampleRate (double sr)  { sampleRate.store (sr, std::memory_order_relaxed); }

    void setBpm (double b)          { bpm.store (juce::jlimit (20.0, 999.0, b), std::memory_order_relaxed); }
    double getBpm() const           { return bpm.load (std::memory_order_relaxed); }

    // Play restarts from bar 1 so arpeggiator phase is predictable.
    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay)
        {
            timeInSamples.store (0, std::memory_order_relaxed);
            ppqPosition.store (0.0, std::memory_order_relaxed);
        }
        playing.store (shouldPlay, std::memory_order_relaxed);
    }
    bool isPlaying() const          { return playing.load (std::memory_order_relaxed); }

private:
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> ppqPosition { 0.0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<juce::int64> timeInSamples { 0 };
    std::atomic<bool> playing { false };   // starts stopped; the user starts the clock
};
