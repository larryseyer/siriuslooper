#include "ida/Bus.h"

#include "ida/IWetCaptureSink.h"
#include "ida/Rational.h"

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
            // P7 T5 slice 3 — propagate the entry's persisted bypass flag.
            // setInternalFxAtSlot just reset the bypass entry to absent; this
            // call re-creates it iff bypassed==true (try_emplace short-circuit
            // path inside the host). The audio thread observes the new flag
            // on the next pumpSlot acquire-load.
            host_->setInternalFxBypassAtSlot (nodeKey, slotIdx, entries[slotIdx].bypassed);
        }
        else
        {
            host_->setInternalFxAtSlot (nodeKey, slotIdx, std::nullopt);
            // nullopt erase inside setInternalFxAtSlot already cleared any
            // bypass entry — no separate setInternalFxBypassAtSlot call needed.
        }
    }
}

void Bus::process (float* const* output, int numChannels, int numSamples) const noexcept
{
    // M7 S3 body, split into processInline / processChain helpers (P7 T3a M-2).
    // No host or empty/all-bypassed chain → processInline (M5 inline, default).
    // Host + at least one active slot → processChain (copy → pump → wet-tap →
    // post-fader gain → output accumulate). Meter writeback (peak + LUFS
    // publish) and mixBuffer_ zero stay here so they apply uniformly. On a
    // pumpSlot miss the slot's contribution is the dry mix already in
    // processedBuffer_ (pipelined 1-buffer delay per the M7 S3 design).
    if (numSamples <= 0) return;

    const int clampedSamples = std::min (numSamples,
                                         static_cast<int> (kMaxBusMixSamples));

    // Metering / output decoupling (2026-05-22 fix for the "bus meter dead on
    // direct-out" bug). Usable output → meter + write at the caller's width.
    // No usable output (e.g. renderInputGraph direct-out before the P8
    // bridge supplies a real buffer) → fall back to the bus's configured
    // width so metering still runs on the queued signal. Output writeback
    // is per-channel-guarded and silently skipped when unusable.
    const bool outputUsable  = (output != nullptr) && (numChannels > 0);
    const int  activeChannels = outputUsable
                                    ? std::min (numChannels, kMaxBusChannelsHard)
                                    : std::clamp (config_.channelCount, 1,
                                                  static_cast<int> (kMaxBusChannelsHard));

    bool hasActiveSlot = false;
    if (host_ != nullptr)
    {
        for (const auto& entry : effectChain_.entries())
        {
            if (! entry.bypassed) { hasActiveSlot = true; break; }
        }
    }

    float peaks[kMaxBusChannelsHard] = { 0.0f, 0.0f };
    const float* lufsBuf = nullptr;

    if (! hasActiveSlot)
    {
        processInline (output, outputUsable, activeChannels, clampedSamples, peaks);
        lufsBuf = mixBuffer_.data();
    }
    else
    {
        processChain (output, activeChannels, clampedSamples, peaks);
        lufsBuf = processedBuffer_.data();
    }

    // Meter writeback — peak + LUFS publish, common to both paths.
    peakLeft_.store  (peaks[0], std::memory_order_relaxed);
    peakRight_.store (activeChannels > 1 ? peaks[1] : peaks[0],
                      std::memory_order_relaxed);
    {
        const float* const lufsL = lufsBuf;
        const float* const lufsR = activeChannels > 1
                                       ? lufsBuf + kMaxBusMixSamples
                                       : lufsL;
        lufsMeter_.process (lufsL, lufsR, clampedSamples);   // mono → dual-mono
    }

    // Zero mixBuffer_ for the next buffer — common to both paths.
    for (int c = 0; c < activeChannels; ++c)
        std::memset (mixBuffer_.data() + static_cast<std::size_t> (c) * kMaxBusMixSamples,
                     0, static_cast<std::size_t> (clampedSamples) * sizeof (float));
}

void Bus::processInline (float* const* output, bool outputUsable,
                         int activeChannels, int clampedSamples,
                         float* peaksOut) const noexcept
{
    // Inline path — bit-for-bit equivalent to M5 Session 3 at default
    // gain 1.0 / unmuted. Gain and mute are loaded once per buffer. Applies
    // post-fader gain to mixBuffer_ in place so the caller's LUFS feed reads
    // post-fader values without needing a separate scratch.
    const float inlineGain = muted_.load (std::memory_order_relaxed)
                                 ? 0.0f
                                 : gainLinear_.load (std::memory_order_relaxed);
    for (int c = 0; c < activeChannels; ++c)
    {
        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        const bool canWrite = outputUsable && output[c] != nullptr;
        float p = 0.0f;
        for (int s = 0; s < clampedSamples; ++s)
        {
            const float v = mix[s] * inlineGain;
            mix[s] = v;                              // post-fader, in place for the LUFS feed
            p = std::max (p, std::fabs (v));
            if (canWrite) output[c][s] += v;
        }
        peaksOut[c] = p;
    }
}

void Bus::processChain (float* const* output,
                        int activeChannels, int clampedSamples,
                        float* peaksOut) const noexcept
{
    // Set up the per-channel processed scratch from the current bus mix.
    // processedBuffer_ uses the same channel-major layout as mixBuffer_
    // (stride = kMaxBusMixSamples per channel).
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
    // through processedBuffer_ — on a miss, processedBuffer_ keeps the dry
    // signal (becomes input to the next slot or the post-fader accumulate
    // below). In-place: in and out both point at processedBuffer_; the host
    // is contractually required to read all input before writing any output
    // — see `IEffectChainHost::pumpSlot` docblock.
    const auto& entries = effectChain_.entries();
    for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
    {
        if (entries[slotIdx].bypassed) continue;
        (void) host_->pumpSlot (id_.value(), slotIdx,
                                processedPtrs, processedPtrs,
                                activeChannels, clampedSamples);
    }

    // M8 S4 — wet-capture tap (pre-fader per the M8 S4 contract; MUST run
    // before the post-fader gain loop below). Default-off (null sink). A
    // false return (queue full / oversized) drops this buffer. CEILING: the
    // sink's per-message cap is kMaxTapeWriteMessageBytes (32 KB = 4096
    // stereo float32 frames), LESS than kMaxBusMixSamples (8192) — so at
    // activeChannels==2 a host buffer above 4096 frames silently drops the
    // wet buffer. Safe (no overflow), unreachable until M9+ production
    // wiring drives the seam — revisit the cap then.
    if (wetSink_ != nullptr)
    {
        const float* wetPtrs[kMaxBusChannelsHard] {};
        for (int c = 0; c < activeChannels; ++c)
            wetPtrs[c] = processedBuffer_.data()
                       + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        (void) wetSink_->tryEnqueueWet (wetCaptureId_, Rational { 0 },
                                        wetPtrs, activeChannels, clampedSamples);
    }

    // Post-fader gain/mute + output accumulate (in place on processedBuffer_
    // so the caller's LUFS feed reads post-fader values).
    const float chainGain = muted_.load (std::memory_order_relaxed)
                                ? 0.0f
                                : gainLinear_.load (std::memory_order_relaxed);
    for (int c = 0; c < activeChannels; ++c)
    {
        float* const proc = processedPtrs[c];
        float p = 0.0f;
        for (int s = 0; s < clampedSamples; ++s)
        {
            const float v = proc[s] * chainGain;
            proc[s] = v;
            p = std::max (p, std::fabs (v));
            if (output[c] != nullptr) output[c][s] += v;
        }
        peaksOut[c] = p;
    }
}

float* Bus::mixBufferChannel (int c) const noexcept
{
    if (c < 0 || c >= kMaxBusChannelsHard) return nullptr;
    return mixBuffer_.data()
         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
}

} // namespace sirius
