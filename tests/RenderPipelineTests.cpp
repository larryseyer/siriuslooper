// Tests for ida::RenderPipeline — the outbound render pipeline (white paper
// Parts 3.6, 5.2). These build concrete Constituent trees and pin down what the
// pipeline reports as sounding: placement spans, exact loop-cycle arithmetic,
// cardinality bounds, dormant non-free-running triggers, and the composition of
// nested time domains that makes polymetric coexistence work.
#include "ida/RenderPipeline.h"

#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

using ida::Constituent;
using ida::ConstituentId;
using ida::Position;
using ida::Rational;
using ida::RenderPipeline;
using ida::TapeId;
using ida::TapeReference;
using ida::TempoMap;

namespace
{
    /// A leaf loop placed at [placeIn, placeOut) in its parent's time, playing
    /// the tape slice [sliceIn, sliceOut).
    std::shared_ptr<const Constituent> makeLoop (std::int64_t id,
                                                 Rational placeIn, Rational placeOut,
                                                 std::int64_t tape,
                                                 Rational sliceIn, Rational sliceOut)
    {
        const Constituent loop { ConstituentId (id), Position (placeIn), Position (placeOut) };
        return std::make_shared<const Constituent> (
            loop.withTapeReference (TapeReference (TapeId (tape), sliceIn, sliceOut)));
    }

    /// A session Constituent spanning [0, lengthWholeNotes) with the given child.
    std::shared_ptr<const Constituent> makeSession (Rational lengthWholeNotes,
                                                    std::shared_ptr<const Constituent> child)
    {
        const Constituent session (ConstituentId (1), Position(), Position (lengthWholeNotes));
        return std::make_shared<const Constituent> (session.withChildAdded (std::move (child)));
    }
}

TEST_CASE ("the pipeline requires a non-null root", "[renderpipeline]")
{
    CHECK_THROWS_AS (RenderPipeline (nullptr, TempoMap::fromBpm (Rational (120))),
                     std::invalid_argument);
}

TEST_CASE ("a free-running loop reads forward through its cycles", "[renderpipeline]")
{
    // Session is 8 whole notes at 120 BPM => 16 LMC seconds. One loop fills it,
    // playing a 2-second tape slice [2, 4).
    const auto session = makeSession (Rational (8),
        makeLoop (10, Rational (0), Rational (8), 100, Rational (2), Rational (4)));
    const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

    SECTION ("at the loop's start, the read head is at tapeIn")
    {
        const auto reads = pipeline.activeReadsAt (Rational (0));
        REQUIRE (reads.size() == 1);
        CHECK (reads[0].loop == ConstituentId (10));
        CHECK (reads[0].tape == TapeId (100));
        CHECK (reads[0].tapePosition == Rational (2));
        CHECK (reads[0].cycle == 0);
    }

    SECTION ("midway through the first cycle")
    {
        const auto reads = pipeline.activeReadsAt (Rational (1));
        REQUIRE (reads.size() == 1);
        CHECK (reads[0].tapePosition == Rational (3)); // tapeIn 2 + offset 1
        CHECK (reads[0].cycle == 0);
    }

    SECTION ("into a later cycle, the read wraps back to tapeIn")
    {
        const auto reads = pipeline.activeReadsAt (Rational (5)); // 5 s in, cycle length 2
        REQUIRE (reads.size() == 1);
        CHECK (reads[0].cycle == 2);                   // floor(5 / 2)
        CHECK (reads[0].tapePosition == Rational (3)); // offset 5 - 2*2 = 1
    }

    SECTION ("outside the placement span nothing sounds")
    {
        CHECK (pipeline.activeReadsAt (Rational (-1)).empty());
        CHECK (pipeline.activeReadsAt (Rational (16)).empty()); // span is half-open [0, 16)
        CHECK (pipeline.activeReadsAt (Rational (100)).empty());
    }
}

TEST_CASE ("a loop placed partway into the session sounds only within its span",
           "[renderpipeline]")
{
    // Loop placed at session-time [2, 6) => LMC [4, 12) at 120 BPM.
    const auto session = makeSession (Rational (8),
        makeLoop (10, Rational (2), Rational (6), 100, Rational (0), Rational (2)));
    const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

    CHECK (pipeline.activeReadsAt (Rational (3)).empty());   // before the span
    CHECK (pipeline.activeReadsAt (Rational (12)).empty());  // at the half-open end

    const auto reads = pipeline.activeReadsAt (Rational (4)); // the span start
    REQUIRE (reads.size() == 1);
    CHECK (reads[0].cycle == 0);
    CHECK (reads[0].tapePosition == Rational (0));
}

TEST_CASE ("cardinality bounds how many cycles a loop sounds", "[renderpipeline][repetition]")
{
    using namespace ida;

    SECTION ("Once: only the first cycle sounds")
    {
        Constituent loop (ConstituentId (10), Position(), Position (Rational (16)));
        RepetitionRules rules;
        rules.cardinality = cardinality::Once {};
        loop = loop.withRepetitionRules (rules)
                   .withTapeReference (TapeReference (TapeId (1), Rational (0), Rational (2)));
        const auto session = makeSession (Rational (16),
            std::make_shared<const Constituent> (loop));
        const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

        CHECK (pipeline.activeReadsAt (Rational (0)).size() == 1); // cycle 0
        CHECK (pipeline.activeReadsAt (Rational (1)).size() == 1); // still cycle 0
        CHECK (pipeline.activeReadsAt (Rational (2)).empty());     // cycle 1 — silent
        CHECK (pipeline.activeReadsAt (Rational (9)).empty());     // long past
    }

    SECTION ("NTimes: the loop sounds for exactly N cycles")
    {
        Constituent loop (ConstituentId (10), Position(), Position (Rational (16)));
        RepetitionRules rules;
        rules.cardinality = cardinality::NTimes (3);
        loop = loop.withRepetitionRules (rules)
                   .withTapeReference (TapeReference (TapeId (1), Rational (0), Rational (2)));
        const auto session = makeSession (Rational (16),
            std::make_shared<const Constituent> (loop));
        const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

        CHECK (pipeline.activeReadsAt (Rational (5)).size() == 1); // cycle 2 — last allowed
        CHECK (pipeline.activeReadsAt (Rational (6)).empty());     // cycle 3 — silent
        CHECK (pipeline.activeReadsAt (Rational (7)).empty());
    }
}

TEST_CASE ("a loop whose trigger is not free-running is dormant", "[renderpipeline][repetition]")
{
    using namespace ida;

    // White paper Part 10.1: an on-demand loop is dormant until triggered. The
    // M3 pipeline has no trigger events, so the loop correctly does not sound.
    Constituent loop (ConstituentId (10), Position(), Position (Rational (16)));
    RepetitionRules rules;
    rules.trigger = trigger::OnDemand {};
    loop = loop.withRepetitionRules (rules)
               .withTapeReference (TapeReference (TapeId (1), Rational (0), Rational (2)));
    const auto session = makeSession (Rational (16),
        std::make_shared<const Constituent> (loop));
    const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

    CHECK (pipeline.activeReadsAt (Rational (0)).empty());
    CHECK (pipeline.activeReadsAt (Rational (5)).empty());
}

TEST_CASE ("nested time domains compose — the polymetric wiring", "[renderpipeline][conceptual-time]")
{
    // A phrase placed at session-time [2, 6) carries its own local tempo map
    // running at half speed (one phrase whole note spans two session whole
    // notes). A loop fills the phrase's local time [0, 2). The pipeline must
    // compose the phrase's domain with the session's: the loop should sound
    // over LMC [4, 12), reading its 1-second slice on repeat.
    Constituent phrase (ConstituentId (20), Position (Rational (2)), Position (Rational (6)));
    phrase = phrase.withLocalTempoMap (TempoMap::constant (Rational (2)));
    phrase = phrase.withChildAdded (
        makeLoop (30, Rational (0), Rational (2), 300, Rational (0), Rational (1)));

    const auto session = makeSession (Rational (8),
        std::make_shared<const Constituent> (phrase));
    const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

    CHECK (pipeline.activeReadsAt (Rational (3)).empty());   // before the composed span
    CHECK (pipeline.activeReadsAt (Rational (12)).empty());  // at the half-open end

    const auto atStart = pipeline.activeReadsAt (Rational (4));
    REQUIRE (atStart.size() == 1);
    CHECK (atStart[0].loop == ConstituentId (30));
    CHECK (atStart[0].cycle == 0);
    CHECK (atStart[0].tapePosition == Rational (0));

    const auto midway = pipeline.activeReadsAt (Rational (Rational (11, 2))); // LMC 5.5
    REQUIRE (midway.size() == 1);
    CHECK (midway[0].cycle == 1);
    CHECK (midway[0].tapePosition == Rational (1, 2));
}

TEST_CASE ("polymetric loops coexist without aligning", "[renderpipeline][conceptual-time]")
{
    // Two loops in one phrase with different cycle lengths — a 2-second loop and
    // a 7/4-second loop. They share the phrase's boundaries but their cycles do
    // not line up, and the pipeline reports each one's exact, independent read.
    Constituent phrase (ConstituentId (20), Position(), Position (Rational (8)));
    phrase = phrase.withChildAdded (
        makeLoop (40, Rational (0), Rational (8), 400, Rational (0), Rational (2)));
    phrase = phrase.withChildAdded (
        makeLoop (41, Rational (0), Rational (8), 410, Rational (0), Rational (7, 4)));

    const auto session = makeSession (Rational (8),
        std::make_shared<const Constituent> (phrase));
    const RenderPipeline pipeline (session, TempoMap::fromBpm (Rational (120)));

    // At LMC 7 s: loop 40 (2 s cycle) is at cycle 3, offset 1; loop 41 (7/4 s
    // cycle) is at cycle 4 exactly — they meet here in absolute time but on
    // different cycle counts, exactly.
    const auto reads = pipeline.activeReadsAt (Rational (7));
    REQUIRE (reads.size() == 2);
    CHECK (reads[0].loop == ConstituentId (40));
    CHECK (reads[0].cycle == 3);                    // floor(7 / 2)
    CHECK (reads[0].tapePosition == Rational (1));   // 7 - 3*2
    CHECK (reads[1].loop == ConstituentId (41));
    CHECK (reads[1].cycle == 4);                    // floor(7 / (7/4)) = floor(4)
    CHECK (reads[1].tapePosition == Rational (0));   // 7 - 4*(7/4) = 0, exactly
}

TEST_CASE ("loop repetition is drift-free at the hundredth cycle", "[renderpipeline][conceptual-time]")
{
    // White paper Part 9.3: the read position at the hundredth cycle is
    // bit-identical to the first.
    const auto session = makeSession (Rational (1000),
        makeLoop (10, Rational (0), Rational (1000), 100, Rational (0), Rational (1)));
    const RenderPipeline pipeline (session, TempoMap::constant (Rational (1))); // 1 wn = 1 s

    const auto firstCycle = pipeline.activeReadsAt (Rational (1, 2));    // 0.5 s in
    const auto cycle100   = pipeline.activeReadsAt (Rational (201, 2));  // 100.5 s in

    REQUIRE (firstCycle.size() == 1);
    REQUIRE (cycle100.size() == 1);
    CHECK (firstCycle[0].cycle == 0);
    CHECK (cycle100[0].cycle == 100);
    CHECK (cycle100[0].tapePosition == firstCycle[0].tapePosition); // no drift
    CHECK (cycle100[0].tapePosition == Rational (1, 2));
}
