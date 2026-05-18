#pragma once

#include "sirius/EffectChain.h"
#include "sirius/IEffectChainHost.h"
#include "sirius/OutOfProcessPluginInstance.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace juce { class File; }

namespace sirius
{

/// Out-of-process concrete implementation of `IEffectChainHost` (M7 S3).
/// Owns one `OutOfProcessPluginInstance` per active slot of an
/// `EffectChain` on a given `Bus`. Each slot mints a CLAP-mode host child
/// at `configureBus(...)` time on the message thread; the audio thread
/// later calls `pumpSlot(...)` to ship one buffer through the slot's IPC
/// rings.
///
/// **No production consumer in S3.** MainComponent is unchanged this
/// session; the integration test in
/// `tests/OutputMixerPluginHostIntegrationTests.cpp` is the sole caller.
/// MainComponent wiring lands when the plug-in-adding UI is real (post-M7).
///
/// Threading contract (same shape as the M5/M6 collaborator pattern —
/// e.g. `OutputMixer::setBusEffectChain`):
///   - `configureBus(...)` and the destructor are **message-thread** only.
///     Both may allocate, spawn / reap child processes, mmap / munmap shm.
///   - `pumpSlot(...)` is **audio-thread** only. Wait-free, allocation-
///     free, lock-free, bounded per `docs/RT_SAFETY_CONTRACT.md §6`.
///   - The `instances_` map is mutated ONLY by `configureBus`, which must
///     run to completion before the audio thread first calls `pumpSlot`.
///     This is the same "set-once before audio starts" contract every
///     other engine collaborator obeys.
class OutOfProcessEffectChainHost : public IEffectChainHost
{
public:
    OutOfProcessEffectChainHost();
    ~OutOfProcessEffectChainHost() override;

    OutOfProcessEffectChainHost (const OutOfProcessEffectChainHost&) = delete;
    OutOfProcessEffectChainHost& operator= (const OutOfProcessEffectChainHost&) = delete;
    OutOfProcessEffectChainHost (OutOfProcessEffectChainHost&&) = delete;
    OutOfProcessEffectChainHost& operator= (OutOfProcessEffectChainHost&&) = delete;

    /// Message-thread setter. Spawns one `OutOfProcessPluginInstance` per
    /// non-bypassed slot of `chain` in CLAP mode against `clapBundle`,
    /// keyed by `(busId, slotIndex)`. Any pre-existing instances for the
    /// same `busId` that no longer have a corresponding slot are torn down.
    ///
    /// S3 simplification: every non-bypassed slot in the chain spawns
    /// against the SAME `clapBundle` argument — the integration test
    /// uses a single synthetic identity plug-in. Real plug-in selection
    /// (one bundle per `EffectChainEntry::descriptor`) lands once the
    /// plug-in scanner surfaces bundle paths on PluginDescriptor (post-M7
    /// per V7 plan line 533+).
    void configureBus (std::int64_t       busId,
                       const EffectChain& chain,
                       const juce::File&  hostBinary,
                       const juce::File&  clapBundle);

    /// Audio-thread dispatch — see `IEffectChainHost::pumpSlot`. This
    /// concrete override packages the supplied stereo float planes into
    /// the CLAP-mode wire format (`uint32_t frameCount` + interleaved
    /// stereo floats) and ships them through the slot's
    /// `OutOfProcessPluginInstance::tryWriteBytes` /
    /// `tryReadBytes` pair. Returns true if a response was popped this
    /// buffer; false on a miss (caller leaves `outChannels` unchanged).
    ///
    /// Pipelined 1-buffer delay: the push for buffer N and the pop of
    /// the response for buffer N-1 happen in the same call. The first
    /// call after `configureBus` always returns false (no prior response
    /// to pop); subsequent calls return true at steady state.
    bool pumpSlot (std::int64_t        busId,
                   std::size_t         slotIndex,
                   const float* const* inChannels,
                   float* const*       outChannels,
                   int                 numChannels,
                   int                 numSamples) noexcept override;

private:
    /// `(busId, slotIndex)` lookup key. Composed of trivially copyable
    /// integers so the `unordered_map` hashing stays allocation-free.
    using SlotKey = std::pair<std::int64_t, std::size_t>;

    struct SlotKeyHash
    {
        std::size_t operator() (const SlotKey& key) const noexcept
        {
            // Boost-style hash combine — adequate for the ~10s-of-slots
            // population this map will hold and stable across runs.
            const auto a = static_cast<std::uint64_t> (key.first);
            const auto b = static_cast<std::uint64_t> (key.second);
            std::uint64_t h = a + 0x9e3779b97f4a7c15ULL + (b << 6) + (b >> 2);
            h ^= b;
            return static_cast<std::size_t> (h);
        }
    };

    /// Audio-thread scratch for the interleaved CLAP wire bytes. Sized
    /// at construction to `PluginIpcMessage::kMaxPayloadBytes` — one
    /// max-size message. `mutable` so `pumpSlot` (which is `noexcept`
    /// and operates on internal state the audio thread owns end-to-end)
    /// can write to it without losing the `noexcept` shape. Stored
    /// inside the class rather than re-allocated per call to keep
    /// `pumpSlot` allocation-free.
    ///
    /// Layout per pump: `[uint32_t frameCount][float L0, float R0,
    /// float L1, float R1, …]`. Caller's `numChannels` is clamped to
    /// `kPumpChannels` (2) on the audio thread — surround buses are
    /// out of scope through M7.
    static constexpr int kPumpChannels = 2;

    std::unordered_map<SlotKey,
                       std::unique_ptr<OutOfProcessPluginInstance>,
                       SlotKeyHash> instances_;

    /// Pre-allocated scratch bytes for the wire-format packaging in
    /// pumpSlot. Sized to one PluginIpcMessage payload. Pre-allocated in
    /// the constructor; never resized.
    mutable std::vector<std::byte> wireScratch_;
};

} // namespace sirius
