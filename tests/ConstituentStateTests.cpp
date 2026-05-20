// Tests for sirius::validate — the Constituent state machine (white paper
// §17.7). State is derived, never persisted: a leaf loop whose tape does not
// resolve is Broken; a node placed outside its parent is Invalid; both render
// as silence with identity intact.
#include "sirius/ConstituentValidator.h"

#include "sirius/Constituent.h"
#include "sirius/TapeReference.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::ConstituentState;
using sirius::Position;
using sirius::Rational;
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
