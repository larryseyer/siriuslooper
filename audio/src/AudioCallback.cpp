#include "ida/AudioCallback.h"

#include "ida/InputMixer.h"
#include "ida/IOttoRenderSource.h"
#include "ida/Lmc.h"
#include "ida/NotificationBus.h"
#include "ida/OutputMixer.h"
#include "ida/TapePrefetcher.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace ida
{

AudioCallback::AudioCallback (EngineConfig config) noexcept
    : config_ (config)
{
    // Pre-allocate the OTTO MIDI scratch ONCE (message thread, construction)
    // so OTTO's per-block `addEvent` never reallocates on the audio thread.
    // 8 KB ≈ hundreds of note events — far beyond any single block's output.
    ottoMidiScratch_.ensureSize (8192);
}

void AudioCallback::bindPlaybackSlot (int slot, TapePrefetcher* pre,
                                      float* scratchL, float* scratchR) noexcept
{
    if (slot < 0 || slot >= kMaxPhraseSlots) return;
    playbackSlots_[static_cast<std::size_t> (slot)] = { pre, scratchL, scratchR, false };
}

void AudioCallback::renderPlaybackStep (int numSamples) noexcept
{
    if (activeReads_ == nullptr) return;
    activeReads_->read (playbackSnapshot_);   // lock-free seqlock read into reused member

    std::array<bool, kMaxPhraseSlots> active {};
    for (int i = 0; i < playbackSnapshot_.count; ++i)
    {
        const auto& s = playbackSnapshot_.slots[static_cast<std::size_t> (i)];
        if (s.active && s.slot >= 0 && s.slot < kMaxPhraseSlots)
            active[static_cast<std::size_t> (s.slot)] = true;
    }

    for (int slot = 0; slot < kMaxPhraseSlots; ++slot)
    {
        auto& ps = playbackSlots_[static_cast<std::size_t> (slot)];
        if (ps.l == nullptr) continue;
        if (active[static_cast<std::size_t> (slot)] && ps.pre != nullptr)
        {
            ps.pre->pull (ps.l, ps.r, numSamples);  // fills + zero-fills underrun; wait-free
            ps.wasActive = true;
        }
        else if (ps.wasActive)
        {
            // Active→inactive: silence the scratch exactly once (not every block).
            std::fill (ps.l, ps.l + numSamples, 0.0f);
            std::fill (ps.r, ps.r + numSamples, 0.0f);
            ps.wasActive = false;
        }
    }
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
    // Denormal protection for the ENTIRE callback (input graph, OTTO render,
    // output graph + bus FX). Reverb/delay tails, decaying IIR filter state,
    // and fading signal all generate denormal floats; denormal arithmetic is
    // 10-100x slower on the CPU, which blows the buffer-time budget and causes
    // underruns (crackle). FTZ/DAZ flush them to zero. This is the single
    // top-level guard for the whole audio thread — `OttoHost::renderBlock`
    // also sets one for OTTO-standalone reuse; nesting is harmless.
    const juce::ScopedNoDenormals noDenormals;

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

    // Step 2b: S3c — drive OTTO's full per-block pump (housekeeping prefix
    // drains Play/Stop/TempoChange messages + advances conductor + dispatches
    // MIDI into sfizz + generates pattern MIDI; per-channel sum populates the
    // 32 stereo-pair buffers IDA's Output Mixer reads; housekeeping suffix
    // advances totalSamplePosition + syncs fillMode). RT-safe per OTTO's
    // CLAUDE.md. No-op when no source is attached (default for sessions /
    // tests without OTTO).
    // OTTO writes pattern-generated note events INTO this buffer during
    // playback. A fresh local `juce::MidiBuffer` would have zero capacity and
    // malloc on the FIRST addEvent of every note-bearing block — a per-block
    // audio-thread allocation that crackles. Reuse the pre-sized member and
    // clear() it (clearQuick keeps capacity, no alloc). IDA discards OTTO's
    // MIDI-out; the buffer exists only to receive it. (Feeding file-input /
    // external MIDI INTO OTTO is a future slice — design spec §7.)
    ottoMidiScratch_.clear();
    if (ottoRenderSource_ != nullptr)
        ottoRenderSource_->renderBlock (numSamples, ottoMidiScratch_);

    // Step 2c: T0b playback-resolution. Fill each sounding phrase channel's
    // stable scratch from its prefetch ring (lock-free seqlock snapshot read
    // with bounded expected-zero retries + wait-free ring pull + memcpy).
    // RT-safe: no alloc/lock/IO/decode/tree-walk.
    renderPlaybackStep (numSamples);

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

    // Step 3b: publish the master mix point's meter snapshot (S3a T3).
    // OutputMixer renders the master bus (BusId{0}) into hardware pair 0 —
    // i.e. outputChannelData[0,1] — so the finalized stereo master lives
    // there once `dispatchOutputMixer` returns. `publish()` is RT-safe
    // (atomic store of an RMS/peak snapshot, no allocation). Guarded
    // against headless / mono devices so we never deref a null channel
    // pointer; on miss we silently skip (no logging on the audio thread).
    if (numOutputChannels >= 2
        && outputChannelData[0] != nullptr
        && outputChannelData[1] != nullptr)
    {
        masterMeter_.publish (outputChannelData[0],
                              outputChannelData[1],
                              numSamples);
        // S3a T4 — also publish the master FFT spectrum from the same
        // finalized stereo master. Shares the guard with masterMeter_'s
        // publish; both are RT-safe (atomic stores, pre-allocated scratch).
        masterSpectrum_.publish (outputChannelData[0],
                                 outputChannelData[1],
                                 numSamples);
    }

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
        const double sampleRate   = device->getCurrentSampleRate();
        const int    maxBlockSize = device->getCurrentBufferSizeSamples();
        currentSampleRate_.store (sampleRate,   std::memory_order_release);
        currentBufferSize_.store (maxBlockSize, std::memory_order_release);

        // S3a T3 — prepare the master meter for the device's rate / block
        // size. Runs on the message thread (JUCE's device-setup callback),
        // before any audio block reaches the meter's `publish()`.
        masterMeter_.prepare (sampleRate, maxBlockSize);

        // S3a T4 — prepare the master spectrum (256 bins → 512-point FFT).
        // 256 bins matches the plan's hardcoded fast-default; if a UI ever
        // needs finer resolution it lands as a follow-on. Also message-
        // thread; allocates window + scratch + atomic bin vector.
        masterSpectrum_.prepare (sampleRate, maxBlockSize, 256);
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
