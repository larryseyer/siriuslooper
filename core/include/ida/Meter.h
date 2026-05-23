#pragma once

#include "ida/Rational.h"

namespace ida
{

/// A time signature — e.g. 7/8.
///
/// In this engine meter is a *property* of a Constituent, declared locally, not
/// a global session constraint (white paper Part 9.6). Meter does not define
/// conceptual time: conceptual positions are measured in whole notes regardless
/// of meter. Meter is what lets a position be *named* in musical terms (bar,
/// beat) and what gives bar and beat their lengths.
class Meter
{
public:
    /// Constructs a meter, e.g. Meter(7, 8) for 7/8. Both arguments must be
    /// positive; throws std::invalid_argument otherwise.
    Meter (int beatsPerBar, int beatUnit);

    int beatsPerBar() const noexcept { return beatsPerBar_; }
    int beatUnit()    const noexcept { return beatUnit_; }

    /// Length of one beat, in whole notes. For 7/8 this is 1/8.
    Rational beatLength() const;

    /// Length of one bar, in whole notes. For 7/8 this is 7/8.
    Rational barLength() const;

    bool operator== (const Meter& other) const noexcept;
    bool operator!= (const Meter& other) const noexcept;

private:
    int beatsPerBar_;
    int beatUnit_;
};

} // namespace ida
