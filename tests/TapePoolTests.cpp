// Tests for sirius::TapePool — the project's pool of tapes (tape subsystem
// slice 1). Pins the >=1 invariant, monotonic id allocation, add/remove/rename,
// the explicit-list ctor's validation, and the SessionFormat round-trip.
#include "sirius/TapePool.h"

#include "sirius/SessionFormat.h"
#include "sirius/TapeDescriptor.h"
#include "sirius/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::TapeDescriptor;
using sirius::TapeId;
using sirius::TapePool;

TEST_CASE ("default TapePool holds exactly one primary tape", "[tape-pool]")
{
    TapePool pool;
    REQUIRE (pool.count() == 1);
    CHECK (pool.at (0).id == TapeId (1));
    CHECK (pool.at (0).name == "Tape 1");
    CHECK (pool.primary() == TapeId (1));
    CHECK (pool.find (TapeId (1)) != nullptr);
    CHECK (pool.find (TapeId (999)) == nullptr);
    CHECK (pool.tapes().size() == 1u);
}

TEST_CASE ("TapePool::add appends a tape with a fresh monotonic id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("Drums");
    const auto b = pool.add ("Bass");

    CHECK (a == TapeId (2));
    CHECK (b == TapeId (3));
    REQUIRE (pool.count() == 3);
    CHECK (pool.at (1).id == a);
    CHECK (pool.at (1).name == "Drums");
    CHECK (pool.at (2).id == b);
    CHECK (pool.at (2).name == "Bass");
    CHECK (pool.primary() == TapeId (1)); // primary unchanged by add
}

TEST_CASE ("TapePool::remove erases a tape but enforces the >=1 floor", "[tape-pool]")
{
    TapePool pool;
    const auto drums = pool.add ("Drums");
    REQUIRE (pool.count() == 2);

    SECTION ("removing a non-last tape succeeds")
    {
        CHECK (pool.remove (drums));
        CHECK (pool.count() == 1);
        CHECK (pool.find (drums) == nullptr);
        CHECK (pool.primary() == TapeId (1));
    }

    SECTION ("removing the last remaining tape is refused (floor of 1)")
    {
        REQUIRE (pool.remove (drums));        // back down to 1
        REQUIRE (pool.count() == 1);
        CHECK_FALSE (pool.remove (TapeId (1))); // refused
        CHECK (pool.count() == 1);              // unchanged — falsifiable
        CHECK (pool.find (TapeId (1)) != nullptr);
    }

    SECTION ("removing an unknown id returns false")
    {
        CHECK_FALSE (pool.remove (TapeId (999)));
        CHECK (pool.count() == 2);
    }

    SECTION ("removing the primary is refused even above the floor")
    {
        // The primary tape is permanent: InputMixer pins it and refuses removal,
        // so the pool must agree or the two desync (primary writer closed while the
        // pool entry vanishes). This must hold with >=2 tapes, not just at the floor.
        CHECK_FALSE (pool.remove (pool.primary()));
        CHECK (pool.count() == 2);                 // unchanged — falsifiable
        CHECK (pool.find (pool.primary()) != nullptr);
    }
}

TEST_CASE ("TapePool::add after remove never reuses an id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("A"); // id 2
    REQUIRE (pool.remove (a));     // erase id 2
    const auto b = pool.add ("B"); // must be id 3, not 2
    CHECK (b == TapeId (3));
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
    CHECK (pool.primary() == TapeId (4));
    CHECK (pool.at (1).name == "Gtr");

    // nextId_ seeded one past the max id (7) -> next add is 8.
    CHECK (pool.add ("New") == TapeId (8));
}

TEST_CASE ("TapePool explicit-list ctor rejects empty and duplicate ids", "[tape-pool]")
{
    REQUIRE_THROWS_AS (TapePool (std::vector<TapeDescriptor> {}),
                       std::invalid_argument);

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

    const auto json   = sirius::persistence::serializeTapePool (original);
    const auto loaded = sirius::persistence::deserializeTapePool (json);

    REQUIRE (loaded.count() == original.count());
    for (int i = 0; i < original.count(); ++i)
        CHECK (loaded.at (i) == original.at (i));

    // nextId_ survives: next add is one past the max imported id (9) -> 10.
    auto loadedCopy = loaded;
    CHECK (loadedCopy.add ("New") == TapeId (10));
}

TEST_CASE ("deserializeTapePool rejects empty and malformed documents", "[tape-pool][sessionformat]")
{
    // Present-but-empty tapes array violates the >=1 on-disk contract.
    REQUIRE_THROWS_AS (sirius::persistence::deserializeTapePool ("{ \"tapes\": [] }"),
                       std::runtime_error);
    // Not valid JSON.
    REQUIRE_THROWS_AS (sirius::persistence::deserializeTapePool ("{ not json"),
                       std::runtime_error);
    // Object without a tapes array.
    REQUIRE_THROWS_AS (sirius::persistence::deserializeTapePool ("{}"),
                       std::runtime_error);
}
