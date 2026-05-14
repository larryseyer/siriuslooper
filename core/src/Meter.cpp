#include "sirius/Meter.h"

#include <stdexcept>

namespace sirius
{

Meter::Meter (int beatsPerBar, int beatUnit)
    : beatsPerBar_ (beatsPerBar), beatUnit_ (beatUnit)
{
    if (beatsPerBar_ <= 0)
        throw std::invalid_argument ("sirius::Meter: beatsPerBar must be positive");
    if (beatUnit_ <= 0)
        throw std::invalid_argument ("sirius::Meter: beatUnit must be positive");
}

Rational Meter::beatLength() const
{
    return Rational (1, beatUnit_);
}

Rational Meter::barLength() const
{
    return Rational (beatsPerBar_, beatUnit_);
}

bool Meter::operator== (const Meter& other) const noexcept
{
    return beatsPerBar_ == other.beatsPerBar_ && beatUnit_ == other.beatUnit_;
}

bool Meter::operator!= (const Meter& other) const noexcept
{
    return ! (*this == other);
}

} // namespace sirius
