#include "PluginDpiPolicy.h"

namespace
{
    constexpr const char* settingsKey = "pluginEditorScaling";

    std::unique_ptr<juce::XmlElement> loadRoot (juce::PropertiesFile& settings)
    {
        auto xml = settings.getXmlValue (settingsKey);
        if (xml == nullptr || ! xml->hasTagName ("SCALING"))
            xml = std::make_unique<juce::XmlElement> ("SCALING");
        return xml;
    }

    juce::XmlElement* findEntry (juce::XmlElement& root, const juce::String& id, bool createIfMissing)
    {
        for (auto* e : root.getChildWithTagNameIterator ("PLUGIN"))
            if (e->getStringAttribute ("id") == id)
                return e;

        if (! createIfMissing)
            return nullptr;

        auto* e = root.createNewChildElement ("PLUGIN");
        e->setAttribute ("id", id);
        return e;
    }

    void saveRoot (juce::PropertiesFile& settings, juce::XmlElement& root)
    {
        // Drop entries that say nothing any more.
        for (int i = root.getNumChildElements(); --i >= 0;)
        {
            auto* e = root.getChildElement (i);
            if (e->getStringAttribute ("choice").isEmpty() && ! e->getBoolAttribute ("oversized"))
                root.removeChildElement (e, true);
        }

        settings.setValue (settingsKey, &root);
        settings.saveIfNeeded();
    }

    juce::String toString (EditorScaling s)
    {
        switch (s)
        {
            case EditorScaling::native: return "native";
            case EditorScaling::bitmap: return "bitmap";
            case EditorScaling::automatic: break;
        }
        return {};
    }

    EditorScaling fromString (const juce::String& s)
    {
        if (s == "native") return EditorScaling::native;
        if (s == "bitmap") return EditorScaling::bitmap;
        return EditorScaling::automatic;
    }
}

namespace PluginDpiPolicy
{

EditorScaling getUserChoice (juce::PropertiesFile& settings, const juce::PluginDescription& desc)
{
    auto root = loadRoot (settings);
    if (auto* e = findEntry (*root, desc.createIdentifierString(), false))
        return fromString (e->getStringAttribute ("choice"));
    return EditorScaling::automatic;
}

void setUserChoice (juce::PropertiesFile& settings, const juce::PluginDescription& desc, EditorScaling choice)
{
    auto root = loadRoot (settings);
    auto* e = findEntry (*root, desc.createIdentifierString(), true);
    const auto text = toString (choice);
    if (text.isEmpty())
        e->removeAttribute ("choice");
    else
        e->setAttribute ("choice", text);
    saveRoot (settings, *root);
}

bool wasDetectedOversized (juce::PropertiesFile& settings, const juce::PluginDescription& desc)
{
    auto root = loadRoot (settings);
    if (auto* e = findEntry (*root, desc.createIdentifierString(), false))
        return e->getBoolAttribute ("oversized", false);
    return false;
}

void setDetectedOversized (juce::PropertiesFile& settings, const juce::PluginDescription& desc, bool oversized)
{
    auto root = loadRoot (settings);
    auto* e = findEntry (*root, desc.createIdentifierString(), true);
    if (oversized)
        e->setAttribute ("oversized", true);
    else
        e->removeAttribute ("oversized");
    saveRoot (settings, *root);
}

bool shouldHostDpiUnaware (juce::PropertiesFile& settings, const juce::PluginDescription& desc)
{
    switch (getUserChoice (settings, desc))
    {
        case EditorScaling::native: return false;
        case EditorScaling::bitmap: return true;
        case EditorScaling::automatic: break;
    }
    return wasDetectedOversized (settings, desc);
}

} // namespace PluginDpiPolicy
