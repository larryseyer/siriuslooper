#pragma once

#include "sirius/TapeId.h"

namespace sirius
{

/// Per-tape capture-sink seam (tape subsystem slice 2). The input mixer sums
/// every node routed to a given tape and delivers ONE stereo block per tape per
/// audio block. Slice 3 implements this over real per-tape TapeWriters in
/// MainComponent; tests implement a recording fake.
///
/// RT-safety: deliverTapeBlock is called on the audio thread. Implementations
/// MUST be noexcept and allocation/lock/I/O-free (docs/RT_SAFETY_CONTRACT.md).
class ITapeSink
{
public:
    virtual ~ITapeSink() = default;

    /// `left`/`right` are `numSamples` non-interleaved post-mix samples for the
    /// pooled tape `tape`. Called at most once per tape per block, only for tapes
    /// that received signal.
    virtual void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                   int numSamples) noexcept = 0;
};

} // namespace sirius
