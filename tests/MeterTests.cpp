// Golden-value tests for ida::Meter. Meter is a property of a Constituent,
// not a global constraint (white paper Part 9.6); these tests pin down the
// bar/beat lengths it derives, as exact rationals.
#include "ida/Meter.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::Meter;
using ida::Rational;

TEST_CASE ("common-practice meter has the expected bar and beat lengths", "[meter]")
{
    const Meter fourFour (4, 4);
    CHECK (fourFour.beatLength() == Rational (1, 4));
    CHECK (fourFour.barLength()  == Rational (1));      // a 4/4 bar is one whole note
}

TEST_CASE ("an odd meter is exact, not approximate", "[meter][conceptual-time]")
{
    // 7/8 — the kind of meter a polymetric phrase uses (white paper Part IX).
    const Meter sevenEight (7, 8);
    CHECK (sevenEight.beatLength() == Rational (1, 8));
    CHECK (sevenEight.barLength()  == Rational (7, 8));

    const Meter threeFour (3, 4);
    CHECK (threeFour.beatLength() == Rational (1, 4));
    CHECK (threeFour.barLength()  == Rational (3, 4));
}

TEST_CASE ("meter equality compares both numbers", "[meter]")
{
    CHECK (Meter (4, 4) == Meter (4, 4));
    CHECK (Meter (7, 8) != Meter (4, 4));
    CHECK (Meter (3, 4) != Meter (3, 8));
}

TEST_CASE ("non-positive meter components are rejected loudly", "[meter]")
{
    CHECK_THROWS_AS (Meter (0, 4), std::invalid_argument);
    CHECK_THROWS_AS (Meter (4, 0), std::invalid_argument);
    CHECK_THROWS_AS (Meter (-3, 4), std::invalid_argument);
}
