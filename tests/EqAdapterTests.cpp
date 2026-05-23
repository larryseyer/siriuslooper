// Tests for ida::EqAdapter — the first internal-FX adapter (T3a of the
// P7 umbrella). We do NOT test DSP correctness (PlayerEQ owns that); we
// test the IDA-side wrapper: prepare/process/reset state, the miss
// contract on an un-prepared adapter, finite output on a known sine, and
// in-place vs out-of-place equivalence.
//
// EqAdapter lives in engine/src/fx/ (private to the engine target). The
// IdaTests target adds engine/src to its PRIVATE include path so this
// test can reach the adapter header without exposing it on the engine's
// public surface — see tests/CMakeLists.txt.
#include "fx/EqAdapter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>
#include <vector>

using ida::EqAdapter;

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

TEST_CASE ("EqAdapter::process returns false and leaves output untouched when not prepared",
           "[internal-fx][eq-adapter]")
{
    EqAdapter adapter;

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

TEST_CASE ("EqAdapter::process after prepare produces finite, bounded output for a 1 kHz sine",
           "[internal-fx][eq-adapter]")
{
    EqAdapter adapter;
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

    // PlayerEQ's default-flat config (HP at the bypass-equivalent 20 Hz, all
    // shelves at 0 dB, LP at the bypass-equivalent 20 kHz) should yield a
    // peak no greater than ~10x the input — generous slack for transient IIR
    // settling on the first block.
    const float outPeak = std::max (peakAbs (outLeft.data(),  kBlockSamples),
                                    peakAbs (outRight.data(), kBlockSamples));
    CHECK (outPeak <= 10.0f * inPeak);
}

TEST_CASE ("EqAdapter::reset after processing leaves the adapter usable",
           "[internal-fx][eq-adapter]")
{
    EqAdapter adapter;
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

    // After reset, the adapter must still process correctly with finite output.
    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));
}

TEST_CASE ("EqAdapter::process supports in-place invocation (in == out aliased pointers)",
           "[internal-fx][eq-adapter]")
{
    // Run the same input through two adapter instances — one out-of-place,
    // one in-place — and confirm both produce the same finite output. This
    // proves the in-place copy elision path in process() preserves audio
    // when the caller aliases inChannels and outChannels.
    EqAdapter outOfPlace;
    EqAdapter inPlace;
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

    // The two paths must produce bit-identical output: PlayerEQ is a
    // deterministic LTI filter starting from a freshly-prepared state, and
    // both adapters processed the same input block.
    for (int i = 0; i < kBlockSamples; ++i)
    {
        REQUIRE (oopOutLeft[i]  == ipLeft[i]);
        REQUIRE (oopOutRight[i] == ipRight[i]);
    }
}
