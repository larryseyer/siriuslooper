#include "sirius/TimelineViewState.h"

#include "sirius/RepetitionRules.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <variant>

namespace sirius
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

    void walk (const Constituent& c,
               const ParentToLmc& parentToLmc,
               TimelineViewState& out)
    {
        const Rational spanStart = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational spanEnd   = parentToLmc (c.conceptualOut().wholeNotes());

        if (c.isPhrase())
        {
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
            const Rational childOffset = c.conceptualIn().wholeNotes();
            const auto     localMap    = c.localTempoMap();
            const ParentToLmc childToLmc =
                [parentToLmc, childOffset, localMap] (Rational t)
                {
                    const Rational inParent =
                        childOffset + (localMap ? localMap->apply (t) : t);
                    return parentToLmc (inParent);
                };
            for (const auto& child : c.children())
                walk (*child, childToLmc, out);
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
    walk (root, rootToLmc, state);

    return state;
}

} // namespace sirius
