#include "sirius/Bus.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace sirius
{

Bus::Bus (BusId id, BusConfig config)
    : id_ (id),
      config_ (std::move (config)),
      mixBuffer_ (kMaxBusMixSamples * static_cast<std::size_t> (kMaxBusChannelsHard), 0.0f)
{
    // Fail loud per CLAUDE.md rule 8 — silently truncating an out-of-range
    // channelCount in Bus::process would mask configuration mistakes.
    jassert (config_.channelCount >= 1 && config_.channelCount <= kMaxBusChannelsHard);
}

void Bus::process (float* const* output, int numChannels, int numSamples) const noexcept
{
    // M5 Session 3 body — additive accumulate from mixBuffer_ into output,
    // then zero mixBuffer_ for the next buffer. effectChain_ is held but
    // NOT invoked in M5 (V7 alignment plan line 387: "EQ/dynamics stubs in
    // M5"); plugin invocation lands with M7. When that lands, run the
    // effect chain between "read mixBuffer_" and "write output" — for now,
    // it's a pass-through with zero on read.
    if (output == nullptr || numChannels <= 0 || numSamples <= 0) return;

    const int activeChannels = std::min (numChannels, kMaxBusChannelsHard);
    const int clampedSamples = std::min (numSamples,
                                         static_cast<int> (kMaxBusMixSamples));

    for (int c = 0; c < activeChannels; ++c)
    {
        if (output[c] == nullptr) continue;
        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;

        // Additive into the output — bus contribution layered on top of any
        // prior writer's signal (e.g. DirectLayer's bypass routes that also
        // additively write into the same physical output buffers).
        for (int s = 0; s < clampedSamples; ++s)
            output[c][s] += mix[s];

        // Zero the mix scratch so the next buffer starts at silence.
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
}

float* Bus::mixBufferChannel (int c) const noexcept
{
    if (c < 0 || c >= kMaxBusChannelsHard) return nullptr;
    return mixBuffer_.data()
         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
}

} // namespace sirius
