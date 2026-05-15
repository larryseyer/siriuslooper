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

    void scanOneFormat (juce::AudioPluginFormat& format,
                        const juce::FileSearchPath& path,
                        PluginScanResult& result)
    {
        juce::KnownPluginList list;
        juce::PluginDirectoryScanner scanner (list, format, path,
                                              /*recursive*/ true,
                                              /*deadMansPedalFile*/ juce::File(),
                                              /*allowAsync*/ false);

        juce::String currentName;
        while (scanner.scanNextFile (/*dontRescanIfAlreadyInList*/ true, currentName))
            ; // each pass advances scanner state; we drain to completion synchronously

        for (const auto& failed : scanner.getFailedFiles())
            result.failedFiles.push_back (failed.toStdString());

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
