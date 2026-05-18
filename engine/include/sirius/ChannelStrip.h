#pragma once

#include "sirius/ProcessingChain.h"
#include "sirius/SignalType.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <utility>

namespace sirius
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
    ChannelStrip() noexcept : gainLinear_ (1.0f), panNormalized_ (0.5f) {}

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

    float gain() const noexcept { return gainLinear_.load (std::memory_order_relaxed); }
    float pan()  const noexcept { return panNormalized_.load (std::memory_order_relaxed); }

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

        const float g = gainLinear_.load (std::memory_order_relaxed);

        if (numChannels == 1)
        {
            float* const left = channelData[0];
            if (left == nullptr) return;
            for (int s = 0; s < numSamples; ++s)
                left[s] *= g;
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

        for (int s = 0; s < numSamples; ++s)
        {
            left[s]  *= leftGain;
            right[s] *= rightGain;
        }
    }

private:
    std::atomic<float> gainLinear_;
    std::atomic<float> panNormalized_;
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

} // namespace sirius
