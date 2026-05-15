#include "sirius/Promotion.h"

#include <stdexcept>
#include <unordered_set>

namespace sirius::promotion
{

namespace
{
    /// Walk `c` and throw std::logic_error if any ConstituentId appears more
    /// than once in the subtree. The single-instance write-protect: until the
    /// shared-placement-with-overlays architecture lands (todo.md), promotion
    /// must refuse to operate on a tree containing repeated placements.
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
}

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      /*sessionToLmc*/,
                         const CaptureRegion& region,
                         Rational             /*lmcAtMarkIn*/,
                         const IdAllocator&   /*allocateId*/)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "sirius::promotion::promote: region duration must be strictly positive");

    std::unordered_set<std::int64_t> seen;
    enforceSingleInstance (root, seen);

    // Remaining behaviour staged across follow-on tasks (host-finding, mint,
    // attach). Throws until those land. The plan tracks these as Task 3+.
    throw std::logic_error ("sirius::promotion::promote: not yet implemented");
}

} // namespace sirius::promotion
