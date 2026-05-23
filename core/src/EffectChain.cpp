#include "sirius/EffectChain.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace sirius
{

const EffectChainEntry& EffectChain::at (std::size_t index) const
{
    if (index >= entries_.size())
        throw std::out_of_range ("sirius::EffectChain: index out of range");
    return entries_[index];
}

EffectChain EffectChain::withAppended (EffectChainEntry entry) const
{
    if (entries_.size() >= kMaxSlots)
        throw std::length_error ("sirius::EffectChain::withAppended: chain is full (kMaxSlots = "
                                 + std::to_string (kMaxSlots) + ")");

    EffectChain next (*this);
    next.entries_.push_back (std::move (entry));
    return next;
}

EffectChain EffectChain::withReplaced (std::size_t index, EffectChainEntry entry) const
{
    if (index >= entries_.size())
        throw std::out_of_range ("sirius::EffectChain: index out of range");
    EffectChain next (*this);
    next.entries_[index] = std::move (entry);
    return next;
}

EffectChain EffectChain::withRemoved (std::size_t index) const
{
    if (index >= entries_.size())
        throw std::out_of_range ("sirius::EffectChain: index out of range");
    EffectChain next (*this);
    next.entries_.erase (next.entries_.begin()
                         + static_cast<std::vector<EffectChainEntry>::difference_type> (index));
    return next;
}

EffectChain EffectChain::withMoved (std::size_t fromIndex, std::size_t toIndex) const
{
    if (fromIndex >= entries_.size() || toIndex >= entries_.size())
        throw std::out_of_range ("sirius::EffectChain: index out of range");
    EffectChain next (*this);
    std::swap (next.entries_[fromIndex], next.entries_[toIndex]);
    return next;
}

EffectChainEntry EffectChainEntry::makeInternal (InternalFxId id)
{
    EffectChainEntry e;
    e.kind = EffectChainSlotKind::Internal;
    e.internalId = id;
    e.displayName = internalFxIdToString (id);
    return e;
}

EffectChainEntry EffectChainEntry::makePlugin (PluginDescriptor descriptor,
                                               std::string      displayName,
                                               std::string      stateBase64)
{
    EffectChainEntry e;
    e.kind = EffectChainSlotKind::Plugin;
    e.descriptor = std::move (descriptor);
    e.displayName = std::move (displayName);
    e.stateBase64 = std::move (stateBase64);
    return e;
}

} // namespace sirius
