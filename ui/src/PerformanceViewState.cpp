#include "sirius/PerformanceViewState.h"

#include "sirius/RepetitionRules.h"

#include <cstdint>
#include <functional>
#include <variant>

namespace sirius
{

namespace
{
    struct WalkResult
    {
        /// The deepest named container the playhead is inside — a phrase, a
        /// section, a song. This is what the performer's mental model expects
        /// to see (white paper 14.5: "what is playing right now"), in contrast
        /// to the bookkeeping name a leaf loop might carry.
        const Constituent* deepestNamedContainer { nullptr };
        /// Fallback for sessions whose only named element is the leaf itself.
        const Constituent* deepestNamed         { nullptr };
        const Constituent* firstSoundingLoop    { nullptr };
        std::int64_t       firstLoopCycle       { 0 };
        int                soundingLoopCount    { 0 };
    };

    using ParentToLmc = std::function<Rational (Rational)>;

    void walk (const Constituent& c, const ParentToLmc& parentToLmc,
               Rational lmcTime, WalkResult& out)
    {
        const Rational spanStart = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational spanEnd   = parentToLmc (c.conceptualOut().wholeNotes());

        if (lmcTime < spanStart || lmcTime >= spanEnd)
            return;

        if (! c.name().empty())
        {
            out.deepestNamed = &c;
            if (! c.children().empty())
                out.deepestNamedContainer = &c;
        }

        if (c.tapeReference().has_value()
            && std::holds_alternative<trigger::FreeRunning> (c.repetitionRules().trigger))
        {
            const Rational cycleLength = c.tapeReference()->sliceLength();
            const Rational elapsed     = lmcTime - spanStart;
            const std::int64_t cycle   = (elapsed / cycleLength).floor();
            if (out.firstSoundingLoop == nullptr)
            {
                out.firstSoundingLoop = &c;
                out.firstLoopCycle    = cycle;
            }
            ++out.soundingLoopCount;
        }

        if (! c.children().empty())
        {
            const Rational childOffset = c.conceptualIn().wholeNotes();
            const auto localMap = c.localTempoMap();
            const ParentToLmc childToLmc =
                [parentToLmc, childOffset, localMap] (Rational t)
                {
                    const Rational inParent =
                        childOffset + (localMap ? localMap->apply (t) : t);
                    return parentToLmc (inParent);
                };
            for (const auto& child : c.children())
                walk (*child, childToLmc, lmcTime, out);
        }
    }

    std::string formatCycleStatus (const Constituent& loop, std::int64_t cycle)
    {
        const auto& card = loop.repetitionRules().cardinality;
        if (std::holds_alternative<cardinality::Once> (card))
            return "once";
        if (const auto* n = std::get_if<cardinality::NTimes> (&card))
            return std::to_string (cycle + 1) + " of " + std::to_string (n->count);
        if (std::holds_alternative<cardinality::Forever> (card))
            return "loop " + std::to_string (cycle + 1);
        // UntilSilenced / UntilConstituentStarts / UntilNextDownbeat — all
        // open-ended in their own way. "...loop N" keeps the legible budget.
        return "loop " + std::to_string (cycle + 1);
    }
}

PerformanceViewState selectPerformanceView (const Constituent& root,
                                            const TempoMap&    sessionToLmc,
                                            Rational           playheadLmcSeconds)
{
    PerformanceViewState state;
    state.playheadSeconds = playheadLmcSeconds.toDouble();

    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    WalkResult result;
    walk (root, rootToLmc, playheadLmcSeconds, result);

    state.soundingLoopCount = result.soundingLoopCount;
    state.isSilent = (result.firstSoundingLoop == nullptr);

    const Constituent* foreground = result.deepestNamedContainer != nullptr
                                  ? result.deepestNamedContainer
                                  : result.deepestNamed;
    if (foreground != nullptr)
        state.currentPhraseName = foreground->name();

    if (result.firstSoundingLoop != nullptr)
        state.cycleStatus = formatCycleStatus (*result.firstSoundingLoop,
                                               result.firstLoopCycle);

    return state;
}

} // namespace sirius
