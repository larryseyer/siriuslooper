// Tests for the visual/tactile latency tracker (white paper Part 14.8). The
// audio path's <30 ms causal-coupling budget is enforced upstream; this is
// the UI's honest mirror of the same number — it tracks measured frame
// latencies and reports the fraction that landed within budget, so the system
// can announce its own degradation rather than hiding it (Part 13.3, rule 3).
#include "sirius/LatencyBudget.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::bandOf;
using sirius::LatencyBand;
using sirius::LatencyBudget;

TEST_CASE ("perceptual bands match white paper 14.8 thresholds", "[latency]")
{
    CHECK (bandOf (5.0)   == LatencyBand::Proprioceptive); // < 10 ms
    CHECK (bandOf (9.999) == LatencyBand::Proprioceptive);
    CHECK (bandOf (10.0)  == LatencyBand::Causal);         // 10–30 ms
    CHECK (bandOf (29.999) == LatencyBand::Causal);
    CHECK (bandOf (30.0)  == LatencyBand::Response);       // 30–100 ms
    CHECK (bandOf (99.999) == LatencyBand::Response);
    CHECK (bandOf (100.0) == LatencyBand::Waiting);        // > 100 ms
}

TEST_CASE ("an empty tracker reports zero stats and a clean budget", "[latency]")
{
    // Silent absence is not degradation: a tracker that has never measured a
    // frame should report success, not phantom failure.
    LatencyBudget budget (32);
    CHECK (budget.empty());
    CHECK (budget.size() == 0);
    CHECK (budget.meanMs() < 1e-12);
    CHECK (budget.worstMs() < 1e-12);
    CHECK (budget.fractionWithinBudget() > 0.999);
    CHECK (budget.meetsBudget());
}

TEST_CASE ("rejects bad inputs and constructor arguments", "[latency]")
{
    CHECK_THROWS_AS (LatencyBudget (0), std::invalid_argument);
    LatencyBudget budget (4);
    CHECK_THROWS_AS (budget.record (-0.01), std::invalid_argument);
}

TEST_CASE ("records samples and exposes mean/worst/budget statistics",
           "[latency]")
{
    LatencyBudget budget (16);
    budget.record (5.0);
    budget.record (15.0);
    budget.record (40.0); // outside the 30-ms budget

    CHECK (budget.size() == 3);
    CHECK (budget.worstMs() > 39.99);
    CHECK (budget.worstMs() < 40.01);
    CHECK (budget.meanMs() > 19.99);
    CHECK (budget.meanMs() < 20.01);

    // Two of three samples landed within budget — that's the honest report.
    CHECK (budget.fractionWithinBudget() > 0.66);
    CHECK (budget.fractionWithinBudget() < 0.67);
    CHECK_FALSE (budget.meetsBudget());
}

TEST_CASE ("the ring window forgets older samples", "[latency]")
{
    // The window is rolling, not cumulative — a single long-ago spike should
    // not poison the readout once it has rolled out of the window.
    LatencyBudget budget (4);
    budget.record (1000.0); // one bad apple
    for (int i = 0; i < 4; ++i)
        budget.record (5.0);

    CHECK (budget.size() == 4);
    CHECK (budget.worstMs() < 6.0);     // the spike has been overwritten
    CHECK (budget.meetsBudget());
}
