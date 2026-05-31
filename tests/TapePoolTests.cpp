// Tests for ida::TapePool — the project's pool of tapes (tape subsystem
// slice 1, blank-slate revision). The pool is now possibly empty: a default
// pool is the New Song state, primary() is optional, and remove() may empty
// the pool and never pins the primary. Pins monotonic id allocation,
// add/remove/rename, the explicit-list ctor's validation, and the
// SessionFormat round-trip.
#include "ida/TapePool.h"

#include "ida/SessionFormat.h"
#include "ida/TapeDescriptor.h"
#include "ida/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using ida::TapeDescriptor;
using ida::TapeId;
using ida::TapePool;

TEST_CASE ("default TapePool is empty (blank-slate)", "[tape-pool]")
{
    TapePool pool;
    CHECK (pool.count() == 0);
    CHECK (pool.tapes().empty());
    CHECK_FALSE (pool.primary().has_value());
    CHECK (pool.find (TapeId (1)) == nullptr);
}

TEST_CASE ("TapePool::add seeds the first tape and primary follows the front", "[tape-pool]")
{
    TapePool pool;
    const auto first = pool.add ("Tape 1");
    CHECK (first == TapeId (1));
    REQUIRE (pool.primary().has_value());
    CHECK (*pool.primary() == TapeId (1));
    const auto second = pool.add ("Drums");
    CHECK (second == TapeId (2));
    CHECK (*pool.primary() == TapeId (1)); // primary unchanged by add
}

TEST_CASE ("TapePool::add appends a tape with a fresh monotonic id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("Drums");
    const auto b = pool.add ("Bass");

    CHECK (a == TapeId (1));
    CHECK (b == TapeId (2));
    REQUIRE (pool.count() == 2);
    CHECK (pool.at (0).id == a);
    CHECK (pool.at (0).name == "Drums");
    CHECK (pool.at (1).id == b);
    CHECK (pool.at (1).name == "Bass");
    REQUIRE (pool.primary().has_value());
    CHECK (*pool.primary() == TapeId (1)); // primary is the front
}

TEST_CASE ("TapePool::remove can empty the pool and does not pin the primary", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("A");
    const auto b = pool.add ("B");
    REQUIRE (pool.count() == 2);

    SECTION ("removing the primary is allowed; front advances")
    {
        CHECK (pool.remove (a));
        CHECK (pool.count() == 1);
        REQUIRE (pool.primary().has_value());
        CHECK (*pool.primary() == b);
    }
    SECTION ("removing the last tape empties the pool")
    {
        REQUIRE (pool.remove (a));
        REQUIRE (pool.remove (b));
        CHECK (pool.count() == 0);
        CHECK_FALSE (pool.primary().has_value());
    }
    SECTION ("removing an unknown id returns false")
    {
        CHECK_FALSE (pool.remove (TapeId (999)));
        CHECK (pool.count() == 2);
    }
}

TEST_CASE ("TapePool::add after remove never reuses an id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("A"); // id 1
    REQUIRE (pool.remove (a));     // erase id 1 (pool now empty)
    const auto b = pool.add ("B"); // must be id 2, not 1
    CHECK (b == TapeId (2));
}

TEST_CASE ("TapePool::rename changes a tape's name", "[tape-pool]")
{
    TapePool pool;
    const auto drums = pool.add ("Drms"); // typo

    CHECK (pool.rename (drums, "Drums"));
    CHECK (pool.find (drums)->name == "Drums");

    CHECK_FALSE (pool.rename (TapeId (999), "Nope")); // unknown id
}

TEST_CASE ("TapePool explicit-list ctor seeds from a non-empty unique list", "[tape-pool]")
{
    TapePool pool (std::vector<TapeDescriptor> {
        TapeDescriptor { TapeId (4), "Vox" },
        TapeDescriptor { TapeId (7), "Gtr" } });

    REQUIRE (pool.count() == 2);
    REQUIRE (pool.primary().has_value());
    CHECK (*pool.primary() == TapeId (4));
    CHECK (pool.at (1).name == "Gtr");

    // nextId_ seeded one past the max id (7) -> next add is 8.
    CHECK (pool.add ("New") == TapeId (8));
}

TEST_CASE ("TapePool explicit-list ctor accepts an empty list", "[tape-pool]")
{
    TapePool pool (std::vector<TapeDescriptor> {});
    CHECK (pool.count() == 0);
    CHECK (pool.add ("First") == TapeId (1)); // empty list ⇒ nextId_ stays 1
}

TEST_CASE ("TapePool explicit-list ctor rejects duplicate ids", "[tape-pool]")
{
    REQUIRE_THROWS_AS (
        TapePool (std::vector<TapeDescriptor> {
            TapeDescriptor { TapeId (3), "A" },
            TapeDescriptor { TapeId (3), "B" } }),
        std::invalid_argument);
}

TEST_CASE ("TapePool round-trips through SessionFormat", "[tape-pool][sessionformat]")
{
    TapePool original (std::vector<TapeDescriptor> {
        TapeDescriptor { TapeId (1), "Tape 1" },
        TapeDescriptor { TapeId (5), "Drums" },
        TapeDescriptor { TapeId (9), "Bass" } });

    const auto json   = ida::persistence::serializeTapePool (original);
    const auto loaded = ida::persistence::deserializeTapePool (json);

    REQUIRE (loaded.count() == original.count());
    for (int i = 0; i < original.count(); ++i)
        CHECK (loaded.at (i) == original.at (i));

    // nextId_ survives: next add is one past the max imported id (9) -> 10.
    auto loadedCopy = loaded;
    CHECK (loadedCopy.add ("New") == TapeId (10));
}

TEST_CASE ("empty TapePool round-trips through SessionFormat", "[tape-pool][sessionformat]")
{
    TapePool empty (std::vector<TapeDescriptor> {});
    const auto json   = ida::persistence::serializeTapePool (empty);
    const auto loaded = ida::persistence::deserializeTapePool (json);
    CHECK (loaded.count() == 0);
}

TEST_CASE ("deserializeTapePool accepts an empty array but rejects malformed documents",
           "[tape-pool][sessionformat]")
{
    // Present-but-empty tapes array is the legal blank-slate state — it loads
    // to an empty pool rather than throwing (the >=1 floor is overturned).
    CHECK (ida::persistence::deserializeTapePool ("{ \"tapes\": [] }").count() == 0);
    // Not valid JSON.
    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool ("{ not json"),
                       std::runtime_error);
    // Object without a tapes array.
    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool ("{}"),
                       std::runtime_error);
}

// ── TAPECOLOR Slice 2: per-tape tri-state (None / BeforeWrite / AfterRead) ──
//
// Operator design lock 2026-05-24 (`[[project_tapecolor_placement]]`): each
// tape carries its own TAPECOLOR mode, defaulting to None. Switching it on
// snaps to AfterRead (clean disk + color on playback). BeforeWrite bakes the
// color into the FLAC. The mode rides on TapeDescriptor and serializes
// wire-stably so a project saved before the operator ever touches TAPECOLOR
// reloads identically.

TEST_CASE ("TapeDescriptor defaults tapeColor to None", "[tape-pool][tapecolor]")
{
    TapeDescriptor d { TapeId (1), "Tape 1" };
    CHECK (d.tapeColor == ida::TapeColorMode::None);
}

TEST_CASE ("the first added TapePool tape defaults tapeColor == None",
           "[tape-pool][tapecolor]")
{
    TapePool pool;
    pool.add ("Tape 1");
    CHECK (pool.at (0).tapeColor == ida::TapeColorMode::None);

    // add() preserves the default — newly-added tapes are silent until the
    // operator explicitly turns TAPECOLOR on for that tape.
    const auto drums = pool.add ("Drums");
    CHECK (pool.find (drums)->tapeColor == ida::TapeColorMode::None);
}

TEST_CASE ("TapePool tapeColor round-trips through SessionFormat", "[tape-pool][tapecolor][sessionformat]")
{
    TapePool original (std::vector<TapeDescriptor> {
        TapeDescriptor { TapeId (1), "Tape 1", ida::TapeColorMode::None },
        TapeDescriptor { TapeId (2), "Drums",  ida::TapeColorMode::BeforeWrite },
        TapeDescriptor { TapeId (3), "Bass",   ida::TapeColorMode::AfterRead } });

    const auto json   = ida::persistence::serializeTapePool (original);
    const auto loaded = ida::persistence::deserializeTapePool (json);

    REQUIRE (loaded.count() == original.count());
    for (int i = 0; i < original.count(); ++i)
    {
        CHECK (loaded.at (i).id        == original.at (i).id);
        CHECK (loaded.at (i).name      == original.at (i).name);
        CHECK (loaded.at (i).tapeColor == original.at (i).tapeColor);
    }
}

TEST_CASE ("deserializeTapePool back-compat: missing tape_color reads as None",
           "[tape-pool][tapecolor][sessionformat]")
{
    // Projects saved before Slice 2 have no tape_color field. They MUST
    // reload with every tape's mode defaulting to None so back-compat is
    // bit-equivalent to the pre-Slice-2 behavior.
    const juce::String legacy =
        R"({ "tapes": [ { "id": 1, "name": "Tape 1" },
                        { "id": 5, "name": "Drums"  } ] })";

    const auto loaded = ida::persistence::deserializeTapePool (legacy);

    REQUIRE (loaded.count() == 2);
    CHECK (loaded.at (0).tapeColor == ida::TapeColorMode::None);
    CHECK (loaded.at (1).tapeColor == ida::TapeColorMode::None);
}

TEST_CASE ("deserializeTapePool rejects an unknown tape_color string",
           "[tape-pool][tapecolor][sessionformat]")
{
    // Wire-stable tokens are "None" / "BeforeWrite" / "AfterRead". Anything
    // else means the on-disk format has drifted under us — fail loud rather
    // than silently snapping to None and discarding the operator's intent.
    const juce::String bad =
        R"({ "tapes": [ { "id": 1, "name": "Tape 1", "tape_color": "Glitter" } ] })";

    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool (bad),
                       std::runtime_error);
}
