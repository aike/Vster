#include "EngineCallback.h"

void EngineCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();

    work.setSize (2, juce::jmax (block, 16));
    midi.ensureSize (2048);

    playHead.setSampleRate (sampleRate);
    midiCollector.reset (sampleRate);
    chain.prepareAll (sampleRate, block);
    currentGain = targetGain.load (std::memory_order_relaxed);
}

void EngineCallback::audioDeviceStopped()
{
    chain.releaseAll();
}

void EngineCallback::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                       float* const* out, int numOut,
                                                       int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    if (numOut <= 0)
        return;

    // A block larger than the device announced shouldn't happen; keep silence.
    if (numSamples > work.getNumSamples())
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::clear (out[ch], numSamples);
        return;
    }

    midi.clear();
    midiCollector.removeNextBlockOfMessages (midi, numSamples);
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, numSamples);
    chain.process (view, midi);

    // Click-free master gain: constant-rate ramp, full scale in 20 ms.
    const float target = targetGain.load (std::memory_order_relaxed);
    if (juce::approximatelyEqual (currentGain, target))
    {
        currentGain = target;
        view.applyGain (currentGain);
    }
    else
    {
        const float maxStep = (float) (numSamples / (0.02 * sampleRate));
        const float next = currentGain + juce::jlimit (-maxStep, maxStep, target - currentGain);
        view.applyGainRamp (0, numSamples, currentGain, next);
        currentGain = next;
    }

    meter.push (view, numSamples);
    playHead.advance (numSamples);

    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::copy (out[ch], view.getReadPointer (juce::jmin (ch, 1)), numSamples);
}
