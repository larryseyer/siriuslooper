#include "sirius/Bus.h"

#include "sirius/IWetCaptureSink.h"
#include "sirius/Rational.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace sirius
{

Bus::Bus (BusId id, BusConfig config)
    : id_ (id),
      config_ (std::move (config)),
      mixBuffer_ (kMaxBusMixSamples * static_cast<std::size_t> (kMaxBusChannelsHard), 0.0f),
      processedBuffer_ (kMaxBusMixSamples * static_cast<std::size_t> (kMaxBusChannelsHard), 0.0f)
{
    // Fail loud per CLAUDE.md rule 8 — silently truncating an out-of-range
    // channelCount in Bus::process would mask configuration mistakes.
    jassert (config_.channelCount >= 1 && config_.channelCount <= kMaxBusChannelsHard);
}

void Bus::process (float* const* output, int numChannels, int numSamples) const noexcept
{
    // M7 S3 body. Two paths:
    //   1. No host bound OR effect chain empty / all bypassed → take the
    //      M5 inline path (bit-for-bit equivalent to the previous body).
    //      Zero performance regression for the default configuration.
    //   2. Host bound AND at least one active slot → copy mixBuffer_ into
    //      processedBuffer_, dispatch each non-bypassed slot through
    //      host_->pumpSlot in-place on processedBuffer_, then additively
    //      sum processedBuffer_ into output. On a pumpSlot miss (returns
    //      false), the slot's contribution is the dry mix carried into
    //      processedBuffer_ in step 1 — pipelined 1-buffer delay model
    //      per the M7 S3 design decisions.
    if (output == nullptr || numChannels <= 0 || numSamples <= 0) return;

    const int activeChannels = std::min (numChannels, kMaxBusChannelsHard);
    const int clampedSamples = std::min (numSamples,
                                         static_cast<int> (kMaxBusMixSamples));

    // Determine whether the effect-chain path applies. Empty chain or
    // all-bypassed counts as "no chain" — same as not having a host bound.
    bool hasActiveSlot = false;
    if (host_ != nullptr)
    {
        for (const auto& entry : effectChain_.entries())
        {
            if (! entry.bypassed) { hasActiveSlot = true; break; }
        }
    }

    if (! hasActiveSlot)
    {
        // Inline path — identical to the M5 Session 3 body.
        for (int c = 0; c < activeChannels; ++c)
        {
            if (output[c] == nullptr) continue;
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;

            for (int s = 0; s < clampedSamples; ++s)
                output[c][s] += mix[s];

            std::memset (mix, 0,
                         static_cast<std::size_t> (clampedSamples) * sizeof (float));
        }
        return;
    }

    // Effect-chain path — set up the per-channel processed scratch from
    // the current bus mix. processedBuffer_ uses the same channel-major
    // layout as mixBuffer_ (stride = kMaxBusMixSamples per channel).
    float* processedPtrs[kMaxBusChannelsHard] = { nullptr, nullptr };
    for (int c = 0; c < activeChannels; ++c)
    {
        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        float* const proc = processedBuffer_.data()
                          + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memcpy (proc, mix,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
        processedPtrs[c] = proc;
    }

    // Iterate the effect chain. Each non-bypassed slot pumps in-place
    // through processedBuffer_ — on a miss, processedBuffer_ keeps the
    // dry signal from this slot (which becomes the input to the next
    // slot, or to the additive accumulate below if this was the last).
    const auto& entries = effectChain_.entries();
    for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
    {
        if (entries[slotIdx].bypassed) continue;

        // In-place: in and out both point at processedBuffer_. The host
        // is contractually required to read all input before writing any
        // output (or to copy through internal scratch); see
        // `IEffectChainHost::pumpSlot` docblock.
        (void) host_->pumpSlot (id_.value(), slotIdx,
                                processedPtrs, processedPtrs,
                                activeChannels, clampedSamples);
    }

    // M8 S4 — wet-capture tap. Default-off (null sink). When set, hand the
    // post-effects signal to the sink. Planar pointers into processedBuffer_;
    // stride is kMaxBusMixSamples per channel. lmcTime=0 parallels the dry
    // path. A false return (queue full / oversized) drops this buffer.
    // CEILING: the sink's per-message cap is kMaxTapeWriteMessageBytes (32 KB =
    // 4096 stereo float32 frames), which is LESS than kMaxBusMixSamples (8192).
    // So at activeChannels==2 a host buffer above 4096 frames returns false and
    // silently drops the wet buffer. Safe (no overflow), unreachable until the
    // M9+ production wiring drives the seam — revisit the cap then.
    if (wetSink_ != nullptr)
    {
        const float* wetPtrs[kMaxBusChannelsHard] {};
        for (int c = 0; c < activeChannels; ++c)
            wetPtrs[c] = processedBuffer_.data()
                       + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        (void) wetSink_->tryEnqueueWet (wetCaptureId_, Rational { 0 },
                                        wetPtrs, activeChannels, clampedSamples);
    }

    // Additively accumulate processedBuffer_ into output, then zero
    // mixBuffer_ for the next buffer (same shape as the inline path).
    for (int c = 0; c < activeChannels; ++c)
    {
        if (output[c] == nullptr) continue;

        const float* const proc = processedPtrs[c];
        for (int s = 0; s < clampedSamples; ++s)
            output[c][s] += proc[s];

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
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
