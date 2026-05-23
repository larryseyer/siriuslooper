// Golden-value tests for ida::Rational — the exact-arithmetic bedrock of the
// conceptual-time engine. These tests encode *why* the type exists: exactness,
// no tolerance, no accumulated drift, loud failure on overflow. A test here
// fails exactly when the white paper's "exact by construction" claim is broken.
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

using ida::Rational;

TEST_CASE ("default-constructed Rational is canonical zero", "[rational]")
{
    const Rational zero;
    CHECK (zero.numerator() == 0);
    CHECK (zero.denominator() == 1);
    CHECK (zero.isZero());
    CHECK (zero.isInteger());
}

TEST_CASE ("construction normalizes to lowest terms", "[rational]")
{
    SECTION ("common factor is reduced")
    {
        const Rational r (6, 4);
        CHECK (r.numerator() == 3);
        CHECK (r.denominator() == 2);
    }

    SECTION ("sign is canonicalized onto the numerator")
    {
        const Rational r (1, -2);
        CHECK (r.numerator() == -1);
        CHECK (r.denominator() == 2);
        CHECK (r.isNegative());
    }

    SECTION ("negative over negative is positive")
    {
        const Rational r (-3, -9);
        CHECK (r.numerator() == 1);
        CHECK (r.denominator() == 3);
    }

    SECTION ("any zero numerator collapses to 0/1")
    {
        CHECK (Rational (0, 5) == Rational());
        CHECK (Rational (0, -7).denominator() == 1);
    }

    SECTION ("zero denominator is rejected loudly")
    {
        CHECK_THROWS_AS (Rational (1, 0), std::invalid_argument);
    }
}

TEST_CASE ("equality is exact, with no tolerance", "[rational]")
{
    CHECK (Rational (1, 2) == Rational (50, 100));
    CHECK (Rational (2, 3) != Rational (3, 2));
    // Equality compares normalized forms, so equal values are bit-identical.
    CHECK (Rational (-4, 8) == Rational (1, -2));
}

TEST_CASE ("arithmetic produces exact golden values", "[rational]")
{
    SECTION ("addition")
    {
        CHECK (Rational (1, 2) + Rational (1, 3) == Rational (5, 6));
        CHECK (Rational (1, 2) + Rational (1, 2) == Rational (1));
    }

    SECTION ("subtraction")
    {
        CHECK (Rational (3, 4) - Rational (1, 4) == Rational (1, 2));
        CHECK (Rational (1, 3) - Rational (1, 3) == Rational());
    }

    SECTION ("multiplication")
    {
        CHECK (Rational (2, 3) * Rational (3, 4) == Rational (1, 2));
        CHECK (Rational (5, 1) * Rational (0) == Rational());
    }

    SECTION ("division")
    {
        CHECK (Rational (1, 2) / Rational (1, 4) == Rational (2));
        CHECK (Rational (2, 3) / Rational (2, 3) == Rational (1));
    }

    SECTION ("negation")
    {
        CHECK (-Rational (3, 7) == Rational (-3, 7));
        CHECK (-Rational() == Rational());
    }

    SECTION ("compound assignment")
    {
        Rational r (1, 2);
        r += Rational (1, 4);
        CHECK (r == Rational (3, 4));
        r -= Rational (3, 4);
        CHECK (r.isZero());
    }
}

TEST_CASE ("division by zero is rejected loudly", "[rational]")
{
    CHECK_THROWS_AS (Rational (1, 2) / Rational (0), std::invalid_argument);
}

TEST_CASE ("tuplets are exact — the PPQ problem dissolves", "[rational][conceptual-time]")
{
    // White paper Part 3.3: a 7-tuplet is "7 in the space of 4", a structural
    // relationship, not a number on a grid. Seven sevenths of a whole note must
    // sum to exactly one whole note — no rounding, no leftover.
    const Rational oneSeventh (1, 7);
    Rational sum;
    for (int i = 0; i < 7; ++i)
        sum += oneSeventh;
    CHECK (sum == Rational (1));

    // And 7 * (1/7) directly.
    CHECK (Rational (7) * oneSeventh == Rational (1));

    // An 11-tuplet against a 4/4 bar — the kind of subdivision a fixed PPQ grid
    // cannot represent — is exact here.
    CHECK (Rational (11) * Rational (1, 11) == Rational (1));
}

TEST_CASE ("no accumulated drift over many operations", "[rational][conceptual-time]")
{
    // White paper Part 3.3: "a loop in its hundredth cycle drifts from a loop
    // in its first cycle" only in numerical systems. Adding 1/3 one hundred
    // times must land on exactly 100/3 — the hundredth cycle is as exact as
    // the first.
    Rational accumulated;
    for (int i = 0; i < 100; ++i)
        accumulated += Rational (1, 3);
    CHECK (accumulated == Rational (100, 3));
    CHECK (accumulated.numerator() == 100);
    CHECK (accumulated.denominator() == 3);
}

TEST_CASE ("ordering is correct across signs", "[rational]")
{
    CHECK (Rational (1, 3) < Rational (1, 2));
    CHECK (Rational (-1, 2) < Rational (1, 3));
    CHECK (Rational (-3, 4) < Rational (-1, 2));
    CHECK (Rational (1, 2) <= Rational (2, 4));
    CHECK (Rational (2, 3) > Rational (1, 2));
    CHECK (Rational (5, 6) >= Rational (5, 6));
}

TEST_CASE ("overflow throws rather than wrapping silently", "[rational]")
{
    constexpr std::int64_t big = std::numeric_limits<std::int64_t>::max();

    // A product that cannot fit in int64 must throw — a quiet wrong answer is
    // the one failure mode this type exists to prevent.
    CHECK_THROWS_AS (Rational (big, 1) * Rational (big, 1), std::overflow_error);
    CHECK_THROWS_AS (Rational (big, 1) + Rational (big, 1), std::overflow_error);

    // INT64_MIN has no positive counterpart, so a Rational can never hold it:
    // normalization rejects it at construction rather than risk a silent wrap
    // (or UB in std::gcd) later.
    constexpr std::int64_t small = std::numeric_limits<std::int64_t>::min();
    CHECK_THROWS_AS (Rational (small, 1), std::overflow_error);
    CHECK_THROWS_AS (Rational (1, small), std::overflow_error);
}

TEST_CASE ("toDouble is the explicit lossy membrane conversion", "[rational]")
{
    // 0.5, -0.75, and 0.0 are exactly representable, so each toDouble must
    // land on the bit pattern of its literal — tolerance < 1e-15 is just
    // enough to dodge -Wfloat-equal without weakening the claim.
    CHECK (std::abs (Rational (1, 2).toDouble()  -  0.5)  < 1e-15);
    CHECK (std::abs (Rational (-3, 4).toDouble() - -0.75) < 1e-15);
    CHECK (std::abs (Rational().toDouble())              < 1e-15);
}

TEST_CASE ("toString reports normalized form", "[rational]")
{
    CHECK (Rational (6, 4).toString() == "3/2");
    CHECK (Rational (0, 9).toString() == "0/1");
    CHECK (Rational (1, -2).toString() == "-1/2");
}

TEST_CASE ("floor rounds toward negative infinity, exactly", "[rational]")
{
    CHECK (Rational (7, 2).floor()  == 3);   // 3.5 -> 3
    CHECK (Rational (4).floor()     == 4);   // exact integer
    CHECK (Rational (0).floor()     == 0);
    CHECK (Rational (1, 3).floor()  == 0);   // 0.333... -> 0
    // The case naive truncation gets wrong: a negative non-integer rounds
    // *down*, not toward zero.
    CHECK (Rational (-7, 2).floor() == -4);  // -3.5 -> -4, not -3
    CHECK (Rational (-1, 3).floor() == -1);  // -0.333... -> -1
    CHECK (Rational (-6, 3).floor() == -2);  // exact negative integer
}
