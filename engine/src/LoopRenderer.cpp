#include "sirius/LoopRenderer.h"

#include <stdexcept>

namespace sirius
{

LoopRenderer::LoopRenderer (Rational tapeIn, Rational tapeOut, Rational loopStartLmc)
    : tapeIn_ (tapeIn), tapeOut_ (tapeOut), loopStartLmc_ (loopStartLmc)
{
    if (! (tapeIn_ < tapeOut_))
        throw std::invalid_argument (
            "ida::LoopRenderer: tapeOut must be strictly greater than tapeIn");
}

LoopRenderer::ReadPosition LoopRenderer::at (Rational lmcPresentTime) const
{
    const Rational elapsed = lmcPresentTime - loopStartLmc_;

    // Before the loop has started, nothing sounds.
    if (elapsed.isNegative())
        return ReadPosition { false, Rational (0), 0 };

    const Rational length = loopLength();

    // Which repetition are we in, and how far into it? floor() makes this
    // exact: cycle 100 reads exactly the same offset as cycle 0, with no
    // accumulated drift.
    const std::int64_t cycle = (elapsed / length).floor();
    const Rational offsetInCycle = elapsed - Rational (cycle) * length;

    return ReadPosition { true, tapeIn_ + offsetInCycle, cycle };
}

} // namespace sirius
