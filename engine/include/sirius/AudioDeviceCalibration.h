#pragma once

#include "sirius/Rational.h"

namespace sirius
{

/// A device clock's calibration against the LMC (white paper Part 4.3).
///
/// Every hardware clock at the membrane drifts relative to the LMC: it runs at
/// a slightly wrong rate, and starts from a slightly wrong origin. This record
/// captures both — a rate factor and a constant offset — so the membrane can
/// convert between device-native time and LMC time in either direction.
///
/// Until a one-time loopback calibration measures the real values, the
/// identity calibration (rate factor 1, offset 0) stands in: device time is
/// taken to equal LMC time.
class AudioDeviceCalibration
{
public:
    /// `rateFactor` is the measured ratio of device rate to LMC rate
    /// (e.g. 0.999987). `offsetSeconds` is the constant offset between the
    /// device's time origin and the LMC's. Throws std::invalid_argument if the
    /// rate factor is not positive.
    AudioDeviceCalibration (Rational rateFactor, Rational offsetSeconds);

    /// The un-calibrated default: device time is taken to equal LMC time.
    static AudioDeviceCalibration identity();

    Rational rateFactor()    const noexcept { return rateFactor_; }
    Rational offsetSeconds() const noexcept { return offsetSeconds_; }

    /// Converts a device-native timestamp to LMC time.
    Rational deviceToLmc (Rational deviceSeconds) const;

    /// Converts an LMC timestamp to device-native time.
    Rational lmcToDevice (Rational lmcSeconds) const;

private:
    Rational rateFactor_;
    Rational offsetSeconds_;
};

} // namespace sirius
