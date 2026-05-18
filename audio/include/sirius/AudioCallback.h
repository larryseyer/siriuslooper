#pragma once

#include "sirius/EngineConfig.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstddef>

namespace sirius
{

/// The single audio-thread entry point for the standalone app — V7 white paper
/// Part V plus Part 5.6's realtime-safety contract, lowered to JUCE's
/// `AudioIODeviceCallback`.
///
/// Session 1 of M1 lands the skeleton: identity input→output pass-through
/// behind an explicit `Enable monitoring` gate (the gate exists because a
/// looper on stage with a hot mic and live monitoring is one slip away from
/// feedback). LMC sample-clock wiring (Session 2) and the existing engine
/// pieces — Asrc, OverloadProtection, AudioDeviceCalibration, RetroactiveRing
/// — (Session 3) attach to this same callback later in M1.
///
/// Realtime-safety invariants (V7 §5.6, codified for this class):
///  * No allocation, no lock acquisition, no synchronous I/O, no unbounded
///    loops in any method below other than the constructor/destructor.
///  * The monitoring flag is plain `std::atomic<bool>` written from the
///    message thread, read from the audio thread — no fence beyond the
///    default-acquire/release.
///  * Channel count is read once per buffer from the JUCE arguments; the
///    class never assumes a fixed layout.
class AudioCallback final : public juce::AudioIODeviceCallback
{
public:
    explicit AudioCallback (EngineConfig config) noexcept;
    ~AudioCallback() override = default;

    AudioCallback (const AudioCallback&) = delete;
    AudioCallback& operator= (const AudioCallback&) = delete;

    // -- juce::AudioIODeviceCallback ------------------------------------------------
    void audioDeviceIOCallbackWithContext (
        const float* const* inputChannelData,
        int                 numInputChannels,
        float* const*       outputChannelData,
        int                 numOutputChannels,
        int                 numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // -- message-thread surface ----------------------------------------------------
    /// Toggle pass-through monitoring. Default is `false` — the audio
    /// thread writes silence to output until monitoring is explicitly armed.
    void setMonitoringEnabled (bool enabled) noexcept
    {
        monitoringEnabled_.store (enabled, std::memory_order_release);
    }

    bool isMonitoringEnabled() const noexcept
    {
        return monitoringEnabled_.load (std::memory_order_acquire);
    }

    /// Sample rate JUCE last reported in `audioDeviceAboutToStart`. Zero
    /// before the first start and after a stop.
    double currentSampleRate() const noexcept
    {
        return currentSampleRate_.load (std::memory_order_acquire);
    }

    /// Buffer size JUCE last reported in `audioDeviceAboutToStart`. Zero
    /// before the first start and after a stop.
    int currentBufferSize() const noexcept
    {
        return currentBufferSize_.load (std::memory_order_acquire);
    }

    /// The EngineConfig this callback was constructed with. Immutable for
    /// the callback's lifetime — capability-tier changes (M11) construct a
    /// new callback rather than mutating an existing one.
    const EngineConfig& config() const noexcept { return config_; }

private:
    EngineConfig config_;

    std::atomic<bool>   monitoringEnabled_ { false };
    std::atomic<double> currentSampleRate_ { 0.0 };
    std::atomic<int>    currentBufferSize_ { 0 };
};

} // namespace sirius
