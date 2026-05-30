#pragma once

#include <cstdint>

namespace ida {

/// Lock-free transport playhead snapshot published by OttoHost each block and
/// read by the playback-resolution worker + the audio-callback playback step.
/// Trivially copyable POD.
struct TransportPlayhead {
    double positionInSeconds { 0.0 };
    bool   isPlaying         { false };
};

/// Advance an elapsed-played-samples counter. Advances by `numSamples` only
/// while playing; holds while stopped; ignores non-positive blocks. Pure.
inline std::int64_t advancePlayedSamples (std::int64_t prev,
                                          int          numSamples,
                                          bool         isPlaying) noexcept
{
    if (isPlaying && numSamples > 0)
        return prev + static_cast<std::int64_t> (numSamples);
    return prev;
}

/// Identity calibration: played-samples -> LMC seconds. Guards sr<=0 -> 0.
inline double playheadSeconds (std::int64_t playedSamples, double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return 0.0;
    return static_cast<double> (playedSamples) / sampleRate;
}

} // namespace ida
