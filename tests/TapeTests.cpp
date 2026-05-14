// Golden-value tests for sirius::Tape — the append-only, immutable source of
// truth (white paper Part VI). These tests pin down the append-only contract:
// events go on the end in non-decreasing LMC time, existing events are never
// touched, and a tape only accepts events that belong to it.
#include "sirius/Tape.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::Position;
using sirius::Rational;
using sirius::Tape;
using sirius::TapeEvent;
using sirius::TapeId;

namespace
{
    // A stand-in payload. Every tape shares one event format; only the payload
    // type differs (audio samples, video frames, MIDI, control, automation).
    TapeEvent<int> makeEvent (TapeId tape, Rational lmc, int payload)
    {
        return TapeEvent<int> { Position (lmc), lmc, tape, payload };
    }
}

TEST_CASE ("a new tape is empty and knows its id", "[tape]")
{
    const Tape<int> tape (TapeId (7));
    CHECK (tape.id() == TapeId (7));
    CHECK (tape.empty());
    CHECK (tape.size() == 0);
}

TEST_CASE ("appending events grows the tape in order", "[tape]")
{
    Tape<int> tape (TapeId (1));
    tape.append (makeEvent (TapeId (1), Rational (0),    100));
    tape.append (makeEvent (TapeId (1), Rational (1, 2), 200));
    tape.append (makeEvent (TapeId (1), Rational (1),    300));

    REQUIRE (tape.size() == 3);
    CHECK (tape.events()[0].payload == 100);
    CHECK (tape.events()[1].payload == 200);
    CHECK (tape.events()[2].payload == 300);
}

TEST_CASE ("a tape records forward in time", "[tape]")
{
    Tape<int> tape (TapeId (1));
    tape.append (makeEvent (TapeId (1), Rational (1), 100));

    // Going backwards in LMC time is rejected.
    CHECK_THROWS_AS (tape.append (makeEvent (TapeId (1), Rational (1, 2), 200)),
                     std::invalid_argument);

    // Equal timestamps are fine — simultaneous events (a stereo pair, a chord)
    // share a moment.
    CHECK_NOTHROW (tape.append (makeEvent (TapeId (1), Rational (1), 300)));
    CHECK (tape.size() == 2);
}

TEST_CASE ("a tape only accepts events that belong to it", "[tape]")
{
    Tape<int> tape (TapeId (1));
    CHECK_THROWS_AS (tape.append (makeEvent (TapeId (2), Rational (0), 100)),
                     std::invalid_argument);
    CHECK (tape.empty());
}

TEST_CASE ("events are queryable by LMC time, half-open", "[tape]")
{
    Tape<int> tape (TapeId (1));
    tape.append (makeEvent (TapeId (1), Rational (0),    10));
    tape.append (makeEvent (TapeId (1), Rational (1),    20));
    tape.append (makeEvent (TapeId (1), Rational (2),    30));
    tape.append (makeEvent (TapeId (1), Rational (3),    40));

    const auto inRange = tape.eventsInLmcRange (Rational (1), Rational (3));
    REQUIRE (inRange.size() == 2);          // [1, 3): includes 1 and 2, excludes 3
    CHECK (inRange[0].payload == 20);
    CHECK (inRange[1].payload == 30);
}

TEST_CASE ("events are queryable by conceptual time, half-open", "[tape]")
{
    Tape<int> tape (TapeId (1));
    // conceptualTimestamp == lmcTimestamp for these synthetic events.
    tape.append (makeEvent (TapeId (1), Rational (0),    10));
    tape.append (makeEvent (TapeId (1), Rational (1, 2), 20));
    tape.append (makeEvent (TapeId (1), Rational (1),    30));

    const auto inRange = tape.eventsInConceptualRange (Position (Rational (0)),
                                                       Position (Rational (1)));
    REQUIRE (inRange.size() == 2);          // [0, 1): excludes the event at 1
    CHECK (inRange[0].payload == 10);
    CHECK (inRange[1].payload == 20);
}
