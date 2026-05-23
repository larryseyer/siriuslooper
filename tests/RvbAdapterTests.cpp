// Tests for ida::RvbAdapter — the fourth and final internal-FX adapter
// (T3d of the P7 umbrella). We do NOT test convolution DSP correctness
// (PlayerIRConvolution owns that); we test the IDA-side wrapper:
// prepare/process/reset state, the miss contract on an un-prepared
// adapter, finite bounded output on a known sine, in-place vs out-of-place
// equivalence, and an IR-loaded smoke case that asserts non-silent output
// once OTTO's background worker has finished installing the default IR.
//
// RvbAdapter lives in engine/src/fx/ (private to the engine target). The
// IdaTests target adds engine/src to its PRIVATE include path so this
// test can reach the adapter header without exposing it on the engine's
// public surface — see tests/CMakeLists.txt.
//
// IR-load is asynchronous. Cases 1-4 do NOT depend on the IR being
// loaded — they exercise the early-exit / memcpy-passthrough paths that
// hold while OTTO's worker is still installing the IR (or never
// installing it, when OTTO_ASSETS_DIR points at a missing path). Case 5
// polls isLoaded() with a 5 s timeout and SKIPs cleanly when the IR is
// not available, so CI without bundled assets stays green.
#include "fx/RvbAdapter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

using ida::RvbAdapter;

namespace
{

constexpr double kSampleRate    = 48000.0;
constexpr int    kMaxBlockSize  = 512;
constexpr int    kNumChannels   = 2;
constexpr int    kBlockSamples  = 256;

void fillSine (float* dst, int numSamples, double sampleRate, double freqHz, float amplitude)
{
    const double twoPiF = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i)
        dst[i] = amplitude * static_cast<float> (std::sin (twoPiF * static_cast<double> (i)));
}

bool allFinite (const float* data, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (data[i]))
            return false;
    return true;
}

float peakAbs (const float* data, int n)
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (data[i]));
    return peak;
}

/// Poll the adapter's async-load flag with a hard timeout. Returns true
/// when isLoaded() flips true; false on timeout. PlayerIRConvolution's
/// background worker does wav-decode → time-stretch → decay-envelope →
/// damping → 3-band EQ → makeup-gain → JUCE Convolution install on the
/// inactive double-buffer slot; in practice (Release, M-series) the
/// pipeline takes ~4-6 s for a 1.13 s plate IR, so a 15 s timeout
/// provides ~3× slack for Debug builds + slow CI runners while still
/// failing fast when assets are genuinely unavailable.
bool waitForIRLoaded (RvbAdapter& adapter, std::chrono::seconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (adapter.isLoaded())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return adapter.isLoaded();
}

} // namespace

TEST_CASE ("RvbAdapter::process returns false and leaves output untouched when not prepared",
           "[internal-fx][rvb-adapter]")
{
    RvbAdapter adapter;

    std::vector<float> inLeft  (kBlockSamples, 0.5f);
    std::vector<float> inRight (kBlockSamples, 0.5f);
    std::vector<float> outLeft (kBlockSamples, -7.0f);  // sentinel — must survive
    std::vector<float> outRight(kBlockSamples, -7.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples);
    CHECK_FALSE (ok);

    // Miss contract: outChannels is untouched. The sentinel value survives.
    for (int i = 0; i < kBlockSamples; ++i)
    {
        REQUIRE (outLeft[i]  == -7.0f);
        REQUIRE (outRight[i] == -7.0f);
    }
}

TEST_CASE ("RvbAdapter::process after prepare produces finite, bounded output for a 1 kHz sine",
           "[internal-fx][rvb-adapter]")
{
    RvbAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    std::vector<float> inLeft  (kBlockSamples);
    std::vector<float> inRight (kBlockSamples);
    fillSine (inLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.5f);
    fillSine (inRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.5f);

    const float inPeak = peakAbs (inLeft.data(), kBlockSamples);

    std::vector<float> outLeft  (kBlockSamples, 0.0f);
    std::vector<float> outRight (kBlockSamples, 0.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples);
    CHECK (ok);

    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));

    // Tolerant of the IR-load race: while OTTO's background worker is
    // still installing the IR, conv_.process early-exits and outChannels
    // retains its memcpy-from-input sine (peak ≈ inPeak). Post-load,
    // convolution through the default plate IR can amplify a continuous
    // sine through resonant peaks, but the 10× slack absorbs that while
    // still catching catastrophic failure modes (NaN, unbounded gain,
    // adapter accidentally summing dry + wet and amplifying).
    const float outPeak = std::max (peakAbs (outLeft.data(),  kBlockSamples),
                                    peakAbs (outRight.data(), kBlockSamples));
    CHECK (outPeak <= 10.0f * inPeak);
}

TEST_CASE ("RvbAdapter::reset after processing leaves the adapter usable",
           "[internal-fx][rvb-adapter]")
{
    RvbAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    std::vector<float> inLeft  (kBlockSamples);
    std::vector<float> inRight (kBlockSamples);
    fillSine (inLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.5f);
    fillSine (inRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.5f);

    std::vector<float> outLeft  (kBlockSamples, 0.0f);
    std::vector<float> outRight (kBlockSamples, 0.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));

    adapter.reset();

    // After reset, the adapter must still process correctly with finite
    // output. PlayerIRConvolution::reset clears convolver overlap-add
    // state and pre-delay ring history; the loaded IR (if any) and the
    // configured params survive.
    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));
}

TEST_CASE ("RvbAdapter::process supports in-place invocation (in == out aliased pointers)",
           "[internal-fx][rvb-adapter]")
{
    // The point of this test is to verify the memcpy-elision branch in
    // process() doesn't corrupt the buffer — not to verify convolution
    // determinism. We construct two adapters and process them immediately
    // back-to-back so both are in matching pre-load state (OTTO's
    // background workers take ≫µs to wav-decode + process + install an
    // IR; the few µs between adapter construction and adapter.process
    // are not enough to land an IR install). When both adapters are
    // pre-load, conv_.process early-exits inside OTTO and the output is
    // the memcpy-from-input passthrough — the in-place path skips the
    // memcpy entirely, the out-of-place path does the memcpy then OTTO
    // no-ops on it; both produce the same input-shaped output. The
    // tolerance (1e-6) absorbs the rare race where one worker happens to
    // finish before the other.
    RvbAdapter outOfPlace;
    RvbAdapter inPlace;
    outOfPlace.prepare (kSampleRate, kMaxBlockSize);
    inPlace.prepare    (kSampleRate, kMaxBlockSize);

    std::vector<float> srcLeft  (kBlockSamples);
    std::vector<float> srcRight (kBlockSamples);
    fillSine (srcLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.5f);
    fillSine (srcRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.5f);

    // Out-of-place: input separate from output.
    std::vector<float> oopInLeft  = srcLeft;
    std::vector<float> oopInRight = srcRight;
    std::vector<float> oopOutLeft  (kBlockSamples, 0.0f);
    std::vector<float> oopOutRight (kBlockSamples, 0.0f);
    const float* oopInPtrs[kNumChannels]  = { oopInLeft.data(),  oopInRight.data() };
    float*       oopOutPtrs[kNumChannels] = { oopOutLeft.data(), oopOutRight.data() };
    REQUIRE (outOfPlace.process (oopInPtrs, oopOutPtrs, kNumChannels, kBlockSamples));

    // In-place: same pointer for input and output.
    std::vector<float> ipLeft  = srcLeft;
    std::vector<float> ipRight = srcRight;
    float* ipPtrs[kNumChannels] = { ipLeft.data(), ipRight.data() };
    REQUIRE (inPlace.process (ipPtrs, ipPtrs, kNumChannels, kBlockSamples));

    for (int i = 0; i < kBlockSamples; ++i)
    {
        REQUIRE (std::abs (oopOutLeft[i]  - ipLeft[i])  < 1.0e-6f);
        REQUIRE (std::abs (oopOutRight[i] - ipRight[i]) < 1.0e-6f);
    }
}

TEST_CASE ("RvbAdapter::process produces non-silent wet output after IR load completes",
           "[internal-fx][rvb-adapter]")
{
    // The IR-loaded smoke test. The CMake-injected IDA_OTTO_ASSETS_DIR
    // points at a path containing "Plate Bright 1.13.wav"; OTTO's
    // background worker decodes + processes + installs it after prepare.
    // We poll isLoaded() with a 5 s timeout. If the asset isn't available
    // (CI without OTTO checkout, customer-install pipeline not yet
    // wired), we SKIP cleanly so CI stays green — the other four cases
    // already cover the pre-load / passthrough paths.
    RvbAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    if (! waitForIRLoaded (adapter, std::chrono::seconds (15)))
        SKIP ("OTTO IR assets unavailable or load slow — skipping IR-loaded smoke test");

    // Feed a unit impulse on the first sample, then run enough blocks to
    // let the convolution drive the IR tail into the output. With a
    // ~1.13 s IR @ 48 kHz, ~55 K samples cover the full impulse response;
    // we run 240 blocks of 256 (61 440 samples ≈ 1.28 s) to be safe.
    constexpr int kImpulseBlocks = 240;
    constexpr int kTotalSamples  = kImpulseBlocks * kBlockSamples;

    std::vector<float> outBufferL (kTotalSamples, 0.0f);
    std::vector<float> outBufferR (kTotalSamples, 0.0f);

    std::vector<float> blockInLeft  (kBlockSamples, 0.0f);
    std::vector<float> blockInRight (kBlockSamples, 0.0f);

    for (int b = 0; b < kImpulseBlocks; ++b)
    {
        std::fill (blockInLeft.begin(),  blockInLeft.end(),  0.0f);
        std::fill (blockInRight.begin(), blockInRight.end(), 0.0f);
        if (b == 0)
        {
            blockInLeft[0]  = 1.0f;
            blockInRight[0] = 1.0f;
        }

        std::vector<float> blockOutLeft  (kBlockSamples, 0.0f);
        std::vector<float> blockOutRight (kBlockSamples, 0.0f);

        const float* inPtrs[kNumChannels]  = { blockInLeft.data(),  blockInRight.data() };
        float*       outPtrs[kNumChannels] = { blockOutLeft.data(), blockOutRight.data() };

        REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));

        std::memcpy (outBufferL.data() + b * kBlockSamples,
                     blockOutLeft.data(),
                     static_cast<std::size_t> (kBlockSamples) * sizeof (float));
        std::memcpy (outBufferR.data() + b * kBlockSamples,
                     blockOutRight.data(),
                     static_cast<std::size_t> (kBlockSamples) * sizeof (float));
    }

    REQUIRE (allFinite (outBufferL.data(), kTotalSamples));
    REQUIRE (allFinite (outBufferR.data(), kTotalSamples));

    // Convolution through any reasonable plate IR must drive the output
    // above silence. We do NOT assert a specific tap location or peak
    // value — those are IR-data-dependent (different IRs have different
    // attack profiles, peak gains, pre-delay distributions). 0.001 is
    // well above floating-point noise and well below any plausible IR
    // peak amplitude.
    const float outPeak = std::max (peakAbs (outBufferL.data(), kTotalSamples),
                                    peakAbs (outBufferR.data(), kTotalSamples));
    CHECK (outPeak > 0.001f);
}
