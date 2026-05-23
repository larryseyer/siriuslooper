#include "ida/PluginScanner.h"
#include "ida/ClapScanner.h"

#include <cstdio>

namespace sirius
{

namespace
{
    /// Maps the JUCE format-name string to the IDA enum. JUCE uses
    /// human-readable names ("VST3", "AudioUnit", "AudioUnit v3"); the
    /// descriptors that hold them are produced by JUCE itself.
    PluginFormat formatFromJuce (const juce::String& formatName)
    {
        if (formatName == "VST3")          return PluginFormat::Vst3;
        if (formatName == "AudioUnit")     return PluginFormat::AudioUnit;
        if (formatName == "AudioUnit v3")  return PluginFormat::AudioUnitV3;
        if (formatName == "CLAP")          return PluginFormat::Clap;
        return PluginFormat::Vst3;
    }

    PluginDescriptor fromJuceDescription (const juce::PluginDescription& description)
    {
        PluginDescriptor out;
        out.format       = formatFromJuce (description.pluginFormatName);
        out.uniqueId     = description.createIdentifierString().toStdString();
        out.version      = description.version.toStdString();
        out.name         = description.name.toStdString();
        out.manufacturer = description.manufacturerName.toStdString();
        out.filePath     = description.fileOrIdentifier.toStdString();
        return out;
    }

    void scanOneFormat (juce::AudioPluginFormat& format,
                        const juce::FileSearchPath& path,
                        PluginScanResult& result)
    {
        juce::KnownPluginList list;

        const auto candidates = format.searchPathsForPlugins (
            path, /*recursive*/ true, /*allowAsync*/ false);

        for (const auto& fileOrId : candidates)
        {
            juce::OwnedArray<juce::PluginDescription> typesFound;
            list.scanAndAddFile (fileOrId,
                                 /*dontRescanIfAlreadyInList*/ true,
                                 typesFound,
                                 format);
        }

        for (const auto& type : list.getTypes())
            result.descriptors.push_back (fromJuceDescription (type));
    }

    void postScanProgress (INotificationSink* sink,
                           const char* phase,
                           const char* formatName,
                           std::size_t count)
    {
        if (sink == nullptr) return;
        // Comfortably fits the known format names ("VST3", "AudioUnit v3", "CLAP")
        // plus the phase word and the "(%zu descriptors)" count suffix.
        constexpr std::size_t kScanProgressMsgBytes = 96;
        char msg[kScanProgressMsgBytes];
        std::snprintf (msg, sizeof (msg),
                       "scan %s: %s (%zu descriptors)",
                       phase, formatName, count);
        sink->post (NotificationLevel::Info, Category::PluginEvent, msg);
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

void PluginScanner::setNotificationSink (INotificationSink* sink) noexcept
{
    notificationSink_ = sink;
}

PluginScanResult PluginScanner::scanDefaultLocations()
{
    PluginScanResult result;
    for (int i = 0; i < formats_.getNumFormats(); ++i)
    {
        auto* format = formats_.getFormat (i);
        const juce::String name = format->getName();
        const auto before = result.descriptors.size();
        postScanProgress (notificationSink_, "begin", name.toRawUTF8(), 0);
        scanOneFormat (*format, format->getDefaultLocationsToSearch(), result);
        postScanProgress (notificationSink_, "end", name.toRawUTF8(),
                          result.descriptors.size() - before);
    }

    // CLAP — non-JUCE format, ours.
    const auto before = result.descriptors.size();
    postScanProgress (notificationSink_, "begin", "CLAP", 0);
    {
        ClapScanner clapScanner;
        auto clapResult = clapScanner.scanDefaultLocations();
        for (auto& d : clapResult.descriptors)
            result.descriptors.push_back (std::move (d));
        for (auto& f : clapResult.failedFiles)
            result.failedFiles.push_back (std::move (f));
    }
    postScanProgress (notificationSink_, "end", "CLAP",
                      result.descriptors.size() - before);

    return result;
}

PluginScanResult PluginScanner::scan (const juce::FileSearchPath& path)
{
    PluginScanResult result;
    for (int i = 0; i < formats_.getNumFormats(); ++i)
    {
        auto* format = formats_.getFormat (i);
        const juce::String name = format->getName();
        const auto before = result.descriptors.size();
        postScanProgress (notificationSink_, "begin", name.toRawUTF8(), 0);
        scanOneFormat (*format, path, result);
        postScanProgress (notificationSink_, "end", name.toRawUTF8(),
                          result.descriptors.size() - before);
    }

    // Walk every path the caller supplied for CLAP too.
    const auto before = result.descriptors.size();
    postScanProgress (notificationSink_, "begin", "CLAP", 0);
    {
        ClapScanner clapScanner;
        for (int i = 0; i < path.getNumPaths(); ++i)
        {
            auto sub = clapScanner.scan (path[i]);
            for (auto& d : sub.descriptors)
                result.descriptors.push_back (std::move (d));
            for (auto& f : sub.failedFiles)
                result.failedFiles.push_back (std::move (f));
        }
    }
    postScanProgress (notificationSink_, "end", "CLAP",
                      result.descriptors.size() - before);

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
