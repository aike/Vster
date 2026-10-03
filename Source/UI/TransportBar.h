#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"

// Small lamp that lights while hardware MIDI is arriving.
class MidiIndicator : public juce::Component
{
public:
    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const float d = juce::jmin (area.getHeight() - 4.0f, 10.0f);
        const auto circle = juce::Rectangle<float> (d, d)
                                .withCentre ({ area.getX() + d * 0.5f + 2.0f, area.getCentreY() });

        g.setColour (lit ? juce::Colours::limegreen : juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (circle);
        g.setColour (juce::Colours::grey);
        g.drawEllipse (circle, 1.0f);

        g.setColour (getLookAndFeel().findColour (juce::Label::textColourId));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("MIDI", area.withTrimmedLeft (d + 7.0f), juce::Justification::centredLeft);
    }

private:
    bool lit = false;
};

// Top strip: BPM, play/stop, a MIDI input lamp, and a status readout
// (device, sample rate, buffer, summed plugin latency). File and settings
// actions live in the menu bar.
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar (AudioEngine&);

    void resized() override;
    void syncFromEngine();   // after a session load

private:
    void timerCallback() override;

    AudioEngine& engine;

    juce::Slider bpmSlider;
    juce::Label clockLabel { {}, "Clock:" };
    juce::TextButton playButton { "Stopped" };
    MidiIndicator midiLamp;
    juce::TextButton muteButton { "MUTE" };
    juce::Label statusLabel;

    uint32_t lastMidiCount = 0;
    int midiHoldTicks = 0;
};
