#include "PluginHostManager.h"
#include <cstdlib>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
    const char* const scanWorkerFlag = "--vster-scan";
    constexpr int scanTimeoutMs = 60000;   // a plugin that takes longer is treated as hung
}

//==============================================================================
// Runs in the main process: probes each plugin file in a child process.
class PluginHostManager::OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        const auto resultFile = juce::File::createTempFile (".xml");
        const juce::ScopeGuard deleteResult { [&] { resultFile.deleteFile(); } };

        juce::ChildProcess child;
        const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                       scanWorkerFlag, fileOrIdentifier, resultFile.getFullPathName() };

        if (! child.start (args, 0))
        {
            // Can't spawn the worker: fall back to scanning in-process.
            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }

        const auto startTime = juce::Time::getMillisecondCounter();

        while (! child.waitForProcessToFinish (100))
        {
            if (juce::Thread::currentThreadShouldExit())
            {
                child.kill();
                return true;   // cancelled: not the plugin's fault, don't blacklist
            }

            if (juce::Time::getMillisecondCounter() - startTime > (juce::uint32) scanTimeoutMs)
            {
                child.kill();
                DBG ("Scan timed out: " << fileOrIdentifier);
                return false;  // hung: blacklist
            }
        }

        const auto xml = child.getExitCode() == 0 ? juce::parseXML (resultFile) : nullptr;

        if (xml == nullptr || ! xml->hasTagName ("VSTERSCAN"))
        {
            DBG ("Scan failed (exit code " << (int) child.getExitCode() << "): " << fileOrIdentifier);
            return false;      // crashed or failed: blacklist
        }

        for (auto* e : xml->getChildIterator())
        {
            auto desc = std::make_unique<juce::PluginDescription>();

            if (desc->loadFromXml (*e))
                result.add (desc.release());
        }

        return true;
    }
};

bool PluginHostManager::isScanWorkerCommandLine (const juce::StringArray& args)
{
    return args.size() >= 3 && args[0] == scanWorkerFlag;
}

// Runs in the child process. Never returns: exits immediately after writing
// the result, so a plugin misbehaving during process teardown can't turn a
// successful scan into a failure.
void PluginHostManager::runScanWorker (const juce::StringArray& args)
{
   #if JUCE_WINDOWS
    // No "Vster has stopped working" dialogs: a crash here just ends the child.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif

    const auto pluginPath = args[1];
    const juce::File resultFile (args[2]);

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, pluginPath);

    juce::XmlElement root ("VSTERSCAN");

    for (auto* d : found)
        root.addChildElement (d->createXml().release());

    const bool written = root.writeTo (resultFile);
    std::_Exit (written ? 0 : 1);
}

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
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

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
