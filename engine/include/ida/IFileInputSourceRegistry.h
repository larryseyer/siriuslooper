#pragma once

#include "ida/Channel.h"   // ida::InputId

namespace ida
{

/// A resolved pull function for one file-input source. The pull function
/// is RT-safe (noexcept, no allocation, no locks, no I/O); returns true
/// on success or false when the source has no data to provide (caller
/// fills the destination with silence). The userdata pointer remains
/// valid until the source is unregistered.
///
/// Engine resolves these on the message thread and caches them in channel
/// state; the audio thread invokes the cached pair directly with zero
/// map lookups.
struct FileInputPullCallable
{
    using Fn = bool (*) (void* userdata, float* L, float* R, int numFrames) noexcept;

    Fn    fn       { nullptr };
    void* userdata { nullptr };

    bool valid() const noexcept { return fn != nullptr; }
};

/// JUCE-free seam: engine consumes this; audio/FileInputRegistry implements it.
/// One method by design — keeps the engine layer free of juce_audio_formats.
class IFileInputSourceRegistry
{
public:
    virtual ~IFileInputSourceRegistry() = default;

    /// Resolves a pull callable for the source registered under `id`.
    /// Returns an invalid callable (fn == nullptr) when `id` is unknown.
    /// Message thread only. The returned callable's userdata is owned by
    /// the registry; unregistering the source must bracket with audio-
    /// callback removal (the engine never invalidates the cached pair on
    /// its own).
    virtual FileInputPullCallable resolveFileInputPull (InputId id) noexcept = 0;
};

} // namespace ida
