// Tests for the session JSON format (white paper Part 7.8). The single load-
// bearing property here is round-trip fidelity: a serialized session, parsed
// and re-serialized, must produce identical JSON. That property covers every
// field of a Constituent — id, boundaries, anchor, name, local meter and tempo
// map, all five repetition-rule variants, phrase metadata, tape references,
// and children — without us having to compare each one by hand. Negative tests
// pin down that corruption fails loud (white paper Part 13.3, rule 3 — never
// silent).
#include "sirius/Arrangement.h"
#include "sirius/Constituent.h"
#include "sirius/Meter.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/RepetitionRules.h"
#include "sirius/SessionFormat.h"
#include "sirius/TapeId.h"
#include "sirius/TapeReference.h"
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <variant>

using sirius::AnchorToParent;
using sirius::Constituent;
using sirius::ConstituentId;
using sirius::Meter;
using sirius::Mutation;
using sirius::Position;
using sirius::Rational;
using sirius::RepetitionRules;
using sirius::TapeId;
using sirius::TapeReference;
using sirius::TempoMap;

namespace
{
    /// Builds a Constituent tree exercising every field the serializer touches:
    /// non-default boundaries and anchor, a local meter and tempo map, every
    /// repetition-rule dimension set to a non-default value with parameters,
    /// phrase metadata with a grammatical link, a tape reference, and a layered
    /// pair of children.
    std::shared_ptr<const Constituent> exhaustiveTree()
    {
        RepetitionRules rules;
        rules.trigger     = sirius::trigger::EveryNBars (4);
        rules.cardinality = sirius::cardinality::NTimes (8);
        rules.phase       = sirius::phase::QuantizedToGrid (Rational (1, 4));
        rules.mutation    = Mutation::Decaying;
        rules.termination = sirius::termination::FadeOverBars (Rational (2));

        sirius::PhraseMetadata phrase;
        phrase.role = "verse";
        phrase.intent = "introduce the harmonic centre";
        phrase.entrance = sirius::EntranceCharacter::Pickup;
        phrase.exit = sirius::ExitCharacter::HandOff;
        phrase.isRoleFillable = true;
        phrase.grammaticalLinks.push_back (
            { sirius::GrammaticalLink::Kind::CallAndResponse, ConstituentId (99) });

        const Constituent loop =
            Constituent (ConstituentId (10), Position(), Position (Rational (4)))
                .withName ("intro loop")
                .withTapeReference (TapeReference (TapeId (200), Rational (1), Rational (5)));

        Constituent verse =
            Constituent (ConstituentId (20), Position(), Position (Rational (8)))
                .withName ("verse")
                .withAnchor (AnchorToParent::Start)
                .withLocalMeter (Meter (7, 8))
                .withLocalTempoMap (TempoMap::fromBpm (Rational (132)))
                .withRepetitionRules (rules)
                .withPhraseMetadata (phrase);

        verse = sirius::arrangement::layer (verse,
            { std::make_shared<const Constituent> (loop),
              std::make_shared<const Constituent> (
                  Constituent (ConstituentId (11), Position(), Position (Rational (4)))
                      .withName ("intro layer")) });

        const Constituent root =
            Constituent (ConstituentId (1), Position(), Position (Rational (12)))
                .withName ("demo session");
        return std::make_shared<const Constituent> (
            sirius::arrangement::sequence (root, { std::make_shared<const Constituent> (verse) }));
    }
}

TEST_CASE ("a session round-trips through JSON byte-for-byte", "[sessionformat]")
{
    const auto original = exhaustiveTree();
    const auto firstPass = sirius::persistence::serializeSession (*original);
    const auto reconstructed = sirius::persistence::deserializeSession (firstPass);
    const auto secondPass = sirius::persistence::serializeSession (*reconstructed);
    CHECK (firstPass == secondPass);
}

TEST_CASE ("round-trip preserves the structural claims of the data model",
           "[sessionformat]")
{
    // Spot-check the fields that carry the white paper's load-bearing claims:
    // identity is preserved (id survives), the tree shape survives, and each
    // variant dimension carries its parameters across.
    const auto original = exhaustiveTree();
    const auto json = sirius::persistence::serializeSession (*original);
    const auto round = sirius::persistence::deserializeSession (json);

    CHECK (round->id() == ConstituentId (1));
    REQUIRE (round->children().size() == 1);

    const auto& verse = *round->children()[0];
    CHECK (verse.name() == "verse");
    CHECK (verse.anchor() == AnchorToParent::Start);
    REQUIRE (verse.localMeter().has_value());
    CHECK (verse.localMeter().value() == Meter (7, 8));
    REQUIRE (verse.localTempoMap().has_value());

    const auto& rules = verse.repetitionRules();
    CHECK (std::get<sirius::trigger::EveryNBars> (rules.trigger).bars == 4);
    CHECK (std::get<sirius::cardinality::NTimes> (rules.cardinality).count == 8);
    CHECK (std::get<sirius::phase::QuantizedToGrid> (rules.phase).division == Rational (1, 4));
    CHECK (rules.mutation == Mutation::Decaying);
    CHECK (std::get<sirius::termination::FadeOverBars> (rules.termination).bars == Rational (2));

    REQUIRE (verse.phraseMetadata().has_value());
    CHECK (verse.phraseMetadata()->role == "verse");
    CHECK (verse.phraseMetadata()->isRoleFillable);
    REQUIRE (verse.phraseMetadata()->grammaticalLinks.size() == 1);
    CHECK (verse.phraseMetadata()->grammaticalLinks[0].target == ConstituentId (99));

    REQUIRE (verse.children().size() == 2);
    const auto& loop = *verse.children()[0];
    REQUIRE (loop.tapeReference().has_value());
    CHECK (loop.tapeReference()->tape == TapeId (200));
    CHECK (loop.tapeReference()->tapeIn == Rational (1));
    CHECK (loop.tapeReference()->tapeOut == Rational (5));
}

TEST_CASE ("a minimal default Constituent round-trips", "[sessionformat]")
{
    // Default-only fields exercise the serializer's "absent optional" handling
    // and confirm the defaults survive the trip — the freshly-loaded session
    // must look identical to a freshly-constructed one.
    const Constituent c (ConstituentId (1), Position(), Position (Rational (4)));
    const auto json = sirius::persistence::serializeSession (c);
    const auto round = sirius::persistence::deserializeSession (json);

    CHECK (round->id() == ConstituentId (1));
    CHECK (round->name().empty());
    CHECK (round->anchor() == AnchorToParent::Free);
    CHECK_FALSE (round->localMeter().has_value());
    CHECK_FALSE (round->localTempoMap().has_value());
    CHECK_FALSE (round->phraseMetadata().has_value());
    CHECK_FALSE (round->tapeReference().has_value());
    CHECK (round->children().empty());
}

TEST_CASE ("malformed JSON is rejected with a hard error", "[sessionformat]")
{
    // Rule 3: degradation is announced, not silent. A corrupt session must
    // throw, never silently return a degraded tree.
    CHECK_THROWS_AS (sirius::persistence::deserializeSession ("{not json}"),
                     std::runtime_error);
    CHECK_THROWS_AS (sirius::persistence::deserializeSession ("[1,2,3]"),
                     std::runtime_error);
    CHECK_THROWS_AS (sirius::persistence::deserializeSession ("{}"),
                     std::runtime_error);
}

TEST_CASE ("an unsupported version is rejected", "[sessionformat]")
{
    CHECK_THROWS_AS (
        sirius::persistence::deserializeSession (R"({ "version": 99, "root": {} })"),
        std::runtime_error);
}

TEST_CASE ("an unknown variant tag is rejected", "[sessionformat]")
{
    // Start from a valid document, then poison one variant kind. This catches
    // both forward-compat unknowns and outright corruption.
    const Constituent c (ConstituentId (1), Position(), Position (Rational (1)));
    auto json = sirius::persistence::serializeSession (c);
    json = json.replace ("FreeRunning", "SomethingWeird");
    CHECK_THROWS_AS (sirius::persistence::deserializeSession (json),
                     std::runtime_error);
}
