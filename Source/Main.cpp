#include <JuceHeader.h>
#include "Engine/AudioEngine.h"
#include "Plugins/PluginHostManager.h"
#include "Plugins/PluginWindowManager.h"
#include "Session/SessionManager.h"
#include "UI/MainComponent.h"

class VsterApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Vster"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // Scan workers run alongside the main instance.
        return PluginHostManager::isScanWorkerCommandLine (getCommandLineParameterArray());
    }

    void initialise (const juce::String&) override
    {
        if (const auto args = getCommandLineParameterArray(); PluginHostManager::isScanWorkerCommandLine (args))
            PluginHostManager::runScanWorker (args);   // never returns

        juce::PropertiesFile::Options options;
        options.applicationName = "Vster";
        options.filenameSuffix = ".settings";
        options.folderName = "Vster";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);
        auto* settings = appProperties.getUserSettings();

        engine = std::make_unique<AudioEngine>();
        pluginManager = std::make_unique<PluginHostManager> (*settings);
        windowManager = std::make_unique<PluginWindowManager>();
        session = std::make_unique<SessionManager> (*engine, *pluginManager, *windowManager);

        const auto savedDeviceState = settings->getXmlValue ("audioDeviceState");
        engine->initialise (savedDeviceState.get());

        mainWindow = std::make_unique<MainWindow> (
            "Vster", std::make_unique<MainComponent> (*engine, *pluginManager, *windowManager, *session));

        session->offerAutosaveRecovery();
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
