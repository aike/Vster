#pragma once
#include <JuceHeader.h>
#include "../Plugins/PluginHostManager.h"

// Option > Plugin Manager: the three scan modes plus the blacklist editor.
// Closing only hides it, so the owner can keep one instance around.
class PluginManagerWindow : public juce::DocumentWindow
{
public:
    explicit PluginManagerWindow (PluginHostManager&);

    void closeButtonPressed() override { setVisible (false); }

private:
    class Content;

    JUCE_DECLARE_NON_COPYABLE (PluginManagerWindow)
};
