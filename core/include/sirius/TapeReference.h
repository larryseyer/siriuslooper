#pragma once

#include "sirius/Rational.h"
#include "sirius/TapeId.h"

#include <stdexcept>

namespace sirius
{

/// A leaf loop Constituent's reference into its source tape — which tape, and
/// the slice [tapeIn, tapeOut) it plays. White paper Part 7.4: a loop is the
/// Constituent type that directly references tape data; everything above it in
/// the hierarchy is structure that organises when and how loops are heard.
///
/// At M3 the slice bounds are in LMC seconds, consistent with the M2
/// LoopRenderer and the LMC-timestamped tape. Expressing the slice in a
/// phrase's conceptual time is a later refinement.
struct TapeReference
{
    /// Constructs a reference to the slice [tapeIn, tapeOut) of `tape`. Throws
    /// std::invalid_argument if the slice is empty or reversed.
    TapeReference (TapeId sourceTape, Rational sliceIn, Rational sliceOut)
        : tape (sourceTape), tapeIn (sliceIn), tapeOut (sliceOut)
    {
        if (! (tapeIn < tapeOut))
            throw std::invalid_argument (
                "ida::TapeReference: tapeOut must be strictly greater than tapeIn");
    }

    /// The slice length, in seconds — the loop's natural cycle length.
    Rational sliceLength() const { return tapeOut - tapeIn; }

    TapeId   tape;
    Rational tapeIn;
    Rational tapeOut;
};

} // namespace sirius
