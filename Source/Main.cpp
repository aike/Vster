#include <JuceHeader.h>
#include "Engine/AudioEngine.h"
#include "Plugins/PluginHostManager.h"
#include "Plugins/PluginWindowManager.h"
#include "Session/SessionManager.h"
#include "UI/MainComponent.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <signal.h>
 #include <unistd.h>
#endif

// "--restart <pid of the instance being replaced>": started by
// MainComponent::onRestartRequested when a plugin's HiDPI hosting mode
// changed. The new process waits for the old one to exit (it still holds the
// audio device) and restores the autosaved session without asking.
static constexpr const char* restartFlag = "--restart";

static juce::String getCurrentProcessId()
{
   #if JUCE_WINDOWS
    return juce::String ((juce::int64) GetCurrentProcessId());
   #else
    return juce::String ((juce::int64) getpid());
   #endif
}

static void waitForProcessToExit (const juce::String& pid, int timeoutMs)
{
   #if JUCE_WINDOWS
    if (auto handle = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) pid.getLargeIntValue()))
    {
        WaitForSingleObject (handle, (DWORD) timeoutMs);
        CloseHandle (handle);
    }
   #else
    for (int waited = 0; waited < timeoutMs && kill ((pid_t) pid.getLargeIntValue(), 0) == 0; waited += 100)
        juce::Thread::sleep (100);
   #endif
}

class VsterApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Vster"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // Scan workers run alongside the main instance, and a restarted
        // instance starts while the old one is still shutting down.
        const auto args = getCommandLineParameterArray();
        return PluginHostManager::isScanWorkerCommandLine (args) || args.contains (restartFlag);
    }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        if (PluginHostManager::isScanWorkerCommandLine (args))
            PluginHostManager::runScanWorker (args);   // never returns

        const int restartIndex = args.indexOf (restartFlag);
        const bool restarted = restartIndex >= 0;
        if (restarted && restartIndex + 1 < args.size())
            waitForProcessToExit (args[restartIndex + 1], 15000);

        juce::PropertiesFile::Options options;
        options.applicationName = "Vster";
        options.filenameSuffix = ".settings";
        options.folderName = "Vster";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);
        auto* settings = appProperties.getUserSettings();

        engine = std::make_unique<AudioEngine>();
        pluginManager = std::make_unique<PluginHostManager> (*settings);
        windowManager = std::make_unique<PluginWindowManager> (*settings);
        session = std::make_unique<SessionManager> (*engine, *pluginManager, *windowManager);

        const auto savedDeviceState = settings->getXmlValue ("audioDeviceState");
        engine->initialise (savedDeviceState.get());

        auto content = std::make_unique<MainComponent> (*engine, *pluginManager, *windowManager, *session);
        content->onRestartRequested = [this] { restart(); };
        mainWindow = std::make_unique<MainWindow> ("Vster", std::move (content));

        if (restarted)
            session->restoreAutosave();
        else
            session->offerAutosaveRecovery();
    }

    // Hands the session over to a new process and quits. Used when a plugin's
    // HiDPI hosting mode changed, which only a fresh process can apply.
    void restart()
    {
        session->autosaveNow();
        appProperties.getUserSettings()->saveIfNeeded();   // the hosting record must be on disk first

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        if (! exe.startAsProcess (juce::String (restartFlag) + " " + getCurrentProcessId()))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Restart failed",
                                                    getApplicationName() + " could not start a new instance. Please restart it by hand.");
            return;
        }

        quit();
    }

    void shutdown() override
    {
        if (auto* settings = appProperties.getUserSettings())
        {
            if (auto deviceState = engine->deviceManager.createStateXml())
                settings->setValue ("audioDeviceState", deviceState.get());
            settings->saveIfNeeded();
        }

        session->autosaveNow();

        mainWindow.reset();          // UI first
        session.reset();
        windowManager->closeAll();   // editors must die before their plugins
        engine->shutdown();          // stops audio, then destroys the plugins
        engine.reset();
        windowManager.reset();
        pluginManager.reset();       // persists the known-plugin list
        appProperties.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, std::unique_ptr<MainComponent> content)
            : juce::DocumentWindow (name,
                                    juce::Desktop::getInstance().getDefaultLookAndFeel()
                                        .findColour (juce::ResizableWindow::backgroundColourId),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            // Menu bar first, so resize-to-fit below accounts for its height.
            setMenuBar (content.get());
            setContentOwned (content.release(), true);
            setResizable (true, false);
            setResizeLimits (640, 258 + getMenuBarComponent()->getHeight(), 4096, 2160);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        ~MainWindow() override
        {
            // The content is the menu model; detach before it is destroyed.
            setMenuBar (nullptr);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    juce::ApplicationProperties appProperties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<PluginHostManager> pluginManager;
    std::unique_ptr<PluginWindowManager> windowManager;
    std::unique_ptr<SessionManager> session;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (VsterApplication)
