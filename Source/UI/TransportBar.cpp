#include "TransportBar.h"

TransportBar::TransportBar (AudioEngine& e, PluginHostManager& p, SessionManager& s)
    : engine (e), plugins (p), session (s)
{
    settingsButton.onClick = [this] { showAudioSettings(); };

    scanButton.onClick = [this]
    {
        if (! plugins.isScanning())
            plugins.scanDefaultPaths (nullptr);
    };

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (20.0, 300.0, 0.01);
    bpmSlider.setValue (engine.getBpm(), juce::dontSendNotification);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setNumDecimalPlacesToDisplay (2);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); };

    playButton.setClickingTogglesState (true);
    playButton.setToggleState (engine.isTransportPlaying(), juce::dontSendNotification);
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkgreen);
    playButton.onClick = [this]
    {
        engine.setPlaying (playButton.getToggleState());
        playButton.setButtonText (playButton.getToggleState() ? "Play" : "Stopped");
    };

    saveButton.onClick = [this] { session.saveInteractive(); };
    loadButton.onClick = [this] { session.loadInteractive(); };

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (13.0f));

    addAndMakeVisible (settingsButton);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (bpmSlider);
    addAndMakeVisible (playButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (statusLabel);

    startTimer (500);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (4);
    settingsButton.setBounds (area.removeFromLeft (120));
    area.removeFromLeft (4);
    scanButton.setBounds (area.removeFromLeft (100));
    area.removeFromLeft (12);
    bpmSlider.setBounds (area.removeFromLeft (130));
    area.removeFromLeft (4);
    playButton.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (12);
    saveButton.setBounds (area.removeFromLeft (60));
    area.removeFromLeft (4);
    loadButton.setBounds (area.removeFromLeft (60));
    area.removeFromLeft (8);
    statusLabel.setBounds (area);
}

void TransportBar::syncFromEngine()
{
    bpmSlider.setValue (engine.getBpm(), juce::dontSendNotification);
    playButton.setToggleState (engine.isTransportPlaying(), juce::dontSendNotification);
    playButton.setButtonText (engine.isTransportPlaying() ? "Play" : "Stopped");
}

void TransportBar::timerCallback()
{
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
        statusLabel.setText ("No audio device — open Audio Settings", juce::dontSendNotification);
    }
}

void TransportBar::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.deviceManager,
        0, 0,      // no audio inputs
        2, 2,      // stereo output
        true,      // MIDI input selection
        false,     // no MIDI output
        true,      // channels as stereo pairs
        false);
    selector->setSize (520, 480);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio / MIDI Settings";
    options.componentToCentreAround = getTopLevelComponent();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
