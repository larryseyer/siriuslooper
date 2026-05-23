#include "sirius/TapePool.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace sirius
{

TapePool::TapePool()
{
    tapes_.push_back (TapeDescriptor { TapeId (1), "Tape 1" });
    nextId_ = 2;
}

TapePool::TapePool (std::vector<TapeDescriptor> tapes)
    : tapes_ (std::move (tapes))
{
    if (tapes_.empty())
        throw std::invalid_argument ("ida::TapePool: tape list must be non-empty (>=1 invariant)");

    std::unordered_set<std::int64_t> seen;
    std::int64_t maxId = 0;
    for (const auto& t : tapes_)
    {
        if (! seen.insert (t.id.value()).second)
            throw std::invalid_argument ("ida::TapePool: duplicate tape id");
        maxId = std::max (maxId, t.id.value());
    }
    nextId_ = maxId + 1;
}

TapeId TapePool::add (std::string name)
{
    const TapeId id (nextId_++);
    tapes_.push_back (TapeDescriptor { id, std::move (name) });
    return id;
}

bool TapePool::remove (TapeId id)
{
    if (tapes_.size() <= 1) // >=1 floor: never empty the pool
        return false;
    if (id == primary())    // primary is permanent (InputMixer pins TapeId{1} too)
        return false;

    const auto it = std::find_if (tapes_.begin(), tapes_.end(),
                                  [id] (const TapeDescriptor& t) { return t.id == id; });
    if (it == tapes_.end())
        return false;

    tapes_.erase (it);
    return true;
}

bool TapePool::rename (TapeId id, std::string name)
{
    for (auto& t : tapes_)
    {
        if (t.id == id)
        {
            t.name = std::move (name);
            return true;
        }
    }
    return false;
}

int TapePool::count() const noexcept { return static_cast<int> (tapes_.size()); }

const std::vector<TapeDescriptor>& TapePool::tapes() const noexcept { return tapes_; }

TapeId TapePool::primary() const noexcept { return tapes_.front().id; }

const TapeDescriptor* TapePool::find (TapeId id) const noexcept
{
    for (const auto& t : tapes_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const TapeDescriptor& TapePool::at (int index) const
{
    return tapes_.at (static_cast<std::size_t> (index));
}

} // namespace sirius
