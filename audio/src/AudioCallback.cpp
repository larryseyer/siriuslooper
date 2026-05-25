#include "ida/AudioCallback.h"

#include "ida/InputMixer.h"
#include "ida/Lmc.h"
#include "ida/NotificationBus.h"
#include "ida/OutputMixer.h"

#include <cstdio>
#include <cstring>

namespace ida
{

AudioCallback::AudioCallback (EngineConfig config) noexcept
    : config_ (config)
{
}

namespace
{

/// OutputMixer render. Writes additively into the physical output buffers
/// (the AudioCallback zero-fills outputs at Step 1 so this can safely
/// accumulate). M5 default: OutputMixer is empty (no registered channels),
/// so this call is a hot-path no-op via the early-return inside
/// `renderBuffer`. V9 Slice 3 auto-creates a MON channel on the OutputMixer
/// whose audio source reads the matching InputMixer channel's post-strip
/// stereo buffer (whitepaper §5.2 / §7.2) — that channel renders here.
void dispatchOutputMixer (const OutputMixer*  mixer,
                          const float* const* inputChannelData,
                          int                 numInputChannels,
                          float* const*       outputChannelData,
                          int                 numOutputChannels,
                          int                 numSamples) noexcept
{
    if (mixer == nullptr) return;

    mixer->renderBuffer (inputChannelData,
                         numInputChannels,
                         outputChannelData,
                         numOutputChannels,
                         numSamples);
}

} // namespace

void AudioCallback::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int                 numInputChannels,
    float* const*       outputChannelData,
    int                 numOutputChannels,
    int                 numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Wall-clock at the start of the buffer — `getHighResolutionTicks` maps
    // to `mach_absolute_time` on Apple Silicon (userspace VDSO, ~10 ns,
    // RT-safe). End-of-buffer minus start gives the elapsed seconds we
    // publish for the non-RT load consumer to divide against the buffer-time
    // budget. The pair lives off the audio thread's hot work so it never
    // gates on a syscall or a lock.
    const auto startTicks = juce::Time::getHighResolutionTicks();

    // Step 1: zero all outputs. OutputMixer is additive — if no channel hits
    // an output, it must read as silence rather than whatever JUCE handed us.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            std::memset (outputChannelData[ch], 0,
                         static_cast<std::size_t> (numSamples) * sizeof (float));

    // Step 2: full input routing graph (tape subsystem slice 3). One pass does
    // strip processing + per-strip peak/LUFS metering + graph routing + per-tape
    // summing + ITapeSink delivery. Also fills each channel's post-strip stereo
    // buffer (V9 Slice 2 seam) which OutputMixer's MON channels read from in
    // Step 3 below. `directOut` is null/0: hardware-output routing is not active
    // by default; the operator's per-channel MON path lives in the OutputMixer.
    if (inputMixer_ != nullptr)
        inputMixer_->renderInputGraph (inputChannelData, numInputChannels,
                                       nullptr, 0, numSamples);

    // Step 3: OutputMixer render. Writes additively into the output buffers.
    // V9 Slice 3 auto-created MON channels read the InputMixer's post-strip
    // buffers and contribute the operator's processed-monitor signal here.
    // M5 default (no registered channels + no MON-on) makes this a fast no-op.
    dispatchOutputMixer (outputMixer_,
                         inputChannelData,
                         numInputChannels,
                         outputChannelData,
                         numOutputChannels,
                         numSamples);

    // Step 4: sample-clock to LMC (white paper §4.4). A single fetch_add +
    // store on the LMC's atomic pair. Re-loading the rate per buffer rather
    // than capturing it at start handles devices that change rate mid-session.
    if (lmc_ != nullptr)
        lmc_->advanceBySamples (numSamples,
                                currentSampleRate_.load (std::memory_order_acquire));

    // Step 5: publish the elapsed wall-clock for the buffer. The message
    // thread divides by `currentBufferSize() / currentSampleRate()` to get a
    // load fraction it then feeds into `OverloadProtection::reportLoad`. Done
    // last so the measurement covers all of the audio-thread work above.
    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - startTicks;
    const double elapsedSec = juce::Time::highResolutionTicksToSeconds (elapsedTicks);
    lastCallbackElapsedSec_.store (elapsedSec, std::memory_order_release);
}

void AudioCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // M6 Session 2 — engine→UI truthfulness post for the device-up event.
    // Built on the stack so the message buffer is bounded; NotificationBus::post
    // copies into its own 128-byte buffer with truncation, so any device name
    // longer than the prefix-plus-suffix budget is safely clipped. We don't go
    // through `juce::String::operator+` here because that allocates on the heap;
    // `snprintf` on a stack buffer is allocation-free and the equivalent in shape
    // to NotificationBus's own copy-into-fixed-array discipline.
    if (notificationBus_ != nullptr)
    {
        char msg[128];
        const char* deviceName = (device != nullptr)
            ? device->getName().toRawUTF8()
            : "(no device)";
        std::snprintf (msg, sizeof (msg),
                       "audio device started — %s", deviceName);
        notificationBus_->post (NotificationLevel::Info,
                                Category::DeviceEvent,
                                msg);
    }

    if (device != nullptr) {
        currentSampleRate_.store (device->getCurrentSampleRate(), std::memory_order_release);
        currentBufferSize_.store (device->getCurrentBufferSizeSamples(), std::memory_order_release);
    } else {
        currentSampleRate_.store (0.0, std::memory_order_release);
        currentBufferSize_.store (0,   std::memory_order_release);
    }
}

void AudioCallback::audioDeviceStopped()
{
    if (notificationBus_ != nullptr)
        notificationBus_->post (NotificationLevel::Info,
                                Category::DeviceEvent,
                                "audio device stopped");

    currentSampleRate_.store (0.0, std::memory_order_release);
    currentBufferSize_.store (0,   std::memory_order_release);
    lastCallbackElapsedSec_.store (0.0, std::memory_order_release);
}

} // namespace ida
