// Tests for ida::AudioCallback — the single audio-thread entry point. Covers:
//   * the silence-on-default contract (which prevents a hot mic from going to
//     the monitors before any output source is wired);
//   * the sample-rate / buffer-size capture from `audioDeviceAboutToStart`;
//   * the load-publish path (lastCallbackElapsedSec);
//   * tape subsystem slice 3: the callback drives renderInputGraph, so a
//     tape-routed channel reaches its ITapeSink in a real callback cycle.
//
// The callback is constructed standalone — no juce::AudioDeviceManager — so the
// behaviour is exercised deterministically without touching a real device.
//
// V9 Slice 4 deleted the DirectLayer module and the V8 global monitoring gate.
// The V8 DirectLayer-routing integration tests (identity pass-through behind
// `setMonitoringEnabled`) are gone with it; the equivalent V9 path (MON →
// auto-created OutputMixer channel reading the input's post-strip buffer) is
// covered end-to-end by InputMixerMonOutputChannelTests +
// InputMixerMonitorModeLifecycleTests.
#include "ida/AudioCallback.h"
#include "ida/EngineConfig.h"
#include "ida/InputMixer.h"
#include "ida/IOttoRenderSource.h"
#include "ida/ITapeSink.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"
#include "ida/TapeId.h"
#include "ida/TapeMode.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <cstring>
#include <vector>

using ida::AudioCallback;
using ida::EngineConfig;
using ida::InputId;
using ida::InputMixer;

namespace
{
    // A minimal pointer-of-pointers fixture so the test exercises the same
    // shape JUCE delivers: `float* const*` per channel, all the same length.
    struct Buffers
    {
        explicit Buffers (int numChannels, int numSamples)
            : storage (static_cast<std::size_t> (numChannels),
                       std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f)),
              pointers (static_cast<std::size_t> (numChannels), nullptr)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                pointers[static_cast<std::size_t> (ch)] =
                    storage[static_cast<std::size_t> (ch)].data();
        }

        float* const*       writable() { return pointers.data(); }
        const float* const* readable() const
        {
            return const_cast<const float* const*> (pointers.data());
        }

        std::vector<std::vector<float>> storage;
        std::vector<float*>             pointers;
    };

    juce::AudioIODeviceCallbackContext emptyContext()
    {
        juce::AudioIODeviceCallbackContext ctx;
        ctx.hostTimeNs = nullptr;
        return ctx;
    }
}

TEST_CASE ("AudioCallback writes silence by default — no source wired", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 64;

    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    // Fill input with a known non-zero pattern and pre-fill output with junk
    // so we can prove the callback wrote zeros rather than leaving it alone.
    for (int ch = 0; ch < channels; ++ch)
    {
        for (int n = 0; n < samples; ++n)
        {
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] =
                0.25f * static_cast<float> (n + 1);
            out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = 0.9f;
        }
    }

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            CHECK (out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] == 0.0f);
}

TEST_CASE ("AudioCallback exposes sample rate and buffer size after device start", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };
    REQUIRE (cb.currentSampleRate() == 0.0);
    REQUIRE (cb.currentBufferSize() == 0);

    // A null device is the operator-pulled-the-plug case — the callback must
    // clear state, not propagate a sentinel from somewhere else.
    cb.audioDeviceAboutToStart (nullptr);
    CHECK (cb.currentSampleRate() == 0.0);
    CHECK (cb.currentBufferSize() == 0);

    cb.audioDeviceStopped();
    CHECK (cb.currentSampleRate() == 0.0);
    CHECK (cb.currentBufferSize() == 0);
}

TEST_CASE ("AudioCallback carries the EngineConfig it was constructed with", "[audio-callback]")
{
    EngineConfig cfg;
    cfg.asrcQuality            = ida::Asrc::Quality::Medium;
    cfg.preferredSampleRate    = 96000.0;
    cfg.preferredBufferSize    = 256;
    cfg.minPreferredBufferSize = 64;

    AudioCallback cb { cfg };

    CHECK (cb.config().asrcQuality            == ida::Asrc::Quality::Medium);
    CHECK (cb.config().preferredSampleRate    == 96000.0);
    CHECK (cb.config().preferredBufferSize    == 256u);
    CHECK (cb.config().minPreferredBufferSize == 64u);
}

// =============================================================================
// M1 Session 3 — load-publish path
//
// The audio thread measures wall-clock elapsed per buffer and publishes it via
// `lastCallbackElapsedSec()`. MainComponent's 30 Hz timer divides this by the
// buffer-time budget (currentBufferSize / currentSampleRate) to derive the
// load fraction it then hands to OverloadProtection::reportLoad — keeping the
// throwing API entirely off the audio thread.
//
// These tests exercise the publish side directly (no JUCE device needed).
// =============================================================================

TEST_CASE ("AudioCallback elapsed-seconds is zero before any callback fires", "[audio-callback][load-publish]")
{
    AudioCallback cb { EngineConfig {} };
    CHECK (cb.lastCallbackElapsedSec() == 0.0);
}

// Pumps the callback up to `maxAttempts` times against `samples`-sample buffers
// and returns the last published elapsed. The platform clock granularity
// (mach_absolute_time on Apple Silicon resolves in tens of nanoseconds — but
// JUCE's tick conversion can round a sub-tick callback to zero) means a single
// tiny callback can register as elapsed == 0 even when work happened. Looping
// gives the clock something to catch.
double pumpUntilPositive (AudioCallback& cb,
                          Buffers& in, Buffers& out, int samples,
                          int maxAttempts = 1000)
{
    auto ctx = emptyContext();
    const int channels = static_cast<int> (in.pointers.size());
    for (int i = 0; i < maxAttempts; ++i)
    {
        cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                             out.writable(), channels,
                                             samples, ctx);
        if (cb.lastCallbackElapsedSec() > 0.0)
            return cb.lastCallbackElapsedSec();
    }
    return cb.lastCallbackElapsedSec();
}

TEST_CASE ("AudioCallback publishes a positive elapsed time after a buffer", "[audio-callback][load-publish]")
{
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 256;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    const double elapsed = pumpUntilPositive (cb, in, out, samples);
    // Positive — a real wall-clock measurement was taken — and bounded.
    // 100 ms ceiling rules out garbage values (e.g., misinterpreted scale
    // factor) without being flaky on cold caches or contended CI.
    CHECK (elapsed > 0.0);
    CHECK (elapsed < 0.1);
}

TEST_CASE ("AudioCallback elapsed-seconds resets to zero on device stop", "[audio-callback][load-publish]")
{
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 64;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    REQUIRE (pumpUntilPositive (cb, in, out, samples) > 0.0);

    cb.audioDeviceStopped();
    CHECK (cb.lastCallbackElapsedSec() == 0.0);
}

// =============================================================================
// Tape subsystem slice 3: the callback drives renderInputGraph
//
// Before slice 3, the callback called processBuffer (M3 legacy path) + a
// separate processDeviceInputs metering pass. Slice 3 replaces both with a
// single renderInputGraph call. This test verifies the end-to-end path: a
// tape-routed channel registered in InputMixer reaches its ITapeSink in an
// actual audioDeviceIOCallbackWithContext cycle.
// =============================================================================

// =============================================================================
// M-OTTO-4 slice 2 — IOttoRenderSource drive
//
// The audio callback owns the "pump OTTO's mixer once per buffer" call so
// downstream OutputMixer channels that source OTTO audio can read fresh
// per-output buffers. Verified by attaching a counting stub source.
// =============================================================================

TEST_CASE ("AudioCallback invokes IOttoRenderSource::renderBlock once per buffer with the buffer's numSamples",
           "[audio-callback][otto-render]")
{
    struct CountingSource : ida::IOttoRenderSource
    {
        int  calls               = 0;
        int  lastNumSamples      = -1;
        void renderBlock (int numSamples, juce::MidiBuffer&) noexcept override
        {
            ++calls;
            lastNumSamples = numSamples;
        }
    } source;

    AudioCallback cb { EngineConfig {} };
    cb.setOttoRenderSource (&source);

    constexpr int channels = 2;
    constexpr int samples  = 128;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);

    CHECK (source.calls          == 1);
    CHECK (source.lastNumSamples == samples);

    // A second callback with a different block size lands too — slice 2's
    // wiring is per-buffer, not one-shot.
    constexpr int samples2 = 64;
    Buffers in2  (channels, samples2);
    Buffers out2 (channels, samples2);
    cb.audioDeviceIOCallbackWithContext (in2.readable(), channels,
                                         out2.writable(), channels,
                                         samples2, ctx);

    CHECK (source.calls          == 2);
    CHECK (source.lastNumSamples == samples2);
}

TEST_CASE ("AudioCallback skips IOttoRenderSource drive when no source is attached",
           "[audio-callback][otto-render]")
{
    // No setOttoRenderSource call. The callback must not crash and must
    // not allocate a source on the fly — the nullptr branch is the
    // configure-before-audio-starts default for sessions without OTTO.
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 64;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);

    // SUCCESS: no crash, no UB. The silence-by-default contract above
    // already pins the output behaviour; this case pins the OTTO opt-out.
}

TEST_CASE ("AudioCallback drives renderInputGraph: a tape-routed channel reaches the sink",
           "[audio-callback][render]")
{
    using ida::SignalType;
    using ida::TapeId;
    using ida::TapeMode;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId { 0 }, SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);
    mixer.setChannelInputSource (ch, 0, 1, /*stereo*/ true);
    // Default main-out after addChannel is Tape (primary tape, TapeId 1);
    // an explicit setChannelMainOutToTape call is not required but is
    // harmless — we call it here to make the intent explicit for readers.
    REQUIRE (mixer.setChannelMainOutToTape (ch));

    struct Sink : ida::ITapeSink {
        bool         got    = false;
        std::int64_t tapeId = -1;
        void deliverTapeBlock (ida::TapeId t, const float*, const float*,
                               int n) noexcept override
        {
            got = (n > 0);
            tapeId = t.value();
        }
    } sink;
    mixer.setTapeSink (&sink);

    AudioCallback cb { EngineConfig {} };
    cb.setInputMixer (&mixer);

    constexpr int n = 64;
    Buffers in  (2, n);
    Buffers out (2, n);

    // Fill input with a non-zero signal so the strip has something to process.
    for (int s = 0; s < n; ++s)
    {
        in.storage[0][static_cast<std::size_t> (s)] = 0.3f;
        in.storage[1][static_cast<std::size_t> (s)] = 0.3f;
    }

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), 2, out.writable(), 2, n, ctx);

    CHECK (sink.got);
    CHECK (sink.tapeId == 1); // primary tape
}
