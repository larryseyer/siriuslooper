#include "ida/FileInputPersistence.h"

#include "ida/FileInputDescriptor.h"
#include "ida/FileInputRegistry.h"
#include "ida/LoopScope.h"

#include <algorithm>

namespace ida
{

namespace
{

juce::String loopScopeToString (LoopScope s)
{
    switch (s)
    {
        case LoopScope::Off:   return "off";
        case LoopScope::Track: return "track";
        case LoopScope::List:  return "list";
    }
    return "off";
}

LoopScope loopScopeFromString (const juce::String& s)
{
    if (s == "track") return LoopScope::Track;
    if (s == "list")  return LoopScope::List;
    return LoopScope::Off;   // default for unknown / "off"
}

} // namespace

juce::var serializeFileInputs (const FileInputRegistry& registry)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> arr;

    for (const auto& [k, d] : registry.allFileInputDescriptors())
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("displayName",   juce::String (d.displayName));
        o->setProperty ("loopScope",     loopScopeToString (d.loopScope));
        o->setProperty ("windowOpacity", d.windowOpacity);
        o->setProperty ("alwaysOnTop",   d.alwaysOnTop);

        juce::Array<juce::var> entries;
        for (const auto& e : d.entries)
        {
            juce::DynamicObject::Ptr ej = new juce::DynamicObject();
            ej->setProperty ("entryId", (juce::int64) e.entryId.value());
            ej->setProperty ("path",    juce::String (e.path));
            entries.add (juce::var (ej.get()));
        }
        o->setProperty ("entries", entries);
        arr.add (juce::var (o.get()));
    }

    root->setProperty ("fileInputs", arr);
    return juce::var (root.get());
}

bool deserializeFileInputs (FileInputRegistry& registry, const juce::var& root)
{
    if (! root.isObject()) return false;

    const auto fileInputs = root.getProperty ("fileInputs", juce::var());
    if (! fileInputs.isArray()) return true;  // backward compat: missing key is OK

    for (const auto& f : *fileInputs.getArray())
    {
        if (! f.isObject()) continue;

        FileInputDescriptor desc;
        desc.displayName = f.getProperty ("displayName", "").toString().toStdString();
        desc.loopScope   = loopScopeFromString (f.getProperty ("loopScope", "off").toString());

        const float opacity = (float) (double) f.getProperty ("windowOpacity", 0.92);
        desc.windowOpacity = std::clamp (opacity, 0.5f, 1.0f);
        desc.alwaysOnTop = (bool) f.getProperty ("alwaysOnTop", false);

        // Drop persisted entryIds — the registry's FileInputSource allocates
        // fresh handles via addFileInputEntry.
        const auto id = registry.registerFileInput (desc);

        if (const auto entries = f.getProperty ("entries", juce::var()); entries.isArray())
            for (const auto& e : *entries.getArray())
                if (e.isObject())
                    registry.addFileInputEntry (id, e.getProperty ("path", "").toString().toStdString());
    }

    return true;
}

} // namespace ida
