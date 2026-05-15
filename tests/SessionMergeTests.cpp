// Tests for the CRDT session merge (white paper Part 12.6). The white paper
// makes a strong architectural claim — "Reconciliation requires no human
// conflict resolution. The data model makes conflicts structurally
// impossible." These tests pin that down by exercising the algebraic
// properties of a CRDT: commutativity, associativity, idempotence, and
// last-writer-wins on the active version after a divergent edit.
#include "sirius/SessionMerge.h"

#include "sirius/Constituent.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::ConstituentVersion;
using sirius::MergeableSession;
using sirius::Position;
using sirius::Rational;

namespace
{
    ConstituentVersion version (std::int64_t id, std::int64_t ts, const char* name = "")
    {
        return { ConstituentId (id),
                 std::make_shared<const Constituent> (
                     Constituent (ConstituentId (id), Position(), Position (Rational (1)))
                         .withName (name)),
                 ts };
    }

    bool sameVersions (const MergeableSession& a, const MergeableSession& b)
    {
        if (a.versions.size() != b.versions.size()) return false;
        for (std::size_t i = 0; i < a.versions.size(); ++i)
        {
            const auto& x = a.versions[i];
            const auto& y = b.versions[i];
            if (x.id.value() != y.id.value()) return false;
            if (x.editTimestamp != y.editTimestamp) return false;
        }
        return a.tapeHashes == b.tapeHashes;
    }
}

TEST_CASE ("merge unions tape hashes — different content can never collide",
           "[crdt-merge]")
{
    MergeableSession alice;
    alice.tapeHashes.insert ("hash-A");
    alice.tapeHashes.insert ("hash-shared");

    MergeableSession bob;
    bob.tapeHashes.insert ("hash-B");
    bob.tapeHashes.insert ("hash-shared");

    const auto merged = sirius::merge (alice, bob);
    CHECK (merged.tapeHashes.size() == 3);
    CHECK (merged.tapeHashes.count ("hash-A")      == 1);
    CHECK (merged.tapeHashes.count ("hash-B")      == 1);
    CHECK (merged.tapeHashes.count ("hash-shared") == 1);
}

TEST_CASE ("merge unions Constituent versions — immutability eliminates conflict",
           "[crdt-merge]")
{
    // Two nodes both edit identity 42 during a partition, producing different
    // versions at different timestamps. The merge keeps both versions; the
    // active one is determined by last-writer-wins.
    MergeableSession a;
    a.versions = { version (1, 100, "shared"),
                   version (42, 200, "alice-edit") };

    MergeableSession b;
    b.versions = { version (1, 100, "shared"),
                   version (42, 300, "bob-edit") };

    const auto merged = sirius::merge (a, b);
    CHECK (merged.versions.size() == 3); // shared was deduped; the two edits to 42 are both kept

    const auto active = sirius::activeVersions (merged);
    REQUIRE (active.count (42) == 1);
    CHECK (active.at (42).editTimestamp == 300);
    CHECK (active.at (42).state->name() == "bob-edit");
}

TEST_CASE ("merge is commutative — order of partitions does not matter",
           "[crdt-merge]")
{
    MergeableSession a;
    a.tapeHashes = { "h1", "h2" };
    a.versions = { version (1, 10), version (2, 20) };

    MergeableSession b;
    b.tapeHashes = { "h2", "h3" };
    b.versions = { version (2, 30), version (3, 40) };

    CHECK (sameVersions (sirius::merge (a, b), sirius::merge (b, a)));
}

TEST_CASE ("merge is associative — three-way merge does not depend on grouping",
           "[crdt-merge]")
{
    MergeableSession a;
    a.versions = { version (1, 10) };
    MergeableSession b;
    b.versions = { version (2, 20) };
    MergeableSession c;
    c.versions = { version (3, 30) };

    const auto leftFirst  = sirius::merge (sirius::merge (a, b), c);
    const auto rightFirst = sirius::merge (a, sirius::merge (b, c));
    CHECK (sameVersions (leftFirst, rightFirst));
}

TEST_CASE ("merge is idempotent — merging a session with itself changes nothing",
           "[crdt-merge]")
{
    MergeableSession a;
    a.tapeHashes = { "h1" };
    a.versions = { version (1, 10), version (2, 20) };
    CHECK (sameVersions (sirius::merge (a, a), a));
}

TEST_CASE ("active version uses last-writer-wins on timestamp",
           "[crdt-merge]")
{
    MergeableSession s;
    s.versions = { version (1, 100, "old"),
                   version (1, 200, "newer"),
                   version (1, 150, "middle") };
    const auto active = sirius::activeVersions (s);
    REQUIRE (active.count (1) == 1);
    CHECK (active.at (1).editTimestamp == 200);
    CHECK (active.at (1).state->name() == "newer");
}
