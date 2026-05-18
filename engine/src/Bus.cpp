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

void Bus::process (float* const* output, int numChannels, int numSamples) noexcept
{
    // M5 Session 2 stub — identity-zero output. Session 3 replaces this
    // body with the real "sum sends from mixBuffer_, run effectChain_,
    // accumulate into output" pipeline. The zero-write keeps the
    // OutputMixer audio path well-defined for the Session 3 wiring even
    // before the real DSP lands.
    if (output == nullptr || numChannels <= 0 || numSamples <= 0) return;

    const int activeChannels = std::min (numChannels, kMaxBusChannelsHard);
    for (int c = 0; c < activeChannels; ++c)
    {
        if (output[c] == nullptr) continue;
        std::memset (output[c], 0, static_cast<std::size_t> (numSamples) * sizeof (float));
    }
}

} // namespace sirius
