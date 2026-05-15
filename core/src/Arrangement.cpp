#include "sirius/Arrangement.h"

#include <stdexcept>
#include <utility>

namespace sirius
{

RoleSlot::RoleSlot (std::string role, Position conceptualIn, Position conceptualOut)
    : role_ (std::move (role)), conceptualIn_ (conceptualIn), conceptualOut_ (conceptualOut)
{
    if (conceptualOut_ < conceptualIn_)
        throw std::invalid_argument (
            "sirius::RoleSlot: conceptualOut must not precede conceptualIn");
}

Rational RoleSlot::duration() const
{
    return conceptualOut_.wholeNotes() - conceptualIn_.wholeNotes();
}

RoleSlot RoleSlot::withFilledBy (ConstituentId id) const
{
    RoleSlot next (*this);
    next.filledBy_ = id;
    return next;
}

RoleSlot RoleSlot::withoutFill() const
{
    RoleSlot next (*this);
    next.filledBy_.reset();
    return next;
}

namespace arrangement
{

namespace
{
    /// The end of `parent`'s existing children, in its local time — the latest
    /// `conceptualOut` among them, or the start of the domain if it has none.
    Position childrenEnd (const Constituent& parent)
    {
        Position end;
        for (const auto& child : parent.children())
            if (child->conceptualOut() > end)
                end = child->conceptualOut();
        return end;
    }

    /// A copy of `child` repositioned to span [in, in + child->duration()).
    Constituent::ChildPtr placedAt (const Constituent::ChildPtr& child, Position in)
    {
        const Position out (in.wholeNotes() + child->duration());
        return std::make_shared<const Constituent> (child->withBoundaries (in, out));
    }
}

Constituent sequence (const Constituent& parent,
                      const std::vector<Constituent::ChildPtr>& children)
{
    for (const auto& child : children)
        if (child == nullptr)
            throw std::invalid_argument ("sirius::arrangement::sequence: child must not be null");

    Constituent result (parent);
    Position cursor = childrenEnd (parent);

    for (const auto& child : children)
    {
        const Constituent::ChildPtr placed = placedAt (child, cursor);
        cursor = placed->conceptualOut();
        result = result.withChildAdded (placed);
    }

    return result;
}

Constituent layer (const Constituent& parent,
                   const std::vector<Constituent::ChildPtr>& children)
{
    for (const auto& child : children)
        if (child == nullptr)
            throw std::invalid_argument ("sirius::arrangement::layer: child must not be null");

    Constituent result (parent);

    for (const auto& child : children)
        result = result.withChildAdded (placedAt (child, Position()));

    return result;
}

} // namespace arrangement

} // namespace sirius
