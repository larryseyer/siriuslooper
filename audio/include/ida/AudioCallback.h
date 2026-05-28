#pragma once

#include "ida/EngineConfig.h"
#include "ida/MasterMeter.h"

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
class IOttoRenderSource;

/// The single audio-thread entry point for the standalone app — V7 white paper
/// Part V plus Part 5.6's realtime-safety contract, lowered to JUCE's
/// `AudioIODeviceCallback`.
///
/// Session 1 of M1 landed the skeleton; V9 Slice 4 collapsed the global
/// `Enable monitoring` gate (the M1 raw bypass) — per-channel MON is now
/// driven through an auto-created OutputMixer channel reading the input's
/// post-strip buffer (whitepaper V9 §5.2 / §7.2; see InputMixer::
/// attachOutputMixer + setChannelMonitorMode). Session 2 wires the LMC
/// sample-clock — if an Lmc is attached,
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
///  * Channel count is read once per buffer from the JUCE arguments; the
///    class never assumes a fixed layout.
///
/// Threading contract — configure-before-audio-starts:
///  * All collaborator setters (`setInputMixer`, `setOutputMixer`,
///    `setLmc`, `setAsrcInputs`, `setAsrcOutputs`, `setCalibration`,
///    `setNotificationBus`) are SET-ONCE on the message thread BEFORE
///    `audioDeviceAboutToStart` is called. The audio thread reads the
///    collaborator pointers without synchronization — mutation during a
///    live callback is undefined behavior. Stop the device before
///    reconfiguring. Operator-facing mutation-during-audio surfaces are
///    a post-M5 concern and will require either a stop-callback-mutate-
///    restart guard or a real lock-free publish.
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

    /// Attach the engine-side mixers the audio thread drives. Set-once on
    /// the message thread before `audioDeviceAboutToStart`; non-owning.
    /// The mixers must outlive this callback (MainComponent guarantees
    /// this by declaring the AudioCallback after the mixers, so destruction
    /// unwinds in reverse).
    ///
    /// `outputMixer_` is `const` because `renderBuffer` is `const noexcept`
    /// (V7 plan line 386: "render is a function of state, not a state
    /// mutator"; all state mutation lives in the message-thread setters).
    /// `inputMixer_` stays non-const because `InputMixer::processBuffer`
    /// mutates internal state (TapeWriter queue push, overload reporting).
    void setInputMixer  (InputMixer*  mixer) noexcept   { inputMixer_  = mixer;  }
    void setOutputMixer (const OutputMixer* mixer) noexcept { outputMixer_ = mixer; }

    /// M-OTTO-4 slice 2 — attach the audio-thread OTTO render source.
    /// `audioDeviceIOCallbackWithContext` calls `renderBlock(numSamples)`
    /// once per buffer, BEFORE OutputMixer dispatch, so any OutputMixer
    /// channel sourced from OTTO's per-output buffers reads fresh
    /// audio. Set-once on the message thread before
    /// `audioDeviceAboutToStart`; non-owning (MainComponent owns the
    /// OttoHost and destroys the AudioCallback first). Pass `nullptr`
    /// to detach (default for sessions / tests without OTTO).
    void setOttoRenderSource (IOttoRenderSource* source) noexcept { ottoRenderSource_ = source; }

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

    /// S3a T3 — read-only access to the master mix point's meter snapshot
    /// publisher. The MasterMeter is owned by the AudioCallback as a value
    /// member, prepared in `audioDeviceAboutToStart`, and `publish()`'d at
    /// the end of every audio block from the finalized stereo output. UI
    /// consumers (TransportBar's master meter) call `getMasterMeter().snapshot()`
    /// on the message thread — that's an atomic load, no synchronization with
    /// the audio thread is required.
    const ida::MasterMeter& getMasterMeter() const noexcept { return masterMeter_; }

private:
    EngineConfig config_;
    Lmc*         lmc_ { nullptr };

    // Session 3 scaffolding: held but not invoked from the audio thread
    // for M1. M2-M8 grow real routing through these references; the
    // RT-safety audit row for each is filled in by Session 3's contract.
    std::vector<class Asrc*>             asrcInputs_;
    std::vector<class Asrc*>             asrcOutputs_;
    const AudioDeviceCalibration*        calibration_ { nullptr };

    // Collaborator pointers — set-once on the message thread before
    // `audioDeviceAboutToStart` and read by the audio thread without locks.
    InputMixer*         inputMixer_       { nullptr };
    const OutputMixer*  outputMixer_      { nullptr };
    NotificationBus*    notificationBus_  { nullptr };
    IOttoRenderSource*  ottoRenderSource_ { nullptr };

    std::atomic<double> currentSampleRate_       { 0.0 };
    std::atomic<int>    currentBufferSize_       { 0 };
    std::atomic<double> lastCallbackElapsedSec_  { 0.0 };

    // S3a T3 — value-member meter publisher driven from the master mix point.
    // Prepared on the message thread in `audioDeviceAboutToStart`; published
    // from the audio thread at the end of `audioDeviceIOCallbackWithContext`
    // after OutputMixer has written the finalized master stereo into
    // outputChannelData[0,1]. `publish()` is RT-safe (atomic store, no alloc).
    MasterMeter masterMeter_;
};

} // namespace ida
