#pragma once

#include "ida/EffectChain.h"
#include "ida/IEffectChainHost.h"
#include "ida/InternalFxId.h"
#include "ida/LufsMeter.h"
#include "ida/ProcessingChain.h"
#include "ida/SignalType.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace ida
{

/// π/2 — pan-law angle ceiling for the equal-power pan curve in
/// `ChannelStrip<Audio>::process`. File-scope `constexpr` because CLAUDE.md's
/// C++ section forbids magic numbers in the audio-thread hot loop.
inline constexpr float kHalfPi = 1.5707963267948966f;

/// Per-`SignalType` processing strip. M5 Session 1 lands this template as the
/// shared implementation between InputMixer and OutputMixer channels per V3
/// Step 7 + V7 alignment plan amendment §3 (specs/2026-05-18-m3-design.md).
///
/// The `Audio` specialization is the only one with a real body in M5; the
/// `Midi` / `Video` / `File` specializations are stubs that report their
/// `SignalType` and gain real bodies in M9 / M12 / M13 respectively. Each
/// specialization is a `final` concrete class so down-casting via
/// `signalType()` is unambiguous.
///
/// `ChannelStrip<SignalType::Audio>` supersedes the M3-era `AudioChain` —
/// `makeProcessingChain(SignalType::Audio)` now returns this template's
/// Audio specialization. The other M3-era chains (`MidiChain` / `VideoChain` /
/// `FileChain`) remain in `ProcessingChain.h` until their respective real-DSP
/// milestones land.
///
/// Audio-thread invariants (docs/RT_SAFETY_CONTRACT.md §6):
///   - `process(...)` is `noexcept`, allocation-free, lock-free, I/O-free.
///   - Setters (`setGain` / `setPan`) are message-thread only; they publish
///     via `std::atomic<float>` so the audio thread sees a coherent value
///     without locking. The audio thread loads each atomic exactly once per
///     `process()` call.
template <SignalType type>
class ChannelStrip;

/// Audio specialization — real gain + equal-power pan. EQ + dynamics are
/// declared out of scope for M5 Session 1 (spec line 429: "EQ / dynamics
/// DSP: stubs in M5"); a deferred sub-milestone fills them.
template <>
class ChannelStrip<SignalType::Audio> final : public ProcessingChain
{
public:
    /// Slice EC — auto-seeds the strip's `EffectChain` with an EQ slot at
    /// index 0 and a CMP slot at index 1 so every channel strip ships with
    /// the pro-audio rack convention (every console has EQ + CMP per
    /// input strip). Operator may still remove either via the insert
    /// popup; the EQ + CMP detail tabs show an "+ Add" empty-state when
    /// a slot is absent.
    ///
    /// Allocation: pushing two entries into the underlying vector can
    /// allocate, so the ctor is no longer `noexcept`. Channel strips are
    /// always constructed on the message thread (importGraphState,
    /// MainComponent setup) — never the audio thread — so this is safe.
    /// The bind-side dispatch happens later, in `setEffectChainHost` /
    /// `setEffectChain`.
    ChannelStrip()
        : gainLinear_ (1.0f), panNormalized_ (0.5f)
    {
        effectChain_ = effectChain_
                           .withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq))
                           .withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp));
    }

    SignalType signalType() const noexcept override { return SignalType::Audio; }

    /// Message-thread setter — publishes via atomic so the audio thread sees
    /// the new value on its next buffer without locking. `relaxed` is
    /// sufficient: each scalar is read independently by the audio thread and
    /// no companion data is being synchronized-with.
    void setGain (float linear) noexcept { gainLinear_.store (linear, std::memory_order_relaxed); }

    /// Message-thread setter — `normalized` is clamped to [0, 1] (0 = full
    /// left, 0.5 = center, 1 = full right). Clamping inside the setter keeps
    /// the audio-thread `cos`/`sin` arguments inside their well-defined range
    /// so a stray caller cannot inject NaN into the output buffer.
    void setPan (float normalized) noexcept
    {
        panNormalized_.store (std::clamp (normalized, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    /// Message-thread setter — stereo width via mid/side. `w` is clamped to
    /// [0, 2] (0 = mono-collapse, 1 = unchanged, 2 = double-wide), matching
    /// OTTO's MixerChannel width. Clamping in the setter (like `setPan`) keeps
    /// the audio-thread side-gain finite. Applied after pan, on the stereo path
    /// only — a mono buffer has no stereo field to widen.
    void setWidth (float w) noexcept
    {
        width_.store (std::clamp (w, 0.0f, 2.0f), std::memory_order_relaxed);
    }

    float gain()  const noexcept { return gainLinear_.load (std::memory_order_relaxed); }
    float pan()   const noexcept { return panNormalized_.load (std::memory_order_relaxed); }
    float width() const noexcept { return width_.load (std::memory_order_relaxed); }

    /// Message-thread mute setter. A muted strip emits silence and reports zero
    /// meters. `relaxed` for the same reason as gain/pan — an independent scalar
    /// the audio thread loads once per `process()`. Solo is a mixer-level policy
    /// (the mixer computes each strip's effective mute from the mute + solo
    /// button state); this flag is that effective mute.
    void setMuted (bool m) noexcept { muted_.store (m, std::memory_order_relaxed); }
    bool muted() const noexcept { return muted_.load (std::memory_order_relaxed); }

    /// Non-owning pointer to the mute atomic for collaborators that need to
    /// read the strip's mute state outside the strip's own `process()`. Today's
    /// only caller is `DirectLayer`, which stamps the pointer on a raw or
    /// processed route at message-thread route-registration time and reads it
    /// on the audio thread to skip accumulation when the strip is muted (the
    /// operator-facing kill-switch reading of mute, whitepaper §7 + the
    /// 2026-05-24 monitor-leak fix). Pointer lifetime is the strip's lifetime;
    /// `InputMixer::setChannelMonitorMode` removes the route before the strip
    /// is destroyed (channel removal), so the pointer is never dangling on the
    /// audio thread.
    const std::atomic<bool>* mutedAtomic() const noexcept { return &muted_; }

    /// Post-fader peak level for each side of the last processed block, in [0, ∞).
    /// The audio thread writes these once per `process()`; the UI reads them on
    /// its timer and applies its own meter ballistics. Stereo (the invariant);
    /// a mono block reports the same peak on both sides (dual-mono).
    float peakLeft()  const noexcept { return peakLeft_.load (std::memory_order_relaxed); }
    float peakRight() const noexcept { return peakRight_.load (std::memory_order_relaxed); }

    /// Message-thread setup for the per-channel EBU R128 loudness meter. Call
    /// before the audio thread runs `process` (e.g. when the device's sample
    /// rate is known / on device start). Allocates; not RT-safe. Until called,
    /// `lufsIntegrated()` reads silence and `process` skips the loudness work.
    void prepare (double sampleRate, int maxBlockSize) { lufsMeter_.prepare (sampleRate, maxBlockSize); }

    /// Short-term loudness (LUFS, 3 s window) of the post-fader signal — the
    /// LUFS half of the dual peak+LUFS channel meter (OTTO parity). This is
    /// what the UI meter reads on its timer: it tracks the live signal and
    /// self-zeroes to silence when audio stops.
    float lufsShortTerm() const noexcept { return lufsMeter_.getShortTerm(); }

    /// Integrated loudness (LUFS) of the post-fader signal — the canonical EBU
    /// R128 measurement (cumulative since reset). Used by tests/diagnostics,
    /// not the live meter feed.
    float lufsIntegrated() const noexcept { return lufsMeter_.getIntegrated(); }

    /// Message-thread setter — copies the insert chain in (routing-graph
    /// Phase 4). Set-once before the audio thread starts; mutating after
    /// start is a threading-contract violation (same collaborator contract
    /// as Bus::setEffectChain).
    ///
    /// P7 T3a-C: after the chain is stored, if a host is bound, sweeps
    /// every slot index up to `EffectChain::kMaxSlots` and re-binds each
    /// internal adapter (`kind == Internal`) or unbinds (`kind == Plugin |
    /// Empty` and any index past the chain's size) via
    /// `host_->setInternalFxAtSlot(...)`. Same shape as Bus::setEffectChain
    /// — see the docblock there for the unbind-on-non-Internal rationale.
    void setEffectChain (EffectChain chain)
    {
        effectChain_ = std::move (chain);
        dispatchAllSlotsToHost();
    }

    const EffectChain& effectChain() const noexcept { return effectChain_; }

    /// Message-thread setter — wires the audio-thread effect-chain dispatcher
    /// and the node's `pumpSlot` key. The strip does NOT own the host; the
    /// caller owns its lifetime AND guarantees `nodeKey` does not collide
    /// with any other node sharing the same host (channels and buses can
    /// collide on raw id values — the caller partitions the key space).
    /// Pass `nullptr` to disable dispatch — the pre-Phase-4 inline path runs
    /// unchanged. Set-once before the audio thread starts.
    void setEffectChainHost (IEffectChainHost* host, std::int64_t nodeKey)
    {
        host_    = host;
        nodeKey_ = nodeKey;
        // Slice EC — bind any pre-seeded chain (the ctor's EQ + CMP
        // default, or anything imported via setEffectChain before the
        // host was set) so the operator sees active adapters as soon as
        // the strip is wired. Inert when no chain or no host.
        dispatchAllSlotsToHost();
    }

    IEffectChainHost* effectChainHost() const noexcept { return host_; }

    /// Audio-thread DSP entry point. `channelData[c][s]` lays out
    /// non-interleaved float samples (matches JUCE's `AudioBuffer<float>`
    /// internal layout and `AudioIODeviceCallback`'s argument shape, so
    /// no copy is needed between AudioCallback / InputMixer scratch buffers
    /// and this call).
    ///
    /// Signature is JUCE-free deliberately — the engine layer's public API
    /// stays free of `juce_audio_basics` (see `engine/CMakeLists.txt` header
    /// comment); JUCE-side callers wrap their `AudioBuffer<float>` via
    /// `getArrayOfWritePointers()` before calling.
    ///
    /// Mono buffers (`numChannels == 1`) apply gain only; pan is ignored.
    /// Stereo buffers (`numChannels >= 2`) apply equal-power pan to the
    /// first two channels; any further channels are left untouched (M5 does
    /// not specify surround panning).
    ///
    /// `const` because `process` only mutates the caller's `channelData`
    /// buffer (and reads its own atomics); `*this` is logically const. Lets
    /// `OutputMixer::renderBuffer` (also `const`) invoke through a
    /// `const ChannelStrip*` without a deep-const escape hatch.
    void process (float* const* channelData, int numChannels, int numSamples) const noexcept
    {
        if (channelData == nullptr || numChannels <= 0 || numSamples <= 0) return;

        // Muted: emit silence and report zero meters. Cheap branch on the hot
        // path; the common (un-muted) case falls straight through.
        if (muted_.load (std::memory_order_relaxed))
        {
            for (int c = 0; c < numChannels; ++c)
                if (channelData[c] != nullptr)
                    for (int s = 0; s < numSamples; ++s)
                        channelData[c][s] = 0.0f;
            peakLeft_.store  (0.0f, std::memory_order_relaxed);
            peakRight_.store (0.0f, std::memory_order_relaxed);
            // Feed the loudness meter the (now silent) output so it decays.
            const float* r = (numChannels >= 2) ? channelData[1] : channelData[0];
            lufsMeter_.process (channelData[0], r, numSamples);
            return;
        }

        // Routing-graph Phase 4 — inserts run pre-fader (before gain/pan/
        // width) so the post-fader meter reflects the post-insert signal.
        // Inert (byte-identical to the pre-Phase-4 body) when no host is
        // bound or every slot is empty/bypassed.
        dispatchInserts (channelData, numChannels, numSamples);

        const float g = gainLinear_.load (std::memory_order_relaxed);

        if (numChannels == 1)
        {
            float* const left = channelData[0];
            if (left == nullptr) return;
            float peak = 0.0f;
            for (int s = 0; s < numSamples; ++s)
            {
                left[s] *= g;
                peak = std::max (peak, std::fabs (left[s]));
            }
            peakLeft_.store  (peak, std::memory_order_relaxed);
            peakRight_.store (peak, std::memory_order_relaxed);
            lufsMeter_.process (left, left, numSamples);   // mono → dual-mono
            return;
        }

        // Equal-power pan: at p=0.5, both gains = sqrt(0.5) ~ 0.707, which
        // preserves perceived loudness across the sweep. p=0 → left=1, right=0;
        // p=1 → left=0, right=1.
        const float p = panNormalized_.load (std::memory_order_relaxed);
        const float angle = p * kHalfPi;
        const float leftGain  = g * std::cos (angle);
        const float rightGain = g * std::sin (angle);

        float* const left  = channelData[0];
        float* const right = channelData[1];
        if (left == nullptr || right == nullptr) return;

        // Stereo width via mid/side, applied after pan. At w=1 the fold is the
        // identity (mid+side = L, mid-side = R), so the unity case skips it —
        // the pan-only output stays byte-identical and there is no per-sample
        // cost. The branch is on a value constant across the block.
        const float w = width_.load (std::memory_order_relaxed);
        const bool applyWidth = (w != 1.0f);

        float peakL = 0.0f, peakR = 0.0f;
        for (int s = 0; s < numSamples; ++s)
        {
            float l = left[s]  * leftGain;
            float r = right[s] * rightGain;
            if (applyWidth)
            {
                const float mid  = (l + r) * 0.5f;
                const float side = (l - r) * 0.5f * w;
                l = mid + side;
                r = mid - side;
            }
            left[s]  = l;
            right[s] = r;
            peakL = std::max (peakL, std::fabs (l));
            peakR = std::max (peakR, std::fabs (r));
        }
        peakLeft_.store  (peakL, std::memory_order_relaxed);
        peakRight_.store (peakR, std::memory_order_relaxed);
        lufsMeter_.process (left, right, numSamples);
    }

private:
    std::atomic<float> gainLinear_;
    std::atomic<float> panNormalized_;
    std::atomic<float> width_ { 1.0f };
    std::atomic<bool>  muted_ { false };
    // Metering is written from the const `process` (the strip is logically const
    // there), so the peak atomics + loudness meter are mutable. Audio-thread
    // writes, UI reads. The LufsMeter is the dual-meter's LUFS source (OTTO
    // parity); it self-no-ops until prepare() is called with the sample rate.
    mutable std::atomic<float> peakLeft_  { 0.0f };
    mutable std::atomic<float> peakRight_ { 0.0f };
    mutable LufsMeter          lufsMeter_;

    // Routing-graph Phase 4 — per-channel insert chain. `effectChain_` is the
    // ordered slot list (copy-in via setEffectChain); `host_` dispatches each
    // non-bypassed slot on the audio thread; `nodeKey_` is the host's
    // `pumpSlot` key for this strip. null `host_` (default) = the pre-Phase-4
    // path, byte-identical. Message-thread set-once; the audio thread only
    // reads.
    EffectChain       effectChain_;
    IEffectChainHost* host_    { nullptr };
    std::int64_t      nodeKey_ { 0 };

    /// Walks every slot index up to `EffectChain::kMaxSlots` and rebinds
    /// each non-empty Internal slot's adapter (or erases when the chain
    /// has no entry at that index / the entry is Plugin / Empty). Called
    /// from both `setEffectChain` (replace path) and `setEffectChainHost`
    /// (initial bind path so a pre-seeded chain wires up as soon as the
    /// host is attached). Inert if `host_ == nullptr`.
    void dispatchAllSlotsToHost()
    {
        if (host_ == nullptr) return;

        const auto& entries = effectChain_.entries();
        for (std::size_t slotIdx = 0; slotIdx < EffectChain::kMaxSlots; ++slotIdx)
        {
            if (slotIdx < entries.size()
                && entries[slotIdx].kind == EffectChainSlotKind::Internal)
            {
                host_->setInternalFxAtSlot (nodeKey_, slotIdx, entries[slotIdx].internalId);
                // P7 T5 slice 3 — propagate the entry's persisted bypass flag
                // (same shape as Bus::setEffectChain).
                host_->setInternalFxBypassAtSlot (nodeKey_, slotIdx, entries[slotIdx].bypassed);
            }
            else
            {
                host_->setInternalFxAtSlot (nodeKey_, slotIdx, std::nullopt);
                // nullopt erase already cleared the bypass entry.
            }
        }
    }

    /// Stereo invariant — inserts dispatch at most two channels.
    static constexpr int kMaxInsertChannels = 2;

    /// Audio-thread per-channel insert dispatch (routing-graph Phase 4).
    /// Runs each non-bypassed slot in ascending index order, in-place on
    /// `channelData` (in == out; the host contractually reads all input
    /// before writing any output). Early-out — and therefore byte-identical
    /// to the pre-Phase-4 body — when no host is bound or every slot is
    /// empty/bypassed. `noexcept`, allocation-free, lock-free: it only reads
    /// `host_`/`nodeKey_`/`effectChain_` (message-thread set-once) and calls
    /// the host's `noexcept` `pumpSlot`.
    void dispatchInserts (float* const* channelData, int numChannels,
                          int numSamples) const noexcept
    {
        if (host_ == nullptr) return;

        const auto& entries = effectChain_.entries();
        bool hasActiveSlot = false;
        for (const auto& e : entries)
            if (! e.bypassed) { hasActiveSlot = true; break; }
        if (! hasActiveSlot) return;

        const int insertChannels = std::min (numChannels, kMaxInsertChannels);
        for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
        {
            if (entries[slotIdx].bypassed) continue;
            // In-place: in and out both point at the caller's buffer. On a
            // miss (false), pumpSlot leaves channelData unchanged — the dry
            // signal carries to the next slot / the fader (1-buffer-delay).
            (void) host_->pumpSlot (nodeKey_, slotIdx,
                                    channelData, channelData,
                                    insertChannels, numSamples);
        }
    }
};

/// MIDI specialization — stub until M9 wires real UMP processing. Matches the
/// shape of the (still-present) `MidiChain` in ProcessingChain.h; both can
/// coexist until M9 supersedes one.
template <>
class ChannelStrip<SignalType::Midi> final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Midi; }
};

/// Video specialization — stub until M12.
template <>
class ChannelStrip<SignalType::Video> final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Video; }
};

/// File specialization — stub until M13.
template <>
class ChannelStrip<SignalType::File> final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::File; }
};

static_assert (noexcept (std::declval<ChannelStrip<SignalType::Audio>&>()
                             .process (static_cast<float* const*> (nullptr), 0, 0)),
               "ChannelStrip<Audio>::process must be noexcept (RT-safety contract §6)");

} // namespace ida
