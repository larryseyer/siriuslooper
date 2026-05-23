// Tests for ClapScanner (M8 S2) — the non-JUCE walker that lists
// CLAP plug-ins on disk and produces PluginDescriptors.
#include "ida/ClapScanner.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>

#ifndef IDA_SYNTHETIC_CLAP_PATH
    #error "IDA_SYNTHETIC_CLAP_PATH must be defined for ClapScannerTests"
#endif

using ida::ClapScanner;

TEST_CASE ("ClapScanner::defaultSearchPaths returns macOS CLAP paths",
           "[clap-scanner]")
{
   #ifdef __APPLE__
    const auto paths = ClapScanner::defaultSearchPaths();
    REQUIRE (! paths.empty());

    bool foundUserPath   = false;
    bool foundSystemPath = false;
    for (const auto& p : paths)
    {
        const auto s = p.getFullPathName();
        if (s.contains ("/Library/Audio/Plug-Ins/CLAP")
            && s.startsWithChar ('/')
            && ! s.startsWith ("/Library"))
            foundUserPath = true;
        if (s == "/Library/Audio/Plug-Ins/CLAP")
            foundSystemPath = true;
    }
    CHECK (foundUserPath);
    CHECK (foundSystemPath);
   #else
    SUCCEED ("non-macOS path coverage deferred");
   #endif
}

TEST_CASE ("ClapScanner::scan on empty directory returns empty result",
           "[clap-scanner]")
{
    const auto tmp = juce::File::createTempFile ("sirius_clap_empty");
    REQUIRE (tmp.createDirectory().wasOk());

    ClapScanner scanner;
    const auto result = scanner.scan (tmp);
    CHECK (result.descriptors.empty());
    CHECK (result.failedFiles.empty());

    tmp.deleteRecursively();
}

TEST_CASE ("ClapScanner::scan finds the synthetic CLAP",
           "[clap-scanner]")
{
    // IDA_SYNTHETIC_CLAP_PATH points at the bundle; its parent
    // directory is what the scanner walks.
    const juce::File bundle (IDA_SYNTHETIC_CLAP_PATH);
    const auto parent = bundle.getParentDirectory();

    ClapScanner scanner;
    const auto result = scanner.scan (parent);
    const bool found = std::any_of (
        result.descriptors.begin(), result.descriptors.end(),
        [] (const ida::PluginDescriptor& d) {
            return d.uniqueId == "com.sirius.synthetic.identity";
        });
    CHECK (found);
    CHECK (result.failedFiles.empty());
}

TEST_CASE ("ClapScanner::scan records malformed bundles in failedFiles",
           "[clap-scanner]")
{
    // Make a fake .clap directory that is not a valid bundle.
    const auto tmp = juce::File::createTempFile ("sirius_clap_bad");
    REQUIRE (tmp.createDirectory().wasOk());
    const auto fake = tmp.getChildFile ("NotACLAP.clap");
    REQUIRE (fake.createDirectory().wasOk());
    REQUIRE (fake.getChildFile ("notes.txt").create().wasOk());

    ClapScanner scanner;
    const auto result = scanner.scan (tmp);
    CHECK (result.descriptors.empty());
    CHECK (result.failedFiles.size() == 1);

    tmp.deleteRecursively();
}
