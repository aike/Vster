#include "PluginManagerWindow.h"

class PluginManagerWindow::Content : public juce::Component,
                                     private juce::ListBoxModel,
                                     private juce::Timer
{
public:
    explicit Content (PluginHostManager& p) : plugins (p)
    {
        fullScanButton.onClick = [this] { plugins.scanDefaultPaths (nullptr); };
        scanModifiedButton.onClick = [this] { plugins.scanModified (nullptr); };
        scanByNameButton.onClick = [this] { startNameScan(); };

        nameEditor.onReturnKey = [this] { startNameScan(); };
        nameEditor.setTextToShowWhenEmpty ("part of a plugin filename", juce::Colours::grey);

        blacklistLabel.setText ("Blacklisted plugins:", juce::dontSendNotification);
        blacklistLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));

        blacklist.setModel (this);
        blacklist.setRowHeight (22);

        removeButton.onClick = [this] { removeSelected(); };

        addAndMakeVisible (fullScanButton);
        addAndMakeVisible (scanModifiedButton);
        addAndMakeVisible (scanByNameButton);
        addAndMakeVisible (nameEditor);
        addAndMakeVisible (blacklistLabel);
        addAndMakeVisible (blacklist);
        addAndMakeVisible (removeButton);

        setSize (580, 420);
        refresh();
        startTimer (300);   // picks up scan state and new blacklist entries
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        auto row = area.removeFromTop (28);
        fullScanButton.setBounds (row.removeFromLeft (130));
        row.removeFromLeft (8);
        scanModifiedButton.setBounds (row.removeFromLeft (130));

        area.removeFromTop (8);
        row = area.removeFromTop (28);
        scanByNameButton.setBounds (row.removeFromLeft (130));
        row.removeFromLeft (8);
        nameEditor.setBounds (row);

        area.removeFromTop (16);
        blacklistLabel.setBounds (area.removeFromTop (20));
        area.removeFromTop (4);

        auto bottom = area.removeFromBottom (28);
        removeButton.setBounds (bottom.removeFromRight (170));
        area.removeFromBottom (8);
        blacklist.setBounds (area);
    }

private:
    void startNameScan()
    {
        const auto text = nameEditor.getText().trim();
        if (text.isNotEmpty() && ! plugins.isScanning())
            plugins.scanByName (text, nullptr);
    }

    void removeSelected()
    {
        const int row = blacklist.getSelectedRow();
        if (juce::isPositiveAndBelow (row, blacklistedFiles.size()))
        {
            plugins.removeFromBlacklist (blacklistedFiles[row]);
            refresh();
        }
    }

    void refresh()
    {
        const auto& current = plugins.knownPlugins.getBlacklistedFiles();
        if (blacklistedFiles != current)
        {
            blacklistedFiles = current;
            blacklist.deselectAllRows();
            blacklist.updateContent();
            blacklist.repaint();
        }

        const bool scanning = plugins.isScanning();
        fullScanButton.setEnabled (! scanning);
        scanModifiedButton.setEnabled (! scanning);
        scanByNameButton.setEnabled (! scanning);
        removeButton.setEnabled (! scanning && blacklist.getSelectedRow() >= 0);
    }

    void timerCallback() override { refresh(); }

    // ListBoxModel --------------------------------------------------------
    int getNumRows() override { return blacklistedFiles.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, blacklistedFiles.size()))
            return;

        if (selected)
            g.fillAll (findColour (juce::TextEditor::highlightColourId));

        const juce::File file (blacklistedFiles[row]);

        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (file.getFileName(), 6, 0, 240, height, juce::Justification::centredLeft);

        g.setColour (findColour (juce::Label::textColourId).withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (file.getFullPathName(), 252, 0, width - 258, height, juce::Justification::centredLeft);
    }

    void selectedRowsChanged (int) override { refresh(); }

    PluginHostManager& plugins;

    juce::TextButton fullScanButton { "Full Scan" };
    juce::TextButton scanModifiedButton { "Scan Modified" };
    juce::TextButton scanByNameButton { "Scan by Name" };
    juce::TextEditor nameEditor;
    juce::Label blacklistLabel;
    juce::ListBox blacklist;
    juce::TextButton removeButton { "Remove from Blacklist" };
    juce::StringArray blacklistedFiles;
};

PluginManagerWindow::PluginManagerWindow (PluginHostManager& plugins)
    : juce::DocumentWindow ("Plugin Manager",
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (plugins), true);
    setResizable (true, false);
    setResizeLimits (460, 320, 2000, 1400);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
