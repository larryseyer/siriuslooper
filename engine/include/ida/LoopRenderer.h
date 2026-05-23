#pragma once

#include "ida/Rational.h"

#include <cstdint>

namespace sirius
{

/// Renders a single repeating loop — the outbound membrane's job at M2 (white
/// paper Part 5.2: "render a single loop Constituent back to audio").
///
/// A loop is a region of a tape, [tapeIn, tapeOut), that repeats forever from
/// some start moment. Given an LMC time at which a sample will be heard, the
/// renderer answers where in the tape to read. At M2 the loop works directly in
/// LMC seconds — the tape's events are LMC-timestamped; placing the loop inside
/// a phrase's conceptual time domain is M3.
///
/// The cycle math is exact rational arithmetic: the read position at the
/// hundredth cycle is as precise as at the first (white paper Part 9.3, "the
/// math is in symbolic structure, not in numerical approximation").
class LoopRenderer
{
public:
    /// Where, and whether, the loop is sounding at a queried moment.
    struct ReadPosition
    {
        bool         sounding;      ///< false if the moment precedes the loop's start
        Rational     tapePosition;  ///< LMC time to read from the tape (valid when sounding)
        std::int64_t cycle;         ///< which repetition, 0-based (valid when sounding)
    };

    /// Constructs a loop over the tape region [tapeIn, tapeOut), starting at
    /// `loopStartLmc`. Throws std::invalid_argument if the region is empty or
    /// reversed (tapeOut must be strictly greater than tapeIn).
    LoopRenderer (Rational tapeIn, Rational tapeOut, Rational loopStartLmc);

    /// The loop's cycle length — tapeOut - tapeIn, in seconds.
    Rational loopLength() const noexcept { return tapeOut_ - tapeIn_; }

    /// Where to read from the tape for a sample heard at `lmcPresentTime`.
    ReadPosition at (Rational lmcPresentTime) const;

private:
    Rational tapeIn_;
    Rational tapeOut_;
    Rational loopStartLmc_;
};

} // namespace sirius
