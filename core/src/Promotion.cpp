#include "sirius/Promotion.h"

#include "sirius/TapeReference.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace sirius::promotion
{

namespace
{
    void enforceSingleInstance (const Constituent& c,
                                std::unordered_set<std::int64_t>& seen)
    {
        const auto rawId = c.id().value();
        if (! seen.insert (rawId).second)
            throw std::logic_error (
                "sirius::promotion: shared-placement architecture not yet implemented; "
                "see todo.md \"Shared-placement-with-per-instance-overlays architecture\"");
        for (const auto& child : c.children())
            enforceSingleInstance (*child, seen);
    }

    /// Compose a child's parent-to-LMC mapping from its placement and any
    /// optional local TempoMap. Same pattern as TimelineViewState::walk.
    using ParentToLmc = std::function<Rational (Rational)>;

    ParentToLmc childMapping (const Constituent& parent,
                              const ParentToLmc& parentToLmc)
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

    /// Result of finding the deepest Phrase whose LMC span contains `lmcAtMarkIn`.
    struct HostHit
    {
        std::vector<std::size_t> path;  // index path through children from root
        Rational hostStartLmc;
        Rational hostEndLmc;
        std::string hostName;
    };

    void findHostRecursive (const Constituent& c,
                            const ParentToLmc& parentToLmc,
                            Rational           lmcAtMarkIn,
                            std::vector<std::size_t>& currentPath,
                            std::optional<HostHit>& deepestSoFar)
    {
        const Rational startLmc = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational endLmc   = parentToLmc (c.conceptualOut().wholeNotes());

        if (c.isPhrase() && lmcAtMarkIn >= startLmc && lmcAtMarkIn < endLmc)
            deepestSoFar = HostHit { currentPath, startLmc, endLmc, c.name() };

        const auto childMap = childMapping (c, parentToLmc);
        for (std::size_t i = 0; i < c.children().size(); ++i)
        {
            currentPath.push_back (i);
            findHostRecursive (*c.children()[i], childMap, lmcAtMarkIn,
                               currentPath, deepestSoFar);
            currentPath.pop_back();
        }
    }
}

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         const IdAllocator&   allocateId)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "sirius::promotion::promote: region duration must be strictly positive");

    std::unordered_set<std::int64_t> seen;
    enforceSingleInstance (root, seen);

    // M3 simplification: conceptual ↔ LMC is treated as 1:1 by the boundary
    // construction below. The find-host walk uses the *real* sessionToLmc so
    // host detection composes correctly through any tempo map; only the new
    // Loop's conceptual boundaries are the simplified part.
    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    std::vector<std::size_t> path;
    std::optional<HostHit> hit;
    findHostRecursive (root, rootToLmc, lmcAtMarkIn, path, hit);

    if (hit.has_value())
    {
        const auto loopId = allocateId();

        // Clamp to host's LMC span. The TapeReference keeps the *unclamped*
        // original LMC times — the audio beyond the host boundary still exists
        // on the tape and remains referenceable; only the Constituent's
        // structural placement is clipped.
        const Rational clampedInLmc  = std::max (region.inLmcSeconds,  hit->hostStartLmc);
        const Rational clampedOutLmc = std::min (region.outLmcSeconds, hit->hostEndLmc);

        // Post-clamp invariant: loopOut > loopIn. Holds because Mark In is inside
        // [hostStart, hostEnd) by virtue of being on this branch, so
        // clampedInLmc <= lmcAtMarkIn < hostEnd; and region.out > region.in (defensive
        // throw at function top), so clampedOutLmc > clampedInLmc. Constituent's
        // constructor would otherwise throw std::invalid_argument on reversed bounds.
        const Position loopIn  (clampedInLmc  - hit->hostStartLmc);
        const Position loopOut (clampedOutLmc - hit->hostStartLmc);

        // Loop carries no name or role: a leaf with TapeReference is enough to
        // identify it; the timeline derives display from the TapeReference, and
        // editing names/roles is a future Preparation-pane feature.
        Constituent loop (loopId, loopIn, loopOut);
        loop = loop.withTapeReference (
            TapeReference (region.tape,
                           region.inLmcSeconds, region.outLmcSeconds));

        // Walk down the path again to splice the Loop into the host via
        // copy-on-write of every Constituent on that path.
        std::function<Constituent (const Constituent&, std::size_t)> spliced;
        spliced = [&] (const Constituent& c, std::size_t depth) -> Constituent
        {
            if (depth == hit->path.size())
                return c.withChildAdded (std::make_shared<const Constituent> (loop));
            const std::size_t i = hit->path[depth];
            auto childCopy = std::make_shared<const Constituent> (
                spliced (*c.children()[i], depth + 1));
            return c.withChildReplaced (i, childCopy);
        };

        Constituent newRoot = spliced (root, 0);

        std::string label = hit->hostName.empty()
                            ? std::string ("capture loop")
                            : "capture loop into " + hit->hostName;

        return PromotionResult { std::move (newRoot), loopId, std::nullopt, std::move (label) };
    }

    // Mint case (no host) — implemented in Task 4.
    throw std::logic_error ("sirius::promotion::promote: mint path not yet implemented");
}

} // namespace sirius::promotion
