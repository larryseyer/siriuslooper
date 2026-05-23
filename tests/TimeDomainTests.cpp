// Golden-value tests for ida::TimeDomain — the tree of conceptual time
// domains and the "unroll" that renders a local position to absolute time
// (white paper Parts 3.4, 3.6, IX). These tests encode the architecture's
// headline claims: polymetric coexistence is exact, and there is no
// accumulated drift at the hundredth cycle.
#include "ida/TimeDomain.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::Meter;
using ida::Position;
using ida::Rational;
using ida::TempoMap;
using ida::TimeDomain;

TEST_CASE ("the root domain unrolls straight to seconds", "[timedomain]")
{
    const auto root = TimeDomain::createRoot (Meter (4, 4), TempoMap::fromBpm (Rational (120)));

    CHECK (root->isRoot());
    CHECK (root->depth() == 1);
    // One 4/4 bar at 120 BPM is two seconds.
    CHECK (root->toAbsoluteSeconds (Position (Rational (1))) == Rational (2));
    // Bar 5, beat 1 — four whole notes in — is eight seconds.
    CHECK (root->toAbsoluteSeconds (Position::fromBarBeat (Meter (4, 4), 5, 1))
           == Rational (8));
}

TEST_CASE ("a nested tree composes tempo maps top to bottom", "[timedomain]")
{
    // session -> song -> phrase, each child running 1:1 with its parent.
    const auto session = TimeDomain::createRoot (Meter (4, 4), TempoMap::fromBpm (Rational (120)));
    const auto song    = TimeDomain::createChild (session, Meter (4, 4), TempoMap::constant (Rational (1)));
    const auto phrase  = TimeDomain::createChild (song,    Meter (4, 4), TempoMap::constant (Rational (1)));

    CHECK (phrase->depth() == 3);
    CHECK (phrase->parent() == song.get());
    // A whole note in the phrase still renders to two seconds: the 1:1 child
    // maps pass it through unchanged until the root applies the tempo.
    CHECK (phrase->toAbsoluteSeconds (Position (Rational (1))) == Rational (2));
}

TEST_CASE ("a child domain may run at its own tempo", "[timedomain]")
{
    const auto root = TimeDomain::createRoot (Meter (4, 4), TempoMap::fromBpm (Rational (120)));
    // A half-speed child: one of its whole notes spans two of the parent's.
    const auto halfSpeed = TimeDomain::createChild (root, Meter (4, 4), TempoMap::constant (Rational (2)));

    // One whole note here = 2 parent whole notes = 4 seconds at 120 BPM.
    CHECK (halfSpeed->toAbsoluteSeconds (Position (Rational (1))) == Rational (4));
}

TEST_CASE ("polymetric coexistence is exact by definition", "[timedomain][conceptual-time]")
{
    // White paper Part 9.3: a 4/4 drum loop and a 7/8 ostinato live in their
    // own time domains inside one phrase, sharing only the eighth-note pulse.
    // Over the phrase's 8 bars of 4/4 the 7/8 ostinato completes 64/7 cycles —
    // it does not land evenly, and that non-alignment is the musical point.
    const auto session  = TimeDomain::createRoot (Meter (4, 4), TempoMap::fromBpm (Rational (120)));
    const auto drums    = TimeDomain::createChild (session, Meter (4, 4), TempoMap::constant (Rational (1)));
    const auto ostinato = TimeDomain::createChild (session, Meter (7, 8), TempoMap::constant (Rational (1)));

    // The phrase spans 8 session whole notes. Both children reach that point
    // at the same absolute time — they meet at the phrase boundary.
    const Position phraseEnd (Rational (8));
    CHECK (drums->toAbsoluteSeconds (phraseEnd)    == Rational (16));
    CHECK (ostinato->toAbsoluteSeconds (phraseEnd) == Rational (16));

    // But they meet there in *different* musical time. The drum loop has played
    // exactly 8 of its 4/4 bars; the ostinato has played 64/7 of its 7/8 bars.
    CHECK (Rational (8) / drums->meter().barLength()    == Rational (8));
    CHECK (Rational (8) / ostinato->meter().barLength() == Rational (64, 7));
    // 64/7 is not an integer: the ostinato's cycles do not align with the
    // phrase boundary, and the engine represents that exactly.
    CHECK_FALSE ((Rational (8) / ostinato->meter().barLength()).isInteger());
}

TEST_CASE ("no accumulated drift at the hundredth cycle", "[timedomain][conceptual-time]")
{
    // White paper Part 9.3: a 4-against-7 relationship "holds across thousands
    // of cycles" because the math is symbolic, not numerical. The start of the
    // 101st cycle of a 7/8 loop must render to exactly the same absolute time
    // whether computed directly or by accumulating one bar at a time.
    const auto session  = TimeDomain::createRoot (Meter (4, 4), TempoMap::fromBpm (Rational (120)));
    const auto ostinato = TimeDomain::createChild (session, Meter (7, 8), TempoMap::constant (Rational (1)));
    const Meter sevenEight = ostinato->meter();

    // 100 cycles of 7/8 = exactly 100 * 7/8 = 87.5 whole notes.
    Position accumulated;
    for (int cycle = 0; cycle < 100; ++cycle)
        accumulated = accumulated + Position (sevenEight.barLength());

    const Position directly = Position::fromBarBeat (sevenEight, 101, 1);
    CHECK (accumulated == directly);
    CHECK (accumulated.wholeNotes() == Rational (175, 2));

    // And it renders to an exact absolute time — 175 seconds — with no drift.
    const Rational seconds = ostinato->toAbsoluteSeconds (accumulated);
    CHECK (seconds == Rational (175));
    CHECK (seconds.denominator() == 1);
}

TEST_CASE ("a child domain requires a non-null parent", "[timedomain]")
{
    CHECK_THROWS_AS (TimeDomain::createChild (nullptr, Meter (4, 4),
                                              TempoMap::constant (Rational (1))),
                     std::invalid_argument);
}
