#include "sirius/EffectChain.h"

#include <stdexcept>
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

} // namespace sirius
