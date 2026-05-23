#include "sirius/RenderPipeline.h"

#include "sirius/ConstituentValidator.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sirius
{

namespace
{
    /// Maps a position in a Constituent's *parent's* local time (whole notes)
    /// to absolute LMC seconds. Rebuilt at each level of the descent as the
    /// pipeline composes one more time domain onto the chain.
    using ParentToLmc = std::function<Rational (Rational)>;

    /// Whether a loop on `cycle` (0-based) is still sounding under `cardinality`.
    /// The query is only ever reached with cycle >= 0.
    bool cardinalityAllowsCycle (const Cardinality& cardinality, std::int64_t cycle)
    {
        if (std::holds_alternative<cardinality::Once> (cardinality))
            return cycle == 0;

        if (const auto* nTimes = std::get_if<cardinality::NTimes> (&cardinality))
            return cycle < nTimes->count;

        // Forever and the until-conditions: at M3 the placement span is the
        // bound. The until-conditions' external evaluation context is a later
        // subsystem.
        return true;
    }

    void collect (const Constituent& constituent,
                  const ConstituentValidation& validation,
                  const ParentToLmc& parentToLmc,
                  Rational lmcTime,
                  std::vector<ActiveRead>& out)
    {
        // White paper §17.7: a Broken or Invalid Constituent renders as silence,
        // and so does everything nested inside it — its slot in the parent is
        // silent. Skip the node and its whole subtree.
        if (validation.state (constituent.id()) != ConstituentState::Valid)
            return;

        // conceptualIn / conceptualOut are positions in the parent's local time.
        const Rational spanStartLmc = parentToLmc (constituent.conceptualIn().wholeNotes());
        const Rational spanEndLmc   = parentToLmc (constituent.conceptualOut().wholeNotes());

        // Outside this Constituent's placement span, neither it nor anything
        // nested inside it can sound.
        if (lmcTime < spanStartLmc || lmcTime >= spanEndLmc)
            return;

        // A leaf loop emits a read — but only if it is free-running. Any other
        // trigger leaves the loop correctly dormant: it awaits an external
        // trigger event the M3 pipeline does not provide.
        if (constituent.tapeReference().has_value()
            && std::holds_alternative<trigger::FreeRunning> (
                   constituent.repetitionRules().trigger))
        {
            const TapeReference& ref = *constituent.tapeReference();
            const Rational cycleLength = ref.sliceLength();
            const Rational elapsed = lmcTime - spanStartLmc;

            // Which repetition, and how far into it. floor() keeps this exact:
            // the hundredth cycle reads the same offset as the first, with no
            // accumulated drift (white paper Part 9.3).
            const std::int64_t cycle = (elapsed / cycleLength).floor();

            if (cardinalityAllowsCycle (constituent.repetitionRules().cardinality, cycle))
            {
                const Rational offsetInCycle = elapsed - Rational (cycle) * cycleLength;
                out.push_back (ActiveRead { constituent.id(),
                                            ref.tape,
                                            ref.tapeIn + offsetInCycle,
                                            cycle });
            }
        }

        // Descend. A child's own local time maps into this Constituent's local
        // time as `conceptualIn + (localTempoMap ? localTempoMap(t) : t)`, then
        // up to LMC via parentToLmc — composing one more domain onto the chain.
        if (! constituent.children().empty())
        {
            const Rational childOffset = constituent.conceptualIn().wholeNotes();
            const std::optional<TempoMap> localMap = constituent.localTempoMap();

            const ParentToLmc childToLmc =
                [parentToLmc, childOffset, localMap] (Rational localTime)
                {
                    const Rational inParent =
                        childOffset + (localMap ? localMap->apply (localTime) : localTime);
                    return parentToLmc (inParent);
                };

            for (const auto& child : constituent.children())
                collect (*child, validation, childToLmc, lmcTime, out);
        }
    }
}

RenderPipeline::RenderPipeline (std::shared_ptr<const Constituent> root, TempoMap sessionToLmc)
    : RenderPipeline (std::move (root), std::move (sessionToLmc), ConstituentValidation())
{
}

RenderPipeline::RenderPipeline (std::shared_ptr<const Constituent> root,
                                TempoMap                            sessionToLmc,
                                ConstituentValidation               validation)
    : root_ (std::move (root)),
      sessionToLmc_ (std::move (sessionToLmc)),
      validation_ (std::move (validation))
{
    if (root_ == nullptr)
        throw std::invalid_argument ("ida::RenderPipeline: root must not be null");
}

std::vector<ActiveRead> RenderPipeline::activeReadsAt (Rational lmcTime) const
{
    std::vector<ActiveRead> reads;

    // The root's placement is expressed in the session conceptual time that
    // sessionToLmc_ consumes, so the root's own ParentToLmc is sessionToLmc_.
    const TempoMap sessionMap = sessionToLmc_;
    const ParentToLmc rootToLmc =
        [sessionMap] (Rational t) { return sessionMap.apply (t); };

    collect (*root_, validation_, rootToLmc, lmcTime, reads);
    return reads;
}

} // namespace sirius
