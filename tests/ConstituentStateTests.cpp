// Tests for sirius::validate — the Constituent state machine (white paper
// §17.7). State is derived, never persisted: a leaf loop whose tape does not
// resolve is Broken; a node placed outside its parent is Invalid; both render
// as silence with identity intact.
#include "sirius/ConstituentValidator.h"

#include "sirius/Constituent.h"
#include "sirius/RenderPipeline.h"
#include "sirius/TapeReference.h"
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::ConstituentState;
using sirius::Position;
using sirius::Rational;
using sirius::RenderPipeline;
using sirius::TapeId;
using sirius::TapeReference;

namespace
{
    /// A leaf loop placed at [placeIn, placeOut) referencing tape `tape`.
    std::shared_ptr<const Constituent> makeLoop (std::int64_t id,
                                                 Rational placeIn, Rational placeOut,
                                                 std::int64_t tape)
    {
        const Constituent loop { ConstituentId (id), Position (placeIn), Position (placeOut) };
        return std::make_shared<const Constituent> (
            loop.withTapeReference (TapeReference (TapeId (tape), Rational (0), Rational (2))));
    }

    /// A session [0, length) holding `child`.
    Constituent makeSession (std::int64_t id, Rational length,
                             std::shared_ptr<const Constituent> child)
    {
        const Constituent session { ConstituentId (id), Position(), Position (length) };
        return session.withChildAdded (std::move (child));
    }
}

TEST_CASE ("a well-formed tree validates entirely Valid", "[constituent-state]")
{
    const Constituent root = makeSession (1, Rational (8),
        makeLoop (10, Rational (0), Rational (8), 100));

    const auto validation = sirius::validate (root, sirius::alwaysResolves);

    CHECK (validation.state (ConstituentId (1)) == ConstituentState::Valid);
    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Valid);
    CHECK (validation.renderable (ConstituentId (10)));
}

TEST_CASE ("a leaf loop whose tape does not resolve is Broken", "[constituent-state]")
{
    const Constituent root = makeSession (1, Rational (8),
        makeLoop (10, Rational (0), Rational (8), 100));

    // Resolver fails only tape 100 — the loop's tape.
    const auto resolver = [] (const TapeReference& ref)
    { return ref.tape != TapeId (100); };

    const auto validation = sirius::validate (root, resolver);

    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Broken);
    CHECK_FALSE (validation.renderable (ConstituentId (10)));
    CHECK (validation.state (ConstituentId (1)) == ConstituentState::Valid); // parent intact
}

TEST_CASE ("a child extending past the parent's end is Invalid", "[constituent-state]")
{
    // Parent length 4; child placed [0, 6) overflows the end.
    const Constituent root = makeSession (1, Rational (4),
        makeLoop (10, Rational (0), Rational (6), 100));

    const auto validation = sirius::validate (root, sirius::alwaysResolves);

    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Invalid);
}

TEST_CASE ("a child filling the parent exactly is Valid (half-open bound)", "[constituent-state]")
{
    const Constituent root = makeSession (1, Rational (4),
        makeLoop (10, Rational (0), Rational (4), 100));

    const auto validation = sirius::validate (root, sirius::alwaysResolves);

    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Valid);
}

TEST_CASE ("a node that is both out-of-bounds and tape-broken reports Invalid", "[constituent-state]")
{
    // Child overflows (placed [0,6) in a length-4 parent) AND its tape fails.
    const Constituent root = makeSession (1, Rational (4),
        makeLoop (10, Rational (0), Rational (6), 100));

    const auto resolver = [] (const TapeReference&) { return false; };

    const auto validation = sirius::validate (root, resolver);

    // Structural error dominates: Invalid, not Broken.
    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Invalid);
}

TEST_CASE ("validation does not mutate the tree", "[constituent-state]")
{
    const auto leaf = makeLoop (10, Rational (0), Rational (8), 100);
    const Constituent root = makeSession (1, Rational (8), leaf);

    const auto resolver = [] (const TapeReference&) { return false; }; // mark Broken

    const auto validation = sirius::validate (root, resolver);

    // Identity and structure survive (white paper §17.7: repair, not recreate).
    CHECK (validation.state (ConstituentId (10)) == ConstituentState::Broken);
    CHECK (root.id() == ConstituentId (1));
    REQUIRE (root.children().size() == 1);
    CHECK (root.children()[0]->id() == ConstituentId (10));
    CHECK (root.children()[0].get() == leaf.get()); // same shared node, not copied
}

TEST_CASE ("a Broken node and its subtree render as silence; Valid siblings still read",
           "[constituent-state][renderpipeline]")
{
    // Session [0,8) with two leaf loops both filling it. Loop 10's tape fails
    // (Broken); loop 11's resolves (Valid).
    Constituent session (ConstituentId (1), Position(), Position (Rational (8)));
    session = session.withChildAdded (makeLoop (10, Rational (0), Rational (8), 100));
    session = session.withChildAdded (makeLoop (11, Rational (0), Rational (8), 200));
    const auto root = std::make_shared<const Constituent> (session);

    const auto resolver = [] (const TapeReference& ref)
    { return ref.tape != TapeId (100); }; // only loop 10 is Broken

    const auto validation = sirius::validate (*root, resolver);

    const RenderPipeline pipeline (root, sirius::TempoMap::fromBpm (Rational (120)), validation);
    const auto reads = pipeline.activeReadsAt (Rational (0));

    // Only loop 11 sounds; the Broken loop 10 is silent.
    REQUIRE (reads.size() == 1);
    CHECK (reads[0].loop == ConstituentId (11));
}

TEST_CASE ("an Invalid parent silences its whole subtree", "[constituent-state][renderpipeline]")
{
    // Parent length 4. Child phrase placed [0,6) overflows -> Invalid. Inside it,
    // a grandchild loop that would otherwise sound.
    Constituent phrase (ConstituentId (20), Position(), Position (Rational (6)));
    phrase = phrase.withChildAdded (makeLoop (30, Rational (0), Rational (6), 300));
    Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    session = session.withChildAdded (std::make_shared<const Constituent> (phrase));
    const auto root = std::make_shared<const Constituent> (session);

    const auto validation = sirius::validate (*root, sirius::alwaysResolves);
    REQUIRE (validation.state (ConstituentId (20)) == ConstituentState::Invalid);

    const RenderPipeline pipeline (root, sirius::TempoMap::fromBpm (Rational (120)), validation);
    const auto reads = pipeline.activeReadsAt (Rational (0));

    // The Invalid phrase is skipped, so its grandchild loop never sounds.
    CHECK (reads.empty());
}
