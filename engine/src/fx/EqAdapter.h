#pragma once

#include "sirius/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerEQ.h>

namespace sirius
{

/// Sirius-side adapter wrapping OTTO's header-only `PlayerEQ`. T3a of
/// the P7 internal-FX umbrella — the first of four (EQ → CMP → DLY → RVB)
/// adapter implementations that make the `Internal` `EffectChainEntry`
/// case actually do DSP.
///
/// Holds an `otto::effects::PlayerEQ` as a value member (no heap) and a
/// defaulted `PlayerEffectsConfig`. T3a ships **default config / no
/// operator parameter UI** — the constructor flips `cfg_.eqEnabled` to
/// `true` so the EQ actually runs against the default-flat curve, and a
/// later UI slice exposes the parameter surface documented in
/// `docs/design/sirius-internal-fx.md` §EQ. Defaults produce a flat
/// response (HP at the bypass-equivalent 20 Hz, shelves at 0 dB gain,
/// LP at the bypass-equivalent 20 kHz), so a freshly-inserted EQ slot
/// passes audio unchanged until the operator dials something in.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` is `noexcept`, allocation-free, lock-free, bounded.
/// Verify via `grep -nE "new |malloc|throw|std::mutex|Logger|DBG" engine/src/fx/EqAdapter.cpp`.
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

private:
    otto::effects::PlayerEQ          eq_;
    otto::effects::PlayerEffectsConfig cfg_ {};
    bool                             prepared_ = false;
};

} // namespace sirius
