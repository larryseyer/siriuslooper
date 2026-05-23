#include "ida/TempoMap.h"

#include <stdexcept>
#include <utility>

namespace ida
{

TempoMap::TempoMap (std::vector<Breakpoint> breakpoints)
    : breakpoints_ (std::move (breakpoints))
{
    if (breakpoints_.size() < 2)
        throw std::invalid_argument ("ida::TempoMap: needs at least two breakpoints");

    for (std::size_t i = 1; i < breakpoints_.size(); ++i)
    {
        if (! (breakpoints_[i - 1].input < breakpoints_[i].input))
            throw std::invalid_argument (
                "ida::TempoMap: breakpoint inputs must be strictly ascending");
    }
}

TempoMap TempoMap::constant (Rational rate)
{
    // A line through the origin with the given slope: output = rate * input.
    return TempoMap ({ Breakpoint { Rational (0), Rational (0) },
                       Breakpoint { Rational (1), rate } });
}

TempoMap TempoMap::fromBpm (Rational quarterNotesPerMinute)
{
    if (quarterNotesPerMinute.isZero() || quarterNotesPerMinute.isNegative())
        throw std::invalid_argument ("ida::TempoMap: tempo must be positive");

    // bpm quarter notes per 60 seconds => bpm/4 whole notes per 60 seconds
    // => one whole note lasts 60 / (bpm/4) = 240 / bpm seconds.
    return constant (Rational (240) / quarterNotesPerMinute);
}

Rational TempoMap::apply (Rational input) const
{
    // Choose the bracketing segment. Inputs below the first breakpoint use the
    // first segment; inputs above the last use the last segment; both
    // extrapolate. Strictly-ascending inputs guarantee a non-zero run.
    std::size_t lower = 0;

    if (input <= breakpoints_.front().input)
    {
        lower = 0;
    }
    else if (input >= breakpoints_.back().input)
    {
        lower = breakpoints_.size() - 2;
    }
    else
    {
        for (std::size_t i = 0; i + 1 < breakpoints_.size(); ++i)
        {
            if (breakpoints_[i].input <= input && input < breakpoints_[i + 1].input)
            {
                lower = i;
                break;
            }
        }
    }

    const Breakpoint& a = breakpoints_[lower];
    const Breakpoint& b = breakpoints_[lower + 1];

    const Rational run  = b.input - a.input;
    const Rational rise = b.output - a.output;
    const Rational slope = rise / run;

    return a.output + (input - a.input) * slope;
}

} // namespace ida
