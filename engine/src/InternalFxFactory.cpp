#include "sirius/InternalFxFactory.h"

#include "fx/CmpAdapter.h"
#include "fx/EqAdapter.h"

namespace sirius
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
        // Sequenced LAST in T3 because the convolution carries
        // background-thread IR-loading complexity and needs
        // ${OTTO_ASSETS_DIR}/IR/... wiring (Decision 3 +
        // continue.md / docs/superpowers/specs/2026-05-22-otto-integration-design.md).
        case InternalFxId::kRvb:
            return nullptr;

        // T3c — DLY adapter wraps otto::effects::PlayerDelay.
        case InternalFxId::kDly:
            return nullptr;
    }

    // Unreachable for the four declared enum values; return nullptr to keep
    // the function total. The enum's underlying type reserves space up to 15
    // for future built-ins; anything in that reserved range that lands here
    // before its adapter is implemented should fall through to nullptr the
    // same way the un-implemented declared values do.
    return nullptr;
}

} // namespace sirius
