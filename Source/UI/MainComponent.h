#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"
#include "../Session/SessionManager.h"
#include "TransportBar.h"
#include "SlotComponent.h"
#include "LevelMeterComponent.h"

class MainComponent : public juce::Component
{
public:
    MainComponent (AudioEngine&, PluginHostManager&, PluginWindowManager&, SessionManager&);
    ~MainComponent() override;

    void resized() override;

private:
    void loadPluginIntoSlot (int slot, const juce::PluginDescription&);
    void clearSlot (int slot);
    void syncControls();   // after a session load

    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;
    SessionManager& session;

    TransportBar transport { engine, plugins, session };
    std::array<std::unique_ptr<SlotComponent>, PluginChain::numSlots> slotComponents;

    juce::Label volumeLabel { {}, "MASTER" };
    juce::Slider volumeSlider;
    juce::TextButton muteButton { "MUTE" };
    LevelMeterComponent meter { engine.meter };
    juce::MidiKeyboardComponent keyboard { engine.keyboardState,
                                           juce::MidiKeyboardComponent::horizontalKeyboard };
};
