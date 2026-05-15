// Tests for the per-Constituent effect chain (white paper Part 7.7). The chain
// is structural data — JUCE-free, copy-on-write, immutable from the caller's
// view — so it can ride inside a Constituent and round-trip through the
// session format without dragging hosting machinery into the structure layer.
// These tests pin down the two claims that make the chain trustworthy: edits
// preserve identity (the original is untouched) and ordering is meaningful
// (effects compose in the order they were placed).
#include "sirius/Constituent.h"
#include "sirius/EffectChain.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/SessionFormat.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::PluginFormat;
using sirius::Position;
using sirius::Rational;

namespace
{
    EffectChainEntry makeEntry (const char* uniqueId, const char* name)
    {
        EffectChainEntry e;
        e.descriptor.format       = PluginFormat::Vst3;
        e.descriptor.uniqueId     = uniqueId;
        e.descriptor.name         = name;
        e.descriptor.manufacturer = "AcmeAudio";
        e.descriptor.filePath     = std::string ("/plugins/") + name + ".vst3";
        e.displayName = name;
        e.stateBase64 = "abc=";
        e.bypassed = false;
        return e;
    }
}

TEST_CASE ("a fresh chain is empty", "[effectchain]")
{
    EffectChain chain;
    CHECK (chain.empty());
    CHECK (chain.size() == 0);
    CHECK_THROWS_AS (chain.at (0), std::out_of_range);
}

TEST_CASE ("appending preserves the receiver and orders the chain",
           "[effectchain]")
{
    const EffectChain empty;
    const EffectChain withEq    = empty.withAppended (makeEntry ("EQ-1",  "Saturn EQ"));
    const EffectChain withReverb = withEq.withAppended (makeEntry ("RV-1", "Plate Reverb"));

    CHECK (empty.empty());          // copy-on-write: the original is untouched
    CHECK (withEq.size() == 1);
    REQUIRE (withReverb.size() == 2);
    CHECK (withReverb.entries()[0].descriptor.uniqueId == "EQ-1");
    CHECK (withReverb.entries()[1].descriptor.uniqueId == "RV-1");
}

TEST_CASE ("replace and remove are bounds-checked and copy-on-write",
           "[effectchain]")
{
    EffectChain chain;
    chain = chain.withAppended (makeEntry ("EQ-1", "EQ"))
                 .withAppended (makeEntry ("RV-1", "Reverb"));

    CHECK_THROWS_AS (chain.withReplaced (5, makeEntry ("X", "X")), std::out_of_range);
    CHECK_THROWS_AS (chain.withRemoved (5),                        std::out_of_range);

    const EffectChain swapped = chain.withReplaced (0, makeEntry ("EQ-2", "Surgical EQ"));
    CHECK (swapped.entries()[0].descriptor.uniqueId == "EQ-2");
    CHECK (chain.entries()[0].descriptor.uniqueId == "EQ-1"); // original unchanged

    const EffectChain trimmed = chain.withRemoved (0);
    CHECK (trimmed.size() == 1);
    CHECK (trimmed.entries()[0].descriptor.uniqueId == "RV-1");
}

TEST_CASE ("move swaps two slots, preserving the rest", "[effectchain]")
{
    EffectChain chain;
    chain = chain.withAppended (makeEntry ("A", "A"))
                 .withAppended (makeEntry ("B", "B"))
                 .withAppended (makeEntry ("C", "C"));

    const EffectChain reordered = chain.withMoved (0, 2);
    CHECK (reordered.entries()[0].descriptor.uniqueId == "C");
    CHECK (reordered.entries()[1].descriptor.uniqueId == "B"); // untouched
    CHECK (reordered.entries()[2].descriptor.uniqueId == "A");
    CHECK_THROWS_AS (chain.withMoved (0, 5), std::out_of_range);
}

TEST_CASE ("a Constituent's effect chain edits are copy-on-write",
           "[constituent][effectchain]")
{
    // White paper Part 7.7: "Effects are applied per-Constituent and are
    // replaceable. Changing the reverb on phrase 7 changes only phrase 7."
    const Constituent dry (ConstituentId (1), Position(), Position (Rational (4)));
    CHECK_FALSE (dry.hasEffectChain());

    const EffectChain chain = EffectChain().withAppended (makeEntry ("EQ-1", "EQ"));
    const Constituent processed = dry.withEffectChain (chain);

    CHECK (processed.hasEffectChain());
    CHECK (processed.effectChain()->size() == 1);
    CHECK_FALSE (dry.hasEffectChain()); // the original is untouched

    const Constituent backToDry = processed.withoutEffectChain();
    CHECK_FALSE (backToDry.hasEffectChain());
    CHECK (processed.hasEffectChain()); // and that edit did not touch `processed`
}

TEST_CASE ("an effect chain round-trips through SessionFormat",
           "[effectchain][sessionformat]")
{
    EffectChain chain;
    chain = chain.withAppended (makeEntry ("EQ-1", "Saturn EQ"))
                 .withAppended (makeEntry ("RV-1", "Plate Reverb"));
    chain = chain.withReplaced (1, [&]
    {
        EffectChainEntry bypassed = chain.at (1);
        bypassed.bypassed = true;
        bypassed.stateBase64 = "ZGVlcA==";
        return bypassed;
    }());

    const Constituent c =
        Constituent (ConstituentId (7), Position(), Position (Rational (4)))
            .withName ("processed")
            .withEffectChain (chain);

    const auto json  = sirius::persistence::serializeSession (c);
    const auto round = sirius::persistence::deserializeSession (json);

    REQUIRE (round->hasEffectChain());
    CHECK (*round->effectChain() == chain);
    CHECK (round->effectChain()->entries()[1].bypassed);
    CHECK (round->effectChain()->entries()[1].stateBase64 == "ZGVlcA==");
}

TEST_CASE ("a Constituent without effect chain stays absent through round-trip",
           "[effectchain][sessionformat]")
{
    // The serializer must not emit the property when the chain is absent, and
    // the deserializer must not invent one.
    const Constituent c (ConstituentId (1), Position(), Position (Rational (1)));
    const auto round = sirius::persistence::deserializeSession (
        sirius::persistence::serializeSession (c));
    CHECK_FALSE (round->hasEffectChain());
}
