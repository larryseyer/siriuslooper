// Tests for the parameter-automation data model (white paper Part 7.7). The
// load-bearing claim is the recursion: an automation curve is a Constituent
// over a parameter tape, in exactly the same way a loop is a Constituent over
// an audio tape. These tests pin down that the parameter event validates its
// own range (no silent corruption), that the shared Tape<> template carries
// parameter events as faithfully as it carries audio, and that the recursion
// — a Constituent whose tapeReference points at a parameter tape — is
// representable without a single new Constituent type.
#include "ida/Constituent.h"
#include "ida/ParameterAutomation.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"
#include "ida/TapeReference.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::Constituent;
using ida::ConstituentId;
using ida::ParameterEvent;
using ida::ParameterTape;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TapeReference;

namespace
{
    ParameterTape::Event eventAt (TapeId tape, Rational lmc, int idx, double value)
    {
        return { Position (lmc), lmc, tape, ParameterEvent (idx, value) };
    }
}

TEST_CASE ("a parameter event rejects values outside [0, 1]", "[parameter-auto]")
{
    CHECK_THROWS_AS (ParameterEvent (0, -0.01), std::invalid_argument);
    CHECK_THROWS_AS (ParameterEvent (0,  1.01), std::invalid_argument);
    CHECK_NOTHROW   (ParameterEvent (0,  0.0));
    CHECK_NOTHROW   (ParameterEvent (0,  1.0));
    CHECK_NOTHROW   (ParameterEvent (0,  0.5));
}

TEST_CASE ("a parameter tape carries automation events forward in LMC time",
           "[parameter-auto]")
{
    // The same Tape<> the audio path uses (white paper Part 6.2) — parameter
    // events differ only in their payload. The append-forward invariant is the
    // same: tape data integrity is sacred (Part 13.3, rule 2).
    const TapeId tape (42);
    ParameterTape automation (tape);

    automation.append (eventAt (tape, Rational (0),       0, 0.0));
    automation.append (eventAt (tape, Rational (1, 2),    0, 0.25));
    automation.append (eventAt (tape, Rational (1),       0, 0.75));

    REQUIRE (automation.size() == 3);

    const auto firstHalf = automation.eventsInLmcRange (Rational (0), Rational (1));
    REQUIRE (firstHalf.size() == 2);
    CHECK (firstHalf[1].payload.valueZeroToOne > 0.249);
    CHECK (firstHalf[1].payload.valueZeroToOne < 0.251);
}

TEST_CASE ("a parameter tape rejects events that move backward in time",
           "[parameter-auto]")
{
    const TapeId tape (1);
    ParameterTape automation (tape);
    automation.append (eventAt (tape, Rational (1), 0, 0.5));
    CHECK_THROWS_AS (automation.append (eventAt (tape, Rational (0), 0, 0.0)),
                     std::invalid_argument);
}

TEST_CASE ("an automation curve is a Constituent over a parameter tape",
           "[parameter-auto][constituent]")
{
    // The recursion in white paper 7.7: a Constituent whose tapeReference
    // points at a parameter tape *is* the automation curve. No new Constituent
    // subtype, no special automation node — the data model is recursive at
    // every level. This test exists to encode the claim; it succeeds the
    // moment the types compose without modification.
    const TapeId paramTape (100);
    const Constituent curve =
        Constituent (ConstituentId (5), Position(), Position (Rational (8)))
            .withName ("filter cutoff envelope")
            .withTapeReference (TapeReference (paramTape, Rational (0), Rational (4)));

    REQUIRE (curve.tapeReference().has_value());
    CHECK (curve.tapeReference()->tape == paramTape);
    CHECK (curve.isLoop()); // "loop" here is the structural name: a leaf with a tape
}
