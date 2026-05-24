#pragma once

#include "ida/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerEQ.h>

#include <array>
#include <atomic>

namespace ida
{

/// IDA-side adapter wrapping OTTO's header-only `PlayerEQ`. T3a of
/// the P7 internal-FX umbrella — the first of four (EQ → CMP → DLY → RVB)
/// adapter implementations that make the `Internal` `EffectChainEntry`
/// case actually do DSP.
///
/// Holds an `otto::effects::PlayerEQ` as a value member (no heap) plus a
/// two-entry config array with an atomic live-index — the lock-free
/// double-buffered config-swap pattern from
/// `external/OTTO/src/otto-core/include/otto/mixer/MasterBus.h:217-240`.
/// Construction enables the EQ (`cfg.eqEnabled = true`) so a freshly-
/// inserted adapter actually runs against PlayerEQ's flat default
/// (HP at 20 Hz, shelves at 0 dB gain, LP at 20 kHz) — audio passes
/// unchanged until the operator dials something in. Operator edits
/// arrive via `scratchConfig()` + `commitConfig()`; the audio thread
/// observes them through `liveConfig()`.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` and `liveConfig()` are `noexcept`, allocation-free,
/// lock-free. `commitConfig()` calls `PlayerEQ::updateCoefficients`,
/// which DOES allocate (juce::dsp::IIR::Coefficients heap objects), so
/// it is message-thread only — same precondition as
/// `OutOfProcessEffectChainHost::setInternalFxAtSlot` (the audio
/// callback must be detached). Verify via
/// `grep -nE "new |malloc|throw|std::mutex|Logger|DBG" engine/src/fx/EqAdapter.cpp`.
class EqAdapter final : public IInternalFxAdapter
{
public:
    EqAdapter();
    ~EqAdapter() override = default;

    EqAdapter (const EqAdapter&) = delete;
    EqAdapter& operator= (const EqAdapter&) = delete;
    EqAdapter (EqAdapter&&) = delete;
    EqAdapter& operator= (EqAdapter&&) = delete;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    bool process (const float* const* inChannels,
                  float* const*       outChannels,
                  int                 numChannels,
                  int                 numSamples) noexcept override;

    /// Message-thread write surface. Returns a mutable reference to the
    /// stale half of the double-buffered config; the caller mutates it
    /// in place and then calls `commitConfig()` to publish.
    otto::effects::PlayerEffectsConfig& scratchConfig() noexcept;

    /// Publish whatever was written to `scratchConfig()`: flip the live
    /// index with release ordering, call PlayerEQ::updateCoefficients
    /// against the new live config, then copy the new live snapshot back
    /// to the now-stale half so the next `scratchConfig()` starts from
    /// the current state rather than a partially-edited prior scratch.
    /// Allocates inside updateCoefficients — message-thread only.
    void commitConfig() noexcept;

    /// Audio-thread read surface. Acquire-loads the live index so writes
    /// from `commitConfig()` (release on the message thread) are visible
    /// here without a mutex. Safe to call from `process()`.
    const otto::effects::PlayerEffectsConfig& liveConfig() const noexcept;

    /// Slice EC — typed setter the host calls from
    /// `setInternalEqConfigAt(...)`. Maps the IDA-side `EqConfig` into
    /// the OTTO `PlayerEffectsConfig` fields, then routes through the
    /// scratch/commit pattern.
    void setEqConfig (const EqConfig& cfg) noexcept override;

    /// Slice EC — typed getter the host calls from
    /// `internalEqConfigAt(...)`. Snapshots `liveConfig()` and extracts
    /// the EQ fields.
    EqConfig eqConfig() const noexcept override;

private:
    otto::effects::PlayerEQ                                    eq_;
    std::array<otto::effects::PlayerEffectsConfig, 2>          cfgs_ {};
    std::atomic<int>                                           liveIndex_ { 0 };
    bool                                                       prepared_ = false;
};

} // namespace ida
