#include "ida/InternalFxFactory.h"

#include "fx/CmpAdapter.h"
#include "fx/DlyAdapter.h"
#include "fx/EqAdapter.h"
#include "fx/RvbAdapter.h"
#include "fx/TapeColorAdapter.h"

namespace ida
{

std::unique_ptr<IInternalFxAdapter> makeInternalFxAdapter (InternalFxId id)
{
    switch (id)
    {
        case InternalFxId::kEq:
            return std::make_unique<EqAdapter>();

        // T3b — CMP adapter wraps otto::effects::PlayerCompressor with the
        // default config (sidechain derived internally from input;
        // sidechain HPF on at 100 Hz Butterworth × 4 stages). The ctor
        // flips compEnabled=true so a freshly-inserted CMP slot does
        // DSP from sample 0.
        case InternalFxId::kCmp:
            return std::make_unique<CmpAdapter>();

        // T3d — RVB adapter wraps otto::effects::PlayerIRConvolution.
        // The ctor flips irEnabled=true and pins the default plate IR
        // ("Plate Bright 1.13"); prepare() requests an async IR load
        // through OTTO's grandfathered background worker. Until the worker
        // installs the IR, conv_.process early-exits and the adapter
        // produces silent pass-through; once installed, 100 % wet
        // convolution. Default IR path resolves through the CMake-injected
        // IDA_OTTO_ASSETS_DIR macro.
        case InternalFxId::kRvb:
            return std::make_unique<RvbAdapter>();

        // T3c — DLY adapter wraps otto::effects::PlayerDelay. The ctor
        // flips delayEnabled=true AND delaySyncEnabled=false so a
        // freshly-inserted DLY slot tap-echoes the input at the default
        // free-running 250 ms — the sync path needs a transport bpm IDA
        // doesn't plumb yet.
        case InternalFxId::kDly:
            return std::make_unique<DlyAdapter>();

        // TAPECOLOR — wraps lsfx::TapeColorProcessor from the
        // lsfx_tapecolor submodule (Phase 5: DC blocker + emphasis EQ +
        // IR convolution + Jiles-Atherton hysteresis + wow/flutter/scrape
        // modulation). Unlike the other four, the adapter ctor does NOT
        // flip enabled=true — TAPECOLOR is default-OFF everywhere per the
        // 2026-05-24 operator design lock; a freshly-inserted slot is
        // silent passthrough until the operator turns it on. The (future
        // slice) param-UI commits config swaps via TapeColorProcessor's
        // scratchConfig / commitConfig pattern.
        case InternalFxId::kTapeColor:
            return std::make_unique<TapeColorAdapter>();
    }

    // Unreachable for the four declared enum values; return nullptr to keep
    // the function total. The enum's underlying type reserves space up to 15
    // for future built-ins; any reserved-but-undeclared id passed here
    // (e.g. through a corrupted load) should fall through to nullptr so the
    // host treats it as "no adapter for this slot" rather than crash.
    return nullptr;
}

} // namespace ida
