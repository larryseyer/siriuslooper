// Tests for sirius::LoopRenderer — the outbound membrane's M2 job, rendering a
// single repeating loop. These encode the white paper's claim that loop
// repetition is exact: the read position at the hundredth cycle is bit-
// identical to the first (Part 9.3).
#include "sirius/LoopRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::LoopRenderer;
using sirius::Rational;

TEST_CASE ("a loop region must be non-empty and forward", "[looprenderer]")
{
    CHECK_THROWS_AS (LoopRenderer (Rational (5), Rational (5), Rational (0)),
                     std::invalid_argument);
    CHECK_THROWS_AS (LoopRenderer (Rational (5), Rational (2), Rational (0)),
                     std::invalid_argument);
    CHECK_NOTHROW (LoopRenderer (Rational (2), Rational (5), Rational (0)));
}

TEST_CASE ("nothing sounds before the loop starts", "[looprenderer]")
{
    // Loop over tape [2, 5), three seconds long, starting at LMC t = 10.
    const LoopRenderer loop (Rational (2), Rational (5), Rational (10));
    CHECK (loop.loopLength() == Rational (3));

    const auto before = loop.at (Rational (9));
    CHECK_FALSE (before.sounding);
}

TEST_CASE ("a loop reads forward through its first cycle", "[looprenderer]")
{
    const LoopRenderer loop (Rational (2), Rational (5), Rational (10));

    SECTION ("at the loop's start, the read head is at tapeIn")
    {
        const auto pos = loop.at (Rational (10));
        CHECK (pos.sounding);
        CHECK (pos.cycle == 0);
        CHECK (pos.tapePosition == Rational (2));
    }

    SECTION ("midway through the first cycle")
    {
        const auto pos = loop.at (Rational (23, 2)); // 1.5 s after the start
        CHECK (pos.sounding);
        CHECK (pos.cycle == 0);
        CHECK (pos.tapePosition == Rational (7, 2)); // tapeIn 2 + offset 1.5
    }

    SECTION ("exactly one loop length later, the next cycle begins at tapeIn")
    {
        const auto pos = loop.at (Rational (13)); // 3 s after the start
        CHECK (pos.sounding);
        CHECK (pos.cycle == 1);
        CHECK (pos.tapePosition == Rational (2));
    }
}

TEST_CASE ("the hundredth cycle reads exactly the same as the first", "[looprenderer][conceptual-time]")
{
    // White paper Part 9.3: drift-free repetition holds across thousands of
    // cycles because the math is symbolic. Cycle 0 at offset 1.5 s and cycle
    // 100 at offset 1.5 s must yield the bit-identical tape position.
    const LoopRenderer loop (Rational (2), Rational (5), Rational (10));

    const auto firstCycle = loop.at (Rational (23, 2)); // start + 1.5 s
    // start + 100 cycles (300 s) + 1.5 s = LMC t = 311.5 s
    const auto cycle100 = loop.at (Rational (623, 2));

    REQUIRE (firstCycle.sounding);
    REQUIRE (cycle100.sounding);
    CHECK (firstCycle.cycle == 0);
    CHECK (cycle100.cycle == 100);
    // The same point in the loop — no accumulated drift.
    CHECK (cycle100.tapePosition == firstCycle.tapePosition);
    CHECK (cycle100.tapePosition == Rational (7, 2));
}
