// Tests for ida::findCandidatesFor / resolveFirst — the engine-side half of
// role-fillable phrase resolution (white paper Part 8.4). The engine does not
// pick which phrase fills a role; it surfaces the eligible pool deterministically
// and provides resolveFirst as the trivial default policy. These tests pin down
// the eligibility predicate (role match + isRoleFillable + carries
// PhraseMetadata), the stable enumeration order, and the no-match fallback.
#include "sirius/RoleResolver.h"

#include "sirius/Constituent.h"
#include "sirius/Phrase.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using ida::Constituent;
using ida::ConstituentId;
using ida::PhraseMetadata;
using ida::Position;
using ida::Rational;
using ida::RoleSlot;

namespace
{
    /// Builds a leaf Constituent with the given id, optionally tagged as a
    /// role-fillable phrase. Roles default to non-fillable so each test must
    /// opt in deliberately.
    std::shared_ptr<const Constituent>
    makePhrase (std::int64_t id, std::string role, bool fillable)
    {
        Constituent base (ConstituentId (id), Position(), Position (Rational (4)));

        PhraseMetadata meta;
        meta.role = std::move (role);
        meta.isRoleFillable = fillable;

        return std::make_shared<const Constituent> (
            base.withPhraseMetadata (std::move (meta)));
    }

    /// A Constituent that carries no PhraseMetadata — a bare loop or slice.
    std::shared_ptr<const Constituent> makeBareLoop (std::int64_t id)
    {
        return std::make_shared<const Constituent> (
            ConstituentId (id), Position(), Position (Rational (4)));
    }

    RoleSlot makeSlot (std::string role)
    {
        return RoleSlot (std::move (role), Position(), Position (Rational (4)));
    }
}

TEST_CASE ("empty pool yields no candidates", "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const auto candidates = ida::findCandidatesFor (slot, {});
    CHECK (candidates.empty());
}

TEST_CASE ("a single matching fillable phrase is returned", "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (42, "chorus", true)
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 1);
    CHECK (candidates[0] == ConstituentId (42));
}

TEST_CASE ("multiple matches are returned in pool order", "[role-resolver]")
{
    const auto slot = makeSlot ("solo");
    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (1, "solo", true),
        makePhrase (2, "solo", true),
        makePhrase (3, "solo", true)
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 3);
    CHECK (candidates[0] == ConstituentId (1));
    CHECK (candidates[1] == ConstituentId (2));
    CHECK (candidates[2] == ConstituentId (3));
}

TEST_CASE ("a phrase whose role does not match is skipped", "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (10, "verse", true),
        makePhrase (11, "bridge", true),
        makePhrase (12, "chorus", true)
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 1);
    CHECK (candidates[0] == ConstituentId (12));
}

TEST_CASE ("a matching phrase that is not role-fillable is skipped",
           "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (20, "chorus", false), // right role, not fillable
        makePhrase (21, "chorus", true)
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 1);
    CHECK (candidates[0] == ConstituentId (21));
}

TEST_CASE ("a Constituent with no phrase metadata is skipped",
           "[role-resolver]")
{
    const auto slot = makeSlot ("fill");
    const std::vector<Constituent::ChildPtr> pool {
        makeBareLoop (30),
        makePhrase (31, "fill", true)
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 1);
    CHECK (candidates[0] == ConstituentId (31));
}

TEST_CASE ("null entries in the pool are skipped without crashing",
           "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const std::vector<Constituent::ChildPtr> pool {
        nullptr,
        makePhrase (40, "chorus", true),
        nullptr,
        makePhrase (41, "chorus", true),
        nullptr
    };

    const auto candidates = ida::findCandidatesFor (slot, pool);
    REQUIRE (candidates.size() == 2);
    CHECK (candidates[0] == ConstituentId (40));
    CHECK (candidates[1] == ConstituentId (41));
}

TEST_CASE ("resolveFirst fills the slot with the first matching candidate",
           "[role-resolver]")
{
    const auto slot = makeSlot ("solo");
    REQUIRE_FALSE (slot.isFilled());

    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (50, "solo", true),
        makePhrase (51, "solo", true)
    };

    const auto resolved = ida::resolveFirst (slot, pool);
    REQUIRE (resolved.isFilled());
    CHECK (*resolved.filledBy() == ConstituentId (50));
    // The slot's other properties — role and span — are preserved.
    CHECK (resolved.role() == slot.role());
    CHECK (resolved.conceptualIn() == slot.conceptualIn());
    CHECK (resolved.conceptualOut() == slot.conceptualOut());
}

TEST_CASE ("resolveFirst leaves the slot unchanged when no candidate matches",
           "[role-resolver]")
{
    const auto slot = makeSlot ("chorus");
    const std::vector<Constituent::ChildPtr> pool {
        makePhrase (60, "verse", true),
        makePhrase (61, "chorus", false),  // matches role but not fillable
        makeBareLoop (62)
    };

    const auto resolved = ida::resolveFirst (slot, pool);
    CHECK_FALSE (resolved.isFilled());
    CHECK (resolved.role() == slot.role());
}
