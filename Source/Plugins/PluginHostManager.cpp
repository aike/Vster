#include "PluginHostManager.h"

//==============================================================================
class PluginHostManager::Scanner : public juce::Thread
{
public:
    Scanner (PluginHostManager& o, std::function<void()> finished)
        : juce::Thread ("VST3 scan"), owner (o), onFinished (std::move (finished))
    {
        // Blacklist whatever crashed a previous scan before trying again.
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (owner.knownPlugins,
                                                                           owner.getDeadMansPedalFile());
        dirScanner = std::make_unique<juce::PluginDirectoryScanner> (
            owner.knownPlugins, *owner.vst3, owner.vst3->getDefaultLocationsToSearch(),
            true, owner.getDeadMansPedalFile(), false);
        startThread();
    }

    ~Scanner() override { stopThread (10000); }

    void run() override
    {
        juce::String name;
        while (! threadShouldExit() && dirScanner->scanNextFile (true, name))
        {
            const juce::ScopedLock sl (nameLock);
            currentName = name;
        }

        juce::MessageManager::callAsync ([cb = onFinished] { if (cb) cb(); });
    }

    juce::String getCurrentName() const
    {
        const juce::ScopedLock sl (nameLock);
        return currentName;
    }

    float getProgress() const { return dirScanner->getProgress(); }

private:
    PluginHostManager& owner;
    std::function<void()> onFinished;
    std::unique_ptr<juce::PluginDirectoryScanner> dirScanner;
    mutable juce::CriticalSection nameLock;
    juce::String currentName;
};

//==============================================================================
class PluginHostManager::ScanWindow : public juce::DocumentWindow,
                                      private juce::Timer
{
public:
    explicit ScanWindow (PluginHostManager& o)
        : juce::DocumentWindow ("Scanning VST3 plugins",
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                0),
          owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new Content (o), true);
        setAlwaysOnTop (true);
        centreWithSize (460, 120);
        setVisible (true);
        startTimerHz (10);
    }

    void closeButtonPressed() override { owner.cancelScan(); }

private:
    struct Content : public juce::Component
    {
        explicit Content (PluginHostManager& o) : owner (o)
        {
            label.setText ("Scanning...", juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            cancel.setButtonText ("Cancel");
            cancel.onClick = [this] { owner.cancelScan(); };
            addAndMakeVisible (label);
            addAndMakeVisible (bar);
            addAndMakeVisible (cancel);
            setSize (460, 120);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            label.setBounds (area.removeFromTop (24));
            area.removeFromTop (6);
            bar.setBounds (area.removeFromTop (22));
            area.removeFromTop (8);
            cancel.setBounds (area.removeFromTop (26).removeFromRight (90));
        }

        PluginHostManager& owner;
        juce::Label label;
        double progress = 0.0;
        juce::ProgressBar bar { progress };
        juce::TextButton cancel;
    };

    void timerCallback() override
    {
        if (auto* s = owner.scanner.get())
            if (auto* c = dynamic_cast<Content*> (getContentComponent()))
            {
                c->label.setText (s->getCurrentName(), juce::dontSendNotification);
                c->progress = (double) s->getProgress();
            }
    }

    PluginHostManager& owner;
};

//==============================================================================
PluginHostManager::PluginHostManager (juce::PropertiesFile& s) : settings (s)
{
    vst3 = new juce::VST3PluginFormat();
    formatManager.addFormat (vst3);   // manager takes ownership

    if (auto xml = settings.getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
}

PluginHostManager::~PluginHostManager()
{
    cancelScan();
    saveKnownPlugins();
}

void PluginHostManager::saveKnownPlugins()
{
    if (auto xml = knownPlugins.createXml())
        settings.setValue ("knownPlugins", xml.get());
    settings.saveIfNeeded();
}

juce::File PluginHostManager::getDeadMansPedalFile() const
{
    return settings.getFile().getParentDirectory().getChildFile ("scan_in_progress.txt");
}

void PluginHostManager::createInstance (const juce::PluginDescription& desc,
                                        double sampleRate, int blockSize,
                                        std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String&)> callback)
{
    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [cb = std::move (callback)] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            cb (std::move (instance), error);
        });
}

juce::Array<juce::PluginDescription> PluginHostManager::findTypesInFile (const juce::File& file)
{
    juce::Array<juce::PluginDescription> result;

    juce::OwnedArray<juce::PluginDescription> found;
    knownPlugins.scanAndAddFile (file.getFullPathName(), true, found, *vst3);
    for (auto* d : found)
        result.add (*d);

    if (! result.isEmpty())
        saveKnownPlugins();

    return result;
}

void PluginHostManager::scanDefaultPaths (std::function<void()> onFinished)
{
    if (scanner != nullptr)
        return;

    scanWindow = std::make_unique<ScanWindow> (*this);
    scanner = std::make_unique<Scanner> (*this, [this, onFinished]
    {
        saveKnownPlugins();
        scanWindow.reset();
        scanner.reset();   // run() has already posted this and is returning
        if (onFinished)
            onFinished();
    });
}

void PluginHostManager::cancelScan()
{
    if (scanner != nullptr)
        scanner->signalThreadShouldExit();   // the finish callback cleans up
}
