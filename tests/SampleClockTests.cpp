// Tests for ida::SampleClock — the exact sample-count <-> time conversion
// the membrane math is built on.
#include "ida/SampleClock.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::Rational;
using ida::SampleClock;

TEST_CASE ("a sample clock converts sample counts to exact time", "[sampleclock]")
{
    const SampleClock at48k (Rational (48000));
    CHECK (at48k.samplesPerSecond() == Rational (48000));

    CHECK (at48k.secondsForSamples (48000) == Rational (1));        // one second
    CHECK (at48k.secondsForSamples (24000) == Rational (1, 2));
    CHECK (at48k.secondsForSamples (0)     == Rational (0));
    // A single sample is an exact rational, not a rounded decimal.
    CHECK (at48k.secondsForSamples (1)     == Rational (1, 48000));
}

TEST_CASE ("a sample clock works at a non-round measured rate", "[sampleclock][conceptual-time]")
{
    // At the membrane "48 kHz" is whatever the crystal is really doing. A
    // measured rate of 47999 samples/sec is still exact here.
    const SampleClock measured (Rational (47999));
    CHECK (measured.secondsForSamples (47999) == Rational (1));
    CHECK (measured.secondsForSamples (1)     == Rational (1, 47999));
}

TEST_CASE ("a non-positive sample rate is rejected loudly", "[sampleclock]")
{
    CHECK_THROWS_AS (SampleClock (Rational (0)),    std::invalid_argument);
    CHECK_THROWS_AS (SampleClock (Rational (-48000)), std::invalid_argument);
}
