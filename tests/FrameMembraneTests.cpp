// Tests for the video membrane's rate-conversion math (white paper Part 5.3).
// The membrane's commitment is nearest-frame selection: at each query, the
// source frame whose presentation time is closest is shown. These tests pin
// down the exact arithmetic of that selection — including the awkward
// broadcast rates (23.976, 29.97, 59.94) where rounding has to stay rational
// to avoid the 1953-NTSC drift the system exists to refuse.
#include "ida/FrameMembrane.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::convertFrameRate;
using ida::FrameMembrane;
using ida::Rational;

TEST_CASE ("the membrane rejects non-positive frame rates", "[membrane]")
{
    CHECK_THROWS_AS (FrameMembrane (Rational (0)),  std::invalid_argument);
    CHECK_THROWS_AS (FrameMembrane (Rational (-1)), std::invalid_argument);
    CHECK_THROWS_AS (convertFrameRate (Rational (0),  Rational (30), 1),
                     std::invalid_argument);
    CHECK_THROWS_AS (convertFrameRate (Rational (30), Rational (0),  1),
                     std::invalid_argument);
    CHECK_THROWS_AS (convertFrameRate (Rational (30), Rational (30), -1),
                     std::invalid_argument);
}

TEST_CASE ("frame presentation times advance by exactly 1/fps", "[membrane]")
{
    FrameMembrane m (Rational (24));
    CHECK (m.presentationTimeOf (0) == Rational (0));
    CHECK (m.presentationTimeOf (1) == Rational (1, 24));
    CHECK (m.presentationTimeOf (24) == Rational (1));    // one full second
    CHECK (m.frameDuration() == Rational (1, 24));
}

TEST_CASE ("awkward broadcast rates stay exact", "[membrane]")
{
    // 23.976 = 24000/1001, 29.97 = 30000/1001 — both expressible exactly as
    // Rationals. The whole point of refusing floating-point in the engine is
    // that pulldown drift cannot accumulate.
    const FrameMembrane m23976 (Rational (24000, 1001));
    CHECK (m23976.presentationTimeOf (24000) == Rational (24000) * Rational (1001, 24000));
    CHECK (m23976.presentationTimeOf (24000) == Rational (1001, 1));
    // Sanity: 24000 frames at 23.976 fps takes exactly 1001 seconds.

    const FrameMembrane m2997 (Rational (30000, 1001));
    CHECK (m2997.frameDuration() == Rational (1001, 30000));
}

TEST_CASE ("nearestFrameIndex picks the source frame closest in time",
           "[membrane]")
{
    // Source at 30 fps. Query times midway between frames should round to the
    // *next* frame (halves round toward +∞), matching the membrane's stated
    // implementation.
    FrameMembrane m (Rational (30));
    CHECK (m.nearestFrameIndex (Rational (0))        == 0);
    CHECK (m.nearestFrameIndex (Rational (1, 60))    == 1); // midway: rounds up
    CHECK (m.nearestFrameIndex (Rational (1, 30))    == 1); // exactly frame 1
    CHECK (m.nearestFrameIndex (Rational (49, 60))   == 25); // 24.5 → 25
}

TEST_CASE ("nearestFrameIndex handles times before the source starts",
           "[membrane]")
{
    FrameMembrane m (Rational (30));
    CHECK (m.nearestFrameIndex (Rational (-1, 30)) == -1);
    CHECK (m.nearestFrameIndex (Rational (-1, 60)) == 0); // midway rounds to 0
}

TEST_CASE ("equal source and target rates pass through 1:1",
           "[membrane][rate-conversion]")
{
    const auto indices = convertFrameRate (Rational (30), Rational (30), 6);
    REQUIRE (indices.size() == 6);
    for (std::size_t i = 0; i < indices.size(); ++i)
        CHECK (indices[i] == static_cast<std::int64_t> (i));
}

TEST_CASE ("24 → 30 stuffs frames: 5 target frames consume 4 source frames",
           "[membrane][rate-conversion]")
{
    // 4:5 ratio. At nearest-frame, the deterministic pattern for the first
    // 5 target frames is 0, 1, 2, 2, 3 — frame index 2 repeats, which is the
    // stuffed frame.
    const auto indices = convertFrameRate (Rational (24), Rational (30), 5);
    REQUIRE (indices.size() == 5);
    CHECK (indices[0] == 0);
    CHECK (indices[1] == 1);
    CHECK (indices[2] == 2);
    CHECK (indices[3] == 2); // stuffed
    CHECK (indices[4] == 3);
}

TEST_CASE ("30 → 24 drops frames: 4 target frames consume 5 source frames",
           "[membrane][rate-conversion]")
{
    // 5:4 ratio. At nearest-frame with halves rounding up, the first 4 target
    // frames map to 0, 1, 3, 4 — frame index 2 is dropped.
    const auto indices = convertFrameRate (Rational (30), Rational (24), 4);
    REQUIRE (indices.size() == 4);
    CHECK (indices[0] == 0);
    CHECK (indices[1] == 1);
    CHECK (indices[2] == 3); // 2 dropped
    CHECK (indices[3] == 4);
}

TEST_CASE ("zero target frames produces an empty conversion",
           "[membrane][rate-conversion]")
{
    const auto indices = convertFrameRate (Rational (24), Rational (30), 0);
    CHECK (indices.empty());
}

TEST_CASE ("offset start times shift the alignment without changing the pattern",
           "[membrane][rate-conversion]")
{
    // Source starts 1 second after target. The first target frame at 0 s
    // lands before source frame 0, so it maps to a negative index — the
    // caller's signal to either hold the first frame or blank.
    const auto indices = convertFrameRate (
        Rational (30), Rational (30), 3,
        /*sourceStart*/ Rational (1),
        /*targetStart*/ Rational (0));
    REQUIRE (indices.size() == 3);
    CHECK (indices[0] == -30); // one second before source's first frame
    CHECK (indices[1] == -29);
    CHECK (indices[2] == -28);
}
