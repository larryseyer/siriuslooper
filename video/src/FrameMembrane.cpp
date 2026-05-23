#include "ida/FrameMembrane.h"

#include <stdexcept>

namespace sirius
{

namespace
{
    /// Round a Rational to the nearest integer, halves rounding toward +∞.
    /// Implemented as floor(r + 1/2) so it stays inside Rational's exact
    /// arithmetic — no double conversion, no precision loss.
    std::int64_t roundToInt (Rational r)
    {
        return (r + Rational (1, 2)).floor();
    }

    void requirePositiveFps (Rational fps, const char* context)
    {
        if (! (fps > Rational (0)))
            throw std::invalid_argument (std::string ("ida::FrameMembrane: ") + context
                                         + " fps must be positive");
    }
}

FrameMembrane::FrameMembrane (Rational sourceFps, Rational sourceStartLmcSeconds)
    : sourceFps_ (sourceFps), sourceStart_ (sourceStartLmcSeconds)
{
    requirePositiveFps (sourceFps_, "source");
    frameDuration_ = Rational (1) / sourceFps_;
}

Rational FrameMembrane::presentationTimeOf (std::int64_t frameIndex) const
{
    return sourceStart_ + Rational (frameIndex) * frameDuration_;
}

std::int64_t FrameMembrane::nearestFrameIndex (Rational lmcTime) const
{
    const Rational elapsed = lmcTime - sourceStart_;
    return roundToInt (elapsed * sourceFps_);
}

std::vector<std::int64_t> convertFrameRate (
    Rational sourceFps,
    Rational targetFps,
    std::int64_t targetFrameCount,
    Rational sourceStartLmcSeconds,
    Rational targetStartLmcSeconds)
{
    requirePositiveFps (sourceFps, "source");
    requirePositiveFps (targetFps, "target");
    if (targetFrameCount < 0)
        throw std::invalid_argument ("ida::convertFrameRate: targetFrameCount must be >= 0");

    const FrameMembrane membrane (sourceFps, sourceStartLmcSeconds);
    const Rational targetFrameDuration = Rational (1) / targetFps;

    std::vector<std::int64_t> result;
    result.reserve (static_cast<std::size_t> (targetFrameCount));
    for (std::int64_t k = 0; k < targetFrameCount; ++k)
    {
        const Rational targetTime = targetStartLmcSeconds + Rational (k) * targetFrameDuration;
        result.push_back (membrane.nearestFrameIndex (targetTime));
    }
    return result;
}

} // namespace sirius
