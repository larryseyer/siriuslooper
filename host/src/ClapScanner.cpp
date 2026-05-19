#include "sirius/ClapScanner.h"

#include "sirius/ClapBundleLoader.h"

#include <utility>

namespace sirius
{

std::vector<juce::File> ClapScanner::defaultSearchPaths()
{
    std::vector<juce::File> out;
   #ifdef __APPLE__
    out.push_back (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Audio/Plug-Ins/CLAP"));
    out.push_back (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
   #elif defined(__linux__)
    out.push_back (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile (".clap"));
    out.push_back (juce::File ("/usr/local/lib/clap"));
    out.push_back (juce::File ("/usr/lib/clap"));
   #elif defined(_WIN32)
    out.push_back (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Programs/Common/CLAP"));
    out.push_back (juce::File ("C:/Program Files/Common Files/CLAP"));
   #endif
    return out;
}

PluginScanResult ClapScanner::scan (const juce::File& path)
{
    PluginScanResult result;
    if (! path.isDirectory())
        return result;

    // CLAP bundles: macOS = `.clap` directories; Linux/Windows = `.clap`
    // shared libraries. JUCE's `findChildFiles` distinguishes between
    // files and directories but uniformly returns matches by suffix.
    const auto matches = path.findChildFiles (
        juce::File::findFilesAndDirectories,
        /*recursive*/ true,
        "*.clap");

    for (const auto& candidate : matches)
    {
       #ifdef __APPLE__
        // On macOS a .clap is a bundle directory; skip plain files
        // that happen to end in .clap (shouldn't exist on a healthy
        // system but we'd rather report them than crash).
        if (! candidate.isDirectory())
        {
            result.failedFiles.push_back (candidate.getFullPathName().toStdString());
            continue;
        }
       #else
        if (candidate.isDirectory())
        {
            // Non-macOS shouldn't see .clap directories; record the
            // anomaly rather than dropping it silently.
            result.failedFiles.push_back (candidate.getFullPathName().toStdString());
            continue;
        }
       #endif

        const auto bundlePath = candidate.getFullPathName().toStdString();
        std::string err;
        auto loader = ClapBundleLoader::load (bundlePath, err);
        if (! loader.valid())
        {
            result.failedFiles.push_back (bundlePath);
            continue;
        }
        auto descs = loader.descriptors (bundlePath);
        for (auto& d : descs)
            result.descriptors.push_back (std::move (d));
    }
    return result;
}

PluginScanResult ClapScanner::scanDefaultLocations()
{
    PluginScanResult merged;
    for (const auto& path : defaultSearchPaths())
    {
        auto sub = scan (path);
        for (auto& d : sub.descriptors)
            merged.descriptors.push_back (std::move (d));
        for (auto& f : sub.failedFiles)
            merged.failedFiles.push_back (std::move (f));
    }
    return merged;
}

} // namespace sirius
