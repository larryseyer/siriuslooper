#pragma once

#include "ida/DirectLayer.h"
#include "ida/EngineConfig.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace ida
{

class Lmc;
class AudioDeviceCalibration;
class InputMixer;
class OutputMixer;
class NotificationBus;

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
///
/// Threading contract — configure-before-audio-starts:
///  * All collaborator setters (`setInputMixer`, `setOutputMixer`,
///    `setDirectLayer`, `setLmc`, `setAsrcInputs`, `setAsrcOutputs`,
///    `setCalibration`, `setNotificationBus`) are SET-ONCE on the message
///    thread BEFORE `audioDeviceAboutToStart` is called. The audio thread
///    reads the collaborator pointers without synchronization — mutation
///    during a live callback is undefined behavior. Stop the device before
///    reconfiguring.
///  * Inherited from `DirectLayer.h`'s contract (continue.md M4
///    constraint #6); M5 OutputMixer extends it. Operator-facing
///    mutation-during-audio surfaces are a post-M5 concern and will
///    require either a stop-callback-mutate-restart guard or a real
///    lock-free publish.
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

    /// M4 Session 3 — attach the engine-side mixers and DirectLayer the
    /// audio thread now drives. Set-once on the message thread before
    /// `audioDeviceAboutToStart`; non-owning. The mixers/layer must outlive
    /// this callback (MainComponent guarantees this by declaring the
    /// AudioCallback after the mixers, so destruction unwinds in reverse).
    ///
    /// `directLayer_` is `const` because M4 Session 2's `routeBuffers` is
    /// const and the audio thread never registers routes. `outputMixer_`
    /// is `const` for the same reason — M5 Session 3's `renderBuffer` is
    /// `const noexcept` (V7 plan line 386: "render is a function of state,
    /// not a state mutator"; all state mutation lives in the message-
    /// thread setters). `inputMixer_` stays non-const because
    /// `InputMixer::processBuffer` mutates internal state (TapeWriter
    /// queue push, overload reporting).
    void setInputMixer  (InputMixer*  mixer) noexcept   { inputMixer_  = mixer;  }
    void setOutputMixer (const OutputMixer* mixer) noexcept { outputMixer_ = mixer; }
    void setDirectLayer (const DirectLayer* layer) noexcept { directLayer_ = layer; }

    /// M6 Session 2 — attach the engine→UI truthfulness channel (V5 §8.6).
    /// The audio thread posts `DeviceEvent` notifications from
    /// `audioDeviceAboutToStart` and `audioDeviceStopped` (both of which run
    /// on JUCE's device-setup callback, not the render thread, but still
    /// must remain allocation-free per the class contract). `NotificationBus::post`
    /// is `noexcept` and allocation-free in either direction, so the contract
    /// holds even if a future JUCE backend invokes these on the render thread.
    /// Set-once on the message thread before `audioDeviceAboutToStart`;
    /// non-owning. The bus must outlive this callback (MainComponent owns
    /// the bus and destroys the callback first).
    void setNotificationBus (NotificationBus* bus) noexcept { notificationBus_ = bus; }

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

    // M4 Session 3 — collaborator pointers and pre-allocated scratch for
    // DirectLayer::routeBuffers. Per the DirectLayer caller contract, span
    // storage must NOT allocate on the audio thread; these vectors are sized
    // in `audioDeviceAboutToStart` (message thread) and only index-mutated
    // (never resized) by the callback. ProcessedChannelBufferView scratch is
    // intentionally absent — M4 wires only the RawRoute path; see the comment
    // next to the empty processedChannels span in AudioCallback.cpp.
    InputMixer*        inputMixer_  { nullptr };
    const OutputMixer* outputMixer_ { nullptr };
    const DirectLayer* directLayer_ { nullptr };
    NotificationBus*   notificationBus_ { nullptr };
    std::vector<RawInputBufferView> rawInputScratch_;
    std::vector<OutputBufferView>   outputScratch_;
    int activeRawScratchCount_    { 0 };
    int activeOutputScratchCount_ { 0 };

    std::atomic<bool>   monitoringEnabled_       { false };
    std::atomic<double> currentSampleRate_       { 0.0 };
    std::atomic<int>    currentBufferSize_       { 0 };
    std::atomic<double> lastCallbackElapsedSec_  { 0.0 };
};

} // namespace ida
