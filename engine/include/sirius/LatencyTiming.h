#pragma once

#include "sirius/Rational.h"
#include "sirius/SampleClock.h"

#include <cstdint>

namespace ida::latency
{

/// The LMC time at which sample `sampleIndex` (0-based) in a captured buffer of
/// `bufferSize` samples was actually captured — not when the audio callback
/// ran. White paper Part 5.5: latency compensation is architectural; every tape
/// entry carries its true conceptual time of capture, not its arrival time.
///
/// The newest sample in the buffer (index bufferSize-1) was captured
/// `inputLatencySeconds` before the callback; earlier samples, proportionally
/// earlier still. Throws std::out_of_range if `sampleIndex` is not in
/// [0, bufferSize).
Rational inboundCaptureTime (Rational lmcCallbackTime,
                             Rational inputLatencySeconds,
                             std::int64_t bufferSize,
                             std::int64_t sampleIndex,
                             const SampleClock& deviceClock);

/// The LMC time at which sample `sampleIndex` (0-based) in an output buffer
/// will actually be heard. White paper Part 5.5: the rendering pipeline aims at
/// this future time, not the present.
///
/// Sample 0 is heard `outputLatencySeconds` after the callback; later samples,
/// proportionally later. Throws std::out_of_range if `sampleIndex` is negative.
Rational outboundPresentTime (Rational lmcCallbackTime,
                              Rational outputLatencySeconds,
                              std::int64_t sampleIndex,
                              const SampleClock& deviceClock);

} // namespace ida::latency
