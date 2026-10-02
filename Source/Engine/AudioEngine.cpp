#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    // MidiMessageCollector asserts if used before reset; the real rate is set
    // again in audioDeviceAboutToStart.
    midiCollector.reset (44100.0);
    updateGain();
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise (const juce::XmlElement* savedDeviceState)
{
    deviceManager.initialise (0, 2, savedDeviceState, true);

    // First run: enable every hardware MIDI input so a connected keyboard
    // just works. After that the saved device state decides.
    if (savedDeviceState == nullptr)
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);

    // Empty identifier = receive from all enabled MIDI inputs.
    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.addAudioCallback (&callback);
    initialised = true;
}

void AudioEngine::shutdown()
{
    if (initialised)
    {
        deviceManager.removeAudioCallback (&callback);
        deviceManager.removeMidiInputDeviceCallback ({}, &midiCollector);
        initialised = false;
    }

    for (int i = 0; i < PluginChain::numSlots; ++i)
        clearSlot (i);
}

juce::String AudioEngine::setPluginInSlot (int slot,
                                           std::unique_ptr<juce::AudioPluginInstance> instance,
                                           const juce::PluginDescription& desc,
                                           const juce::MemoryBlock* initialState)
{
    jassert (juce::isPositiveAndBelow (slot, PluginChain::numSlots));

    juce::String error;
    auto loaded = chain.configure (std::move (instance), desc, slot == 0, error);
    if (loaded == nullptr)
        return error;

    if (initialState != nullptr && initialState->getSize() > 0)
        loaded->instance->setStateInformation (initialState->getData(), (int) initialState->getSize());

    missing[(size_t) slot].reset();

    auto old = chain.exchangeSlot (slot, std::move (loaded));
    if (old != nullptr && old->instance != nullptr)
    {
        old->instance->setPlayHead (nullptr);
        old->instance->releaseResources();
    }
    // `old` is destroyed here, on the message thread, outside the chain lock.
    return {};
}

void AudioEngine::clearSlot (int slot)
{
    missing[(size_t) slot].reset();

    auto old = chain.exchangeSlot (slot, nullptr);
    if (old != nullptr && old->instance != nullptr)
    {
        old->instance->setPlayHead (nullptr);
        old->instance->releaseResources();
    }
}

void AudioEngine::updateGain()
{
    const float gain = muted ? 0.0f : juce::Decibels::decibelsToGain (userGainDb, -60.0f);
    callback.setTargetGain (gain);
}
