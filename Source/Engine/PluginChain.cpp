#include "PluginChain.h"

namespace
{
    // Try one concrete layout: main buses as given, every aux bus disabled.
    bool tryLayout (juce::AudioPluginInstance& p,
                    const juce::AudioChannelSet& mainIn,
                    const juce::AudioChannelSet& mainOut)
    {
        auto layout = p.getBusesLayout();

        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = (i == 0 ? mainIn : juce::AudioChannelSet::disabled());
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = (i == 0 ? mainOut : juce::AudioChannelSet::disabled());

        return p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout);
    }
}

std::unique_ptr<LoadedPlugin> PluginChain::configure (std::unique_ptr<juce::AudioPluginInstance> inst,
                                                      const juce::PluginDescription& desc,
                                                      bool isInstrument,
                                                      juce::String& error)
{
    jassert (inst != nullptr);

    const auto stereo = juce::AudioChannelSet::stereo();
    const auto mono   = juce::AudioChannelSet::mono();
    const auto none   = juce::AudioChannelSet::disabled();

    const bool ok = isInstrument
        ? (tryLayout (*inst, none, stereo) || tryLayout (*inst, stereo, stereo))
        : (tryLayout (*inst, stereo, stereo) || tryLayout (*inst, mono, mono) || tryLayout (*inst, mono, stereo));

    if (! ok)
        inst->enableAllBuses();   // last resort: give the plugin its preferred layout

    const int totalIn  = inst->getTotalNumInputChannels();
    const int totalOut = inst->getTotalNumOutputChannels();

    if (totalOut <= 0)
    {
        error = desc.name + " has no audio output.";
        return nullptr;
    }
    if (totalIn > 32 || totalOut > 32)
    {
        error = desc.name + " uses an unsupported channel count ("
                + juce::String (totalIn) + " in / " + juce::String (totalOut) + " out).";
        return nullptr;
    }

    auto lp = std::make_unique<LoadedPlugin>();
    lp->description = desc;
    lp->totalIn     = totalIn;
    lp->totalOut    = totalOut;
    lp->direct      = (totalOut == 2 && (totalIn == 0 || totalIn == 2));
    lp->instance    = std::move (inst);
    lp->instance->setPlayHead (&playHead);

    if (prepared)
        preparePlugin (*lp, sampleRate, blockSize);

    return lp;
}

void PluginChain::preparePlugin (LoadedPlugin& lp, double sr, int block)
{
    if (! lp.direct)
        lp.scratch.setSize (juce::jmax (lp.totalIn, lp.totalOut, 1), block);

    lp.instance->setRateAndBufferSizeDetails (sr, block);
    lp.instance->prepareToPlay (sr, block);
}

std::unique_ptr<LoadedPlugin> PluginChain::exchangeSlot (int index, std::unique_ptr<LoadedPlugin> newPlugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    jassert (juce::isPositiveAndBelow (index, numSlots));

    {
        const juce::ScopedLock sl (lock);
        std::swap (slots[(size_t) index], newPlugin);
    }
    return newPlugin;   // the old plugin; destroyed by the caller outside the lock
}

juce::AudioPluginInstance* PluginChain::getInstance (int index) const
{
    if (! juce::isPositiveAndBelow (index, numSlots))
        return nullptr;
    auto& s = slots[(size_t) index];
    return s != nullptr ? s->instance.get() : nullptr;
}

const juce::PluginDescription* PluginChain::getDescription (int index) const
{
    if (! juce::isPositiveAndBelow (index, numSlots))
        return nullptr;
    auto& s = slots[(size_t) index];
    return s != nullptr ? &s->description : nullptr;
}

int PluginChain::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots)
        if (s != nullptr && s->instance != nullptr)
            total += s->instance->getLatencySamples();
    return total;
}

void PluginChain::prepareAll (double sr, int block)
{
    const juce::ScopedLock sl (lock);
    sampleRate = sr;
    blockSize  = block;
    for (auto& s : slots)
        if (s != nullptr)
            preparePlugin (*s, sr, block);
    prepared = true;
}

void PluginChain::releaseAll()
{
    const juce::ScopedLock sl (lock);
    prepared = false;
    for (auto& s : slots)
        if (s != nullptr && s->instance != nullptr)
            s->instance->releaseResources();
}

void PluginChain::process (juce::AudioBuffer<float>& host, juce::MidiBuffer& midi)
{
    const juce::ScopedLock sl (lock);

    host.clear();

    if (! prepared)
        return;

    if (auto* instrument = slots[0].get())
        processOne (*instrument, host, midi);

    fxMidi.clear();
    for (int i = 1; i < numSlots; ++i)
        if (auto* fx = slots[(size_t) i].get())
            processOne (*fx, host, fxMidi);
}

void PluginChain::processOne (LoadedPlugin& lp, juce::AudioBuffer<float>& host, juce::MidiBuffer& midi)
{
    auto& p = *lp.instance;
    if (p.isSuspended())
        return;

    const int n = host.getNumSamples();

    if (lp.direct)
    {
        p.processBlock (host, midi);
        return;
    }

    if (lp.scratch.getNumSamples() < n)   // shouldn't happen; keep what we have
        return;

    const int chans = lp.scratch.getNumChannels();
    juce::AudioBuffer<float> view (lp.scratch.getArrayOfWritePointers(), chans, n);
    view.clear();

    if (lp.totalIn == 1)
    {
        view.copyFrom (0, 0, host, 0, 0, n);
        view.addFrom  (0, 0, host, 1, 0, n);
        view.applyGain (0, 0, n, 0.5f);
    }
    else if (lp.totalIn >= 2)
    {
        view.copyFrom (0, 0, host, 0, 0, n);
        view.copyFrom (1, 0, host, 1, 0, n);
    }

    p.processBlock (view, midi);

    if (lp.totalOut == 1)
    {
        host.copyFrom (0, 0, view, 0, 0, n);
        host.copyFrom (1, 0, view, 0, 0, n);
    }
    else
    {
        host.copyFrom (0, 0, view, 0, 0, n);
        host.copyFrom (1, 0, view, juce::jmin (1, chans - 1), 0, n);
    }
}
