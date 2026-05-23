// Golden-value tests for ida::Constituent — the unifying abstraction for
// every musical object (white paper Part VII). These tests encode the two
// claims that make the data model tractable: identity persists across every
// content revision (Part 7.6), and edits are copy-on-write with untouched
// children *shared*, not copied (Part 7.3).
#include "sirius/Constituent.h"

#include "sirius/Phrase.h"
#include "sirius/TapeReference.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <variant>

using ida::AnchorToParent;
using ida::Constituent;
using ida::ConstituentId;
using ida::Meter;
using ida::Position;
using ida::Rational;

namespace
{
    std::shared_ptr<const Constituent> makeLeaf (std::int64_t id, Rational length)
    {
        return std::make_shared<const Constituent> (
            ConstituentId (id), Position(), Position (length));
    }
}

TEST_CASE ("a freshly constructed Constituent is a leaf with the given span", "[constituent]")
{
    const Constituent c (ConstituentId (1), Position(), Position (Rational (4)));
    CHECK (c.id() == ConstituentId (1));
    CHECK (c.isLeaf());
    CHECK (c.children().empty());
    CHECK (c.duration() == Rational (4));
    CHECK (c.anchor() == AnchorToParent::Free);
    CHECK (c.name().empty());
    CHECK_FALSE (c.localMeter().has_value());
    CHECK_FALSE (c.localTempoMap().has_value());
}

TEST_CASE ("a Constituent cannot end before it starts", "[constituent]")
{
    CHECK_THROWS_AS (Constituent (ConstituentId (1),
                                  Position (Rational (4)),
                                  Position (Rational (1))),
                     std::invalid_argument);

    const Constituent c (ConstituentId (1), Position(), Position (Rational (4)));
    CHECK_THROWS_AS (c.withBoundaries (Position (Rational (3)), Position (Rational (2))),
                     std::invalid_argument);
}

TEST_CASE ("identity persists across every content revision", "[constituent][conceptual-time]")
{
    // White paper Part 7.6: a phrase named "the verse" remains the same phrase,
    // with the same id, through every revision of its content.
    const Constituent original (ConstituentId (42), Position(), Position (Rational (2)));

    const Constituent revised = original
        .withName ("the verse")
        .withBoundaries (Position (Rational (1)), Position (Rational (3)))
        .withAnchor (AnchorToParent::Start)
        .withLocalMeter (Meter (7, 8));

    CHECK (revised.id() == ConstituentId (42)); // identity survived every edit
    CHECK (revised.name() == "the verse");
    CHECK (revised.anchor() == AnchorToParent::Start);
    CHECK (revised.localMeter() == Meter (7, 8));
    CHECK (revised.duration() == Rational (2));

    // The original is untouched — edits are copy-on-write.
    CHECK (original.id() == ConstituentId (42));
    CHECK (original.name().empty());
    CHECK (original.anchor() == AnchorToParent::Free);
    CHECK_FALSE (original.localMeter().has_value());
}

TEST_CASE ("local meter and tempo map can be set and cleared", "[constituent]")
{
    const Constituent c (ConstituentId (1), Position(), Position (Rational (1)));

    const Constituent withMeter = c.withLocalMeter (Meter (3, 4));
    CHECK (withMeter.localMeter() == Meter (3, 4));
    CHECK (withMeter.withoutLocalMeter().localMeter().has_value() == false);

    const Constituent withTempo = c.withLocalTempoMap (ida::TempoMap::fromBpm (Rational (90)));
    CHECK (withTempo.localTempoMap().has_value());
    CHECK (withTempo.withoutLocalTempoMap().localTempoMap().has_value() == false);
}

TEST_CASE ("copy-on-write edits share untouched children", "[constituent][conceptual-time]")
{
    // White paper Part 7.3: editing a Constituent produces a new one "sharing
    // the same source references where applicable" — an edit copies a path,
    // never a subtree.
    const auto childA = makeLeaf (10, Rational (1));
    const auto childB = makeLeaf (11, Rational (1));

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (4)));
    const Constituent withKids = parent.withChildAdded (childA).withChildAdded (childB);
    REQUIRE (withKids.children().size() == 2);

    SECTION ("editing the parent shares both children")
    {
        const Constituent renamed = withKids.withName ("verse");
        CHECK (renamed.children()[0].get() == childA.get()); // same object, shared
        CHECK (renamed.children()[1].get() == childB.get());
    }

    SECTION ("replacing one child shares the untouched sibling")
    {
        const auto childC = makeLeaf (12, Rational (1));
        const Constituent swapped = withKids.withChildReplaced (0, childC);
        CHECK (swapped.children()[0].get() == childC.get());
        CHECK (swapped.children()[1].get() == childB.get()); // sibling shared
        // The original is untouched.
        CHECK (withKids.children()[0].get() == childA.get());
        CHECK (withKids.children().size() == 2);
    }

    SECTION ("removing a child leaves the original intact")
    {
        const Constituent trimmed = withKids.withChildRemoved (0);
        CHECK (trimmed.children().size() == 1);
        CHECK (trimmed.children()[0].get() == childB.get());
        CHECK (withKids.children().size() == 2);
    }
}

TEST_CASE ("child edits reject bad indices and null children", "[constituent]")
{
    const Constituent c (ConstituentId (1), Position(), Position (Rational (1)));
    CHECK_THROWS_AS (c.withChildAdded (nullptr), std::invalid_argument);
    CHECK_THROWS_AS (c.withChildReplaced (0, makeLeaf (2, Rational (1))), std::out_of_range);
    CHECK_THROWS_AS (c.withChildRemoved (0), std::out_of_range);

    const Constituent withChild = c.withChildAdded (makeLeaf (2, Rational (1)));
    CHECK_THROWS_AS (withChild.withChildReplaced (0, nullptr), std::invalid_argument);
}

TEST_CASE ("a fresh Constituent carries the default loop repetition rules", "[constituent][repetition]")
{
    const Constituent c (ConstituentId (1), Position(), Position (Rational (4)));
    // White paper Part 10.2: the default rules are the system's best guess.
    CHECK (std::holds_alternative<ida::trigger::FreeRunning> (c.repetitionRules().trigger));
    CHECK (std::holds_alternative<ida::cardinality::Forever> (c.repetitionRules().cardinality));
}

TEST_CASE ("repetition rules are editable copy-on-write, identity preserved", "[constituent][repetition]")
{
    const Constituent original (ConstituentId (7), Position(), Position (Rational (4)));

    ida::RepetitionRules rules;
    rules.cardinality = ida::cardinality::NTimes (8);
    rules.termination = ida::termination::HandOff { ConstituentId (8) };
    const Constituent edited = original.withRepetitionRules (rules);

    CHECK (edited.id() == ConstituentId (7)); // identity survives the edit
    CHECK (std::get<ida::cardinality::NTimes> (edited.repetitionRules().cardinality).count == 8);
    // The original is untouched.
    CHECK (std::holds_alternative<ida::cardinality::Forever> (
               original.repetitionRules().cardinality));
}

TEST_CASE ("a Constituent becomes a phrase when it carries phrase metadata",
           "[constituent][phrase]")
{
    const Constituent loop (ConstituentId (1), Position(), Position (Rational (4)));
    CHECK_FALSE (loop.isPhrase());
    CHECK_FALSE (loop.phraseMetadata().has_value());

    ida::PhraseMetadata meta;
    meta.role = "verse";
    meta.isRoleFillable = true;
    const Constituent phrase = loop.withPhraseMetadata (meta);

    CHECK (phrase.isPhrase());
    REQUIRE (phrase.phraseMetadata().has_value());
    CHECK (phrase.phraseMetadata()->role == "verse");
    CHECK (phrase.id() == ConstituentId (1)); // still the same Constituent
    CHECK_FALSE (loop.isPhrase());            // the original is untouched

    // The phrase role can be dropped again, copy-on-write.
    const Constituent backToLoop = phrase.withoutPhraseMetadata();
    CHECK_FALSE (backToLoop.isPhrase());
    CHECK (phrase.isPhrase()); // and that edit did not touch `phrase`
}

TEST_CASE ("isPlacementWrapper recognises wrappers and rejects non-wrappers",
           "[constituent][placementWrapper]")
{
    using ida::Constituent;
    using ida::ConstituentId;
    using ida::PhraseMetadata;
    using ida::Position;
    using ida::Rational;
    using ida::TapeId;
    using ida::TapeReference;

    const Constituent emptyShell (ConstituentId (1), Position(),
                                  Position (Rational (4)));

    SECTION ("bare Phrase is NOT a wrapper")
    {
        const auto bare = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "verse" });
        CHECK_FALSE (ida::isPlacementWrapper (bare));
    }

    SECTION ("Loop is NOT a wrapper")
    {
        const auto leaf = emptyShell.withTapeReference (
            TapeReference (TapeId (1), Rational (0), Rational (4)));
        CHECK_FALSE (ida::isPlacementWrapper (leaf));
    }

    SECTION ("Phrase whose role is not 'placement' is NOT a wrapper, even with a Phrase child")
    {
        auto child = std::make_shared<const Constituent> (
            emptyShell.withPhraseMetadata (PhraseMetadata { .role = "verse" }));
        const auto outer = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "verse" }).withChildAdded (child);
        CHECK_FALSE (ida::isPlacementWrapper (outer));
    }

    SECTION ("role=='placement' Phrase whose first child is a Phrase IS a wrapper")
    {
        auto sharedPhrase = std::make_shared<const Constituent> (
            emptyShell.withPhraseMetadata (PhraseMetadata { .role = "verse" }));
        const auto wrapper = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "placement" }).withChildAdded (sharedPhrase);
        CHECK (ida::isPlacementWrapper (wrapper));
    }

    SECTION ("role=='placement' Phrase whose first child is a Loop is NOT a wrapper")
    {
        auto leaf = std::make_shared<const Constituent> (
            emptyShell.withTapeReference (
                TapeReference (TapeId (1), Rational (0), Rational (4))));
        const auto bogus = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "placement" }).withChildAdded (leaf);
        CHECK_FALSE (ida::isPlacementWrapper (bogus));
    }
}
