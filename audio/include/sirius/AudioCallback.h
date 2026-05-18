#pragma once

#include "sirius/EngineConfig.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace sirius
{

class Lmc;
class AudioDeviceCalibration;

/// The single audio-thread entry point for the standalone app — V7 white paper
/// Part V plus Part 5.6's realtime-safety contract, lowered to JUCE's
/// `AudioIODeviceCallback`.
///
/// Session 1 of M1 landed the skeleton: identity input→output pass-through
/// behind an explicit `Enable monitoring` gate (the gate exists because a
/// looper on stage with a hot mic and live monitoring is one slip away from
/// feedback). Session 2 wires the LMC sample-clock — if an Lmc is attached,
/// every buffer ends with a single `lmc->advanceBySamples(...)` call so the
/// LMC tracks the device's hardware-counted sample-clock (white paper §4.4).
/// Session 3 attaches the remaining engine pieces (Asrc, AudioDeviceCalibration)
/// as held-but-not-invoked references — the scaffolding M3-M5 routes through —
/// and publishes the per-buffer wall-clock elapsed time so a non-RT consumer
/// (`OverloadProtection` on the message thread) can derive a load fraction
/// without the audio thread ever calling the throwing `reportLoad` API.
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

    /// Attach a Logical Master Clock for sample-clock advancement. Set on
    /// the message thread before the device starts and not changed for the
    /// callback's lifetime. Non-owning — the LMC must outlive this callback,
    /// which `MainComponent` guarantees by destroying the callback first.
    /// Pass `nullptr` to detach (used in tests).
    void setLmc (Lmc* lmc) noexcept { lmc_ = lmc; }

    /// Attach per-channel input ASRCs (M1 Session 3 scaffolding). Held but
    /// not invoked from the audio thread for M1 — M2 routes input through
    /// them. Set once on the message thread before the device starts; the
    /// audio thread reads `asrcInputs_[ch]` by index without locking. Each
    /// pointer is non-owning; the ASRCs must outlive this callback. Pass
    /// an empty vector to detach.
    void setAsrcInputs (std::vector<class Asrc*> asrcsByChannel) noexcept
    {
        asrcInputs_ = std::move (asrcsByChannel);
    }

    /// Attach per-channel output ASRCs (M1 Session 3 scaffolding). Same
    /// ownership / lifetime contract as `setAsrcInputs`.
    void setAsrcOutputs (std::vector<class Asrc*> asrcsByChannel) noexcept
    {
        asrcOutputs_ = std::move (asrcsByChannel);
    }

    /// Attach the audio-device calibration (M1 Session 3 scaffolding).
    /// Held but not invoked from the audio thread for M1 — M8 reads it
    /// to convert between device-native and LMC time once a measured
    /// calibration replaces the identity default. Non-owning; the
    /// calibration must outlive this callback. Pass `nullptr` to detach.
    void setCalibration (const AudioDeviceCalibration* calibration) noexcept
    {
        calibration_ = calibration;
    }

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

    /// Wall-clock seconds the most recent buffer's callback took, end-to-end.
    /// Zero before the first buffer and after `audioDeviceStopped`. The
    /// non-RT consumer (`MainComponent`'s 30 Hz timer) divides this by the
    /// buffer-time budget (`currentBufferSize() / currentSampleRate()`) to
    /// derive an `OverloadProtection` load fraction. Doing the division off
    /// the audio thread keeps the audio thread's contribution to a single
    /// `mach_absolute_time` pair + one `std::atomic<double>` store per buffer.
    double lastCallbackElapsedSec() const noexcept
    {
        return lastCallbackElapsedSec_.load (std::memory_order_acquire);
    }

private:
    EngineConfig config_;
    Lmc*         lmc_ { nullptr };

    // Session 3 scaffolding: held but not invoked from the audio thread
    // for M1. M2-M8 grow real routing through these references; the
    // RT-safety audit row for each is filled in by Session 3's contract.
    std::vector<class Asrc*>             asrcInputs_;
    std::vector<class Asrc*>             asrcOutputs_;
    const AudioDeviceCalibration*        calibration_ { nullptr };

    std::atomic<bool>   monitoringEnabled_       { false };
    std::atomic<double> currentSampleRate_       { 0.0 };
    std::atomic<int>    currentBufferSize_       { 0 };
    std::atomic<double> lastCallbackElapsedSec_  { 0.0 };
};

} // namespace sirius
