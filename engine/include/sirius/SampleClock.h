#pragma once

#include "sirius/Rational.h"

#include <cstdint>
#include <stdexcept>

namespace sirius
{

/// Converts between a count of audio samples and elapsed time.
///
/// The rate here is the *actual measured* sample rate, not the nominal one. At
/// the membrane, "48 kHz" is whatever the crystal oscillator is really doing
/// (white paper Part 2.1) — the membrane math works in the real rate, and the
/// device calibration is what supplies it.
class SampleClock
{
public:
    /// Constructs a clock at the given sample rate. Throws std::invalid_argument
    /// if the rate is not positive.
    explicit SampleClock (Rational samplesPerSecond)
        : samplesPerSecond_ (samplesPerSecond)
    {
        if (samplesPerSecond_.isZero() || samplesPerSecond_.isNegative())
            throw std::invalid_argument ("ida::SampleClock: sample rate must be positive");
    }

    Rational samplesPerSecond() const noexcept { return samplesPerSecond_; }

    /// The elapsed time, in seconds, that `sampleCount` samples span.
    Rational secondsForSamples (std::int64_t sampleCount) const
    {
        return Rational (sampleCount) / samplesPerSecond_;
    }

private:
    Rational samplesPerSecond_;
};

} // namespace sirius
