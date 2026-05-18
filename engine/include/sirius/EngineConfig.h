#pragma once

#include "sirius/Asrc.h"

#include <cstddef>

namespace sirius
{

/// Engine configuration — the tunable knobs the audio I/O layer reads when it
/// brings the engine up. Plain value type, JUCE-free, copyable.
///
/// White paper Part 5.3 gives the ASRC quality / CPU trade-off; Part 16.8
/// decision 31 sets the <30 ms trust-budget latency. The fields here are the
/// seam M11 (capability tiers) will steer per tier — Survival vs Comfortable
/// vs Generous picks a different `asrcQuality` and may bias the preferred
/// buffer size — without forcing a refactor of the audio-callback constructor.
struct EngineConfig
{
    /// ASRC quality tier used when the audio I/O layer constructs per-membrane
    /// resamplers. `High` is the realistic default (Asrc.h: "20-bit — the
    /// realistic membrane default"). The capability-tier milestone will vary
    /// this per tier.
    Asrc::Quality asrcQuality { Asrc::Quality::High };

    /// Preferred sample rate to ask the OS for when neither the OS nor the
    /// audio interface dictates one. The white paper is silent on a required
    /// range; the operator decision (2026-05-17) is "accept anything the
    /// device exposes, default to 48 kHz when nothing is specified."
    double preferredSampleRate { 48000.0 };

    /// Preferred buffer size to ask the OS for when nothing is specified.
    /// Zero means "smallest buffer the device can reliably sustain" — the
    /// audio I/O layer picks the smallest size the device advertises that
    /// isn't smaller than `minPreferredBufferSize`.
    std::size_t preferredBufferSize { 0 };

    /// Floor for the "smallest reliable" buffer the layer will ask for when
    /// `preferredBufferSize == 0`. 128 is below this on most interfaces only
    /// when the user has explicitly lowered the OS default — a value of 128
    /// matches typical modern interface defaults without forcing pathological
    /// latencies on hardware that can't sustain them.
    std::size_t minPreferredBufferSize { 128 };
};

} // namespace sirius
