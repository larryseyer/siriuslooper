#pragma once

#include "sirius/PluginScanner.h"   // PluginScanResult shape

#include <juce_core/juce_core.h>

#include <vector>

namespace sirius
{

/// Non-JUCE CLAP plug-in scanner (M8 S2). Walks the platform's default
/// CLAP search paths OR a caller-supplied path and produces a
/// `PluginScanResult` matching the shape `PluginScanner` returns for
/// VST3 / AU. Failed bundles populate `result.failedFiles` rather than
/// throwing.
///
/// Why this is separate from `PluginScanner`: JUCE's
/// `AudioPluginFormatManager` has no CLAP support — JUCE ships VST3 and
/// AU formats only. The third-party `CLAPPluginFormat` is single-
/// maintainer; we already have a working CLAP loader
/// (`ClapBundleLoader`), so the scanner is just "loop the loader over
/// every `*.clap` on the search paths".
///
/// Threading: message-thread only. `dlopen` allocates and CLAP
/// `entry->init` may take seconds; scanning a 100-bundle folder may
/// take a minute. Synchronous; async scanning lands in a later milestone.
class ClapScanner
{
public:
    /// Returns the platform's CLAP search paths in priority order. macOS:
    /// `~/Library/Audio/Plug-Ins/CLAP`, then `/Library/Audio/Plug-Ins/CLAP`.
    /// Linux: `~/.clap`, `/usr/local/lib/clap`, `/usr/lib/clap`. Windows:
    /// `%LOCALAPPDATA%/Programs/Common/CLAP`, then
    /// `C:/Program Files/Common Files/CLAP`. Paths are returned even if
    /// the directories don't exist on disk — `scan` handles missing dirs
    /// silently.
    static std::vector<juce::File> defaultSearchPaths();

    /// Scans `path` recursively for `*.clap` bundles, returning a
    /// `PluginScanResult` populated with descriptors + failed files.
    /// Returns an empty result if `path` does not exist.
    PluginScanResult scan (const juce::File& path);

    /// Convenience — scans every path returned by `defaultSearchPaths()`
    /// and concatenates the results.
    PluginScanResult scanDefaultLocations();
};

} // namespace sirius
