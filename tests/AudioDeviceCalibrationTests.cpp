// Tests for ida::AudioDeviceCalibration — the device-clock-vs-LMC record
// (white paper Part 4.3) and its two conversion directions.
#include "sirius/AudioDeviceCalibration.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::AudioDeviceCalibration;
using ida::Rational;

TEST_CASE ("the identity calibration leaves time unchanged", "[calibration]")
{
    const auto identity = AudioDeviceCalibration::identity();
    CHECK (identity.rateFactor() == Rational (1));
    CHECK (identity.offsetSeconds() == Rational (0));
    CHECK (identity.deviceToLmc (Rational (5))    == Rational (5));
    CHECK (identity.lmcToDevice (Rational (5))    == Rational (5));
}

TEST_CASE ("calibration converts device time to LMC time and back", "[calibration]")
{
    // A device running slightly slow (rate factor 999/1000) with a 1/10-second
    // offset between its time origin and the LMC's.
    const AudioDeviceCalibration cal (Rational (999, 1000), Rational (1, 10));

    // device -> LMC: deviceSeconds * rateFactor + offset
    CHECK (cal.deviceToLmc (Rational (1000)) == Rational (999) + Rational (1, 10));

    // The two directions are exact inverses — round-tripping any LMC time
    // returns it unchanged.
    const Rational lmc (Rational (7, 3));
    CHECK (cal.deviceToLmc (cal.lmcToDevice (lmc)) == lmc);
    const Rational device (Rational (11, 4));
    CHECK (cal.lmcToDevice (cal.deviceToLmc (device)) == device);
}

TEST_CASE ("a non-positive rate factor is rejected loudly", "[calibration]")
{
    CHECK_THROWS_AS (AudioDeviceCalibration (Rational (0), Rational (0)),
                     std::invalid_argument);
    CHECK_THROWS_AS (AudioDeviceCalibration (Rational (-1), Rational (0)),
                     std::invalid_argument);
}
