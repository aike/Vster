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
// Probes an explicit list of plugin files on a background thread, so the same
// code path serves full, modified-only and by-name scans.
class PluginHostManager::Scanner : public juce::Thread
{
public:
    Scanner (PluginHostManager& o, juce::StringArray files, std::function<void()> finished)
        : juce::Thread ("VST3 scan"), owner (o),
          filesToScan (std::move (files)), onFinished (std::move (finished))
    {
        // Blacklist whatever crashed a previous scan before trying again.
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (owner.knownPlugins,
                                                                           owner.getDeadMansPedalFile());
        startThread();
    }

    ~Scanner() override { stopThread (10000); }

    void run() override
    {
        const auto pedal = owner.getDeadMansPedalFile();
        const int total = filesToScan.size();
        int scannedSinceSave = 0;

        for (int i = 0; i < total && ! threadShouldExit(); ++i)
        {
            const auto path = filesToScan[i];

            {
                const juce::ScopedLock sl (nameLock);
                currentName = juce::File (path).getFileNameWithoutExtension();
            }

            // If this file takes the whole host down, the entry left behind
            // blacklists it at the start of the next scan.
            pedal.replaceWithText (path);

            juce::OwnedArray<juce::PluginDescription> found;
            owner.knownPlugins.scanAndAddFile (path, true, found, *owner.vst3);

            pedal.replaceWithText ({});

            progress.store ((float) (i + 1) / (float) total, std::memory_order_relaxed);

            // Timestamp bookkeeping, plus periodic persistence so a crash or
            // kill during a long scan keeps the progress made so far. Both on
            // the message thread; the owner outlives the scanner, and once
            // the message loop has stopped pending callbacks never run.
            const bool saveNow = ++scannedSinceSave >= savePluginInterval;
            if (saveNow)
                scannedSinceSave = 0;

            juce::MessageManager::callAsync ([&o = owner, path, saveNow]
            {
                o.recordPluginFileTime (path);
                if (saveNow)
                {
                    o.saveKnownPlugins();
                    o.savePluginFileTimes();
                }
            });
        }

        juce::MessageManager::callAsync ([cb = onFinished] { if (cb) cb(); });
    }

    juce::String getCurrentName() const
    {
        const juce::ScopedLock sl (nameLock);
        return currentName;
    }

    float getProgress() const { return progress.load (std::memory_order_relaxed); }

private:
    static constexpr int savePluginInterval = 10;

    PluginHostManager& owner;
    juce::StringArray filesToScan;
    std::function<void()> onFinished;
    std::atomic<float> progress { 0.0f };
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

    loadPluginFileTimes();
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

juce::StringArray PluginHostManager::getCandidatePluginFiles()
{
    return vst3->searchPathsForPlugins (vst3->getDefaultLocationsToSearch(), true, false);
}

void PluginHostManager::scanDefaultPaths (std::function<void()> onFinished)
{
    if (scanner != nullptr)
        return;

    auto files = getCandidatePluginFiles();

    // A full scan sees every existing file, so drop timestamp records of
    // files that have disappeared.
    for (auto it = pluginFileTimes.begin(); it != pluginFileTimes.end();)
        it = files.contains (it->first) ? std::next (it) : pluginFileTimes.erase (it);

    startScan (std::move (files), std::move (onFinished));
}

void PluginHostManager::scanModified (std::function<void()> onFinished)
{
    if (scanner != nullptr)
        return;

    juce::StringArray subset;
    for (const auto& path : getCandidatePluginFiles())
    {
        const auto it = pluginFileTimes.find (path);
        if (it == pluginFileTimes.end()
            || juce::File (path).getLastModificationTime().toMilliseconds() > it->second)
            subset.add (path);
    }

    if (subset.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "Scan Modified", "No new or modified plugins found.");
        if (onFinished)
            onFinished();
        return;
    }

    startScan (std::move (subset), std::move (onFinished));
}

void PluginHostManager::scanByName (const juce::String& nameFragment, std::function<void()> onFinished)
{
    if (scanner != nullptr)
        return;

    const auto needle = nameFragment.trim();
    if (needle.isEmpty())
        return;

    juce::StringArray subset;
    for (const auto& path : getCandidatePluginFiles())
        if (juce::File (path).getFileNameWithoutExtension().containsIgnoreCase (needle))
            subset.add (path);

    if (subset.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "Scan by Name",
                                                "No plugin filenames contain \"" + needle + "\".");
        if (onFinished)
            onFinished();
        return;
    }

    startScan (std::move (subset), std::move (onFinished));
}

void PluginHostManager::startScan (juce::StringArray files, std::function<void()> onFinished)
{
    scanWindow = std::make_unique<ScanWindow> (*this);
    scanner = std::make_unique<Scanner> (*this, std::move (files), [this, onFinished]
    {
        saveKnownPlugins();
        savePluginFileTimes();
        scanWindow.reset();
        scanner.reset();   // run() has already posted this and is returning
        if (onFinished)
            onFinished();
    });
}

void PluginHostManager::removeFromBlacklist (const juce::String& fileOrIdentifier)
{
    knownPlugins.removeFromBlacklist (fileOrIdentifier);

    // The dead-man's-pedal file may still name this plugin; left there it
    // would be re-blacklisted the moment the next scan starts.
    const auto pedal = getDeadMansPedalFile();
    if (pedal.existsAsFile())
    {
        juce::StringArray lines;
        pedal.readLines (lines);
        lines.removeString (fileOrIdentifier);
        lines.removeEmptyStrings();
        pedal.replaceWithText (lines.joinIntoString ("\n"));
    }

    // Forget its timestamp so the next Scan Modified probes it again.
    pluginFileTimes.erase (fileOrIdentifier);
    savePluginFileTimes();
    saveKnownPlugins();
}

void PluginHostManager::recordPluginFileTime (const juce::String& path)
{
    pluginFileTimes[path] = juce::File (path).getLastModificationTime().toMilliseconds();
}

void PluginHostManager::loadPluginFileTimes()
{
    if (auto xml = settings.getXmlValue ("pluginFileTimes"))
        for (auto* e : xml->getChildWithTagNameIterator ("FILE"))
            pluginFileTimes[e->getStringAttribute ("path")] =
                e->getStringAttribute ("time").getLargeIntValue();
}

void PluginHostManager::savePluginFileTimes()
{
    juce::XmlElement root ("PLUGINFILETIMES");
    for (const auto& [path, time] : pluginFileTimes)
    {
        auto* e = root.createNewChildElement ("FILE");
        e->setAttribute ("path", path);
        e->setAttribute ("time", juce::String (time));
    }

    settings.setValue ("pluginFileTimes", &root);
    settings.saveIfNeeded();
}

void PluginHostManager::cancelScan()
{
    if (scanner != nullptr)
        scanner->signalThreadShouldExit();   // the finish callback cleans up
}
