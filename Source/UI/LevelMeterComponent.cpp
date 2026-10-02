#include "LevelMeterComponent.h"

void LevelMeterComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = source.readAndReset (ch);
        displayed[ch] = juce::jmax (peak, displayed[ch] * 0.82f);   // ~ -170 dB/s decay at 30 Hz
        if (displayed[ch] < 1.0e-4f)
            displayed[ch] = 0.0f;

        if (peak >= peakHold[ch])
        {
            peakHold[ch] = peak;
            peakHoldTime[ch] = now;
        }
        else if (now - peakHoldTime[ch] > 1500)
        {
            peakHold[ch] *= 0.8f;
            if (peakHold[ch] < 1.0e-4f)
                peakHold[ch] = 0.0f;
        }
    }

    repaint();
}

void LevelMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (bounds, 3.0f);

    const float columnWidth = bounds.getWidth() / 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto column = juce::Rectangle<float> (bounds.getX() + columnWidth * (float) ch, bounds.getY(),
                                              columnWidth, bounds.getHeight()).reduced (2.0f);

        const float db = juce::Decibels::gainToDecibels (displayed[ch], -60.0f);
        const float fraction = dbToFraction (db);

        if (fraction > 0.0f)
        {
            auto bar = column.withTrimmedTop (column.getHeight() * (1.0f - fraction));
            g.setColour (db > -3.0f ? juce::Colours::red
                       : db > -12.0f ? juce::Colours::yellow
                                     : juce::Colours::limegreen);
            g.fillRect (bar);
        }

        const float holdDb = juce::Decibels::gainToDecibels (peakHold[ch], -60.0f);
        const float holdFraction = dbToFraction (holdDb);
        if (holdFraction > 0.0f)
        {
            g.setColour (juce::Colours::white);
            const float y = column.getBottom() - column.getHeight() * holdFraction;
            g.fillRect (column.getX(), y, column.getWidth(), 1.5f);
        }
    }
}
