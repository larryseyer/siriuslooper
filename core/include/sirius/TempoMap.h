#pragma once

#include "sirius/Rational.h"

#include <vector>

namespace sirius
{

/// A conceptual transformation between two time domains (white paper Part 5.4):
/// it maps a position in one domain ("input") to a position in another
/// ("output"). For a child domain the output is the parent's conceptual time;
/// for the root domain the output is absolute time, in seconds.
///
/// Represented as a sorted list of breakpoints with piecewise-linear
/// interpolation between them — constant tempo per segment. The first and last
/// segments extrapolate beyond the breakpoints, so the map is total. Tempo
/// *ramps* (which would be piecewise-quadratic) are a deliberate future
/// extension and are not modelled here.
///
/// The map is conceptual: it is exact rational arithmetic, evaluated only when
/// a position is unrolled at the membrane — never sampled onto a grid.
class TempoMap
{
public:
    struct Breakpoint
    {
        Rational input;
        Rational output;
    };

    /// Constructs from two or more breakpoints, strictly ascending in `input`.
    /// Throws std::invalid_argument otherwise.
    explicit TempoMap (std::vector<Breakpoint> breakpoints);

    /// A constant-rate map through the origin: output = rate * input.
    static TempoMap constant (Rational rate);

    /// A constant-tempo map from conceptual whole notes to seconds, given a
    /// tempo expressed as quarter-note beats per minute. Throws
    /// std::invalid_argument if the tempo is not positive.
    static TempoMap fromBpm (Rational quarterNotesPerMinute);

    /// Maps an input position to its output position. Exact.
    Rational apply (Rational input) const;

    const std::vector<Breakpoint>& breakpoints() const noexcept { return breakpoints_; }

private:
    std::vector<Breakpoint> breakpoints_;
};

} // namespace sirius
