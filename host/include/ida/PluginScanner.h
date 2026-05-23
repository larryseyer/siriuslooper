#pragma once

#include "ida/PluginDescriptor.h"
#include "ida/INotificationSink.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace sirius
{

/// The result of a scan pass: the descriptors that were successfully read, and
/// the file paths that failed to load. The failed list is part of "degradation
/// is announced, not silent" (white paper Part 13.3, rule 3): a host that
/// silently dropped broken plugins would leave the performer guessing.
struct PluginScanResult
{
    std::vector<PluginDescriptor> descriptors;
    std::vector<std::string>      failedFiles;
};

/// Scans the host system for VST3 and (on macOS) AudioUnit / AudioUnit v3
/// plugins and produces a list of PluginDescriptors that the data model can
/// reference. The scanner is a thin wrapper over JUCE's
/// `AudioPluginFormatManager`: registering the formats, walking each format's
/// search paths, and translating JUCE's `PluginDescription` into IDA's
/// portable PluginDescriptor.
///
/// CLAP is *not* registered — JUCE does not officially host it, and the only
/// existing third-party support (`CLAPPluginFormat`) is a single-maintainer
/// effort. CLAP descriptors are still representable in the data model (so a
/// session that references a CLAP plugin can persist), they are simply not
/// produced by the default scan path.
class PluginScanner
{
public:
    /// Registers the plugin formats supported on this platform (VST3
    /// everywhere; AudioUnit and AudioUnit v3 on macOS).
    PluginScanner();

    /// Scans every format's default OS search path. Synchronous: returns when
    /// every format has finished, or when scanning errors leave a partial
    /// result with failed files listed.
    PluginScanResult scanDefaultLocations();

    /// Scans `path` with every registered format. Useful for tests, custom
    /// plugin folders, and operator-controlled re-scans.
    PluginScanResult scan (const juce::FileSearchPath& path);

    /// The plugin-format names that were registered for this platform. Useful
    /// for diagnostics — "we scanned VST3 only, no AU available" should never
    /// be silent.
    std::vector<std::string> registeredFormatNames() const;

    /// Binds a notification sink so scan-progress events surface in
    /// the operator's notification history. nullptr-tolerant — the
    /// scanner posts only when a sink is bound. Set-once before
    /// the first scan; not thread-safe.
    void setNotificationSink (INotificationSink* sink) noexcept;

private:
    juce::AudioPluginFormatManager formats_;
    INotificationSink* notificationSink_ { nullptr };
};

} // namespace sirius
