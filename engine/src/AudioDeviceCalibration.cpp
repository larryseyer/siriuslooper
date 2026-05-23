#include "ida/AudioDeviceCalibration.h"

#include <stdexcept>

namespace sirius
{

AudioDeviceCalibration::AudioDeviceCalibration (Rational rateFactor, Rational offsetSeconds)
    : rateFactor_ (rateFactor), offsetSeconds_ (offsetSeconds)
{
    if (rateFactor_.isZero() || rateFactor_.isNegative())
        throw std::invalid_argument (
            "ida::AudioDeviceCalibration: rate factor must be positive");
}

AudioDeviceCalibration AudioDeviceCalibration::identity()
{
    return AudioDeviceCalibration (Rational (1), Rational (0));
}

Rational AudioDeviceCalibration::deviceToLmc (Rational deviceSeconds) const
{
    return deviceSeconds * rateFactor_ + offsetSeconds_;
}

Rational AudioDeviceCalibration::lmcToDevice (Rational lmcSeconds) const
{
    return (lmcSeconds - offsetSeconds_) / rateFactor_;
}

} // namespace sirius
