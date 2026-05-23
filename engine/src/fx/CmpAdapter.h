#pragma once

#include "sirius/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerCompressor.h>

namespace sirius
{

/// Sirius-side adapter wrapping OTTO's header-only `PlayerCompressor`. T3b
/// of the P7 internal-FX umbrella — the second of four (EQ → CMP → DLY →
/// RVB) adapter implementations that make the `Internal` `EffectChainEntry`
/// case actually do DSP.
///
/// Holds an `otto::effects::PlayerCompressor` as a value member (no heap)
/// and a defaulted `PlayerEffectsConfig`. T3b ships **default config / no
/// operator parameter UI** — the constructor flips `cfg_.compEnabled` to
/// `true` so the compressor actually runs against PlayerCompressor's
/// default parameters (threshold -12 dB, 4:1 ratio, 10 ms attack,
/// 100 ms release, makeup 0 dB, mix 1.0 fully wet, sidechain HPF on at
/// 100 Hz Butterworth × 4 stages), and a later UI slice exposes the
/// parameter surface documented in `docs/design/ida-internal-fx.md`
/// §CMP. Unlike the EQ's flat-default no-op, a freshly inserted CMP slot
/// immediately compresses peaks above -12 dB — the operator dials from
/// there.
///
/// Sidechain is derived internally from the input (input → optional 4-
/// stage Butterworth HPF at 100 Hz → peak detector → envelope follower
/// → gain calculation). No external sidechain bus required. IDA has
/// no sidechain-bus routing concept today; if the operator later asks
/// for external sidechain, a separate slice will widen the adapter
/// contract to accept a second input pointer.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` is `noexcept`, allocation-free, lock-free, bounded.
/// Verify via `grep -nE "new |malloc|throw|std::mutex|Logger|DBG" engine/src/fx/CmpAdapter.cpp`.
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

private:
    otto::effects::PlayerCompressor    comp_;
    otto::effects::PlayerEffectsConfig cfg_ {};
    bool                               prepared_ = false;
};

} // namespace sirius
