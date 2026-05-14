#include "sirius/Membrane.h"

#include <stdexcept>

namespace sirius::membrane
{

Rational inboundCaptureTime (Rational lmcCallbackTime,
                             Rational inputLatencySeconds,
                             std::int64_t bufferSize,
                             std::int64_t sampleIndex,
                             const SampleClock& deviceClock)
{
    if (sampleIndex < 0 || sampleIndex >= bufferSize)
        throw std::out_of_range ("sirius::membrane::inboundCaptureTime: sampleIndex out of range");

    // The newest sample (bufferSize - 1) was captured `inputLatencySeconds` ago.
    // Sample `sampleIndex` was captured this many samples earlier than that:
    const std::int64_t samplesBeforeNewest = bufferSize - 1 - sampleIndex;

    return lmcCallbackTime
         - inputLatencySeconds
         - deviceClock.secondsForSamples (samplesBeforeNewest);
}

Rational outboundPresentTime (Rational lmcCallbackTime,
                              Rational outputLatencySeconds,
                              std::int64_t sampleIndex,
                              const SampleClock& deviceClock)
{
    if (sampleIndex < 0)
        throw std::out_of_range ("sirius::membrane::outboundPresentTime: sampleIndex must be >= 0");

    return lmcCallbackTime
         + outputLatencySeconds
         + deviceClock.secondsForSamples (sampleIndex);
}

} // namespace sirius::membrane
