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

    // Enable every hardware MIDI input on every launch, not just the first:
    // on Windows a device's identifier changes with the USB port, so relying
    // on the saved device state silently leaves a reconnected keyboard off.
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        knownMidiInputs.add (d.identifier);
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    }

    // Hot-plug: a keyboard connected while the app is running gets enabled
    // too. Only genuinely new devices, so a device the user disabled in the
    // settings dialog stays off for the rest of the session.
    midiListConnection = juce::MidiDeviceListConnection::make ([this] { enableNewMidiInputs(); });

    // Empty identifier = receive from all enabled MIDI inputs.
    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.addMidiInputDeviceCallback ({}, &midiActivity);
    deviceManager.addAudioCallback (&callback);
    initialised = true;
}

void AudioEngine::shutdown()
{
    if (initialised)
    {
        midiListConnection = {};
        deviceManager.removeAudioCallback (&callback);
        deviceManager.removeMidiInputDeviceCallback ({}, &midiActivity);
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

void AudioEngine::enableNewMidiInputs()
{
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (! knownMidiInputs.contains (d.identifier))
        {
            knownMidiInputs.add (d.identifier);
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        }
}

void AudioEngine::updateGain()
{
    const float gain = muted ? 0.0f : juce::Decibels::decibelsToGain (userGainDb, -60.0f);
    callback.setTargetGain (gain);
}
