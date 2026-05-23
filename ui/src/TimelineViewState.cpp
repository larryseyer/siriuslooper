#include "ida/TimelineViewState.h"

#include "ida/RepetitionRules.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <variant>

namespace ida
{

namespace
{
    using ParentToLmc = std::function<Rational (Rational)>;

    /// Result of walking the descendants of a Phrase to compute its Pill's
    /// loop count and tape membership. Only Loop descendants — Constituents
    /// carrying a TapeReference — contribute; nested Phrases and Groups are
    /// containers, not content, on the tape axis.
    struct TapeAggregation
    {
        int loopCount { 0 };
        /// Tape id → number of contained loops referencing it. Used to pick
        /// the primary (mode-by-count) and to enumerate member tapes.
        std::unordered_map<std::int64_t, int> countsByTape;
        /// Insertion order is stable so multi-tape membership outlines draw
        /// in a predictable order rather than hash-iteration order.
        std::vector<TapeId> tapeOrder;
    };

    void aggregate (const Constituent& c, TapeAggregation& agg)
    {
        if (const auto& ref = c.tapeReference())
        {
            ++agg.loopCount;
            const auto rawId = ref->tape.value();
            auto [it, inserted] = agg.countsByTape.insert ({ rawId, 0 });
            ++it->second;
            if (inserted)
                agg.tapeOrder.push_back (ref->tape);
        }
        for (const auto& child : c.children())
            aggregate (*child, agg);
    }

    TapeId pickPrimary (const TapeAggregation& agg)
    {
        TapeId best { 0 };
        int    bestCount { -1 };
        // Prefer earliest-inserted on ties — gives the "first tape captured
        // into this phrase" a stable, predictable anchor, which is what the
        // performer's mental model expects.
        for (const auto& t : agg.tapeOrder)
        {
            const int count = agg.countsByTape.at (t.value());
            if (count > bestCount)
            {
                best      = t;
                bestCount = count;
            }
        }
        return best;
    }

    const char* describeEntrance (EntranceCharacter e)
    {
        switch (e)
        {
            case EntranceCharacter::Pickup:   return "pickup";
            case EntranceCharacter::Downbeat: return "downbeat";
            case EntranceCharacter::Unspecified: break;
        }
        return "";
    }

    const char* describeExit (ExitCharacter e)
    {
        switch (e)
        {
            case ExitCharacter::Resolution: return "resolution";
            case ExitCharacter::HandOff:    return "hand-off";
            case ExitCharacter::Unspecified: break;
        }
        return "";
    }

    /// Build the parent-to-LMC mapping for a Constituent's children. Hoisted
    /// out of `walk` to file scope so both the wrapper-branch and bare-Phrase
    /// branch can call it identically when descending into children.
    ParentToLmc childMapping (const Constituent& parent, const ParentToLmc& parentToLmc)
    {
        const Rational childOffset = parent.conceptualIn().wholeNotes();
        const auto     localMap    = parent.localTempoMap();
        return [parentToLmc, childOffset, localMap] (Rational t)
        {
            const Rational inParent =
                childOffset + (localMap ? localMap->apply (t) : t);
            return parentToLmc (inParent);
        };
    }

    void walk (const Constituent& c,
               const ParentToLmc& parentToLmc,
               TimelineViewState& out,
               std::unordered_map<std::int64_t, const Constituent*>& wrapperSharedKey)
    {
        const Rational spanStart = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational spanEnd   = parentToLmc (c.conceptualOut().wholeNotes());

        // Forked wrappers carry the same shape as placement wrappers — a
        // role-tagged Phrase whose first child is the (now-private) shared
        // Phrase — but with a different role. Handle both as "wrapper" for
        // Pill-emission purposes.
        const bool isPlacement     = isPlacementWrapper (c);
        const bool isForkedWrapper = c.isPhrase()
                                  && c.phraseMetadata()->role == "forked-placement"
                                  && ! c.children().empty()
                                  && c.children()[0]->isPhrase();
        const bool isWrapperShape  = isPlacement || isForkedWrapper;

        if (isWrapperShape)
        {
            const auto& sharedChild = *c.children()[0];

            TapeAggregation agg;
            aggregate (sharedChild, agg);

            PillState pill;
            pill.id              = c.id();                       // wrapper's id
            pill.name            = sharedChild.name();           // from shared Phrase
            pill.startLmcSeconds = spanStart;
            pill.endLmcSeconds   = spanEnd;
            pill.loopCount       = agg.loopCount;
            pill.primaryTape     = pickPrimary (agg);
            pill.memberTapes     = agg.tapeOrder;
            const auto& card = sharedChild.repetitionRules().cardinality;
            pill.phraseLoopActive = ! std::holds_alternative<cardinality::Once> (card);

            const auto& meta = *sharedChild.phraseMetadata();
            pill.entranceName = describeEntrance (meta.entrance);
            pill.exitName     = describeExit     (meta.exit);

            pill.hasOverlays = c.children().size() >= 2;
            pill.isForked    = isForkedWrapper;

            // Stash the shared-child pointer keyed by wrapper id. Used by the
            // post-pass to group placement wrappers (NOT forked wrappers) by
            // pointer-identity for tie-bar grouping.
            if (isPlacement)
                wrapperSharedKey.insert ({ c.id().value(), c.children()[0].get() });

            out.pills.push_back (std::move (pill));

            // Walk children: skip the shared Phrase (already represented by
            // this Pill), but descend into overlay Loops. Overlay Loops are
            // leaves so the descent is shallow, but doing it consistently
            // keeps the recursion uniform.
            const auto childMap = childMapping (c, parentToLmc);
            for (std::size_t i = 1; i < c.children().size(); ++i)
                walk (*c.children()[i], childMap, out, wrapperSharedKey);
            return;
        }

        if (c.isPhrase())
        {
            // Bare Phrase (non-wrapper, non-forked) — existing behaviour.
            TapeAggregation agg;
            aggregate (c, agg);

            PillState pill;
            pill.id              = c.id();
            pill.name            = c.name();
            pill.startLmcSeconds = spanStart;
            pill.endLmcSeconds   = spanEnd;
            pill.loopCount       = agg.loopCount;
            pill.primaryTape     = pickPrimary (agg);
            pill.memberTapes     = agg.tapeOrder;
            // The ↻ toggle: a Phrase whose cardinality is Once plays its
            // span and stops; anything else (Forever, NTimes, Until*) loops.
            const auto& card = c.repetitionRules().cardinality;
            pill.phraseLoopActive = ! std::holds_alternative<cardinality::Once> (card);

            const auto& meta = *c.phraseMetadata();
            pill.entranceName = describeEntrance (meta.entrance);
            pill.exitName     = describeExit     (meta.exit);

            out.pills.push_back (std::move (pill));
        }

        if (! c.children().empty())
        {
            const auto childMap = childMapping (c, parentToLmc);
            for (const auto& child : c.children())
                walk (*child, childMap, out, wrapperSharedKey);
        }
    }
}

TimelineViewState selectTimelineView (const Constituent&                  root,
                                      const TempoMap&                     sessionToLmc,
                                      const std::vector<InputDescriptor>& inputs,
                                      const std::vector<TapeId>&          armedTapes,
                                      TapeId                              focusedTape)
{
    TimelineViewState state;
    state.startLmcSeconds = sessionToLmc.apply (root.conceptualIn().wholeNotes());
    state.endLmcSeconds   = sessionToLmc.apply (root.conceptualOut().wholeNotes());

    state.rows.reserve (inputs.size());
    for (const auto& desc : inputs)
    {
        TrackStripState row;
        row.tapeId      = desc.tapeId;
        row.kind        = desc.inputKind;
        row.displayName = desc.displayName;
        row.isArmed     = std::find (armedTapes.begin(), armedTapes.end(),
                                     desc.tapeId) != armedTapes.end();
        row.isFocused   = (desc.tapeId == focusedTape);
        state.rows.push_back (std::move (row));
    }

    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    std::unordered_map<std::int64_t, const Constituent*> wrapperSharedKey;
    walk (root, rootToLmc, state, wrapperSharedKey);

    // Second pass: group placement wrappers by pointer-identity of their
    // shared Phrase, then fill each Pill's sharedSiblings with the other
    // members of its group. Forked wrappers do not participate (their
    // wrapperSharedKey entry was never inserted, so they do not appear in
    // any group).
    std::unordered_map<const Constituent*, std::vector<ConstituentId>> groups;
    for (const auto& [wrapperId, sharedPtr] : wrapperSharedKey)
        groups[sharedPtr].push_back (ConstituentId (wrapperId));

    for (auto& pill : state.pills)
    {
        const auto it = wrapperSharedKey.find (pill.id.value());
        if (it == wrapperSharedKey.end()) continue;
        const auto& group = groups[it->second];
        if (group.size() < 2) continue;  // not actually shared
        for (const auto& sibling : group)
            if (sibling.value() != pill.id.value())
                pill.sharedSiblings.push_back (sibling);
    }

    return state;
}

} // namespace ida
