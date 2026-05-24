#pragma once

#include "ida/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerCompressor.h>

#include <array>
#include <atomic>

namespace ida
{

/// IDA-side adapter wrapping OTTO's header-only `PlayerCompressor`. T3b
/// of the P7 internal-FX umbrella — the second of four (EQ → CMP → DLY →
/// RVB) adapter implementations that make the `Internal` `EffectChainEntry`
/// case actually do DSP.
///
/// Holds an `otto::effects::PlayerCompressor` as a value member (no heap)
/// plus a two-entry config array with an atomic live-index — the lock-
/// free double-buffered config-swap pattern from
/// `external/OTTO/src/otto-core/include/otto/mixer/MasterBus.h:217-240`.
/// Construction enables the compressor (`cfg.compEnabled = true`) so a
/// freshly-inserted CMP slot immediately compresses peaks above the
/// default -12 dB threshold against PlayerCompressor's defaults
/// (threshold -12 dB, 4:1 ratio, 10 ms attack, 100 ms release, makeup
/// 0 dB, mix 1.0 fully wet, sidechain HPF on at 100 Hz Butterworth × 4
/// stages). Operator edits arrive via `scratchConfig()` +
/// `commitConfig()`; the audio thread observes them through
/// `liveConfig()`.
///
/// Sidechain is derived internally from the input (input → optional 4-
/// stage Butterworth HPF at 100 Hz → peak detector → envelope follower
/// → gain calculation). No external sidechain bus required. IDA has
/// no sidechain-bus routing concept today; if the operator later asks
/// for external sidechain, a separate slice will widen the adapter
/// contract to accept a second input pointer.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` and `liveConfig()` are `noexcept`, allocation-free,
/// lock-free. `commitConfig()` calls `PlayerCompressor::updateParameters`,
/// which precomputes envelope coefficients without heap allocation but
/// is still nominally message-thread-only — same precondition as
/// `OutOfProcessEffectChainHost::setInternalFxAtSlot` (audio callback
/// detached). Verify via
/// `grep -nE "new |malloc|throw|std::mutex|Logger|DBG" engine/src/fx/CmpAdapter.cpp`.
class CmpAdapter final : public IInternalFxAdapter
{
public:
    CmpAdapter();
    ~CmpAdapter() override = default;

    CmpAdapter (const CmpAdapter&) = delete;
    CmpAdapter& operator= (const CmpAdapter&) = delete;
    CmpAdapter (CmpAdapter&&) = delete;
    CmpAdapter& operator= (CmpAdapter&&) = delete;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    bool process (const float* const* inChannels,
                  float* const*       outChannels,
                  int                 numChannels,
                  int                 numSamples) noexcept override;

    /// Message-thread write surface — see EqAdapter::scratchConfig.
    otto::effects::PlayerEffectsConfig& scratchConfig() noexcept;

    /// Publish whatever was written to `scratchConfig()` — see
    /// EqAdapter::commitConfig. Calls PlayerCompressor::updateParameters
    /// against the new live half.
    void commitConfig() noexcept;

    /// Audio-thread read surface — see EqAdapter::liveConfig.
    const otto::effects::PlayerEffectsConfig& liveConfig() const noexcept;

    /// Slice EC — typed setter the host calls from
    /// `setInternalCmpConfigAt(...)`. Maps the IDA-side `CmpConfig` into
    /// the OTTO `PlayerEffectsConfig` fields, then routes through the
    /// scratch/commit pattern.
    void setCmpConfig (const CmpConfig& cfg) noexcept override;

    /// Slice EC — typed getter the host calls from
    /// `internalCmpConfigAt(...)`. Snapshots `liveConfig()` and extracts
    /// the compressor fields.
    CmpConfig cmpConfig() const noexcept override;

private:
    otto::effects::PlayerCompressor                            comp_;
    std::array<otto::effects::PlayerEffectsConfig, 2>          cfgs_ {};
    std::atomic<int>                                           liveIndex_ { 0 };
    bool                                                       prepared_ = false;
};

} // namespace ida
