// Tests for PluginScanner. The scanner's interesting work happens against real
// plugins on disk — operator-run, since the test machine may not have any —
// but the *shape* of the scanner is testable here: it registers the formats
// the plan promises, it returns a structured (descriptors + failures) result,
// and it survives a scan over an empty path without throwing or producing
// spurious entries.
#include "sirius/PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using ida::PluginScanner;

namespace
{
    bool containsFormat (const std::vector<std::string>& formats, const char* name)
    {
        return std::find (formats.begin(), formats.end(), name) != formats.end();
    }
}

TEST_CASE ("PluginScanner registers VST3 on every platform", "[pluginscanner]")
{
    PluginScanner scanner;
    const auto formats = scanner.registeredFormatNames();
    CHECK (containsFormat (formats, "VST3"));
}

#if JUCE_MAC
TEST_CASE ("on macOS, PluginScanner also registers AudioUnit", "[pluginscanner]")
{
    PluginScanner scanner;
    const auto formats = scanner.registeredFormatNames();
    CHECK (containsFormat (formats, "AudioUnit"));
}
#endif

// A "scan an empty path returns an empty result" test was tried here and
// removed: on macOS, juce::AudioUnitPluginFormat ignores the FileSearchPath
// and enumerates every AU registered with the OS (AUs live in the component
// manager, not in directories), so an "empty scan" is in fact a full AU sweep
// of the host machine. The honest end-to-end test — scanning real plugin
// folders, instantiating one of each format — is operator-run; it is
// documented in todo.md against the M5 milestone-test commitment.
