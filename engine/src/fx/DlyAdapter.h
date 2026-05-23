#pragma once

#include "ida/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerDelay.h>

namespace sirius
{

/// Sirius-side adapter wrapping OTTO's header-only `PlayerDelay`. T3c of
/// the P7 internal-FX umbrella — the third of four (EQ → CMP → DLY → RVB)
/// adapter implementations that make the `Internal` `EffectChainEntry`
/// case actually do DSP.
///
/// Holds an `otto::effects::PlayerDelay` as a value member (no heap) and a
/// defaulted `PlayerEffectsConfig`. T3c ships **default config / no
/// operator parameter UI** — the constructor flips two fields:
///   1. `cfg_.delayEnabled = true` so the slot actually runs DSP.
///   2. `cfg_.delaySyncEnabled = false` so the delay time comes from
///      `delayTimeMs` (250 ms default) rather than tempo-sync. PlayerDelay's
///      tempo-sync path requires a live bpm > 0 — the internal-FX adapter
///      surface (`prepare(sr, blk)`) has no transport hookup today, so
///      free-running mode is the only way to get a deterministic delay
///      time. A later slice plumbs bpm into the engine and lets the
///      operator flip sync back on. Free-running 250 ms is the same
///      delay-time floor `PlayerEffectsConfig::resetToDefaults` ships.
///
/// PlayerDelay outputs **100 % wet** (mixing is the caller's job at the
/// send level — IDA's chain dispatch already handles dry/wet through
/// `IInternalFxAdapter::process`'s in/out contract). Feedback defaults to
/// 0.4 with a 80 Hz–8 kHz filtered feedback path, ping-pong off, so a
/// freshly-inserted DLY slot tap-echoes the input every 250 ms with a
/// modest decay tail.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` is `noexcept`, allocation-free, lock-free, bounded.
/// PlayerDelay::process pre-flight confirmed: only `popSample`/`pushSample`
/// on pre-prepared delay lines plus filter `processSample` + atomic
/// fetch_add — no allocation under modulation. (updateFilters allocates
/// IIR coefficients but lives in `updateParameters` on the message
/// thread, called from `prepare`.) Verify via
/// `grep -nE "new |malloc|throw|std::mutex|Logger|DBG" engine/src/fx/DlyAdapter.cpp`.
class DlyAdapter final : public IInternalFxAdapter
{
public:
    DlyAdapter();
    ~DlyAdapter() override = default;

    DlyAdapter (const DlyAdapter&) = delete;
    DlyAdapter& operator= (const DlyAdapter&) = delete;
    DlyAdapter (DlyAdapter&&) = delete;
    DlyAdapter& operator= (DlyAdapter&&) = delete;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    bool process (const float* const* inChannels,
                  float* const*       outChannels,
                  int                 numChannels,
                  int                 numSamples) noexcept override;

private:
    otto::effects::PlayerDelay         dly_;
    otto::effects::PlayerEffectsConfig cfg_ {};
    bool                               prepared_ = false;
};

} // namespace sirius
