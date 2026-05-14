// Golden-value tests for sirius::Position — a symbolic point in conceptual
// time, measured in whole notes. These tests pin down musical-coordinate
// construction, including in odd meters, and confirm positions are exact.
#include "sirius/Position.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::Meter;
using sirius::Position;
using sirius::Rational;

TEST_CASE ("a default position is the start of the domain", "[position]")
{
    CHECK (Position().wholeNotes() == Rational (0));
}

TEST_CASE ("fromBarBeat places positions exactly in 4/4", "[position]")
{
    const Meter fourFour (4, 4);

    // Bar 1, beat 1 is the origin.
    CHECK (Position::fromBarBeat (fourFour, 1, 1).wholeNotes() == Rational (0));
    // Bar 1, beat 3 is two quarter notes in.
    CHECK (Position::fromBarBeat (fourFour, 1, 3).wholeNotes() == Rational (1, 2));
    // Bar 3, beat 1 is two whole notes in.
    CHECK (Position::fromBarBeat (fourFour, 3, 1).wholeNotes() == Rational (2));
}

TEST_CASE ("fromBarBeat is exact in an odd meter", "[position][conceptual-time]")
{
    // White paper Part 3.2: "the third sixteenth of bar 4 of phrase A in its
    // local 7/8 meter" is an unambiguous, exact position.
    const Meter sevenEight (7, 8);

    // Start of bar 4 in 7/8: three bars of 7/8 = 21/8 whole notes.
    CHECK (Position::fromBarBeat (sevenEight, 4, 1).wholeNotes() == Rational (21, 8));

    // The third sixteenth of beat 1 of bar 4: two sixteenths (2/16) into the
    // beat, on top of 21/8.
    const Position thirdSixteenth =
        Position::fromBarBeat (sevenEight, 4, 1, Rational (2, 16));
    CHECK (thirdSixteenth.wholeNotes() == Rational (21, 8) + Rational (1, 8));
    CHECK (thirdSixteenth.wholeNotes() == Rational (11, 4));
}

TEST_CASE ("position arithmetic and ordering are exact", "[position]")
{
    const Position a (Rational (3, 4));
    const Position b (Rational (1, 8));

    CHECK ((a + b).wholeNotes() == Rational (7, 8));
    CHECK ((a - b).wholeNotes() == Rational (5, 8));

    CHECK (b < a);
    CHECK (a > b);
    CHECK (Position (Rational (1, 2)) <= Position (Rational (2, 4)));
    CHECK (Position (Rational (1, 2)) == Position (Rational (2, 4)));
}

TEST_CASE ("bar and beat are 1-based and reject zero or negative", "[position]")
{
    const Meter fourFour (4, 4);
    CHECK_THROWS_AS (Position::fromBarBeat (fourFour, 0, 1), std::invalid_argument);
    CHECK_THROWS_AS (Position::fromBarBeat (fourFour, 1, 0), std::invalid_argument);
    CHECK_THROWS_AS (Position::fromBarBeat (fourFour, -1, 1), std::invalid_argument);
}
