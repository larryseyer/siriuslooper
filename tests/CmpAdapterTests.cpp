// Tests for ida::CmpAdapter — the second internal-FX adapter (T3b of
// the P7 umbrella). We do NOT test compressor DSP correctness
// (PlayerCompressor owns that); we test the Sirius-side wrapper:
// prepare/process/reset state, the miss contract on an un-prepared
// adapter, finite output on a known sine, in-place vs out-of-place
// equivalence, and a DC-pulse case that asserts peak reduction once the
// envelope follower has settled past PlayerCompressor's 10 ms default
// attack.
//
// CmpAdapter lives in engine/src/fx/ (private to the engine target). The
// IdaTests target adds engine/src to its PRIVATE include path so this
// test can reach the adapter header without exposing it on the engine's
// public surface — see tests/CMakeLists.txt.
#include "fx/CmpAdapter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using ida::CmpAdapter;

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

TEST_CASE ("CmpAdapter::process returns false and leaves output untouched when not prepared",
           "[internal-fx][cmp-adapter]")
{
    CmpAdapter adapter;

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

TEST_CASE ("CmpAdapter::process after prepare produces finite, bounded output for a 1 kHz sine",
           "[internal-fx][cmp-adapter]")
{
    CmpAdapter adapter;
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

    // PlayerCompressor's default config (threshold -12 dB, 4:1 ratio,
    // makeup 0 dB, mix=1.0 fully wet) only reduces gain — it never adds
    // gain. Output peak must be bounded by the input peak (modest slack
    // for the first-block envelope-attack transient that can briefly
    // pass un-compressed peaks through).
    const float outPeak = std::max (peakAbs (outLeft.data(),  kBlockSamples),
                                    peakAbs (outRight.data(), kBlockSamples));
    CHECK (outPeak <= 1.5f * inPeak);
}

TEST_CASE ("CmpAdapter::reset after processing leaves the adapter usable",
           "[internal-fx][cmp-adapter]")
{
    CmpAdapter adapter;
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
    // output. Compressor parameters survive the reset; only envelope /
    // dry-buffer / sidechain state is cleared.
    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));
    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));
}

TEST_CASE ("CmpAdapter::process supports in-place invocation (in == out aliased pointers)",
           "[internal-fx][cmp-adapter]")
{
    // Run the same input through two adapter instances — one out-of-place,
    // one in-place — and confirm both produce the same finite output. This
    // proves the in-place copy elision path in process() preserves audio
    // when the caller aliases inChannels and outChannels. PlayerCompressor
    // is deterministic LTI-with-state and both adapters start from
    // identical fresh state, so the two paths must produce bit-identical
    // output for the same input.
    CmpAdapter outOfPlace;
    CmpAdapter inPlace;
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

TEST_CASE ("CmpAdapter::process reduces peak after envelope settles on a sustained signal above threshold",
           "[internal-fx][cmp-adapter]")
{
    // Brick-wall / sustained-loud-signal case: feed input above the default
    // -12 dB threshold (0.25 linear), warm up the 10 ms-attack envelope
    // follower with five 256-sample blocks (~27 ms at 48 kHz, well past
    // the attack constant), then measure peak reduction on the next block.
    //
    // Must use AC, not DC: PlayerCompressor's default config enables a
    // 4-stage Butterworth sidechain HPF at 100 Hz, which fully blocks DC
    // before the peak detector — a DC input would produce zero envelope
    // and never trigger compression. A 1 kHz sine sits a decade above
    // the HPF cutoff and passes the detector cleanly.
    //
    // Default params: threshold -12 dB → 0.25 linear, ratio 4:1, makeup
    // 0 dB, mix=1.0 (fully wet). With sine peak at 0.7 and release
    // (100 ms) >> the 1 ms period, the envelope follower latches close to
    // the peak value (each peak retriggers attack, between-peak decay is
    // negligible). dbAbove ≈ 8.9 dB, dbReduction ≈ 6.7 dB, gainReduction
    // ≈ 0.46, output peak ≈ 0.32. We assert outPeak < 0.6 — generous
    // slack that still catches a broken adapter passing the dry signal
    // through — and > 0.1 to catch the symmetric flatlines-to-silence
    // failure mode.
    CmpAdapter adapter;
    adapter.prepare (kSampleRate, kMaxBlockSize);

    std::vector<float> sinLeft  (kBlockSamples);
    std::vector<float> sinRight (kBlockSamples);
    fillSine (sinLeft.data(),  kBlockSamples, kSampleRate, 1000.0, 0.7f);
    fillSine (sinRight.data(), kBlockSamples, kSampleRate, 1000.0, 0.7f);

    std::vector<float> outLeft  (kBlockSamples, 0.0f);
    std::vector<float> outRight (kBlockSamples, 0.0f);
    const float* inPtrs[kNumChannels]  = { sinLeft.data(),  sinRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    for (int b = 0; b < 5; ++b)
        REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));

    REQUIRE (adapter.process (inPtrs, outPtrs, kNumChannels, kBlockSamples));

    REQUIRE (allFinite (outLeft.data(),  kBlockSamples));
    REQUIRE (allFinite (outRight.data(), kBlockSamples));

    const float outPeak = std::max (peakAbs (outLeft.data(),  kBlockSamples),
                                    peakAbs (outRight.data(), kBlockSamples));
    CHECK (outPeak < 0.6f);
    CHECK (outPeak > 0.1f);
}
