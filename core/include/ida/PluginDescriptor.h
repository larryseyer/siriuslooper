#pragma once

#include <string>

namespace ida
{

/// The plugin formats IDA hosts (white paper Part 5.5). VST3 is the only
/// universal format; AU is the macOS native; AUv3 is the iOS/macOS sandboxed
/// successor with desktop value but limited desktop adoption; CLAP is supported
/// through a third-party `CLAPPluginFormat` on a best-effort basis.
enum class PluginFormat
{
    Vst3,
    AudioUnit,
    AudioUnitV3,
    Clap
};

/// The portable, persistable identity of a hosted plugin. This is a pure value
/// — no JUCE types, no audio framework — so it can ride inside a Constituent's
/// effect chain and round-trip through SessionFormat without dragging hosting
/// machinery into the structure layer. The actual plugin instantiation lives
/// in the host/ runtime, downstream of the conceptual-time engine.
struct PluginDescriptor
{
    PluginFormat format { PluginFormat::Vst3 };
    std::string  uniqueId;     ///< format-specific stable identifier (VST3 UID, AU subtype, etc.)
    std::string  version;      ///< format-reported version string; opaque, used for VersionPinning drift detection
    std::string  name;
    std::string  manufacturer;
    std::string  filePath;     ///< location on disk; for sandboxed formats this may be empty

    bool operator== (const PluginDescriptor& other) const noexcept
    {
        return format == other.format
            && uniqueId == other.uniqueId
            && version == other.version
            && name == other.name
            && manufacturer == other.manufacturer
            && filePath == other.filePath;
    }
    bool operator!= (const PluginDescriptor& other) const noexcept { return ! (*this == other); }
};

} // namespace ida
