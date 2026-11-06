#include "PluginWindowManager.h"
#include <optional>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

class PluginWindowManager::PluginWindow : public juce::DocumentWindow,
                                          private juce::Timer
{
public:
    // `dpiUnaware` hosts the editor in a DPI-unaware window that the OS
    // bitmap-stretches (for plugins that ignore HiDPI, see PluginDpiPolicy).
    // `onOversized` (optional) is called once if a DPI-aware window turns out
    // to be too small for what the plugin actually draws.
    PluginWindow (juce::AudioPluginInstance& p, bool dpiUnaware,
                  std::function<void()> onUserClose, std::function<void()> onOversized)
        : juce::DocumentWindow (p.getName(),
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          plugin (p), onClose (std::move (onUserClose)), onOversizedDetected (std::move (onOversized))
    {
        // Everything created while this is in scope - the window, JUCE's
        // embedding HWND and the HWND the plugin makes when its view is
        // attached (which happens inside setVisible below) - is DPI unaware.
        std::optional<juce::ScopedDPIAwarenessDisabler> disabler;
        if (dpiUnaware)
            disabler.emplace();

        setUsingNativeTitleBar (true);   // VST3 editors behave better under native title bars

        auto* editor = p.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        setContentOwned (editor, true);   // the window deletes the editor
        setResizable (editor->isResizable(), false);
        setVisible (true);

        if (! dpiUnaware && onOversizedDetected != nullptr)
            startTimer (150);
    }

    juce::AudioPluginInstance& getPlugin() const { return plugin; }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    void timerCallback() override
    {
        // The plugin's own window exists as soon as the view is attached, but
        // some plugins settle their size a little later; look a few times.
        if (++checks > 8)
        {
            stopTimer();
            return;
        }

        if (looksOversized())
        {
            stopTimer();
            if (onOversizedDetected)
                onOversizedDetected();
        }
    }

    // True if a native child window is clearly larger than this window's
    // client area on a scaled display: the plugin reports an unscaled size
    // but draws at the monitor's DPI, so the DPI-aware window cuts it off.
    bool looksOversized() const
    {
       #if JUCE_WINDOWS
        auto* peer = getPeer();
        if (peer == nullptr || peer->getPlatformScaleFactor() <= 1.05)
            return false;

        const auto root = (HWND) peer->getNativeHandle();
        RECT client {};
        if (! GetClientRect (root, &client) || client.right < 10 || client.bottom < 10)
            return false;

        struct Extent { int w = 0, h = 0; } extent;
        EnumChildWindows (root, [] (HWND child, LPARAM lp) -> BOOL
        {
            auto& e = *reinterpret_cast<Extent*> (lp);
            RECT r {};
            if (GetWindowRect (child, &r))
            {
                e.w = juce::jmax (e.w, (int) (r.right - r.left));
                e.h = juce::jmax (e.h, (int) (r.bottom - r.top));
            }
            return TRUE;
        }, (LPARAM) &extent);

        // 20 % slack: a plugin window a few pixels larger is not "oversized".
        return extent.w > client.right * 6 / 5 || extent.h > client.bottom * 6 / 5;
       #else
        return false;
       #endif
    }

    juce::AudioPluginInstance& plugin;
    std::function<void()> onClose, onOversizedDetected;
    int checks = 0;
};

PluginWindowManager::PluginWindowManager (juce::PropertiesFile& s) : settings (s) {}
PluginWindowManager::~PluginWindowManager() { closeAll(); }

void PluginWindowManager::showEditorFor (int slot, juce::AudioPluginInstance& instance, juce::Point<int> position)
{
    jassert (juce::isPositiveAndBelow (slot, (int) windows.size()));

    if (windows[(size_t) slot] != nullptr)
    {
        windows[(size_t) slot]->toFront (true);
        return;
    }

    const auto desc = instance.getPluginDescription();
    const bool dpiUnaware = PluginDpiPolicy::shouldHostDpiUnaware (settings, desc);
    const bool detect = ! dpiUnaware
                        && PluginDpiPolicy::getUserChoice (settings, desc) == EditorScaling::automatic;

    // Deleting a window from inside its own callbacks would be deleting
    // `this` mid-callback, so both are deferred.
    std::function<void()> onOversized;
    if (detect)
        onOversized = [this, slot, desc]
        {
            PluginDpiPolicy::setDetectedOversized (settings, desc, true);
            juce::MessageManager::callAsync ([this, slot]
            {
                if (onHostingModeChanged)
                    onHostingModeChanged (slot, true);
                else
                    reopen (slot);
            });
        };

    windows[(size_t) slot] = std::make_unique<PluginWindow> (instance, dpiUnaware,
        [this, slot] { juce::MessageManager::callAsync ([this, slot] { closeEditorFor (slot); }); },
        std::move (onOversized));
    windows[(size_t) slot]->setTopLeftPosition (position);
}

void PluginWindowManager::closeEditorFor (int slot)
{
    if (auto& w = windows[(size_t) slot])
    {
        lastPositions[(size_t) slot] = w->getPosition();
        w.reset();
    }
}

void PluginWindowManager::closeAll()
{
    for (int i = 0; i < (int) windows.size(); ++i)
        closeEditorFor (i);
}

bool PluginWindowManager::isOpen (int slot) const
{
    return windows[(size_t) slot] != nullptr;
}

juce::Point<int> PluginWindowManager::getPosition (int slot) const
{
    if (auto& w = windows[(size_t) slot])
        return w->getPosition();
    return lastPositions[(size_t) slot];
}

void PluginWindowManager::reopen (int slot)
{
    auto& w = windows[(size_t) slot];
    if (w == nullptr)
        return;

    auto& instance = w->getPlugin();
    const auto position = w->getPosition();
    closeEditorFor (slot);
    showEditorFor (slot, instance, position);
}

EditorScaling PluginWindowManager::getEditorScaling (const juce::PluginDescription& desc) const
{
    return PluginDpiPolicy::getUserChoice (settings, desc);
}

void PluginWindowManager::setEditorScaling (int slot, juce::AudioPluginInstance& instance, EditorScaling scaling)
{
    const auto desc = instance.getPluginDescription();
    const bool wasUnaware = PluginDpiPolicy::shouldHostDpiUnaware (settings, desc);

    PluginDpiPolicy::setUserChoice (settings, desc, scaling);
    if (scaling == EditorScaling::automatic)
        PluginDpiPolicy::setDetectedOversized (settings, desc, false);   // detect afresh

    if (wasUnaware != PluginDpiPolicy::shouldHostDpiUnaware (settings, desc))
    {
        if (onHostingModeChanged)
            onHostingModeChanged (slot, false);
        else if (isOpen (slot))
            reopen (slot);
        return;
    }

    showEditorFor (slot, instance, getPosition (slot));
}
