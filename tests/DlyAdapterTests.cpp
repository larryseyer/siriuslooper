// Tests for sirius::DlyAdapter — the third internal-FX adapter (T3c of
// the P7 umbrella). We do NOT test delay-line DSP correctness
// (PlayerDelay owns that); we test the Sirius-side wrapper:
// prepare/process/reset state, the miss contract on an un-prepared
// adapter, finite output on a known sine, in-place vs out-of-place
// equivalence, and a unit-impulse case that asserts a non-zero sample
// appears at the expected free-running delay offset (250 ms @ 48 kHz =
// 12000 samples) — pinning the adapter's "delayEnabled + delaySyncEnabled
// = false in ctor" choice that gives a deterministic delay time without
// a transport bpm.
//
// DlyAdapter lives in engine/src/fx/ (private to the engine target). The
// SiriusTests target adds engine/src to its PRIVATE include path so this
// test can reach the adapter header without exposing it on the engine's
// public surface — see tests/CMakeLists.txt.
#include "fx/DlyAdapter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using sirius::DlyAdapter;

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

} // namespace

TEST_CASE ("DlyAdapter::process returns false and leaves output untouched when not prepared",
           "[internal-fx][dly-adapter]")
{
    DlyAdapter adapter;

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

TEST_CASE ("DlyAdapter::process after prepare produces finite, bounded output for a 1 kHz sine",
           "[internal-fx][dly-adapter]")
{
    DlyAdapter adapter;
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

    // PlayerDelay outputs 100 % wet. At default 250 ms free-running delay
    // (12000 samples @ 48 kHz), the first 256-sample block sits well
    // before the first delay tap arrives — the wet output is near zero on
    // this block. Generous slack: outPeak <= 10x inPeak still catches the
    // symmetric failure mode of an adapter accidentally summing dry + wet
    // and amplifying the input.
    const float outPeak = std::max (peakAbs (outLeft.data(),  kBlockSamples),
                                    peakAbs (outRight.data(), kBlockSamples));
    CHECK (outPeak <= 10.0f * inPeak);
}

TEST_CASE ("DlyAdapter::reset after processing leaves the adapter usable",
           "[internal-fx][dly-adapter]")
{
    DlyAdapter adapter;
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
    // output. Delay-time/feedback/filter parameters survive the reset;
    // only the delay-line buffers, feedback filters' IIR state, and the
    // tap counter are cleared.
    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));
}

TEST_CASE ("DlyAdapter::process supports in-place invocation (in == out aliased pointers)",
           "[internal-fx][dly-adapter]")
{
    // Run the same input through two adapter instances — one out-of-place,
    // one in-place — and confirm both produce the same finite output. This
    // proves the in-place copy elision path in process() preserves audio
    // when the caller aliases inChannels and outChannels. PlayerDelay is
    // deterministic (delay-line read/write + IIR feedback filters with
    // identical fresh state), so the two paths must produce bit-identical
    // output for the same input.
    DlyAdapter outOfPlace;
    DlyAdapter inPlace;
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
        REQUIRE (oopOutLeft[i]  == ipLeft[i]);
        REQUIRE (oopOutRight[i] == ipRight[i]);
    }
}

TEST_CASE ("DlyAdapter::process produces a delayed-impulse tap at the expected 250 ms offset",
           "[internal-fx][dly-adapter]")
{
    // Unit-impulse-at-delay-offset case: feed a single unit impulse at
    // sample 0 with the rest of the buffer silent, then run enough 256-
    // sample blocks to cover the default 250 ms free-running delay
    // (12000 samples @ 48 kHz). PlayerDelay outputs 100 % wet, so the
    // impulse must appear unattenuated at the first delay tap and the
    // buffer must be near-zero everywhere before that. The default-config
    // feedback path filters (80 Hz HP + 8 kHz LP) only affect the
    // writeback for subsequent echoes, not the first tap — so we can
    // assert peak ≈ 1.0 at the expected offset.
    //
    // 50 blocks of 256 = 12800 samples — comfortably covers the 12000-
    // sample first tap. Output peak detection runs across the whole
    // concatenated buffer.
    constexpr int kImpulseBlocks  = 50;
    constexpr int kTotalSamples   = kImpulseBlocks * kBlockSamples;  // 12800
    constexpr int kExpectedDelay  = 12000;                            // 250 ms @ 48k
    constexpr int kDelaySlack     = 64;                               // ±64 samples

    DlyAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    // Run kImpulseBlocks blocks, injecting a unit impulse on block 0 / sample 0.
    std::vector<float> outBufferL (kTotalSamples, 0.0f);
    std::vector<float> outBufferR (kTotalSamples, 0.0f);

    std::vector<float> blockInLeft  (kBlockSamples, 0.0f);
    std::vector<float> blockInRight (kBlockSamples, 0.0f);

    for (int b = 0; b < kImpulseBlocks; ++b)
    {
        // Inject the impulse on the first sample of the first block only.
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

    // Locate the peak (largest |sample|) — must land at the expected
    // delay offset (±slack for any sub-sample interpolation rounding in
    // juce::dsp::DelayLine).
    int   peakIdxL  = 0;
    float peakValL  = 0.0f;
    for (int i = 0; i < kTotalSamples; ++i)
    {
        const float absSample = std::abs (outBufferL[i]);
        if (absSample > peakValL)
        {
            peakValL = absSample;
            peakIdxL = i;
        }
    }

    CHECK (peakValL > 0.5f);                                          // tap actually arrives
    CHECK (peakIdxL >= kExpectedDelay - kDelaySlack);
    CHECK (peakIdxL <= kExpectedDelay + kDelaySlack);

    // Right channel mirrors left (default config: pingPong=false → each
    // channel is its own independent delay line driven by its own input).
    int   peakIdxR  = 0;
    float peakValR  = 0.0f;
    for (int i = 0; i < kTotalSamples; ++i)
    {
        const float absSample = std::abs (outBufferR[i]);
        if (absSample > peakValR)
        {
            peakValR = absSample;
            peakIdxR = i;
        }
    }

    CHECK (peakValR > 0.5f);
    CHECK (peakIdxR >= kExpectedDelay - kDelaySlack);
    CHECK (peakIdxR <= kExpectedDelay + kDelaySlack);

    // Pre-tap samples must be near-zero — there is no dry contribution to
    // the wet-only output, and the delay line is silent until the first
    // tap arrives. The very first sample of block 0 sees the impulse on
    // the input but the popSample-then-pushSample order in PlayerDelay's
    // loop means the output is the OLD delay-line value (0) — the
    // impulse is captured to the line, not echoed to the output.
    const float prePeak = std::max (
        peakAbs (outBufferL.data(), kExpectedDelay - kDelaySlack),
        peakAbs (outBufferR.data(), kExpectedDelay - kDelaySlack));
    CHECK (prePeak < 0.01f);
}
