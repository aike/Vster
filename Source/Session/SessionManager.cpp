#include "SessionManager.h"

static constexpr int sessionFormatVersion = 1;

SessionManager::SessionManager (AudioEngine& e, PluginHostManager& p, PluginWindowManager& w)
    : engine (e), plugins (p), windows (w)
{
    lastSessionDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    startTimer (2 * 60 * 1000);   // autosave every 2 minutes
}

SessionManager::~SessionManager() = default;

//==============================================================================
juce::Result SessionManager::saveToFile (const juce::File& file)
{
    juce::XmlElement root ("VsterSession");
    root.setAttribute ("formatVersion", sessionFormatVersion);
    root.setAttribute ("appVersion", JUCE_APPLICATION_VERSION_STRING);

    auto* transport = root.createNewChildElement ("Transport");
    transport->setAttribute ("bpm", engine.getBpm());
    transport->setAttribute ("playing", engine.isTransportPlaying());

    auto* master = root.createNewChildElement ("Master");
    master->setAttribute ("gainDb", engine.getMasterGainDb());
    master->setAttribute ("muted", engine.isMuted());

    auto* slots = root.createNewChildElement ("Slots");
    for (int i = 0; i < PluginChain::numSlots; ++i)
    {
        auto* slot = slots->createNewChildElement ("Slot");
        slot->setAttribute ("index", i);
        slot->setAttribute ("role", i == 0 ? "instrument" : "fx");

        if (auto* instance = engine.chain.getInstance (i))
        {
            if (auto* desc = engine.chain.getDescription (i))
                slot->addChildElement (desc->createXml().release());

            juce::MemoryBlock state;
            instance->getStateInformation (state);
            slot->createNewChildElement ("State")->addTextElement (state.toBase64Encoding());

            auto* editor = slot->createNewChildElement ("Editor");
            editor->setAttribute ("open", windows.isOpen (i));
            const auto pos = windows.getPosition (i);
            editor->setAttribute ("x", pos.x);
            editor->setAttribute ("y", pos.y);
        }
        else if (auto* m = engine.missing[(size_t) i].get())
        {
            // Plugin was missing on load: write its record back untouched so
            // nothing is lost.
            slot->setAttribute ("missing", true);
            if (m->descriptionXml != nullptr)
                slot->addChildElement (new juce::XmlElement (*m->descriptionXml));
            slot->createNewChildElement ("State")->addTextElement (m->state.toBase64Encoding());
        }
    }

    if (! file.getParentDirectory().createDirectory().wasOk())
        return juce::Result::fail ("Could not create " + file.getParentDirectory().getFullPathName());

    if (! root.writeTo (file))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    return juce::Result::ok();
}

//==============================================================================
void SessionManager::loadFromFile (const juce::File& file,
                                   std::function<void (const juce::String&)> onFinished)
{
    if (isLoading())
    {
        if (onFinished) onFinished ("A session is already loading.");
        return;
    }

    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("VsterSession"))
    {
        if (onFinished) onFinished ("Not a valid Vster session file.");
        return;
    }
    if (xml->getIntAttribute ("formatVersion", 1) > sessionFormatVersion)
    {
        if (onFinished) onFinished ("This session was saved by a newer version of Vster.");
        return;
    }

    if (auto* t = xml->getChildByName ("Transport"))
    {
        engine.setBpm (t->getDoubleAttribute ("bpm", 120.0));
        engine.setPlaying (t->getBoolAttribute ("playing", true));
    }

    savedMuteState = false;
    if (auto* m = xml->getChildByName ("Master"))
    {
        engine.setMasterGainDb ((float) m->getDoubleAttribute ("gainDb", 0.0));
        savedMuteState = m->getBoolAttribute ("muted", false);
    }

    // Keep the output quiet while plugins come and go.
    engine.setMuted (true);

    windows.closeAll();
    for (int i = 0; i < PluginChain::numSlots; ++i)
        engine.clearSlot (i);

    std::vector<std::shared_ptr<SlotJob>> jobs;
    if (auto* slots = xml->getChildByName ("Slots"))
    {
        for (auto* slotXml : slots->getChildWithTagNameIterator ("Slot"))
        {
            auto* descXml = slotXml->getChildByName ("PLUGIN");
            if (descXml == nullptr)
                continue;   // empty slot

            auto job = std::make_shared<SlotJob>();
            job->index = juce::jlimit (0, PluginChain::numSlots - 1, slotXml->getIntAttribute ("index"));
            job->descriptionXml = std::make_unique<juce::XmlElement> (*descXml);
            if (auto* state = slotXml->getChildByName ("State"))
                job->state.fromBase64Encoding (state->getAllSubText().trim());
            if (auto* editor = slotXml->getChildByName ("Editor"))
            {
                job->editorOpen = editor->getBoolAttribute ("open");
                job->editorPos = { editor->getIntAttribute ("x", 120), editor->getIntAttribute ("y", 120) };
            }
            jobs.push_back (std::move (job));
        }
    }

    missingNames.clear();
    loadFinishedCallback = std::move (onFinished);
    pendingLoads = (int) jobs.size();

    if (pendingLoads == 0)
    {
        engine.setMuted (savedMuteState);
        if (onSessionChanged) onSessionChanged();
        if (loadFinishedCallback) { loadFinishedCallback ({}); loadFinishedCallback = nullptr; }
        return;
    }

    for (auto& job : jobs)
    {
        juce::PluginDescription saved;
        juce::Array<juce::PluginDescription> candidates;

        if (saved.loadFromXml (*job->descriptionXml))
        {
            candidates.add (saved);
            // Fallback: same plugin known under a different path (moved folder).
            for (const auto& known : plugins.knownPlugins.getTypes())
                if (known.uniqueId == saved.uniqueId
                    && known.fileOrIdentifier != saved.fileOrIdentifier)
                    candidates.add (known);
        }

        attemptLoad (job, candidates, 0);
    }
}

void SessionManager::attemptLoad (std::shared_ptr<SlotJob> job,
                                  juce::Array<juce::PluginDescription> candidates, int index)
{
    if (index >= candidates.size())
    {
        markMissing (*job);
        finishOne();
        return;
    }

    const auto desc = candidates[index];
    plugins.createInstance (desc, engine.getCurrentSampleRate(), engine.getCurrentBlockSize(),
        [this, job, candidates, index, desc] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::String&)
        {
            if (instance == nullptr)
            {
                attemptLoad (job, candidates, index + 1);
                return;
            }

            const auto error = engine.setPluginInSlot (job->index, std::move (instance), desc, &job->state);
            if (error.isNotEmpty())
            {
                markMissing (*job);
            }
            else if (job->editorOpen)
            {
                if (auto* loaded = engine.chain.getInstance (job->index))
                    windows.showEditorFor (job->index, *loaded, job->editorPos);
            }
            finishOne();
        });
}

void SessionManager::markMissing (SlotJob& job)
{
    auto m = std::make_unique<MissingPlugin>();
    juce::PluginDescription desc;
    if (job.descriptionXml != nullptr && desc.loadFromXml (*job.descriptionXml))
    {
        m->name = desc.name;
        m->uniqueId = desc.uniqueId;
    }
    m->descriptionXml = std::move (job.descriptionXml);
    m->state = std::move (job.state);
    missingNames.add (m->name.isNotEmpty() ? m->name : "(unknown)");
    engine.missing[(size_t) job.index] = std::move (m);
}

void SessionManager::finishOne()
{
    jassert (pendingLoads > 0);
    if (--pendingLoads > 0)
        return;

    engine.setMuted (savedMuteState);
    if (onSessionChanged)
        onSessionChanged();

    juce::String error;
    if (! missingNames.isEmpty())
        error = "Some plugins could not be found: " + missingNames.joinIntoString (", ")
                + "\n\nTheir saved settings are preserved and will be written back on the next save.";

    if (loadFinishedCallback)
    {
        loadFinishedCallback (error);
        loadFinishedCallback = nullptr;
    }
    else if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Missing plugins", error);
    }
}

//==============================================================================
void SessionManager::saveInteractive()
{
    chooser = std::make_unique<juce::FileChooser> ("Save session",
                                                   lastSessionDirectory.getChildFile ("session.vster"),
                                                   "*.vster");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (".vster");
            lastSessionDirectory = file.getParentDirectory();

            const auto result = saveToFile (file);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Save failed", result.getErrorMessage());
        });
}

void SessionManager::loadInteractive()
{
    if (isLoading())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Load session", lastSessionDirectory, "*.vster");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            lastSessionDirectory = file.getParentDirectory();

            loadFromFile (file, [] (const juce::String& error)
            {
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Session load", error);
            });
        });
}

//==============================================================================
juce::File SessionManager::getAutosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Vster").getChildFile ("autosave.vster");
}

void SessionManager::autosaveNow()
{
    if (isLoading())
        return;

    bool anythingLoaded = false;
    for (int i = 0; i < PluginChain::numSlots; ++i)
        if (engine.chain.getInstance (i) != nullptr || engine.missing[(size_t) i] != nullptr)
            anythingLoaded = true;

    if (anythingLoaded)
        saveToFile (getAutosaveFile());
}

void SessionManager::offerAutosaveRecovery()
{
    if (! getAutosaveFile().existsAsFile())
        return;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Restore session?",
        "An autosaved session from the last run was found. Restore it?",
        "Restore", "Ignore", nullptr,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1)
                loadFromFile (getAutosaveFile(), [] (const juce::String& error)
                {
                    if (error.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Session restore", error);
                });
        }));
}
