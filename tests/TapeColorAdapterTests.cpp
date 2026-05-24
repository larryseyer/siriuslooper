// Tests for ida::TapeColorAdapter — the 5th internal-FX adapter
// (TAPECOLOR Slice 1, 2026-05-24 design lock). Unlike EqAdapter/CmpAdapter/
// RvbAdapter/DlyAdapter, TAPECOLOR ships **default OFF**: a freshly-
// inserted slot is a silent passthrough until the operator turns it on
// through the (future-slice) param-UI. This test file pins that contract.
//
// We do NOT test DSP correctness (lsfx_tapecolor owns that); we test the
// IDA-side wrapper: prepare/process/reset state, the miss contract on an
// un-prepared adapter, the default-OFF passthrough behavior, and in-place
// vs out-of-place equivalence.
//
// TapeColorAdapter lives in engine/src/fx/ (private to the engine target).
// IdaTests adds engine/src to its PRIVATE include path — see
// tests/CMakeLists.txt.
#include "fx/TapeColorAdapter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using ida::TapeColorAdapter;

namespace
{

constexpr double kSampleRate   = 48000.0;
constexpr int    kMaxBlockSize = 512;
constexpr int    kNumChannels  = 2;
constexpr int    kBlockSamples = 256;

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

} // namespace

TEST_CASE ("TapeColorAdapter::process returns false and leaves output untouched when not prepared",
           "[internal-fx][tapecolor-adapter]")
{
    TapeColorAdapter adapter;

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

TEST_CASE ("TapeColorAdapter::process after prepare is a dry passthrough by default (TAPECOLOR default OFF)",
           "[internal-fx][tapecolor-adapter]")
{
    TapeColorAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    // Pin the design-lock invariant: a freshly-prepared adapter does NOT
    // alter the signal. lsfx::TapeColorProcessor's default config has
    // `enabled = false`; the adapter ctor deliberately does NOT flip that
    // flag (unlike RVB/CMP/DLY/EQ which DO enable their stages on insert).
    // The result: in→out is bit-identical until the operator turns the
    // slot on through a param-UI commit.
    std::vector<float> inLeft  (kBlockSamples);
    std::vector<float> inRight (kBlockSamples);
    fillSine (inLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.5f);
    fillSine (inRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.5f);

    std::vector<float> outLeft  (kBlockSamples, 0.0f);
    std::vector<float> outRight (kBlockSamples, 0.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));

    // Bit-identical because the processor early-exits on cfg.enabled==false
    // immediately after the adapter memcpys in→out. No DSP touches the
    // buffer.
    for (int i = 0; i < kBlockSamples; ++i)
    {
        REQUIRE (outLeft[i]  == inLeft[i]);
        REQUIRE (outRight[i] == inRight[i]);
    }
}

TEST_CASE ("TapeColorAdapter::reset after processing leaves the adapter usable",
           "[internal-fx][tapecolor-adapter]")
{
    TapeColorAdapter adapter;
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

    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));
}

TEST_CASE ("TapeColorAdapter::process supports in-place invocation (in == out aliased pointers)",
           "[internal-fx][tapecolor-adapter]")
{
    TapeColorAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    std::vector<float> bufLeft  (kBlockSamples);
    std::vector<float> bufRight (kBlockSamples);
    fillSine (bufLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.5f);
    fillSine (bufRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.5f);

    // Snapshot before processing so we can compare bit-for-bit with the
    // out-of-place result on the default-OFF (passthrough) path.
    const std::vector<float> sourceLeft  = bufLeft;
    const std::vector<float> sourceRight = bufRight;

    float* aliased[kNumChannels] = { bufLeft.data(), bufRight.data() };
    const float* const* inAlias  = const_cast<const float* const*> (aliased);

    REQUIRE (adapter.process (inAlias, aliased, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (bufLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (bufRight.data(), kBlockSamples));

    // Default-OFF: in-place processing leaves the signal bit-identical.
    for (int i = 0; i < kBlockSamples; ++i)
    {
        REQUIRE (bufLeft[i]  == sourceLeft[i]);
        REQUIRE (bufRight[i] == sourceRight[i]);
    }
}

TEST_CASE ("TapeColorAdapter rejects sub-stereo channel counts as a miss",
           "[internal-fx][tapecolor-adapter]")
{
    // IDA is stereo-only (hard invariant). The adapter prepares the
    // processor with 2 channels; anything narrower must miss so the
    // caller's dry-passthrough takes over rather than partially-filling
    // outChannels.
    TapeColorAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    std::vector<float> inMono  (kBlockSamples, 0.5f);
    std::vector<float> outMono (kBlockSamples, -9.0f);  // sentinel

    const float* inPtr  = inMono.data();
    float*       outPtr = outMono.data();

    const bool ok = adapter.process (&inPtr, &outPtr, /*numChannels=*/ 1, kBlockSamples);
    CHECK_FALSE (ok);

    for (int i = 0; i < kBlockSamples; ++i)
        REQUIRE (outMono[i] == -9.0f);
}
