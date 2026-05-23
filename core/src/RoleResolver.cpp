#include "ida/RoleResolver.h"

namespace ida
{

std::vector<ConstituentId>
findCandidatesFor (const RoleSlot& slot,
                   const std::vector<Constituent::ChildPtr>& pool)
{
    std::vector<ConstituentId> out;
    out.reserve (pool.size());

    for (const auto& candidate : pool)
    {
        if (candidate == nullptr)
            continue;

        const auto& meta = candidate->phraseMetadata();
        if (! meta.has_value())
            continue;

        if (! meta->isRoleFillable)
            continue;

        if (meta->role != slot.role())
            continue;

        out.push_back (candidate->id());
    }

    return out;
}

RoleSlot resolveFirst (const RoleSlot& slot,
                       const std::vector<Constituent::ChildPtr>& pool)
{
    const auto candidates = findCandidatesFor (slot, pool);
    if (candidates.empty())
        return slot;

    return slot.withFilledBy (candidates.front());
}

} // namespace ida
