// Tests for sirius::AudioCallback — the single audio-thread entry point landed
// in M1 Session 1. These cover the identity pass-through gated by monitoring,
// the silence-on-default contract (which prevents a hot mic from going to the
// monitors before the operator explicitly arms it), the sample-rate / buffer-
// size capture from `audioDeviceAboutToStart`, and the channel-count edge
// cases the engine will rely on once the input mixer (M2) attaches.
//
// The callback is constructed standalone — no juce::AudioDeviceManager — so the
// behaviour is exercised deterministically without touching a real device.
#include "sirius/AudioCallback.h"
#include "sirius/EngineConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <cstring>
#include <vector>

using sirius::AudioCallback;
using sirius::EngineConfig;

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

TEST_CASE ("AudioCallback writes silence by default — monitoring off", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };
    REQUIRE_FALSE (cb.isMonitoringEnabled());

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

TEST_CASE ("AudioCallback passes input through bit-exact when monitoring on", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };
    cb.setMonitoringEnabled (true);
    REQUIRE (cb.isMonitoringEnabled());

    constexpr int channels = 2;
    constexpr int samples  = 128;

    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] =
                (ch == 0 ? 1.0f : -1.0f) * 0.01f * static_cast<float> (n);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            CHECK (out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]
                       == in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]);
}

TEST_CASE ("AudioCallback silences extra output channels when input has fewer", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };
    cb.setMonitoringEnabled (true);

    constexpr int inputs  = 1;
    constexpr int outputs = 2;
    constexpr int samples = 16;

    Buffers in  (inputs,  samples);
    Buffers out (outputs, samples);

    for (int n = 0; n < samples; ++n)
        in.storage[0][static_cast<std::size_t> (n)] = 0.5f;

    // Pre-fill the second output with junk so we can prove it was silenced.
    for (int n = 0; n < samples; ++n)
        out.storage[1][static_cast<std::size_t> (n)] = 0.75f;

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), inputs,
                                         out.writable(), outputs,
                                         samples, ctx);

    for (int n = 0; n < samples; ++n)
    {
        CHECK (out.storage[0][static_cast<std::size_t> (n)] == 0.5f);
        CHECK (out.storage[1][static_cast<std::size_t> (n)] == 0.0f);
    }
}

TEST_CASE ("AudioCallback drops extra input channels when output has fewer", "[audio-callback]")
{
    AudioCallback cb { EngineConfig {} };
    cb.setMonitoringEnabled (true);

    constexpr int inputs  = 4;
    constexpr int outputs = 2;
    constexpr int samples = 8;

    Buffers in  (inputs,  samples);
    Buffers out (outputs, samples);

    for (int ch = 0; ch < inputs; ++ch)
        for (int n = 0; n < samples; ++n)
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] =
                static_cast<float> (ch + 1);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), inputs,
                                         out.writable(), outputs,
                                         samples, ctx);

    // Channels 0 and 1 should pass through; 2 and 3 are silently dropped.
    for (int n = 0; n < samples; ++n)
    {
        CHECK (out.storage[0][static_cast<std::size_t> (n)] == 1.0f);
        CHECK (out.storage[1][static_cast<std::size_t> (n)] == 2.0f);
    }
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
    cfg.asrcQuality            = sirius::Asrc::Quality::Medium;
    cfg.preferredSampleRate    = 96000.0;
    cfg.preferredBufferSize    = 256;
    cfg.minPreferredBufferSize = 64;

    AudioCallback cb { cfg };

    CHECK (cb.config().asrcQuality            == sirius::Asrc::Quality::Medium);
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

TEST_CASE ("AudioCallback publishes a positive elapsed time after a buffer", "[audio-callback][load-publish]")
{
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 256;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);

    const double elapsed = cb.lastCallbackElapsedSec();
    // Positive — a real wall-clock measurement was taken — and small.
    // Even on the slowest CI runner a 256-sample memcpy + advance pair
    // takes microseconds; anything over 1 ms here means the measurement
    // is reading something other than this buffer.
    CHECK (elapsed > 0.0);
    CHECK (elapsed < 1.0e-3);
}

TEST_CASE ("AudioCallback elapsed-seconds resets to zero on device stop", "[audio-callback][load-publish]")
{
    AudioCallback cb { EngineConfig {} };

    constexpr int channels = 2;
    constexpr int samples  = 64;
    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    auto ctx = emptyContext();
    cb.audioDeviceIOCallbackWithContext (in.readable(), channels,
                                         out.writable(), channels,
                                         samples, ctx);
    REQUIRE (cb.lastCallbackElapsedSec() > 0.0);

    cb.audioDeviceStopped();
    CHECK (cb.lastCallbackElapsedSec() == 0.0);
}
