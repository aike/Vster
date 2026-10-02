#include "PluginWindowManager.h"

class PluginWindowManager::PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& p, std::function<void()> onUserClose)
        : juce::DocumentWindow (p.getName(),
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          onClose (std::move (onUserClose))
    {
        setUsingNativeTitleBar (true);   // VST3 editors behave better under native title bars

        auto* editor = p.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        // Escape hatch for plugins that render at the wrong HiDPI scale:
        // editor->setScaleFactor (1.0f);

        setContentOwned (editor, true);   // the window deletes the editor
        setResizable (editor->isResizable(), false);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};

PluginWindowManager::PluginWindowManager() = default;
PluginWindowManager::~PluginWindowManager() { closeAll(); }

void PluginWindowManager::showEditorFor (int slot, juce::AudioPluginInstance& instance, juce::Point<int> position)
{
    jassert (juce::isPositiveAndBelow (slot, (int) windows.size()));

    if (windows[(size_t) slot] != nullptr)
    {
        windows[(size_t) slot]->toFront (true);
        return;
    }

    // Deleting a window from inside its own close callback would be deleting
    // `this` mid-callback, so defer it.
    windows[(size_t) slot] = std::make_unique<PluginWindow> (instance, [this, slot]
    {
        juce::MessageManager::callAsync ([this, slot] { closeEditorFor (slot); });
    });
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
