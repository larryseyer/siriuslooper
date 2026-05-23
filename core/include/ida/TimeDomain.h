#pragma once

#include "ida/Meter.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TempoMap.h"

#include <memory>

namespace ida
{

/// A node in the tree of conceptual time domains (white paper Part 3.4):
/// session -> song -> section -> phrase -> loop -> cycle. Each domain has its
/// own meter and a tempo map relating its local time to its parent's. The root
/// domain's tempo map relates its local time directly to absolute seconds.
///
/// `toAbsoluteSeconds` is the "unroll" of white paper Part 3.6: it walks a
/// local position up the tree, applying each domain's tempo map in turn, to
/// produce absolute time. The arithmetic is exact; numerical rendering belongs
/// only at the membrane. A child holds a strong reference to its parent, so an
/// unroll is always safe as long as the leaf domain is held.
class TimeDomain
{
public:
    /// Creates the root domain. Its tempo map maps local conceptual time (whole
    /// notes) directly to absolute time (seconds).
    static std::shared_ptr<const TimeDomain> createRoot (Meter meter,
                                                         TempoMap toSeconds);

    /// Creates a child domain whose tempo map maps its local conceptual time to
    /// the parent's conceptual time. Throws std::invalid_argument if `parent`
    /// is null.
    static std::shared_ptr<const TimeDomain> createChild (
        std::shared_ptr<const TimeDomain> parent,
        Meter meter,
        TempoMap toParent);

    bool isRoot() const noexcept { return parent_ == nullptr; }
    const TimeDomain* parent() const noexcept { return parent_.get(); }
    const Meter& meter() const noexcept { return meter_; }
    const TempoMap& tempoMap() const noexcept { return tempoMap_; }

    /// Number of domains from this one up to and including the root.
    int depth() const noexcept;

    /// Unrolls a local position through the hierarchy to absolute seconds.
    Rational toAbsoluteSeconds (Position localPosition) const;

private:
    TimeDomain (std::shared_ptr<const TimeDomain> parent,
                Meter meter,
                TempoMap tempoMap);

    std::shared_ptr<const TimeDomain> parent_; // null => this is the root
    Meter meter_;
    TempoMap tempoMap_;
};

} // namespace ida
