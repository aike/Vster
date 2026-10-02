#pragma once
#include <JuceHeader.h>

// Lock-free peak capture: the audio thread stores block peaks, the UI timer
// reads-and-resets at ~30 Hz. A lost update between reads is visually
// irrelevant, so plain relaxed atomics suffice.
struct LevelMeterSource
{
    void push (const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const int src = juce::jmin (ch, buffer.getNumChannels() - 1);
            if (src < 0)
                return;

            const float mag = buffer.getMagnitude (src, 0, numSamples);
            if (mag > peaks[ch].load (std::memory_order_relaxed))
                peaks[ch].store (mag, std::memory_order_relaxed);
        }
    }

    float readAndReset (int ch)
    {
        return peaks[ch].exchange (0.0f, std::memory_order_relaxed);
    }

    std::atomic<float> peaks[2] { 0.0f, 0.0f };
};
