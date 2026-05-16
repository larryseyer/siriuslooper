// Tests for sirius::promotion::promote — the auto-promotion of CaptureRegions
// into the session Constituent tree. Pure-function tests; no JUCE.
//
// Scope is single-instance promotion (see
// docs/superpowers/specs/2026-05-15-capture-promotion-design.md). The
// multi-instance write-protect is verified here so the guard cannot be
// removed silently.
#include "sirius/Promotion.h"

#include "sirius/Arrangement.h"
#include "sirius/CaptureSession.h"
#include "sirius/Constituent.h"
#include "sirius/Phrase.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"
#include "sirius/TapeReference.h"
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

using sirius::CaptureRegion;
using sirius::Constituent;
using sirius::ConstituentId;
using sirius::PhraseMetadata;
using sirius::Position;
using sirius::Rational;
using sirius::TapeId;
using sirius::TapeReference;
using sirius::TempoMap;
using sirius::promotion::IdAllocator;
using sirius::promotion::promote;

namespace
{
    /// 1:1 conceptual ↔ LMC mapping — the M3 simplification, matches the demo.
    TempoMap identityMap()
    {
        return TempoMap::constant (Rational (1));
    }

    /// A monotonic counter producing fresh ConstituentIds.
    struct Counter
    {
        std::int64_t next { 1000 };
        ConstituentId operator() () { return ConstituentId (next++); }
    };

    Constituent emptyRoot()
    {
        return Constituent (ConstituentId (1), Position(), Position (Rational (60)));
    }
}

TEST_CASE ("promote throws when any Constituent id appears more than once",
           "[promotion][guard]")
{
    // Build a root that contains two distinct Constituents sharing id 42.
    // arrangement::sequence does this naturally when the same Phrase is placed
    // multiple times — each placement is a new Constituent object that copies
    // the shared id. The guard must catch this.
    auto sharedPhrase = std::make_shared<const Constituent> (
        Constituent (ConstituentId (42), Position(), Position (Rational (4)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    Constituent root = sirius::arrangement::sequence (emptyRoot(),
                                                      { sharedPhrase, sharedPhrase });

    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;

    CHECK_THROWS_AS (
        promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (5),
                 IdAllocator (std::ref (counter))),
        std::logic_error);
}

TEST_CASE ("promote into an existing Phrase adds a Loop child, no Phrase mint",
           "[promotion][host]")
{
    // Build a root with a single Phrase "verse" spanning [2, 6) seconds.
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (2)), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In at LMC = 3 (inside verse), Mark Out at LMC = 5 (still inside).
    const CaptureRegion region { TapeId (200), Rational (3), Rational (5) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (3),
                           IdAllocator (std::ref (counter)));

    CHECK_FALSE (result.mintedPhraseId.has_value());
    CHECK (result.addedLoopId.value() == 1000);  // first Counter id
    CHECK (result.undoLabel == "capture loop into verse");
    CHECK (result.hostPhraseName.value() == "verse");

    // The new root should have one top-level child (the verse, copy-on-write
    // replaced) which now itself has one child (the Loop).
    REQUIRE (result.newRoot.children().size() == 1);
    const auto& placedVerse = *result.newRoot.children()[0];
    CHECK (placedVerse.id().value() == 10);
    REQUIRE (placedVerse.children().size() == 1);

    const auto& addedLoop = *placedVerse.children()[0];
    CHECK (addedLoop.id().value() == 1000);
    REQUIRE (addedLoop.tapeReference().has_value());
    CHECK (addedLoop.tapeReference()->tape.value()  == 200);
    CHECK (addedLoop.tapeReference()->tapeIn        == Rational (3));
    CHECK (addedLoop.tapeReference()->tapeOut       == Rational (5));
}

TEST_CASE ("promote on an empty root mints a Phrase containing one Loop",
           "[promotion][mint]")
{
    Constituent root = emptyRoot();
    const CaptureRegion region { TapeId (300), Rational (4), Rational (8) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (4),
                           IdAllocator (std::ref (counter)));

    REQUIRE (result.mintedPhraseId.has_value());
    REQUIRE_FALSE (result.hostPhraseName.has_value());
    CHECK (result.mintedPhraseId->value() == 1000);  // first id minted (Phrase)
    CHECK (result.addedLoopId.value()     == 1001);  // second id minted (Loop)
    CHECK (result.undoLabel == "capture phrase");

    REQUIRE (result.newRoot.children().size() == 1);
    const auto& mintedPhrase = *result.newRoot.children()[0];
    CHECK (mintedPhrase.id().value() == 1000);
    REQUIRE (mintedPhrase.isPhrase());
    CHECK (mintedPhrase.phraseMetadata()->role == "capture");
    CHECK (mintedPhrase.conceptualIn()  == Position (Rational (4)));
    CHECK (mintedPhrase.conceptualOut() == Position (Rational (8)));

    REQUIRE (mintedPhrase.children().size() == 1);
    const auto& loop = *mintedPhrase.children()[0];
    CHECK (loop.id().value() == 1001);
    REQUIRE (loop.tapeReference().has_value());
    CHECK (loop.tapeReference()->tape.value() == 300);
    CHECK (loop.tapeReference()->tapeIn  == Rational (4));
    CHECK (loop.tapeReference()->tapeOut == Rational (8));
    CHECK (loop.conceptualIn()  == Position());                      // local-to-Phrase
    CHECK (loop.conceptualOut() == Position (Rational (4)));         // duration of region
}

TEST_CASE ("promote with playhead in a gap between Phrases mints a fresh Phrase",
           "[promotion][mint]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (0)), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In at LMC = 10, far past the verse. No host.
    const CaptureRegion region { TapeId (300), Rational (10), Rational (12) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (10),
                           IdAllocator (std::ref (counter)));

    REQUIRE (result.mintedPhraseId.has_value());
    CHECK (result.undoLabel == "capture phrase");

    // Root now has two children: the original verse and the new Phrase.
    REQUIRE (result.newRoot.children().size() == 2);
    CHECK (result.newRoot.children()[0]->id().value() == 10);
    CHECK (result.newRoot.children()[1]->id().value() == result.mintedPhraseId->value());
}

TEST_CASE ("promote clamps Loop bounds to the host Phrase when region extends past",
           "[promotion][straddle]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (2)), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In = 4 (inside verse [2,6)), Mark Out = 9 (well past verse).
    // Mark In wins: host is verse; Loop must be clamped to [4,6) in LMC,
    // which is [2,4) in verse-local conceptual time.
    const CaptureRegion region { TapeId (200), Rational (4), Rational (9) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (4),
                           IdAllocator (std::ref (counter)));

    REQUIRE_FALSE (result.mintedPhraseId.has_value());
    REQUIRE (result.newRoot.children().size() == 1);
    const auto& placedVerse = *result.newRoot.children()[0];
    REQUIRE (placedVerse.children().size() == 1);
    const auto& loop = *placedVerse.children()[0];

    // Conceptual bounds are clamped to the verse's local time domain.
    CHECK (loop.conceptualIn()  == Position (Rational (2)));   // (4 - 2)
    CHECK (loop.conceptualOut() == Position (Rational (4)));   // (6 - 2), clipped from 9

    // TapeReference keeps the *unclamped* original LMC times — the audio
    // beyond the host boundary still exists on the tape and remains
    // referenceable; only the Constituent's structural placement is clipped.
    CHECK (loop.tapeReference()->tapeIn  == Rational (4));
    CHECK (loop.tapeReference()->tapeOut == Rational (9));
}

TEST_CASE ("promote refuses a hybrid Phrase+Loop Constituent as host and mints instead",
           "[promotion][host][hybrid-rejection]")
{
    // A hybrid carries both PhraseMetadata and TapeReference on a single
    // Constituent. The convention treats Loops as leaves and Phrases as
    // containers, so a hybrid is structurally invalid as a host: attaching a
    // captured Loop as its child would make a Loop a child of a Loop. The
    // host search must reject it; promote() must fall back to minting a
    // fresh Phrase wrapper at the root.
    auto hybrid = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (2)), Position (Rational (6)))
            .withName ("hybrid")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" })
            .withTapeReference (TapeReference (TapeId (999),
                                               Rational (0), Rational (8))));

    Constituent root = emptyRoot().withChildAdded (hybrid);

    // Mark In = 3 LMC (inside the hybrid's span). If the host search were
    // permissive, the hybrid would win and the result would have no minted
    // Phrase. The tightened predicate must drive promote() into the mint path.
    const CaptureRegion region { TapeId (200), Rational (3), Rational (5) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (3),
                           IdAllocator (std::ref (counter)));

    REQUIRE (result.mintedPhraseId.has_value());
    REQUIRE_FALSE (result.hostPhraseName.has_value());
    CHECK (result.undoLabel == "capture phrase");

    // Root now has two children: the original hybrid (untouched) and the
    // freshly minted Phrase carrying the captured Loop.
    REQUIRE (result.newRoot.children().size() == 2);
    CHECK (result.newRoot.children()[0]->id().value() == 10);
    CHECK (result.newRoot.children()[0]->children().empty());  // hybrid unchanged
    CHECK (result.newRoot.children()[1]->id().value() == result.mintedPhraseId->value());
}

TEST_CASE ("promote throws on a zero-duration or reversed region",
           "[promotion][defensive]")
{
    Constituent root = emptyRoot();
    Counter counter;

    SECTION ("zero duration")
    {
        const CaptureRegion bad { TapeId (200), Rational (3), Rational (3) };
        CHECK_THROWS_AS (
            promote (root, identityMap(), bad, Rational (3),
                     IdAllocator (std::ref (counter))),
            std::invalid_argument);
    }

    SECTION ("reversed bounds")
    {
        const CaptureRegion bad { TapeId (200), Rational (5), Rational (3) };
        CHECK_THROWS_AS (
            promote (root, identityMap(), bad, Rational (5),
                     IdAllocator (std::ref (counter))),
            std::invalid_argument);
    }
}
