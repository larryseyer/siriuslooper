#pragma once

#include <cstddef>
#include <cstdint>

namespace sirius
{

/// Audio-thread interface a `Bus` calls into to dispatch one slot of its
/// `EffectChain` (M7 S3). Owners of plug-in instances (`Bus`, `OutputMixer`,
/// `Channel`) hold an `IEffectChainHost*` set-once on the message thread
/// via `setEffectChainHost(...)`. The audio thread calls `pumpSlot` per
/// slot per buffer.
///
/// Lives in `core/` and is JUCE-free by design. The concrete out-of-process
/// implementation (`OutOfProcessEffectChainHost`) lives in `host/` and owns
/// the JUCE-bearing `juce::File` paths for binary + plug-in bundle. This
/// is the dependency-inversion seam — `Bus` (engine) does not know whether
/// its host runs plug-ins in-process, out-of-process, or in a mock.
///
/// **`busId` is `std::int64_t`, not the engine's strong-typed `BusId`**, to
/// keep this header in `core/` and thereby out of `engine/`'s dependency
/// graph. Engine-side callers pass `id_.value()`. The strong-typing lives
/// at the call site; this interface trades it for the layering benefit.
///
/// `pumpSlot` is the audio-thread surface — `noexcept`, allocation-free,
/// lock-free, bounded per `docs/RT_SAFETY_CONTRACT.md §6`. Implementations
/// that cannot meet that contract must reject the call (return false) and
/// arrange for production-time configuration to bypass the slot rather
/// than violate the RT contract on the audio thread.
class IEffectChainHost
{
public:
    virtual ~IEffectChainHost() = default;

    /// Audio-thread dispatch for a single slot of a bus's effect chain.
    /// Reads `numChannels` × `numSamples` floats from `inChannels`,
    /// applies the slot's plug-in, and writes the result to `outChannels`.
    /// `inChannels` and `outChannels` MAY alias (in-place processing) —
    /// implementations must read all input before writing any output OR
    /// copy through an internal scratch.
    ///
    /// Returns true if `outChannels` was written from a real plug-in
    /// response; false if no response was available this buffer (the
    /// pipelined 1-buffer delay's "miss" case — the caller's contract is
    /// to treat the miss as "drop this slot, leave the dry signal
    /// unchanged"). On a `false` return, `outChannels` is left
    /// **unmodified** — the caller must ensure `outChannels` already
    /// holds the dry signal (or a meaningful default) before invoking.
    ///
    /// `busId` is the engine-side `BusId::value()` of the bus whose chain
    /// is being pumped; `slotIndex` is the zero-based index into the
    /// chain's `entries()` vector. The pair together selects which
    /// `OutOfProcessPluginInstance` (or other concrete implementation) to
    /// route through.
    ///
    /// `noexcept` per the M7 S3 row in `docs/RT_SAFETY_CONTRACT.md §6`.
    virtual bool pumpSlot (std::int64_t        busId,
                           std::size_t         slotIndex,
                           const float* const* inChannels,
                           float* const*       outChannels,
                           int                 numChannels,
                           int                 numSamples) noexcept = 0;
};

} // namespace sirius
