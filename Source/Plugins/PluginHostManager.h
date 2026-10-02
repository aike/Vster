#pragma once
#include <JuceHeader.h>

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

    // Background scan of the default VST3 folders with a progress window.
    // A plugin that crashed a previous scan is blacklisted automatically via
    // the dead-man's-pedal file.
    void scanDefaultPaths (std::function<void()> onFinished);
    bool isScanning() const noexcept { return scanner != nullptr; }
    void cancelScan();

    void saveKnownPlugins();

private:
    class Scanner;
    class ScanWindow;

    juce::File getDeadMansPedalFile() const;

    juce::PropertiesFile& settings;
    juce::VST3PluginFormat* vst3 = nullptr;   // owned by formatManager
    std::unique_ptr<Scanner> scanner;
    std::unique_ptr<ScanWindow> scanWindow;

    JUCE_DECLARE_NON_COPYABLE (PluginHostManager)
};
