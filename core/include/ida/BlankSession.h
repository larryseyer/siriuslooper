#pragma once

#include "ida/Constituent.h"
#include "ida/InputDescriptor.h"
#include "ida/Rational.h"
#include "ida/TempoMap.h"

#include <memory>
#include <vector>

namespace ida
{

/// The boot / New-Song state (spec §4.1, §11): a *usable empty project*. Shaped
/// identically to DemoSession in the fields MainComponent consumes, so swapping
/// the constructor's `demo_(buildDemoSession())` for `buildBlankSession()` is a
/// mechanical change and every `demo_.<field>` read keeps working unchanged.
/// Unlike DemoSession there is no pre-authored arrangement: the root Constituent
/// is an empty session shell, there are no inputs, and the span is zero.
struct BlankSession
{
    std::shared_ptr<const Constituent> root;   ///< empty session shell (no children)
    TempoMap sessionToLmc;                       ///< a valid default tempo map
    Rational lengthLmcSeconds;                   ///< zero — nothing is placed yet
    std::vector<InputDescriptor> inputs;         ///< empty — channels are user-created (spec §4.1)
};

/// Builds the blank boot session. Pure (JUCE-free), deterministic, no clock /
/// filesystem access. The session shell carries the canonical root id (1) and a
/// [0,0) span; the default tempo map (120 BPM) places it in LMC time so the
/// existing RenderPipeline / timeline math has a real map to apply even though
/// nothing is placed yet.
BlankSession buildBlankSession();

} // namespace ida
