#pragma once

#include <cstddef>

namespace sirius
{

/// The Sirius-side contract for one built-in (internal) effect — the
/// adapter shape wrapping an OTTO header-only Player FX. One adapter
/// instance per occupied `Internal` slot of an `EffectChain`; the host
/// (today `OutOfProcessEffectChainHost`) owns the adapters keyed by
/// `(nodeKey, slotIdx)` and calls them from its `pumpSlot` dispatch on
/// the audio thread.
///
/// JUCE-free by design — lives in `core/` so both `engine/` and `host/`
/// can depend on it without taking a JUCE dependency through the
/// interface itself. Concrete adapters (in `engine/src/fx/`) are free
/// to include `juce_dsp` to drive the underlying OTTO processor.
///
/// Thread + RT-safety contract (mirrors `IEffectChainHost::pumpSlot` —
/// see `docs/RT_SAFETY_CONTRACT.md §6` for the full audit shape):
///
///   * `prepare` runs on the message thread when the adapter is bound to
///     a slot, or whenever the audio device's sample-rate / max-block
///     changes. It MAY allocate (filter stages, coefficient tables).
///     Never called from the audio thread.
///   * `reset` is `noexcept` and allocation-free; it may run either from
///     the message thread or on the audio thread (e.g. a transport jump
///     that needs to clear filter history). Bounded in time by the
///     adapter's own state size.
///   * `process` is `noexcept`, allocation-free, lock-free, and bounded
///     by `numChannels * numSamples`. No `new`, `malloc`, container
///     growth, `std::mutex`, `juce::Logger`, `DBG`, `throw`, or any
///     blocking syscall. The host calls it once per slot per audio
///     buffer, with the audio callback ATTACHED — any violation
///     introduces unbounded latency on the audio thread.
///
/// `process` returns `true` when `outChannels` was written. It returns
/// `false` when the adapter is not yet ready (e.g. `prepare` has not run
/// yet) — same contract as `IEffectChainHost::pumpSlot`: on `false`,
/// `outChannels` is LEFT UNMODIFIED and the caller treats the miss as a
/// dry passthrough (the caller is responsible for ensuring
/// `outChannels` already holds the dry signal when the adapter is
/// invoked in-place).
///
/// `inChannels` and `outChannels` MAY alias (point at the same
/// buffers). Implementations must support in-place processing — either
/// by reading all input before writing any output, or by copying the
/// input through their own internal state. The contract matches
/// `IEffectChainHost::pumpSlot` so a future `Bus::process` chain branch
/// can drive internal-FX adapters through the same in-place pattern.
class IInternalFxAdapter
{
public:
    virtual ~IInternalFxAdapter() = default;

    /// Message-thread setup. Allocates whatever per-sample-rate /
    /// per-block state the adapter needs. Must complete before the audio
    /// callback is re-attached.
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /// Clear internal state (filter history, delay lines, etc.).
    /// `noexcept` + allocation-free per the RT contract above.
    virtual void reset() noexcept = 0;

    /// Audio-thread DSP. See the class-level contract for the
    /// `outChannels`-unmodified-on-miss semantics and the aliasing rule.
    virtual bool process (const float* const* inChannels,
                          float* const*       outChannels,
                          int                 numChannels,
                          int                 numSamples) noexcept = 0;
};

} // namespace sirius
