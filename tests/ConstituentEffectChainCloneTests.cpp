// V9 Slice 6 — Constituent::withEffectChainClonedFrom is the API surface
// for whitepaper V9 §6.3.2's "dry-tap FX migration" rule: when an Input
// channel is in NonDestructive (dry-tap) tape mode and a phrase is
// captured, the capture-path code (M6+) must seed the resulting phrase's
// local EffectChain with a deep copy of the Input strip's chain at
// capture time. The phrase then evolves independently (copy-on-write
// per §6.8).
//
// This slice lands the builder + a test pinning the two value-semantic
// contracts: cloning a non-empty chain produces an EffectChain-bearing
// Constituent equal to the source, and cloning an empty chain yields no
// chain at all (consistent with `withoutEffectChain`).

#include "ida/ChannelStrip.h"
#include "ida/Constituent.h"
#include "ida/EffectChain.h"
#include "ida/InternalFxId.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

using ida::ChannelStrip;
using ida::Constituent;
using ida::ConstituentId;
using ida::EffectChain;
using ida::EffectChainEntry;
using ida::InternalFxId;
using ida::Position;
using ida::Rational;
using ida::SignalType;

TEST_CASE ("Constituent::withEffectChainClonedFrom", "[constituent][effect-chain][v9-dry-tap]")
{
    // A fresh phrase Constituent — no factory `makeEmptyPhrase` exists, so
    // construct directly (matches the pattern in ConstituentTests.cpp). No
    // PhraseMetadata is required to exercise the effect-chain builder; the
    // V9 §6.3.2 rule only cares about the EffectChain side.
    const Constituent basePhrase (ConstituentId (1), Position(), Position (Rational (4)));
    REQUIRE_FALSE (basePhrase.hasEffectChain());

    SECTION ("cloning from a non-empty EffectChain copies into the Constituent")
    {
        // Build a strip whose effect chain has a content shape distinct from
        // the auto-seeded default. ChannelStrip<Audio>'s ctor already seeds
        // EQ + CMP, so appending a third internal slot (RVB) gives us a
        // chain whose size is observably different from a default-constructed
        // strip — the test must distinguish "we copied the real chain" from
        // "we copied any chain".
        ChannelStrip<SignalType::Audio> strip;
        const auto seededDefault = strip.effectChain();
        REQUIRE (seededDefault.size() == 2);

        const EffectChain configured = seededDefault.withAppended (
            EffectChainEntry::makeInternal (InternalFxId::kRvb));
        strip.setEffectChain (configured);
        REQUIRE (strip.effectChain().size() == 3);

        const Constituent cloned = basePhrase.withEffectChainClonedFrom (strip.effectChain());

        REQUIRE (cloned.hasEffectChain());
        // EffectChain has operator==; deep equality after the clone is the
        // simplest deep-copy assertion.
        CHECK (*cloned.effectChain() == strip.effectChain());

        // Deep-copy semantics: mutating the strip's chain after the clone
        // must not affect the cloned Constituent. EffectChain is value-typed
        // (copy stored in `Constituent::effectChain_`), so re-assigning the
        // strip to a different chain leaves the clone untouched.
        strip.setEffectChain (EffectChain{});
        REQUIRE (strip.effectChain().empty());
        CHECK (cloned.hasEffectChain());
        CHECK (cloned.effectChain()->size() == 3);
    }

    SECTION ("cloning from an empty EffectChain yields no chain on the Constituent")
    {
        // Directly use an empty EffectChain (a default-constructed strip
        // has the auto-seeded EQ + CMP, so we can't reach an empty chain
        // through the strip without clearing it first).
        const EffectChain emptyChain;
        REQUIRE (emptyChain.empty());

        const Constituent cloned = basePhrase.withEffectChainClonedFrom (emptyChain);
        CHECK_FALSE (cloned.hasEffectChain());
    }

    SECTION ("cloning over an existing chain replaces it (mirrors withEffectChain semantics)")
    {
        const Constituent withInitial = basePhrase.withEffectChain (
            EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq)));
        REQUIRE (withInitial.hasEffectChain());
        REQUIRE (withInitial.effectChain()->size() == 1);

        // Source has two slots: the clone replaces the one-slot chain.
        const EffectChain source = EffectChain{}
            .withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp))
            .withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly));

        const Constituent replaced = withInitial.withEffectChainClonedFrom (source);
        REQUIRE (replaced.hasEffectChain());
        CHECK (*replaced.effectChain() == source);

        // And cloning an empty source over an existing chain clears it.
        const Constituent cleared = withInitial.withEffectChainClonedFrom (EffectChain{});
        CHECK_FALSE (cleared.hasEffectChain());
    }
}
