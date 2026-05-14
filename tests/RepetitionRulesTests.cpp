// Golden-value tests for sirius::RepetitionRules — the five orthogonal
// dimensions that describe how a loop plays back (white paper Part X). These
// pin down the defaults the white paper calls the system's best guess, and
// confirm the validating constructors reject nonsense loudly.
#include "sirius/RepetitionRules.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <variant>

using namespace sirius;

TEST_CASE ("the default rules are the system's best guess for a loop", "[repetition]")
{
    // White paper Part 10.2: free-running, forever, unquantized, identical,
    // completing the current cycle on stop.
    const RepetitionRules rules;
    CHECK (std::holds_alternative<trigger::FreeRunning> (rules.trigger));
    CHECK (std::holds_alternative<cardinality::Forever> (rules.cardinality));
    CHECK (std::holds_alternative<phase::Free> (rules.phase));
    CHECK (rules.mutation == Mutation::Identical);
    CHECK (std::holds_alternative<termination::CompleteCurrentCycle> (rules.termination));

    // defaultLoop() is just that, named for intent.
    const RepetitionRules loop = RepetitionRules::defaultLoop();
    CHECK (std::holds_alternative<trigger::FreeRunning> (loop.trigger));
    CHECK (std::holds_alternative<cardinality::Forever> (loop.cardinality));
}

TEST_CASE ("a one-shot is a loop that plays once and is allowed to finish", "[repetition]")
{
    // White paper Part 7.5: a non-looped slice is "a loop with cardinality = Once".
    const RepetitionRules oneShot = RepetitionRules::defaultOneShot();
    CHECK (std::holds_alternative<trigger::FreeRunning> (oneShot.trigger));
    CHECK (std::holds_alternative<cardinality::Once> (oneShot.cardinality));
    CHECK (std::holds_alternative<phase::Free> (oneShot.phase));
    CHECK (oneShot.mutation == Mutation::Identical);
    CHECK (std::holds_alternative<termination::ContinueUntilNaturalEnd> (oneShot.termination));
}

TEST_CASE ("each dimension can carry a non-default, parameterised value", "[repetition]")
{
    RepetitionRules rules;
    rules.trigger     = trigger::EveryNBars (4);
    rules.cardinality = cardinality::NTimes (8);
    rules.phase       = phase::QuantizedToGrid (Rational (1, 4));
    rules.mutation    = Mutation::Decaying;
    rules.termination = termination::FadeOverBars (Rational (2));

    CHECK (std::get<trigger::EveryNBars> (rules.trigger).bars == 4);
    CHECK (std::get<cardinality::NTimes> (rules.cardinality).count == 8);
    CHECK (std::get<phase::QuantizedToGrid> (rules.phase).division == Rational (1, 4));
    CHECK (rules.mutation == Mutation::Decaying);
    CHECK (std::get<termination::FadeOverBars> (rules.termination).bars == Rational (2));
}

TEST_CASE ("dimensions that reference another Constituent carry its id", "[repetition]")
{
    const trigger::AfterConstituent afterX { ConstituentId (5) };
    CHECK (afterX.reference == ConstituentId (5));

    const phase::SynchronizedTo sync { ConstituentId (3), Rational (1, 2) };
    CHECK (sync.reference == ConstituentId (3));
    CHECK (sync.offset == Rational (1, 2));

    const cardinality::UntilConstituentStarts until { ConstituentId (7) };
    CHECK (until.reference == ConstituentId (7));

    const termination::HandOff handOff { ConstituentId (9) };
    CHECK (handOff.next == ConstituentId (9));
}

TEST_CASE ("the validating constructors reject nonsense loudly", "[repetition]")
{
    SECTION ("counts and bar-counts must be positive")
    {
        CHECK_THROWS_AS (trigger::EveryNBars (0), std::invalid_argument);
        CHECK_THROWS_AS (trigger::EveryNBars (-2), std::invalid_argument);
        CHECK_THROWS_AS (cardinality::NTimes (0), std::invalid_argument);
        CHECK_NOTHROW (trigger::EveryNBars (1));
        CHECK_NOTHROW (cardinality::NTimes (1));
    }

    SECTION ("a probability must lie within [0, 1]")
    {
        CHECK_THROWS_AS (trigger::Probabilistic (Rational (2)), std::invalid_argument);
        CHECK_THROWS_AS (trigger::Probabilistic (Rational (-1, 2)), std::invalid_argument);
        CHECK_NOTHROW (trigger::Probabilistic (Rational (0)));
        CHECK_NOTHROW (trigger::Probabilistic (Rational (1, 2)));
        CHECK_NOTHROW (trigger::Probabilistic (Rational (1)));
    }

    SECTION ("grid divisions and fade lengths must be positive")
    {
        CHECK_THROWS_AS (phase::QuantizedToGrid (Rational (0)), std::invalid_argument);
        CHECK_THROWS_AS (phase::QuantizedToGrid (Rational (-1, 4)), std::invalid_argument);
        CHECK_THROWS_AS (termination::FadeOverBars (Rational (0)), std::invalid_argument);
        CHECK_THROWS_AS (termination::FadeOverBars (Rational (-2)), std::invalid_argument);
        CHECK_NOTHROW (phase::QuantizedToGrid (Rational (1, 16)));
        CHECK_NOTHROW (termination::FadeOverBars (Rational (4)));
    }
}
