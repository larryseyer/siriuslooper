#pragma once

#include "ida/Constituent.h"
#include "ida/Rational.h"
#include "ida/TempoMap.h"

#include <string>

namespace sirius
{

/// What the Performance view shows at a given moment — glanceable, not
/// readable (white paper Part 14.5). The performer's eyes spend microseconds
/// here, not whole gestures, so the state is deliberately tiny: what is
/// playing right now, and how its repetition is unfolding.
///
/// Anything richer — every Constituent's metadata, parameter sliders, tempo
/// map breakpoints — belongs to the Preparation view (Part 14.6), where the
/// performer is allowed to look at the screen.
struct PerformanceViewState
{
    /// True when nothing is currently sounding at the playhead.
    bool isSilent { true };

    /// The name (or fallback identifier) of the foreground sounding phrase,
    /// or empty when `isSilent`.
    std::string currentPhraseName;

    /// A short string describing the current cycle: "3 of 8" for `NTimes`,
    /// "∞" for `Forever`, "once" for `Once`, etc. Empty when silent.
    std::string cycleStatus;

    /// Total active loops sounding right now — small integer, fits the
    /// "never more than three things at once" rule (14.5) without overflowing
    /// the legible budget.
    int soundingLoopCount { 0 };

    /// The playhead position the state was computed for, in LMC seconds, as a
    /// floating-point convenience for the renderer (the engine remains exact).
    double playheadSeconds { 0.0 };
};

/// Computes the Performance-view state from the current Constituent tree, the
/// session's tempo map to LMC time, and the playhead position. Walks the tree
/// the same way the RenderPipeline does to identify the foreground phrase the
/// performer should see named — the loop closest to the front of the
/// depth-first traversal that is actually sounding.
PerformanceViewState selectPerformanceView (const Constituent& root,
                                            const TempoMap&    sessionToLmc,
                                            Rational           playheadLmcSeconds);

} // namespace sirius
