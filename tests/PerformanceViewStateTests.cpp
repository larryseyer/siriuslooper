// Tests for the Performance view's state selector (white paper Part 14.5).
// The view is deliberately tiny — "never more than three things at once" —
// so these tests pin down what *those three things are*: the deepest named
// phrase the playhead is inside (so "verse" wins over "demo session"), the
// foreground loop's cycle status, and an honest "silent" report when nothing
// is sounding.
#include "ida/PerformanceViewState.h"

#include "ida/Arrangement.h"
#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/RepetitionRules.h"
#include "ida/TapeId.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::Constituent;
using ida::ConstituentId;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TapeReference;
using ida::TempoMap;

namespace
{
    std::shared_ptr<const Constituent> makeFreeRunningLoop (
        std::int64_t id, const char* name, Rational length, std::int64_t tape)
    {
        const auto loop = Constituent (ConstituentId (id), Position(), Position (length))
            .withName (name)
            .withTapeReference (TapeReference (TapeId (tape), Rational (0), length));
        return std::make_shared<const Constituent> (loop);
    }
}

TEST_CASE ("when the playhead is before the first phrase, the view is silent",
           "[perfview]")
{
    Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    session = session.withName ("session");
    const auto state = ida::selectPerformanceView (
        session, TempoMap::fromBpm (Rational (120)), Rational (-1));
    CHECK (state.isSilent);
    CHECK (state.currentPhraseName.empty());
    CHECK (state.soundingLoopCount == 0);
}

TEST_CASE ("the deepest named ancestor wins as the foreground phrase",
           "[perfview]")
{
    // The session has a name, the verse phrase inside it has a name, and the
    // inner loop also has a name. The view should report the *verse* — the
    // performer cares which phrase they are in, not the outer session or the
    // inner loop's bookkeeping name.
    const auto loop = makeFreeRunningLoop (10, "intro loop", Rational (4), 100);

    Constituent verse (ConstituentId (20), Position(), Position (Rational (8)));
    verse = verse.withName ("verse");
    verse = ida::arrangement::layer (verse, { loop });
    const auto verseShared = std::make_shared<const Constituent> (verse);

    Constituent session (ConstituentId (1), Position(), Position (Rational (8)));
    session = session.withName ("session");
    session = session.withChildAdded (verseShared);

    const auto state = ida::selectPerformanceView (
        session, TempoMap::fromBpm (Rational (120)), Rational (2));
    CHECK_FALSE (state.isSilent);
    CHECK (state.currentPhraseName == "verse");
    CHECK (state.soundingLoopCount == 1);
}

TEST_CASE ("cycle status shows 'N of M' for NTimes and 'loop K' for Forever",
           "[perfview]")
{
    // Build a session containing one free-running loop that has played for two
    // and a half cycles by the time we ask.
    auto loop = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (4)))
            .withName ("groove")
            .withTapeReference (TapeReference (TapeId (1), Rational (0), Rational (1))));
    // length 4 wn at 120 BPM = 8 LMC seconds; tape slice length 1s
    // -> at 2.5 sec elapsed, cycle = 2 (so "loop 3")

    Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    session = session.withName ("session");
    session = session.withChildAdded (loop);

    SECTION ("Forever defaults: 'loop K'")
    {
        const auto state = ida::selectPerformanceView (
            session, TempoMap::fromBpm (Rational (120)), Rational (5, 2));
        CHECK (state.cycleStatus == "loop 3");
    }

    SECTION ("NTimes: 'N of M'")
    {
        ida::RepetitionRules rules;
        rules.cardinality = ida::cardinality::NTimes (4);
        auto loopBounded = std::make_shared<const Constituent> (
            loop->withRepetitionRules (rules));

        Constituent session2 (ConstituentId (1), Position(), Position (Rational (4)));
        session2 = session2.withChildAdded (loopBounded);

        const auto state = ida::selectPerformanceView (
            session2, TempoMap::fromBpm (Rational (120)), Rational (5, 2));
        CHECK (state.cycleStatus == "3 of 4");
    }
}
