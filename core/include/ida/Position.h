#pragma once

#include "ida/Meter.h"
#include "ida/Rational.h"

#include <cstdint>

namespace sirius
{

/// A symbolic position in conceptual time, measured in whole notes from the
/// start of its (contextual) time domain.
///
/// Exact by construction — there is no rounding and no grid (white paper
/// Part III). The domain a Position belongs to is contextual: it is held by
/// the Constituent or TimeDomain that owns the position, not by the Position
/// itself, so a Position stays a pure value type.
class Position
{
public:
    /// The start of the domain — 0 whole notes.
    constexpr Position() = default;

    /// A position a given number of whole notes from the domain's start.
    explicit Position (Rational wholeNotes);

    /// Builds a position from musical coordinates within a meter. `bar` and
    /// `beat` are 1-based, the way musicians count; `offsetInBeat` is an exact
    /// offset into that beat, in whole notes — e.g. Rational(2, 16) for the
    /// third sixteenth of the beat. Throws std::invalid_argument if `bar` or
    /// `beat` is less than 1.
    static Position fromBarBeat (const Meter& meter,
                                 std::int64_t bar,
                                 std::int64_t beat,
                                 Rational offsetInBeat = Rational());

    Rational wholeNotes() const noexcept { return wholeNotes_; }

    Position operator+ (const Position& other) const;
    Position operator- (const Position& other) const;

    bool operator== (const Position& other) const noexcept;
    bool operator!= (const Position& other) const noexcept;
    bool operator<  (const Position& other) const;
    bool operator<= (const Position& other) const;
    bool operator>  (const Position& other) const;
    bool operator>= (const Position& other) const;

private:
    Rational wholeNotes_;
};

} // namespace sirius
