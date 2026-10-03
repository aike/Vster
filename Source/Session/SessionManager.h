#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"

// Saves/loads .vster session files (XML): transport, master, and per slot the
// PluginDescription + base64 plugin state + editor window state. The autosave
// used for crash recovery runs before each plugin load and at shutdown.
class SessionManager
{
public:
    SessionManager (AudioEngine&, PluginHostManager&, PluginWindowManager&);
    ~SessionManager();

    juce::Result saveToFile (const juce::File&);
    void loadFromFile (const juce::File&, std::function<void (const juce::String& error)> onFinished);

    void newInteractive();     // confirm, then reset to an empty default session
    void saveInteractive();    // overwrite the current session file; Save As if there is none
    void saveAsInteractive();  // always asks for a file
    void loadInteractive();

    void autosaveNow();
    juce::File getAutosaveFile() const;
    void offerAutosaveRecovery();

    bool isLoading() const noexcept { return pendingLoads > 0; }

    // Fired whenever a load (or recovery) finished changing the slots;
    // the UI uses it to resync every control.
    std::function<void()> onSessionChanged;

private:
    struct SlotJob
    {
        int index = 0;
        std::unique_ptr<juce::XmlElement> descriptionXml;
        juce::MemoryBlock state;
        bool editorOpen = false;
        juce::Point<int> editorPos { 120, 120 };
    };

    void attemptLoad (std::shared_ptr<SlotJob>, juce::Array<juce::PluginDescription> candidates, int index);
    void markMissing (SlotJob&);
    void finishOne();

    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastSessionDirectory;
    juce::File currentSessionFile;   // target of plain Save; empty until saved/loaded

    int pendingLoads = 0;
    bool savedMuteState = false;
    juce::StringArray missingNames;
    std::function<void (const juce::String&)> loadFinishedCallback;

    JUCE_DECLARE_NON_COPYABLE (SessionManager)
};
