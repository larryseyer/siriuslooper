// Tests for the runtime overload protection state machine (white paper Part
// 13.3). These pin down the unbreakable rules in code: audio is never shed,
// non-audio work is shed in strict priority order as load rises, the hysteresis
// band prevents flapping near a threshold, and the shed state is exposed for
// the UI to announce (rule 3 — degradation is announced, not silent).
#include "sirius/OverloadProtection.h"

#include <catch2/catch_test_macros.hpp>

using ida::OverloadProtection;
using ida::Workload;

TEST_CASE ("nothing is shed at start", "[overload]")
{
    OverloadProtection op;
    CHECK_FALSE (op.isShed (Workload::Audio));
    CHECK_FALSE (op.isShed (Workload::Analyzer));
    CHECK_FALSE (op.isShed (Workload::Ui));
    CHECK_FALSE (op.isShed (Workload::Video));
    CHECK (op.shedCount() == 0);
    CHECK (op.lastReportedLoad() < 1e-12); // default is 0.0; no load reported yet
}

TEST_CASE ("audio is never shed, regardless of load", "[overload]")
{
    // Rule 1: audio output never glitches. There is no load value, not even an
    // overrun (>1.0), that may shed audio. Encoding this as a test makes the
    // rule executable.
    OverloadProtection op;
    op.reportLoad (0.0);   CHECK_FALSE (op.isShed (Workload::Audio));
    op.reportLoad (0.5);   CHECK_FALSE (op.isShed (Workload::Audio));
    op.reportLoad (0.99);  CHECK_FALSE (op.isShed (Workload::Audio));
    op.reportLoad (1.5);   CHECK_FALSE (op.isShed (Workload::Audio));
}

TEST_CASE ("workloads shed in priority order as load rises", "[overload]")
{
    // Rule 4: video first, then UI, then analyzer. Analyzer is held longest
    // because listening work is more musically load-bearing than a video frame.
    OverloadProtection op;

    op.reportLoad (0.50);
    CHECK (op.shedCount() == 0);

    op.reportLoad (0.80);
    CHECK (op.isShed (Workload::Video));
    CHECK_FALSE (op.isShed (Workload::Ui));
    CHECK_FALSE (op.isShed (Workload::Analyzer));

    op.reportLoad (0.90);
    CHECK (op.isShed (Workload::Video));
    CHECK (op.isShed (Workload::Ui));
    CHECK_FALSE (op.isShed (Workload::Analyzer));

    op.reportLoad (0.99);
    CHECK (op.isShed (Workload::Video));
    CHECK (op.isShed (Workload::Ui));
    CHECK (op.isShed (Workload::Analyzer));
    CHECK (op.shedCount() == 3);
}

TEST_CASE ("workloads restore in reverse order as load falls", "[overload]")
{
    OverloadProtection op;
    op.reportLoad (0.99); // everything shed

    op.reportLoad (0.80); // analyzer restores first — its restore threshold is 0.85
    CHECK_FALSE (op.isShed (Workload::Analyzer));
    CHECK (op.isShed (Workload::Ui));
    CHECK (op.isShed (Workload::Video));

    op.reportLoad (0.70); // ui restores — its restore threshold is 0.75
    CHECK_FALSE (op.isShed (Workload::Ui));
    CHECK (op.isShed (Workload::Video));

    op.reportLoad (0.50); // video restores — well below 0.65
    CHECK (op.shedCount() == 0);
}

TEST_CASE ("a load that stays inside a hysteresis band does not flap",
           "[overload]")
{
    // The video band is [0.65, 0.75]. Once shed at >0.75, the state must stay
    // shed until load actually drops below 0.65; otherwise an audio callback
    // hovering at 0.70 would shed-and-restore every report, defeating the
    // purpose of having a ladder at all.
    OverloadProtection op;
    op.reportLoad (0.80); // shed
    REQUIRE (op.isShed (Workload::Video));

    op.reportLoad (0.70); // still inside the band — stays shed
    CHECK (op.isShed (Workload::Video));

    op.reportLoad (0.66); // still inside — stays shed
    CHECK (op.isShed (Workload::Video));

    op.reportLoad (0.60); // clearly below — restored
    CHECK_FALSE (op.isShed (Workload::Video));
}

// NOTE: negative load is now rejected via assert() in debug builds.
// The throw-based test was removed when reportLoad became noexcept (M3 Session 2).
// The contract remains: a negative value is a programming error.

TEST_CASE ("an overrunning callback (load > 1.0) is accepted, not clamped",
           "[overload]")
{
    // Load above 1.0 means the audio callback has overrun its budget — a
    // genuine and informative measurement, not a noisy artifact to discard.
    OverloadProtection op;
    op.reportLoad (1.20);
    CHECK (op.lastReportedLoad() > 1.19); // value is stored verbatim, no arithmetic
    CHECK (op.lastReportedLoad() < 1.21);
    CHECK (op.shedCount() == 3); // every sheddable workload is shed at extreme load
}
