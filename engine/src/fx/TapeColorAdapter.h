#pragma once

#include "ida/IInternalFxAdapter.h"

#include <lsfx_tapecolor/lsfx_tapecolor.h>

namespace ida
{

/// IDA-side adapter wrapping `lsfx::TapeColorProcessor` from the
/// `lsfx_tapecolor` submodule (Phase 5: DC blocker + emphasis EQ + IR
/// convolution + Jiles-Atherton hysteresis + wow/flutter/scrape modulation).
/// TAPECOLOR is the 5th internal FX (`InternalFxId::kTapeColor`); it ships
/// alongside EQ/CMP/RVB/DLY as a slot-anywhere built-in.
///
/// **Default OFF — operator drops it explicitly.** Unlike the other four
/// internal-FX adapters, TapeColorAdapter's constructor does NOT flip an
/// "enabled" flag. The wrapped `lsfx::TapeColorProcessor`'s default config
/// (`TapeColorConfig::enabled = false`) leaves a freshly-inserted slot as a
/// silent passthrough. This matches the operator rule (2026-05-24 design
/// lock): TAPECOLOR is default-off everywhere; the operator must turn it on
/// per-slot. Per-tape Mode A (tape-bound, in slice 2) carries its own tri-
/// state (None / BeforeWrite / AfterRead) on top of this and is wired in a
/// separate slice.
///
/// Audio-thread contract enforced per `docs/RT_SAFETY_CONTRACT.md §6`:
/// `process()` is `noexcept`, allocation-free, lock-free, bounded.
/// `lsfx::TapeColorProcessor::process` is alloc-/lock-/log-/I/O-free per
/// its docs and Phase 5 review. Parameter publication uses the double-
/// buffered config-swap pattern (scratchConfig / commitConfig / liveConfig);
/// only `commitConfig` is needed from the message-thread side, and only the
/// future param-UI slice will exercise it.
class TapeColorAdapter final : public IInternalFxAdapter
{
public:
    TapeColorAdapter();
    ~TapeColorAdapter() override = default;

    TapeColorAdapter (const TapeColorAdapter&) = delete;
    TapeColorAdapter& operator= (const TapeColorAdapter&) = delete;
    TapeColorAdapter (TapeColorAdapter&&) = delete;
    TapeColorAdapter& operator= (TapeColorAdapter&&) = delete;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    bool process (const float* const* inChannels,
                  float* const*       outChannels,
                  int                 numChannels,
                  int                 numSamples) noexcept override;

private:
    lsfx::TapeColorProcessor processor_;
    bool                     prepared_ = false;
};

} // namespace ida
