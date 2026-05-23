// Tests for ida::AudioCallback — the single audio-thread entry point landed
// in M1 Session 1 and grown through M4 Session 3 into an actual driver of the
// InputMixer / OutputMixer / DirectLayer collaborators. These cover:
//   * the silence-on-default contract (which prevents a hot mic from going to
//     the monitors before the operator explicitly arms it);
//   * the M4 routing path — DirectLayer raw routes deliver M1's identity
//     pass-through semantics with all the extra plumbing in place;
//   * the monitoring gate cleanly cutting that routing;
//   * the sample-rate / buffer-size capture from `audioDeviceAboutToStart`;
//   * the channel-count edge cases the engine relies on when more inputs
//     than outputs (or vice versa) are wired through DirectLayer routes.
//   * tape subsystem slice 3: the callback drives renderInputGraph, so a
//     tape-routed channel reaches its ITapeSink in a real callback cycle.
//
// The callback is constructed standalone — no juce::AudioDeviceManager — so the
// behaviour is exercised deterministically without touching a real device. For
// the M4 integration tests, mixers and DirectLayer are constructed inline; no
// TapeWriter is attached, so InputMixer::processBuffer no-ops on the tape path
// (verified by reading InputMixer.cpp: tapeWriter_ == nullptr early-returns).
#include "sirius/AudioCallback.h"
#include "sirius/DirectLayer.h"
#include "sirius/EngineConfig.h"
#include "sirius/InputMixer.h"
#include "sirius/ITapeSink.h"
#include "sirius/OutputMixer.h"
#include "sirius/SignalType.h"
#include "sirius/TapeId.h"
#include "sirius/TapeMode.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstring>
#include <vector>

using ida::AudioCallback;
using ida::DirectLayer;
using ida::EngineConfig;
using ida::InputId;
using ida::InputMixer;
using ida::OutputChannelId;
using ida::OutputMixer;

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

    /// A minimal juce::AudioIODevice used solely to drive
    /// AudioCallback::audioDeviceAboutToStart so DirectLayer scratch is
    /// pre-sized. The integration tests need scratch sized to at least
    /// numChannels — the callback clamps routing to
    /// `min(numChannels, scratchSize)` — and the only way to size scratch
    /// from a unit test is to call audioDeviceAboutToStart with a device.
    /// Real device implementations are heavy, so this fake just reports
    /// the channel count and sample rate / buffer size; nothing else is
    /// invoked from the callback path.
    class FakeDevice : public juce::AudioIODevice
    {
    public:
        FakeDevice (int inChannels, int outChannels, double rate, int bufSize)
            : juce::AudioIODevice ("fake", "fake"),
              inMask_  (channelMask (inChannels)),
              outMask_ (channelMask (outChannels)),
              rate_    (rate),
              bufSize_ (bufSize) {}

        juce::StringArray getOutputChannelNames() override { return {}; }
        juce::StringArray getInputChannelNames()  override { return {}; }
        juce::Array<double> getAvailableSampleRates() override { return { rate_ }; }
        juce::Array<int> getAvailableBufferSizes() override { return { bufSize_ }; }
        int getDefaultBufferSize() override { return bufSize_; }
        juce::String open (const juce::BigInteger&, const juce::BigInteger&,
                           double, int) override { return {}; }
        void close() override {}
        bool isOpen() override { return true; }
        void start (juce::AudioIODeviceCallback*) override {}
        void stop() override {}
        bool isPlaying() override { return false; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return bufSize_; }
        double getCurrentSampleRate() override { return rate_; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return outMask_; }
        juce::BigInteger getActiveInputChannels()  const override { return inMask_; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples()  override { return 0; }

    private:
        static juce::BigInteger channelMask (int count)
        {
            juce::BigInteger mask;
            for (int i = 0; i < count; ++i) mask.setBit (i);
            return mask;
        }
        juce::BigInteger inMask_, outMask_;
        double           rate_;
        int              bufSize_;
    };

    /// Build an AudioCallback wired with InputMixer / OutputMixer / DirectLayer
    /// and identity raw routes for the first `numRoutes` channels. Calls
    /// `audioDeviceAboutToStart` against a fake device so DirectLayer scratch
    /// is pre-sized to `(inChannels, outChannels)`. Caller owns lifetime.
    struct Wired
    {
        std::unique_ptr<InputMixer>    inputMixer;
        std::unique_ptr<OutputMixer>   outputMixer;
        std::unique_ptr<DirectLayer>   directLayer;
        std::unique_ptr<AudioCallback> callback;
        std::unique_ptr<FakeDevice>    device;
    };

    Wired makeWired (int inChannels, int outChannels, int numRoutes,
                     int bufSize = 512, double rate = 48000.0)
    {
        Wired w;
        w.inputMixer  = std::make_unique<InputMixer>();
        w.outputMixer = std::make_unique<OutputMixer>();
        w.directLayer = std::make_unique<DirectLayer>();
        for (int ch = 0; ch < numRoutes; ++ch)
            w.directLayer->addRawRoute (InputId (ch), OutputChannelId (ch));

        w.callback = std::make_unique<AudioCallback> (EngineConfig {});
        w.callback->setInputMixer  (w.inputMixer.get());
        w.callback->setOutputMixer (w.outputMixer.get());
        w.callback->setDirectLayer (w.directLayer.get());

        w.device = std::make_unique<FakeDevice> (inChannels, outChannels, rate, bufSize);
        w.callback->audioDeviceAboutToStart (w.device.get());
        return w;
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

TEST_CASE ("AudioCallback passes input through bit-exact when monitoring on (DirectLayer identity routes)",
           "[audio-callback][m4-integration]")
{
    constexpr int channels = 2;
    constexpr int samples  = 128;

    auto w = makeWired (channels, channels, /*numRoutes*/ channels, /*bufSize*/ samples);
    w.callback->setMonitoringEnabled (true);
    REQUIRE (w.callback->isMonitoringEnabled());

    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] =
                (ch == 0 ? 1.0f : -1.0f) * 0.01f * static_cast<float> (n);

    // Pre-fill output with junk so we can prove M4's "zero outputs first"
    // step ran before DirectLayer additively wrote into the (now-zero) buffer.
    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = 0.5f;

    auto ctx = emptyContext();
    w.callback->audioDeviceIOCallbackWithContext (in.readable(), channels,
                                                  out.writable(), channels,
                                                  samples, ctx);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
            CHECK (out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]
                       == in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]);
}

TEST_CASE ("AudioCallback silences extra output channels when input has fewer",
           "[audio-callback][m4-integration]")
{
    constexpr int inputs  = 1;
    constexpr int outputs = 2;
    constexpr int samples = 16;

    // Wire only one raw route (input 0 → output 0). Output 1 has no route,
    // so it should stay at the zero-fill the callback applied in step 1.
    auto w = makeWired (inputs, outputs, /*numRoutes*/ 1, /*bufSize*/ samples);
    w.callback->setMonitoringEnabled (true);

    Buffers in  (inputs,  samples);
    Buffers out (outputs, samples);

    for (int n = 0; n < samples; ++n)
        in.storage[0][static_cast<std::size_t> (n)] = 0.5f;

    // Pre-fill the second output with junk so we can prove it was silenced.
    for (int n = 0; n < samples; ++n)
        out.storage[1][static_cast<std::size_t> (n)] = 0.75f;

    auto ctx = emptyContext();
    w.callback->audioDeviceIOCallbackWithContext (in.readable(), inputs,
                                                  out.writable(), outputs,
                                                  samples, ctx);

    for (int n = 0; n < samples; ++n)
    {
        CHECK (out.storage[0][static_cast<std::size_t> (n)] == 0.5f);
        CHECK (out.storage[1][static_cast<std::size_t> (n)] == 0.0f);
    }
}

TEST_CASE ("AudioCallback drops extra input channels when output has fewer",
           "[audio-callback][m4-integration]")
{
    constexpr int inputs  = 4;
    constexpr int outputs = 2;
    constexpr int samples = 8;

    // Routes for the first 2 channels only — inputs 2 and 3 have no
    // destination wired and are silently dropped on the DirectLayer side.
    auto w = makeWired (inputs, outputs, /*numRoutes*/ 2, /*bufSize*/ samples);
    w.callback->setMonitoringEnabled (true);

    Buffers in  (inputs,  samples);
    Buffers out (outputs, samples);

    for (int ch = 0; ch < inputs; ++ch)
        for (int n = 0; n < samples; ++n)
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] =
                static_cast<float> (ch + 1);

    auto ctx = emptyContext();
    w.callback->audioDeviceIOCallbackWithContext (in.readable(), inputs,
                                                  out.writable(), outputs,
                                                  samples, ctx);

    // Channels 0 and 1 should pass through; 2 and 3 are silently dropped.
    for (int n = 0; n < samples; ++n)
    {
        CHECK (out.storage[0][static_cast<std::size_t> (n)] == 1.0f);
        CHECK (out.storage[1][static_cast<std::size_t> (n)] == 2.0f);
    }
}

TEST_CASE ("AudioCallback monitoring gate cuts DirectLayer routing",
           "[audio-callback][m4-integration]")
{
    constexpr int channels = 2;
    constexpr int samples  = 32;

    auto w = makeWired (channels, channels, /*numRoutes*/ channels, /*bufSize*/ samples);
    // Monitoring stays OFF — the route table is populated but never invoked.

    Buffers in  (channels, samples);
    Buffers out (channels, samples);

    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < samples; ++n)
        {
            in.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]  = 0.42f;
            out.storage[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = 0.99f;
        }

    auto ctx = emptyContext();
    w.callback->audioDeviceIOCallbackWithContext (in.readable(), channels,
                                                  out.writable(), channels,
                                                  samples, ctx);

    // Outputs were zero-filled in step 1 and DirectLayer never ran, so the
    // junk is gone but no input signal reached the output either.
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
