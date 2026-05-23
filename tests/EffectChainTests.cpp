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
    // Existing fixtures used to assemble plugin-kind entries by hand. After the
    // union widening this is a one-call factory; keeping it as a helper preserves
    // the spelling of every downstream test case.
    EffectChainEntry makeEntry (const char* uniqueId, const char* name)
    {
        sirius::PluginDescriptor desc;
        desc.format       = sirius::PluginFormat::Vst3;
        desc.uniqueId     = uniqueId;
        desc.version      = "1.0.0";
        desc.name         = name;
        desc.manufacturer = "AcmeAudio";
        desc.filePath     = std::string ("/plugins/") + name + ".vst3";
        return EffectChainEntry::makePlugin (std::move (desc), name, "abc=");
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

TEST_CASE ("EffectChain caps at kMaxSlots (8) appended slots", "[effect-chain][cap]")
{
    EffectChain chain;

    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
    {
        CHECK_FALSE (chain.full());
        chain = chain.withAppended (makeEntry ("fx", "Fx"));
    }

    CHECK (chain.size() == EffectChain::kMaxSlots);
    CHECK (chain.full());
    CHECK_THROWS_AS (chain.withAppended (makeEntry ("over", "Over")), std::length_error);
}

TEST_CASE ("EffectChain at the cap still allows replace / remove / move", "[effect-chain][cap]")
{
    EffectChain chain;
    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
        chain = chain.withAppended (makeEntry ("fx", "Fx"));
    REQUIRE (chain.full());

    // Non-growing edits are unaffected by the cap.
    CHECK_NOTHROW (chain.withReplaced (0, makeEntry ("repl", "Repl")));
    CHECK_NOTHROW (chain.withMoved (0, 7));

    // Removing drops below the cap and re-enables append.
    const EffectChain shortened = chain.withRemoved (0);
    CHECK_FALSE (shortened.full());
    CHECK_NOTHROW (shortened.withAppended (makeEntry ("again", "Again")));
}

TEST_CASE ("EffectChainEntry default-constructs as Empty kind", "[effect-chain][union-slot]")
{
    // A fresh entry must NOT silently look like a Plugin — empty-by-default
    // protects callers who forget to set the discriminant.
    EffectChainEntry e;
    CHECK (e.kind == sirius::EffectChainSlotKind::Empty);
    CHECK (e.descriptor.uniqueId.empty());
}

TEST_CASE ("EffectChainEntry::makeInternal stamps the kind + internalId", "[effect-chain][union-slot]")
{
    const auto eq  = EffectChainEntry::makeInternal (sirius::InternalFxId::kEq);
    const auto cmp = EffectChainEntry::makeInternal (sirius::InternalFxId::kCmp);

    CHECK (eq.kind == sirius::EffectChainSlotKind::Internal);
    CHECK (eq.internalId == sirius::InternalFxId::kEq);

    CHECK (cmp.kind == sirius::EffectChainSlotKind::Internal);
    CHECK (cmp.internalId == sirius::InternalFxId::kCmp);

    // descriptor stays default-empty on an Internal slot (no plugin payload)
    CHECK (eq.descriptor.uniqueId.empty());
}

TEST_CASE ("EffectChainEntry::makePlugin stamps the kind + descriptor", "[effect-chain][union-slot]")
{
    sirius::PluginDescriptor d;
    d.format       = sirius::PluginFormat::Vst3;
    d.uniqueId     = "EQ-1";
    d.name         = "Saturn EQ";
    d.manufacturer = "AcmeAudio";

    const auto e = EffectChainEntry::makePlugin (d, "EQ", "");

    CHECK (e.kind == sirius::EffectChainSlotKind::Plugin);
    CHECK (e.descriptor.uniqueId == "EQ-1");
    CHECK (e.displayName == "EQ");
}

TEST_CASE ("EffectChainEntry equality includes the kind + internalId", "[effect-chain][union-slot]")
{
    const auto a = EffectChainEntry::makeInternal (sirius::InternalFxId::kEq);
    const auto b = EffectChainEntry::makeInternal (sirius::InternalFxId::kEq);
    const auto c = EffectChainEntry::makeInternal (sirius::InternalFxId::kCmp);

    EffectChainEntry empty;          // kind == Empty
    EffectChainEntry pluginLooking;  // kind == Empty but with plugin data filled in
    pluginLooking.descriptor.uniqueId = "EQ-1";

    CHECK (a == b);
    CHECK (a != c);
    CHECK (a != empty);
    CHECK (empty != pluginLooking);  // descriptor mismatch is still detected on Empty entries
}

TEST_CASE ("a chain can mix Internal and Plugin slots", "[effect-chain][union-slot]")
{
    sirius::PluginDescriptor d;
    d.format = sirius::PluginFormat::Vst3;
    d.uniqueId = "RV-1";
    d.name = "Plate Reverb";

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (sirius::InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makePlugin (d, "Reverb", ""))
                 .withAppended (EffectChainEntry::makeInternal (sirius::InternalFxId::kCmp));

    REQUIRE (chain.size() == 3);
    CHECK (chain.entries()[0].kind == sirius::EffectChainSlotKind::Internal);
    CHECK (chain.entries()[0].internalId == sirius::InternalFxId::kEq);
    CHECK (chain.entries()[1].kind == sirius::EffectChainSlotKind::Plugin);
    CHECK (chain.entries()[1].descriptor.uniqueId == "RV-1");
    CHECK (chain.entries()[2].kind == sirius::EffectChainSlotKind::Internal);
    CHECK (chain.entries()[2].internalId == sirius::InternalFxId::kCmp);
}

TEST_CASE ("an Internal effect entry round-trips through SessionFormat",
           "[effect-chain][sessionformat][union-slot]")
{
    using sirius::EffectChainEntry;
    using sirius::InternalFxId;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp));

    const Constituent c =
        Constituent (ConstituentId (11), Position(), Position (Rational (4)))
            .withName ("internal-only")
            .withEffectChain (chain);

    const auto json  = sirius::persistence::serializeSession (c);
    const auto round = sirius::persistence::deserializeSession (json);

    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 2);
    CHECK (round->effectChain()->entries()[0].kind == sirius::EffectChainSlotKind::Internal);
    CHECK (round->effectChain()->entries()[0].internalId == InternalFxId::kEq);
    CHECK (round->effectChain()->entries()[1].kind == sirius::EffectChainSlotKind::Internal);
    CHECK (round->effectChain()->entries()[1].internalId == InternalFxId::kCmp);
    CHECK (*round->effectChain() == chain);
}

TEST_CASE ("a mixed Internal + Plugin chain round-trips through SessionFormat",
           "[effect-chain][sessionformat][union-slot]")
{
    using sirius::EffectChainEntry;
    using sirius::InternalFxId;

    sirius::PluginDescriptor d;
    d.format = sirius::PluginFormat::Vst3;
    d.uniqueId = "RV-1";
    d.version = "1.0.0";
    d.name = "Plate Reverb";
    d.manufacturer = "AcmeAudio";
    d.filePath = "/plugins/Plate Reverb.vst3";

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makePlugin (d, "Reverb", "ZmFrZS1zdGF0ZQ=="))
                 .withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly));

    const Constituent c =
        Constituent (ConstituentId (12), Position(), Position (Rational (4)))
            .withName ("mixed")
            .withEffectChain (chain);

    const auto json  = sirius::persistence::serializeSession (c);
    const auto round = sirius::persistence::deserializeSession (json);

    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 3);
    CHECK (*round->effectChain() == chain);
}

TEST_CASE ("a pre-union session (no `kind` field) loads every entry as Plugin",
           "[effect-chain][sessionformat][union-slot][forward-compat]")
{
    // Forward-compat: pre-union JSON had no `kind` field. Every entry it could
    // encode was a hosted plugin (Internal didn't exist yet). The deserializer
    // must default missing `kind` to Plugin so old sessions load.
    //
    // Rather than hand-write a fixture (brittle against envelope changes), we
    // round-trip a real Plugin-kind chain, strip the `kind` field from each
    // entry in the parsed JSON tree, re-serialize, and feed THAT to the
    // deserializer. This proves the missing-kind-default path independently
    // of the current envelope shape.
    EffectChain chain;
    chain = chain.withAppended (makeEntry ("legacy-eq", "Legacy EQ"));

    const Constituent c =
        Constituent (ConstituentId (100), Position(), Position (Rational (4)))
            .withName ("legacy")
            .withEffectChain (chain);

    const auto jsonWithKind = sirius::persistence::serializeSession (c);

    // Parse, walk effects.entries, drop the "kind" property from each entry,
    // re-emit. We do not assume envelope structure beyond `effects.entries`
    // (the only path the union touches).
    juce::var parsed;
    REQUIRE (juce::JSON::parse (jsonWithKind, parsed).wasOk());

    // Walk the parsed var tree and strip "kind" from any object that has an
    // "entries" array (that is the effect-chain envelope). Accepts const ref
    // because mutations are made via getDynamicObject() which returns a mutable
    // pointer into the refcounted object — even copies share the same underlying
    // DynamicObject.
    auto stripKindInEffects = [] (auto& self, const juce::var& node) -> void {
        if (auto* obj = node.getDynamicObject())
        {
            if (obj->hasProperty ("entries"))
            {
                const auto entries = obj->getProperty ("entries");
                if (auto* arr = entries.getArray())
                {
                    for (const auto& entryVar : *arr)
                    {
                        if (auto* entry = entryVar.getDynamicObject())
                            entry->removeProperty ("kind");
                    }
                }
            }
            for (const auto& prop : obj->getProperties())
                self (self, prop.value);
        }
        else if (auto* arr = node.getArray())
        {
            for (const auto& elem : *arr)
                self (self, elem);
        }
    };
    stripKindInEffects (stripKindInEffects, parsed);

    const auto preUnionJson = juce::JSON::toString (parsed);

    const auto round = sirius::persistence::deserializeSession (preUnionJson);
    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 1);
    const auto& entry = round->effectChain()->entries()[0];
    CHECK (entry.kind == sirius::EffectChainSlotKind::Plugin);
    CHECK (entry.descriptor.uniqueId == "legacy-eq");
}
