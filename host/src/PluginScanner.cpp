#include "sirius/PluginScanner.h"

namespace sirius
{

namespace
{
    /// Maps the JUCE format-name string to the Sirius enum. JUCE uses
    /// human-readable names ("VST3", "AudioUnit", "AudioUnit v3"); the
    /// descriptors that hold them are produced by JUCE itself.
    PluginFormat formatFromJuce (const juce::String& formatName)
    {
        if (formatName == "VST3")          return PluginFormat::Vst3;
        if (formatName == "AudioUnit")     return PluginFormat::AudioUnit;
        if (formatName == "AudioUnit v3")  return PluginFormat::AudioUnitV3;
        // Anything else falls through to VST3; CLAP would land here if a third-
        // party format were registered, but the scanner does not register it.
        return PluginFormat::Vst3;
    }

    PluginDescriptor fromJuceDescription (const juce::PluginDescription& description)
    {
        PluginDescriptor out;
        out.format       = formatFromJuce (description.pluginFormatName);
        out.uniqueId     = description.createIdentifierString().toStdString();
        out.name         = description.name.toStdString();
        out.manufacturer = description.manufacturerName.toStdString();
        out.filePath     = description.fileOrIdentifier.toStdString();
        return out;
    }

    /// Testing-only name filter (see todo.md M7 S7 entry). When non-empty,
    /// candidate plug-in files whose path does NOT contain this substring
    /// are skipped before per-file instantiation. Operators with 1000+
    /// plug-ins installed cannot wait minutes for a full scan during
    /// eyes-on; this lets them point at the global plug-in folder and
    /// only pay the instantiation cost for the named vendor.
    constexpr const char* kTestingScanFilter = "FabFilter";

    void scanOneFormat (juce::AudioPluginFormat& format,
                        const juce::FileSearchPath& path,
                        PluginScanResult& result)
    {
        juce::KnownPluginList list;
        const juce::String filter (kTestingScanFilter);

        const auto candidates = format.searchPathsForPlugins (
            path, /*recursive*/ true, /*allowAsync*/ false);

        for (const auto& fileOrId : candidates)
        {
            if (filter.isNotEmpty() && ! fileOrId.containsIgnoreCase (filter))
                continue;

            juce::OwnedArray<juce::PluginDescription> typesFound;
            list.scanAndAddFile (fileOrId,
                                 /*dontRescanIfAlreadyInList*/ true,
                                 typesFound,
                                 format);
            // scanAndAddFile populates `list` directly; typesFound is the
            // per-file result. Anything failing instantiation surfaces via
            // KnownPluginList's failure tracking — we don't have a direct
            // "failed" list without PluginDirectoryScanner, so failures here
            // are silently dropped. Acceptable for the testing-only path;
            // production scan (post-todo.md fix) restores the full reporting.
        }

        for (const auto& type : list.getTypes())
            result.descriptors.push_back (fromJuceDescription (type));
    }
}

PluginScanner::PluginScanner()
{
   #if JUCE_PLUGINHOST_VST3
    formats_.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats_.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
}

PluginScanResult PluginScanner::scanDefaultLocations()
{
    PluginScanResult result;
    for (int i = 0; i < formats_.getNumFormats(); ++i)
    {
        auto* format = formats_.getFormat (i);
        scanOneFormat (*format, format->getDefaultLocationsToSearch(), result);
    }
    return result;
}

PluginScanResult PluginScanner::scan (const juce::FileSearchPath& path)
{
    PluginScanResult result;
    for (int i = 0; i < formats_.getNumFormats(); ++i)
        scanOneFormat (*formats_.getFormat (i), path, result);
    return result;
}

std::vector<std::string> PluginScanner::registeredFormatNames() const
{
    std::vector<std::string> names;
    for (int i = 0; i < formats_.getNumFormats(); ++i)
        names.push_back (formats_.getFormat (i)->getName().toStdString());
    return names;
}

} // namespace sirius
