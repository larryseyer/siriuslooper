#include "sirius/AudioCallback.h"

#include "sirius/InputMixer.h"
#include "sirius/Lmc.h"
#include "sirius/OutputMixer.h"

#include <algorithm>
#include <cstring>

namespace
{
    // Maximum input/output channels supported by the scratch pre-allocation.
    // 32 is generous — the largest realistic Sirius target (multitrack
    // recording interface) is well under this. If a device with more channels
    // ever shows up, the active-count clamp keeps us safe; the extra channels
    // are silently dropped, which is the same shape as the existing
    // numOutputChannels > scratch overflow.
    constexpr int kMaxScratchChannels = 32;
}

namespace sirius
{

AudioCallback::AudioCallback (EngineConfig config) noexcept
    : config_ (config)
{
    rawInputScratch_.resize (kMaxScratchChannels,
                             RawInputBufferView { InputId (0), nullptr, 0 });
    outputScratch_  .resize (kMaxScratchChannels,
                             OutputBufferView   { OutputChannelId (0), nullptr, 0 });
}

namespace
{

/// Step 2 — InputMixer dispatch. Hands each device input channel to the
/// mixer as a byte view onto the live float buffer. Tape recording is
/// independent of the monitoring gate, so this runs unconditionally when an
/// InputMixer is attached. `processBuffer` is the M3 audio-thread surface;
/// it gracefully no-ops on channels it hasn't been told about.
void dispatchInputMixer (InputMixer*         mixer,
                         const float* const* inputChannelData,
                         int                 numInputChannels,
                         int                 numSamples) noexcept
{
    if (mixer == nullptr) return;

    const auto byteCount =
        static_cast<std::size_t> (numSamples) * sizeof (float);

    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        const auto* samples = inputChannelData[ch];
        if (samples == nullptr) continue;
        const auto* bytes = reinterpret_cast<const std::byte*> (samples);
        mixer->processBuffer (ChannelId (ch), bytes, byteCount);
    }
}

/// Step 3 — DirectLayer routing. Populates pre-allocated scratch views and
/// invokes `routeBuffers`. The scratch vectors are message-thread sized in
/// `audioDeviceAboutToStart`; this function only writes through `[]`,
/// never resizes. If the device reports more channels than the scratch
/// holds (shouldn't happen if start was called), the excess is clamped.
///
/// **ProcessedRoute is still passed an empty span post-M5.** `ChannelStrip<Audio>`
/// is real now (M5 S1 ships gain/pan on the input side), but `InputMixer`
/// applies the strip into a private scratch buffer and immediately memcpys
/// the result into the TapeWriter queue — it does NOT expose the post-strip
/// float buffer as a public surface. Wiring ProcessedRoute would require
/// adding either an audio-thread getter to InputMixer (post-strip float
/// view) or a parallel output pointer for the audio callback to capture.
/// Deferred from M5 to M6; OutputMixer's own processed path (Step 4) covers
/// the produced-mix surface in the meantime.
void dispatchDirectLayer (const DirectLayer*               layer,
                          std::vector<RawInputBufferView>& rawInputScratch,
                          std::vector<OutputBufferView>&   outputScratch,
                          int                              activeRawScratchCount,
                          int                              activeOutputScratchCount,
                          const float* const*              inputChannelData,
                          int                              numInputChannels,
                          float* const*                    outputChannelData,
                          int                              numOutputChannels,
                          int                              numSamples) noexcept
{
    if (layer == nullptr) return;

    const int rawCount = std::min (numInputChannels, activeRawScratchCount);
    for (int ch = 0; ch < rawCount; ++ch)
    {
        rawInputScratch[static_cast<std::size_t> (ch)].id          = InputId (ch);
        rawInputScratch[static_cast<std::size_t> (ch)].samples     = inputChannelData[ch];
        rawInputScratch[static_cast<std::size_t> (ch)].sampleCount = numSamples;
    }
    const int outCount = std::min (numOutputChannels, activeOutputScratchCount);
    for (int ch = 0; ch < outCount; ++ch)
    {
        outputScratch[static_cast<std::size_t> (ch)].id          = OutputChannelId (ch);
        outputScratch[static_cast<std::size_t> (ch)].samples     = outputChannelData[ch];
        outputScratch[static_cast<std::size_t> (ch)].sampleCount = numSamples;
    }

    layer->routeBuffers (
        std::span<const RawInputBufferView> (rawInputScratch.data(),
                                             static_cast<std::size_t> (rawCount)),
        std::span<const ProcessedChannelBufferView> {},
        std::span<const OutputBufferView> (outputScratch.data(),
                                           static_cast<std::size_t> (outCount)));
}

/// Step 4 — OutputMixer render. Writes additively into the same physical
/// output buffers as DirectLayer; the AudioCallback zero-fills outputs at
/// Step 1 so both paths can safely accumulate. Distinct from DirectLayer:
/// DirectLayer is the monitoring-gated BYPASS path (raw input → output);
/// OutputMixer is the PRODUCED-MIX path (channel → strip → sends → bus →
/// master → output). M5 default: OutputMixer is empty (no registered
/// channels), so this call is a hot-path no-op via the early-return inside
/// `renderBuffer`. Tests + M6+ Constituent rendering register channels to
/// activate the path.
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

    // Step 1: zero all outputs. DirectLayer is additive — if no route hits
    // an output, it must read as silence rather than whatever JUCE handed us.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            std::memset (outputChannelData[ch], 0,
                         static_cast<std::size_t> (numSamples) * sizeof (float));

    // Step 2: per-channel InputMixer dispatch. Runs regardless of the
    // monitoring gate — tape recording is independent of monitoring.
    dispatchInputMixer (inputMixer_,
                        inputChannelData,
                        numInputChannels,
                        numSamples);

    // Step 3: DirectLayer routing. Gated by monitoringEnabled_ — direct
    // monitoring is where the feedback risk lives. Tape recording (step 2)
    // is unaffected by the monitoring gate.
    const bool monitoring = monitoringEnabled_.load (std::memory_order_acquire);
    if (monitoring)
        dispatchDirectLayer (directLayer_,
                             rawInputScratch_,
                             outputScratch_,
                             activeRawScratchCount_,
                             activeOutputScratchCount_,
                             inputChannelData,
                             numInputChannels,
                             outputChannelData,
                             numOutputChannels,
                             numSamples);

    // Step 4: OutputMixer render. Writes additively into the same output
    // buffers as DirectLayer (Step 3) — DirectLayer is the bypass path,
    // OutputMixer is the produced-mix path. Runs unconditionally (no
    // monitoring gate) — the gate's feedback-risk concern applies only to
    // raw-input bypass; the OutputMixer's signal is post-strip /
    // post-Constituent and not a direct mic-to-speaker loop. M5 default
    // (no registered channels) makes this a fast no-op.
    dispatchOutputMixer (outputMixer_,
                         inputChannelData,
                         numInputChannels,
                         outputChannelData,
                         numOutputChannels,
                         numSamples);

    // Step 5: sample-clock to LMC (white paper §4.4). A single fetch_add +
    // store on the LMC's atomic pair. Re-loading the rate per buffer rather
    // than capturing it at start handles devices that change rate mid-session.
    if (lmc_ != nullptr)
        lmc_->advanceBySamples (numSamples,
                                currentSampleRate_.load (std::memory_order_acquire));

    // Step 6: publish the elapsed wall-clock for the buffer. The message
    // thread divides by `currentBufferSize() / currentSampleRate()` to get a
    // load fraction it then feeds into `OverloadProtection::reportLoad`. Done
    // last so the measurement covers all of the audio-thread work above.
    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - startTicks;
    const double elapsedSec = juce::Time::highResolutionTicksToSeconds (elapsedTicks);
    lastCallbackElapsedSec_.store (elapsedSec, std::memory_order_release);
}

void AudioCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Scratch is pre-sized in the constructor to kMaxScratchChannels; here we only
    // RECORD how many slots the current device actually exercises. Zero allocation,
    // zero throw — the audio thread sees a stable vector pointing at message-thread
    // memory whose capacity was reserved before any callback could fire.
    if (device != nullptr) {
        // BigInteger::getHighestBit() returns -1 for an empty active-channel mask
        // (e.g. mid-reconfigure). +1 gives 0; the std::max(0, ...) clamp is the
        // belt-and-suspenders that survives any future API drift. Empty → scratch
        // stays effectively unused via activeRawScratchCount_ = 0.
        const int rawCount = std::max (0, device->getActiveInputChannels().getHighestBit() + 1);
        const int outCount = std::max (0, device->getActiveOutputChannels().getHighestBit() + 1);
        activeRawScratchCount_    = std::min (rawCount, kMaxScratchChannels);
        activeOutputScratchCount_ = std::min (outCount, kMaxScratchChannels);
        currentSampleRate_.store (device->getCurrentSampleRate(), std::memory_order_release);
        currentBufferSize_.store (device->getCurrentBufferSizeSamples(), std::memory_order_release);
    } else {
        activeRawScratchCount_    = 0;
        activeOutputScratchCount_ = 0;
        currentSampleRate_.store (0.0, std::memory_order_release);
        currentBufferSize_.store (0,   std::memory_order_release);
    }
}

void AudioCallback::audioDeviceStopped()
{
    currentSampleRate_.store (0.0, std::memory_order_release);
    currentBufferSize_.store (0,   std::memory_order_release);
    lastCallbackElapsedSec_.store (0.0, std::memory_order_release);
}

} // namespace sirius
