#pragma once

#include "sirius/IInternalFxAdapter.h"

#include <otto/effects/PlayerEffects.h>
#include <otto/effects/PlayerIRConvolution.h>

namespace sirius
{

/// Sirius-side adapter wrapping OTTO's header-only `PlayerIRConvolution`.
/// T3d of the P7 internal-FX umbrella — the last of four
/// (EQ → CMP → DLY → RVB) adapter implementations that make the `Internal`
/// `EffectChainEntry` case actually do DSP.
///
/// Holds an `otto::effects::PlayerIRConvolution` as a value member (no heap
/// at the adapter level — OTTO's convolver owns its own buffers + background
/// worker thread internally) and a defaulted `PlayerEffectsConfig`. T3d
/// ships **default config / no operator parameter UI** — the constructor
/// flips `cfg_.irEnabled = true` so the slot actually runs DSP, and sets
/// `cfg_.irPresetName` to the T3d default plate IR. Operator-facing IR
/// preset selection + parameter UI lands later with the GUI (T4/T5).
///
/// PlayerIRConvolution outputs **100 % wet** convolution (mixing is the
/// caller's job at the send level — IDA's chain dispatch already handles
/// dry/wet through `IInternalFxAdapter::process`'s in/out contract). A
/// freshly-inserted RVB slot reverberates the input through the default
/// plate IR once OTTO's background worker has finished loading + processing
/// the IR (typically <1 s after `prepare`).
///
/// IR loading is asynchronous: `prepare()` calls
/// `PlayerIRConvolution::requestIRLoad(path)` from the message thread,
/// which queues the path and notifies OTTO's worker. Until the worker
/// completes, `process()` early-exits silently inside OTTO (see
/// `PlayerIRConvolution::process` line 281 — guards `!loaded_`). The
/// audio-thread contract is therefore: pre-load -> silent pass-through
/// of the dry signal (the adapter copies in→out before the OTTO call);
/// post-load -> 100 % wet convolution.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` is `noexcept`, allocation-free, lock-free, bounded.
/// PlayerIRConvolution::process pre-flight: atomic-acquire on
/// `activeIndex_` for the double-buffered convolver swap, then
/// `juce::dsp::Convolution::process` (FFT-based, bounded by maxBlockSize),
/// then optional pre-delay (bounded by 200 ms ring). The grandfathered
/// mutex inside `requestIRLoad` is only contended between OTTO's
/// message-thread requestor and OTTO's worker — never the audio thread,
/// so IDA's RT contract is preserved.
///
/// **Not idempotent at the OTTO level**: each call to
/// `PlayerIRConvolution::prepare` allocates pre-delay vectors and starts
/// a new background thread. T03's host-side prepare early-return gate
/// (`OutOfProcessEffectChainHost`) protects against duplicate-prepare with
/// the same (sr, blk); a future migration to (sr, blk)-changing repeats
/// would need to first destroy + reconstruct the adapter.
///
/// `isLoaded()` is exposed as a test-only convenience — async IR-load
/// completion is data-dependent and tests need to poll for it. Production
/// code does not depend on this accessor.
class RvbAdapter final : public IInternalFxAdapter
{
public:
    RvbAdapter();
    ~RvbAdapter() override = default;

    RvbAdapter (const RvbAdapter&) = delete;
    RvbAdapter& operator= (const RvbAdapter&) = delete;
    RvbAdapter (RvbAdapter&&) = delete;
    RvbAdapter& operator= (RvbAdapter&&) = delete;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    bool process (const float* const* inChannels,
                  float* const*       outChannels,
                  int                 numChannels,
                  int                 numSamples) noexcept override;

    /// Test-only async-load probe. `const noexcept`; returns OTTO's
    /// internal `loaded_` flag (true once the background worker has
    /// finished processing + installing the IR into both convolvers).
    bool isLoaded() const noexcept { return conv_.isLoaded(); }

private:
    otto::effects::PlayerIRConvolution conv_;
    otto::effects::PlayerEffectsConfig cfg_ {};
    bool                               prepared_ = false;
};

} // namespace sirius
