#include "ida/Position.h"

#include <stdexcept>

namespace sirius
{

Position::Position (Rational wholeNotes)
    : wholeNotes_ (wholeNotes)
{
}

Position Position::fromBarBeat (const Meter& meter,
                                std::int64_t bar,
                                std::int64_t beat,
                                Rational offsetInBeat)
{
    if (bar < 1)
        throw std::invalid_argument ("ida::Position: bar is 1-based, must be >= 1");
    if (beat < 1)
        throw std::invalid_argument ("ida::Position: beat is 1-based, must be >= 1");

    const Rational fromBars  = Rational (bar - 1)  * meter.barLength();
    const Rational fromBeats = Rational (beat - 1) * meter.beatLength();
    return Position (fromBars + fromBeats + offsetInBeat);
}

Position Position::operator+ (const Position& other) const
{
    return Position (wholeNotes_ + other.wholeNotes_);
}

Position Position::operator- (const Position& other) const
{
    return Position (wholeNotes_ - other.wholeNotes_);
}

bool Position::operator== (const Position& other) const noexcept
{
    return wholeNotes_ == other.wholeNotes_;
}

bool Position::operator!= (const Position& other) const noexcept
{
    return wholeNotes_ != other.wholeNotes_;
}

bool Position::operator<  (const Position& other) const { return wholeNotes_ <  other.wholeNotes_; }
bool Position::operator<= (const Position& other) const { return wholeNotes_ <= other.wholeNotes_; }
bool Position::operator>  (const Position& other) const { return wholeNotes_ >  other.wholeNotes_; }
bool Position::operator>= (const Position& other) const { return wholeNotes_ >= other.wholeNotes_; }

} // namespace sirius
