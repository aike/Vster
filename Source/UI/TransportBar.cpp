#include "TransportBar.h"

TransportBar::TransportBar (AudioEngine& e)
    : engine (e)
{
    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (20.0, 300.0, 0.01);
    bpmSlider.setValue (engine.getBpm(), juce::dontSendNotification);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setNumDecimalPlacesToDisplay (2);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); };

    clockLabel.setJustificationType (juce::Justification::centredRight);
    clockLabel.setFont (juce::FontOptions (13.0f));

    playButton.setClickingTogglesState (true);
    playButton.setToggleState (engine.isTransportPlaying(), juce::dontSendNotification);
    playButton.setButtonText (engine.isTransportPlaying() ? "Running" : "Stopped");
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkgreen);
    playButton.onClick = [this]
    {
        engine.setPlaying (playButton.getToggleState());
        playButton.setButtonText (playButton.getToggleState() ? "Running" : "Stopped");
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setToggleState (engine.isMuted(), juce::dontSendNotification);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
    muteButton.onClick = [this] { engine.setMuted (muteButton.getToggleState()); };

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (13.0f));

    addAndMakeVisible (bpmSlider);
    addAndMakeVisible (clockLabel);
    addAndMakeVisible (playButton);
    addAndMakeVisible (midiLamp);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (statusLabel);

    // Fast enough for a responsive MIDI lamp; the status text only repaints
    // when it actually changes.
    startTimer (100);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (4);
    bpmSlider.setBounds (area.removeFromLeft (130));
    area.removeFromLeft (8);
    clockLabel.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (2);
    playButton.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (10);
    midiLamp.setBounds (area.removeFromLeft (50));
    area.removeFromLeft (6);
    muteButton.setBounds (area.removeFromLeft (60));
    area.removeFromLeft (6);
    statusLabel.setBounds (area);
}

void TransportBar::syncFromEngine()
{
    bpmSlider.setValue (engine.getBpm(), juce::dontSendNotification);
    playButton.setToggleState (engine.isTransportPlaying(), juce::dontSendNotification);
    playButton.setButtonText (engine.isTransportPlaying() ? "Running" : "Stopped");
    muteButton.setToggleState (engine.isMuted(), juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    // Lamp: lit while messages keep arriving, held ~300 ms after the last one.
    const auto midiCount = engine.midiActivity.count.load (std::memory_order_relaxed);
    if (midiCount != lastMidiCount)
    {
        lastMidiCount = midiCount;
        midiHoldTicks = 3;
    }
    else if (midiHoldTicks > 0)
    {
        --midiHoldTicks;
    }
    midiLamp.setLit (midiHoldTicks > 0);

    juce::String text;

    if (auto* device = engine.deviceManager.getCurrentAudioDevice())
    {
        text << device->getTypeName() << ": " << device->getName()
             << "  " << juce::String (device->getCurrentSampleRate() / 1000.0, 1) << " kHz / "
             << device->getCurrentBufferSizeSamples() << " smp";

        const int pluginLatency = engine.getTotalLatencySamples();
        if (pluginLatency > 0)
            text << "  (+" << pluginLatency << " smp plugin latency)";

        statusLabel.setColour (juce::Label::textColourId,
                               findColour (juce::Label::textColourId, true));
        statusLabel.setText (text, juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
        statusLabel.setText ("No audio device - open Audio Settings", juce::dontSendNotification);
    }
}
