// Golden-value tests for ida::TempoMap — the conceptual transformation
// between two time domains (white paper Part 5.4). These tests pin down the
// exact piecewise-linear mapping, including extrapolation past the breakpoints.
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using ida::Rational;
using ida::TempoMap;

TEST_CASE ("a constant map is a line through the origin", "[tempomap]")
{
    const TempoMap doubling = TempoMap::constant (Rational (2));
    CHECK (doubling.apply (Rational (0))    == Rational (0));
    CHECK (doubling.apply (Rational (3))    == Rational (6));
    CHECK (doubling.apply (Rational (1, 2)) == Rational (1));
    // Extrapolates backwards through the origin too.
    CHECK (doubling.apply (Rational (-5))   == Rational (-10));
}

TEST_CASE ("fromBpm converts whole notes to seconds exactly", "[tempomap][conceptual-time]")
{
    // 120 quarter-note BPM: a 4/4 bar (one whole note) lasts exactly 2 seconds.
    const TempoMap at120 = TempoMap::fromBpm (Rational (120));
    CHECK (at120.apply (Rational (1))    == Rational (2));      // one whole note
    CHECK (at120.apply (Rational (1, 4)) == Rational (1, 2));   // one quarter note
    CHECK (at120.apply (Rational (8))    == Rational (16));     // eight bars of 4/4

    // A tempo that does not divide evenly stays exact: 90 BPM => 240/90 = 8/3
    // seconds per whole note.
    const TempoMap at90 = TempoMap::fromBpm (Rational (90));
    CHECK (at90.apply (Rational (1)) == Rational (8, 3));
    CHECK (at90.apply (Rational (3)) == Rational (8));
}

TEST_CASE ("non-positive tempo is rejected loudly", "[tempomap]")
{
    CHECK_THROWS_AS (TempoMap::fromBpm (Rational (0)),   std::invalid_argument);
    CHECK_THROWS_AS (TempoMap::fromBpm (Rational (-60)), std::invalid_argument);
}

TEST_CASE ("piecewise-linear interpolation between breakpoints is exact", "[tempomap]")
{
    // A tempo change: the first whole note maps 1:1 into the parent, then the
    // rate halves for the second segment.
    const TempoMap tempoChange (std::vector<TempoMap::Breakpoint> {
        { Rational (0), Rational (0) },
        { Rational (1), Rational (1) },
        { Rational (2), Rational (3, 2) } });

    CHECK (tempoChange.apply (Rational (0))    == Rational (0));
    CHECK (tempoChange.apply (Rational (1))    == Rational (1));
    CHECK (tempoChange.apply (Rational (2))    == Rational (3, 2));
    // Midway through the first segment.
    CHECK (tempoChange.apply (Rational (1, 2)) == Rational (1, 2));
    // Midway through the second (half-rate) segment: 1 + (1/2 * 1/2) = 5/4.
    CHECK (tempoChange.apply (Rational (3, 2)) == Rational (5, 4));
}

TEST_CASE ("the first and last segments extrapolate past the breakpoints", "[tempomap]")
{
    const TempoMap map (std::vector<TempoMap::Breakpoint> {
        { Rational (1), Rational (10) },
        { Rational (2), Rational (12) } });

    // Slope is 2. Below the first breakpoint, extrapolate the first segment.
    CHECK (map.apply (Rational (0)) == Rational (8));
    // Above the last breakpoint, extrapolate the last segment.
    CHECK (map.apply (Rational (4)) == Rational (16));
}

TEST_CASE ("a tempo map needs at least two ascending breakpoints", "[tempomap]")
{
    CHECK_THROWS_AS (TempoMap (std::vector<TempoMap::Breakpoint> {
                         { Rational (0), Rational (0) } }),
                     std::invalid_argument);

    // Inputs must be strictly ascending.
    CHECK_THROWS_AS (TempoMap (std::vector<TempoMap::Breakpoint> {
                         { Rational (1), Rational (0) },
                         { Rational (1), Rational (5) } }),
                     std::invalid_argument);
}
