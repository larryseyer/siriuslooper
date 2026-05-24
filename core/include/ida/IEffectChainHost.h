#pragma once

#include "ida/InternalFxConfigs.h"
#include "ida/InternalFxId.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ida
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

    /// P7 T3a-C — message-thread binder for IDA's built-in FX adapters.
    /// Engine call sites (`Bus::setEffectChain`, `ChannelStrip::setEffectChain`,
    /// `OutputMixer::setBusEffectChain`) invoke this from inside the same
    /// audio-callback-bracketed region that already configures plug-in slots,
    /// passing one of:
    ///   - `id == value`  → bind/replace an adapter at `(nodeKey, slotIdx)`.
    ///   - `id == nullopt`→ unbind any existing adapter at `(nodeKey, slotIdx)`.
    /// Default no-op so test fakes that only care about `pumpSlot` keep
    /// compiling. The OOP host overrides to maintain its `internalAdapters_`
    /// table; the audio thread's `pumpSlot` checks that table FIRST and
    /// falls through to the plug-in dispatch on miss.
    virtual void setInternalFxAtSlot (std::int64_t                /*nodeKey*/,
                                      std::size_t                 /*slotIdx*/,
                                      std::optional<InternalFxId> /*id*/) {}

    /// P7 T5 slice 1 — message-thread bypass toggle for a bound internal
    /// adapter. Mirrors `setInternalFxAtSlot`'s message-thread + audio-
    /// detached contract. When `bypassed == true`, the audio thread's
    /// `pumpSlot` short-circuits the bound adapter and returns false
    /// (caller treats as dry pass-through — same semantics as the
    /// adapter-not-bound miss). When `bypassed == false`, dispatch
    /// resumes through `adapter->process(...)` on the next call.
    ///
    /// Calling on a slot with no bound adapter is harmless — the flag is
    /// recorded but ignored until a future `setInternalFxAtSlot(...)`
    /// installs an adapter. Calling `setInternalFxAtSlot(..., id_value)`
    /// resets the bypass flag to false so a re-added slot starts active
    /// regardless of any prior bypass state at that key.
    ///
    /// Default no-op so test fakes that only care about `pumpSlot` keep
    /// compiling — same fake-compat reason as `setInternalFxAtSlot`.
    virtual void setInternalFxBypassAtSlot (std::int64_t /*nodeKey*/,
                                            std::size_t  /*slotIdx*/,
                                            bool         /*bypassed*/) {}

    /// P7 T5 slice 2 — message-thread reorder primitive. Atomically (w.r.t.
    /// the audio callback — caller MUST detach first) moves whatever is
    /// bound at `(nodeKey, fromSlot)` to `(nodeKey, toSlot)` and vice
    /// versa:
    ///   - Both occupied → swap.
    ///   - From occupied, to empty → move (fromSlot becomes empty).
    ///   - From empty, to occupied → move (toSlot becomes empty).
    ///   - Both empty → no-op.
    ///   - `fromSlot == toSlot` → no-op.
    /// The associated bypass flag travels with the adapter — bypassing
    /// slot 3 and then moving slot 3 → slot 5 leaves the bypass on the
    /// adapter at slot 5.
    ///
    /// Exists so the UI's reorder gesture maps to ONE audio-detached
    /// mutation rather than two (erase-then-reinsert leaves a visible
    /// "empty slot" frame between).
    ///
    /// Default no-op so test fakes that only care about `pumpSlot` keep
    /// compiling.
    virtual void moveInternalFxSlot (std::int64_t /*nodeKey*/,
                                     std::size_t  /*fromSlot*/,
                                     std::size_t  /*toSlot*/) {}

    /// P7 T3a-C — message-thread prepare for every bound internal adapter.
    /// Called once at audio-device init and again on every sample-rate /
    /// max-block change (the engine has the hooks; `MainComponent` wires
    /// them). Default no-op for the same fake-compat reason as
    /// `setInternalFxAtSlot`.
    virtual void prepareInternalFx (double /*sampleRate*/, int /*maxBlockSize*/) {}

    /// Slice EC — typed config setters/getters routed through whatever
    /// `IInternalFxAdapter` is bound at `(nodeKey, slotIdx)`. The
    /// concrete implementation looks the adapter up and dispatches
    /// through its `setEqConfig` / `setCmpConfig` virtual; calling on a
    /// slot whose adapter doesn't match the kind (e.g. `setInternalEqConfigAt`
    /// on a slot holding a Cmp adapter) is a silent no-op via the base-
    /// class default in `IInternalFxAdapter`. Getters return nullopt when
    /// no adapter is bound; otherwise they return the adapter's snapshot
    /// (`EqConfig{}` shape for Eq slots, etc).
    ///
    /// Message-thread only — caller MUST detach the audio callback before
    /// invoking, same as `setInternalFxAtSlot`. Default no-op so test
    /// fakes that only care about `pumpSlot` keep compiling.
    virtual void setInternalEqConfigAt (std::int64_t /*nodeKey*/,
                                        std::size_t  /*slotIdx*/,
                                        const EqConfig& /*cfg*/) {}
    virtual std::optional<EqConfig>
            internalEqConfigAt (std::int64_t /*nodeKey*/,
                                std::size_t  /*slotIdx*/) const noexcept { return std::nullopt; }

    virtual void setInternalCmpConfigAt (std::int64_t /*nodeKey*/,
                                         std::size_t  /*slotIdx*/,
                                         const CmpConfig& /*cfg*/) {}
    virtual std::optional<CmpConfig>
            internalCmpConfigAt (std::int64_t /*nodeKey*/,
                                 std::size_t  /*slotIdx*/) const noexcept { return std::nullopt; }
};

} // namespace ida
