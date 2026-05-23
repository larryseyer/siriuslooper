#pragma once

#include "ida/Channel.h"
#include "ida/Rational.h"

namespace ida
{

/// Audio-thread interface a `Bus` calls to hand off its post-effects (wet)
/// audio for capture. JUCE-free by design — the dependency-inversion seam
/// (parallel to `IEffectChainHost`): `Bus` (engine) does not know whether the
/// sink writes to disk, a ring, or a test spy.
///
/// `tryEnqueueWet` is the audio-thread surface — `noexcept`, allocation-free,
/// lock-free, I/O-free per `docs/RT_SAFETY_CONTRACT.md §6`.
class IWetCaptureSink
{
public:
    virtual ~IWetCaptureSink() = default;

    /// Planar input: `channels[c]` points at `numSamples` float32 for channel
    /// `c`. Returns true on enqueue, false on queue-full OR when the
    /// interleaved payload would exceed the per-message cap (caller treats
    /// false as a dropped buffer — same backpressure semantics as the dry tape).
    [[nodiscard]] virtual bool tryEnqueueWet (ChannelId           id,
                                              Rational            lmcTime,
                                              const float* const* channels,
                                              int                 numChannels,
                                              int                 numSamples) noexcept = 0;
};

} // namespace ida
