#include "ida/TimeDomain.h"

#include <stdexcept>
#include <utility>

namespace sirius
{

TimeDomain::TimeDomain (std::shared_ptr<const TimeDomain> parent,
                        Meter meter,
                        TempoMap tempoMap)
    : parent_ (std::move (parent)),
      meter_ (meter),
      tempoMap_ (std::move (tempoMap))
{
}

std::shared_ptr<const TimeDomain> TimeDomain::createRoot (Meter meter, TempoMap toSeconds)
{
    return std::shared_ptr<const TimeDomain> (
        new TimeDomain (nullptr, meter, std::move (toSeconds)));
}

std::shared_ptr<const TimeDomain> TimeDomain::createChild (
    std::shared_ptr<const TimeDomain> parent,
    Meter meter,
    TempoMap toParent)
{
    if (parent == nullptr)
        throw std::invalid_argument ("ida::TimeDomain: child needs a non-null parent");

    return std::shared_ptr<const TimeDomain> (
        new TimeDomain (std::move (parent), meter, std::move (toParent)));
}

int TimeDomain::depth() const noexcept
{
    int d = 1;
    for (const TimeDomain* node = parent_.get(); node != nullptr; node = node->parent_.get())
        ++d;
    return d;
}

Rational TimeDomain::toAbsoluteSeconds (Position localPosition) const
{
    // Apply this domain's tempo map: local conceptual time -> parent's
    // conceptual time (or, at the root, -> absolute seconds).
    const Rational mapped = tempoMap_.apply (localPosition.wholeNotes());

    if (isRoot())
        return mapped;

    // `mapped` is now a position in the parent's conceptual time. Recurse.
    return parent_->toAbsoluteSeconds (Position (mapped));
}

} // namespace sirius
