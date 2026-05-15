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
            .withPhraseMetadata (PhraseMetadata { "verse", "" }));

    Constituent root = sirius::arrangement::sequence (emptyRoot(),
                                                      { sharedPhrase, sharedPhrase });

    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;

    CHECK_THROWS_AS (
        promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (5),
                 IdAllocator (std::ref (counter))),
        std::logic_error);
}
