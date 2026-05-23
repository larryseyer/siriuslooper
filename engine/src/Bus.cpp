#include "sirius/Bus.h"

#include "sirius/IWetCaptureSink.h"
#include "sirius/Rational.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
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

Bus::Bus (Bus&& other) noexcept
    : id_ (other.id_),
      config_ (std::move (other.config_)),
      effectChain_ (std::move (other.effectChain_)),
      host_ (other.host_),
      wetSink_ (other.wetSink_),
      wetCaptureId_ (other.wetCaptureId_),
      mixBuffer_ (std::move (other.mixBuffer_)),
      processedBuffer_ (std::move (other.processedBuffer_)),
      gainLinear_ (other.gainLinear_.load (std::memory_order_relaxed)),
      muted_ (other.muted_.load (std::memory_order_relaxed)),
      peakLeft_ (other.peakLeft_.load (std::memory_order_relaxed)),
      peakRight_ (other.peakRight_.load (std::memory_order_relaxed)),
      lufsMeter_ (std::move (other.lufsMeter_))
{
}

void Bus::setEffectChain (EffectChain chain)
{
    // Message-thread only. Caller has detached the audio callback (same
    // collaborator contract as setEffectChainHost). Storing the chain first
    // means the host's internal-adapter table re-bind below reflects the
    // POST-mutation chain shape — important for the unbind-stale-slots
    // sweep that follows.
    effectChain_ = std::move (chain);

    if (host_ == nullptr)
        return;

    // P7 T3a-C — re-bind every potential slot. For each [0, kMaxSlots):
    //   * Internal slot → bind that adapter id (auto-prepared if host has
    //     been prepared).
    //   * Plugin / Empty slot, or any index past the chain's size → unbind
    //     (nullopt). The unbind on Plugin is defense-in-depth against the
    //     OOP host's configureBus jassert that the (nodeKey, slot) key is
    //     NOT simultaneously in both internalAdapters_ and instances_.
    //     The unbind on indices past chain.size() prevents orphan adapters
    //     from a previous, longer chain.
    const auto& entries = effectChain_.entries();
    const std::int64_t nodeKey = id_.value();
    for (std::size_t slotIdx = 0; slotIdx < EffectChain::kMaxSlots; ++slotIdx)
    {
        if (slotIdx < entries.size()
            && entries[slotIdx].kind == EffectChainSlotKind::Internal)
        {
            host_->setInternalFxAtSlot (nodeKey, slotIdx, entries[slotIdx].internalId);
        }
        else
        {
            host_->setInternalFxAtSlot (nodeKey, slotIdx, std::nullopt);
        }
    }
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
    if (numSamples <= 0) return;

    const int clampedSamples = std::min (numSamples,
                                         static_cast<int> (kMaxBusMixSamples));

    // Metering / output decoupling (2026-05-22 fix for operator-reported
    // "bus meter dead on direct-out" bug). When the caller hands us a usable
    // output buffer, meter and write at their requested width (existing
    // behavior). When they don't (e.g. InputMixer::renderInputGraph dispatching
    // a direct-out route before the P8 input→output bridge supplies a real
    // buffer — numDirectOutChannels == 0), fall back to the bus's own
    // configured channel width so metering still runs on the queued signal
    // in mixBuffer_. Output writeback is per-channel-guarded and silently
    // skipped when no usable destination is available.
    const bool outputUsable  = (output != nullptr) && (numChannels > 0);
    const int  activeChannels = outputUsable
                                    ? std::min (numChannels, kMaxBusChannelsHard)
                                    : std::clamp (config_.channelCount, 1,
                                                  static_cast<int> (kMaxBusChannelsHard));
    const auto canWriteOutput = [output, outputUsable, activeChannels] (int c) noexcept -> bool
    {
        return outputUsable && c < activeChannels && output[c] != nullptr;
    };

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
        // Inline path — bit-for-bit equivalent to M5 Session 3 at default
        // gain 1.0 / unmuted. Gain and mute are loaded once per buffer.
        const float inlineGain = muted_.load (std::memory_order_relaxed)
                                     ? 0.0f
                                     : gainLinear_.load (std::memory_order_relaxed);
        float inlinePeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
        for (int c = 0; c < activeChannels; ++c)
        {
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;
            float p = 0.0f;
            for (int s = 0; s < clampedSamples; ++s)
            {
                const float v = mix[s] * inlineGain;
                mix[s] = v;                              // post-fader, in place for the LUFS feed
                p = std::max (p, std::fabs (v));
                if (canWriteOutput (c)) output[c][s] += v;
            }
            inlinePeak[c] = p;
        }
        peakLeft_.store  (inlinePeak[0], std::memory_order_relaxed);
        peakRight_.store (activeChannels > 1 ? inlinePeak[1] : inlinePeak[0],
                          std::memory_order_relaxed);

        {
            const float* const lufsL = mixBuffer_.data();
            const float* const lufsR = activeChannels > 1
                                           ? mixBuffer_.data() + kMaxBusMixSamples
                                           : lufsL;
            lufsMeter_.process (lufsL, lufsR, clampedSamples);   // mono → dual-mono
        }
        for (int c = 0; c < activeChannels; ++c)
            std::memset (mixBuffer_.data() + static_cast<std::size_t> (c) * kMaxBusMixSamples,
                         0, static_cast<std::size_t> (clampedSamples) * sizeof (float));
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

    // Additively accumulate processedBuffer_ into output (post-fader gain +
    // mute applied here; wet capture above reads pre-fader per M8 S4 semantics),
    // then zero mixBuffer_ for the next buffer (same shape as the inline path).
    const float chainGain = muted_.load (std::memory_order_relaxed)
                                ? 0.0f
                                : gainLinear_.load (std::memory_order_relaxed);
    float chainPeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
    for (int c = 0; c < activeChannels; ++c)
    {
        float* const proc = processedPtrs[c];
        float p = 0.0f;
        for (int s = 0; s < clampedSamples; ++s)
        {
            const float v = proc[s] * chainGain;
            proc[s] = v;                                 // post-fader, in place for the LUFS feed
            p = std::max (p, std::fabs (v));
            if (output[c] != nullptr) output[c][s] += v;
        }
        chainPeak[c] = p;

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
    peakLeft_.store  (chainPeak[0], std::memory_order_relaxed);
    peakRight_.store (activeChannels > 1 ? chainPeak[1] : chainPeak[0],
                      std::memory_order_relaxed);
    {
        const float* const lufsL = processedPtrs[0];
        const float* const lufsR = activeChannels > 1 ? processedPtrs[1] : lufsL;
        lufsMeter_.process (lufsL, lufsR, clampedSamples);   // mono → dual-mono
    }
}

float* Bus::mixBufferChannel (int c) const noexcept
{
    if (c < 0 || c >= kMaxBusChannelsHard) return nullptr;
    return mixBuffer_.data()
         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
}

} // namespace sirius
