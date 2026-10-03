#pragma once
#include <JuceHeader.h>
#include <map>

// VST3 discovery, the known-plugin list (with crash blacklist) and async
// instantiation. Message thread only, except the scan worker thread.
class PluginHostManager
{
public:
    explicit PluginHostManager (juce::PropertiesFile& settings);
    ~PluginHostManager();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // Async creation; the callback arrives on the message thread.
    void createInstance (const juce::PluginDescription&,
                         double sampleRate, int blockSize,
                         std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String&)> callback);

    // Scans one .vst3 file/bundle, adds anything found to the list.
    juce::Array<juce::PluginDescription> findTypesInFile (const juce::File&);

    // Background scans of the default VST3 folders with a progress window.
    // A plugin that crashed a previous scan is blacklisted automatically via
    // the dead-man's-pedal file.
    void scanDefaultPaths (std::function<void()> onFinished);             // everything
    void scanModified (std::function<void()> onFinished);                 // new / changed since last scan
    void scanByName (const juce::String& nameFragment,                    // filename contains it (case-insensitive)
                     std::function<void()> onFinished);
    bool isScanning() const noexcept { return scanner != nullptr; }
    void cancelScan();

    // Un-blacklists a plugin so the next scan probes it again. Also scrubs it
    // from the dead-man's-pedal file and the timestamp record, otherwise it
    // would be re-blacklisted or skipped by the next Scan Modified.
    void removeFromBlacklist (const juce::String& fileOrIdentifier);

    void saveKnownPlugins();

    // Plugins are probed in a child process (this exe started with
    // --vster-scan <plugin> <result.xml>), so a plugin that crashes or hangs
    // while being scanned is blacklisted instead of taking Vster down.
    static bool isScanWorkerCommandLine (const juce::StringArray& args);
    [[noreturn]] static void runScanWorker (const juce::StringArray& args);

private:
    class Scanner;
    class ScanWindow;
    class OutOfProcessScanner;

    juce::File getDeadMansPedalFile() const;
    juce::StringArray getCandidatePluginFiles();
    void startScan (juce::StringArray files, std::function<void()> onFinished);

    // Per-file modification times recorded as each file is scanned; this is
    // what Scan Modified compares against.
    void recordPluginFileTime (const juce::String& path);
    void loadPluginFileTimes();
    void savePluginFileTimes();

    juce::PropertiesFile& settings;
    juce::VST3PluginFormat* vst3 = nullptr;   // owned by formatManager
    std::unique_ptr<Scanner> scanner;
    std::unique_ptr<ScanWindow> scanWindow;
    std::map<juce::String, juce::int64> pluginFileTimes;

    JUCE_DECLARE_NON_COPYABLE (PluginHostManager)
};
