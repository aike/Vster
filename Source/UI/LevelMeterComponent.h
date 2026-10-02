#pragma once
#include <JuceHeader.h>
#include "../Engine/LevelMeterSource.h"

// Stereo peak meter: dB scale with -60 dB floor, exponential decay and a
// 1.5 s peak-hold tick. Driven by a 30 Hz timer.
class LevelMeterComponent : public juce::Component,
                            private juce::Timer
{
public:
    explicit LevelMeterComponent (LevelMeterSource& s) : source (s)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    static float dbToFraction (float db)
    {
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f));
    }

    LevelMeterSource& source;
    float displayed[2] { 0.0f, 0.0f };
    float peakHold[2] { 0.0f, 0.0f };
    juce::uint32 peakHoldTime[2] { 0, 0 };
};
