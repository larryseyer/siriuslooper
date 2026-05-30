// Headless DSP tests for the Jiles-Atherton (J-A) saturation model added to
// lsfx::tapecolor::HysteresisProcessor as a switchable alternative to the
// static asinh waveshaper (2026-05-30 A/B experiment, spec
// docs/superpowers/specs/2026-05-30-jiles-atherton-ab-experiment.md).
//
// The load-bearing test is `J-A small-signal gain is unity` — the 2026-05-25
// J-A attempt was reverted because its small-signal slope (c·Ms/(3a)) sat
// ~13 dB below unity, gating low-level detail. The solver here normalizes that
// to unity; this suite pins it, plus numerical stability (bounded, monotonic,
// no NaN, oversampling-rate stable, state reset).
//
// We exercise lsfx::tapecolor::HysteresisProcessor directly. IdaTests links
// lsfx::lsfx_tapecolor (see tests/CMakeLists.txt). The header pulls in only
// juce_audio_basics, not the GUI editor.

#include <lsfx_tapecolor/dsp/HysteresisProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using lsfx::tapecolor::HysteresisProcessor;
using SaturationModel = HysteresisProcessor::SaturationModel;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Fill a stereo buffer with a sine of the given linear amplitude.
juce::AudioBuffer<float> makeSine (double amplitude, double freqHz,
                                   double sampleRate, int samples)
{
    juce::AudioBuffer<float> buf (2, samples);
    for (int n = 0; n < samples; ++n)
    {
        const auto v = static_cast<float> (amplitude
                         * std::sin (2.0 * kPi * freqHz * n / sampleRate));
        buf.setSample (0, n, v);
        buf.setSample (1, n, v);
    }
    return buf;
}

// Fundamental magnitude at `freqHz` over the final `window` samples of channel
// 0 (single-bin DFT). Isolates the fundamental's level from added harmonics and
// DC — the right measure for "does low-level detail pass at unity?".
double fundamentalMag (const juce::AudioBuffer<float>& buf, double freqHz,
                       double sampleRate, int window)
{
    const int total = buf.getNumSamples();
    const int start = juce::jmax (0, total - window);
    const double w = 2.0 * kPi * freqHz / sampleRate;
    double re = 0.0, im = 0.0;
    for (int n = start; n < total; ++n)
    {
        const double v = buf.getSample (0, n);
        re += v * std::cos (w * n);
        im += v * std::sin (w * n);
    }
    return std::sqrt (re * re + im * im);
}

double peakAbs (const juce::AudioBuffer<float>& buf)
{
    double peak = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            peak = std::max (peak, std::abs (static_cast<double> (buf.getSample (ch, n))));
    return peak;
}

bool allFinite (const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            if (! std::isfinite (buf.getSample (ch, n)))
                return false;
    return true;
}

// Configure a processor for the J-A model at neutral knob settings
// (tape T456 idx 2, 15 ips idx 1, hysteresis 0.5 = baseline, saturation 0.5 =
// unity drive). effectiveSampleRate defaults to the prepared rate (1x).
void configureJA (HysteresisProcessor& hp, double effectiveSampleRate)
{
    hp.updateParameters (/*tape*/ 2, /*speed*/ 1, /*hysteresisAmount*/ 0.5f,
                         /*saturation*/ 0.5f, effectiveSampleRate,
                         SaturationModel::JilesAtherton);
}

double dbDiff (double a, double b) { return 20.0 * std::log10 (a / b); }
} // namespace

TEST_CASE ("J-A small-signal gain is unity in the deep-linear region", "[hysteresis-ja][dsp]")
{
    // The load-bearing regression guard for the 2026-05-25 failure. That bug
    // ATTENUATED low-level detail ~13 dB; here detail must pass at unity.
    // Non-circular: normGain is calibrated internally at a different tiny
    // amplitude (and frequency) than this test probes, so unity here proves
    // the model is genuinely locally linear, not self-fulfilling.
    constexpr double sr = 48000.0;
    HysteresisProcessor hp;

    auto gainDbAt = [&] (double dbfs) {
        hp.prepare (sr, 2);
        configureJA (hp, sr);
        const double amp = std::pow (10.0, dbfs / 20.0);
        auto buf = makeSine (amp, 1000.0, sr, 4096);
        const double inMag = fundamentalMag (buf, 1000.0, sr, 2048);
        hp.process (buf);
        return dbDiff (fundamentalMag (buf, 1000.0, sr, 2048), inMag);
    };

    // Deep-linear: tight unity (the bug would show as a large negative number).
    for (double dbfs : { -60.0, -80.0 })
    {
        INFO ("level " << dbfs << " dBFS, gain " << gainDbAt (dbfs) << " dB");
        REQUIRE (std::abs (gainDbAt (dbfs)) < 0.05);
    }

    // Approaching the knee a mild pre-saturation RISE is musical and expected;
    // what must never return is ATTENUATION of low-level detail.
    INFO ("level -40 dBFS, gain " << gainDbAt (-40.0) << " dB");
    REQUIRE (gainDbAt (-40.0) > -0.02);
    REQUIRE (gainDbAt (-40.0) < 0.20);
}

TEST_CASE ("asinh model remains unity at -60 dBFS (regression)", "[hysteresis-ja][dsp]")
{
    constexpr double sr = 48000.0;
    HysteresisProcessor hp;
    hp.prepare (sr, 2);
    hp.updateParameters (2, 1, 0.5f, 0.5f, sr, SaturationModel::Asinh);

    const double amp = std::pow (10.0, -60.0 / 20.0);
    auto buf = makeSine (amp, 1000.0, sr, 4096);
    const double inMag = fundamentalMag (buf, 1000.0, sr, 2048);
    hp.process (buf);
    const double outMag = fundamentalMag (buf, 1000.0, sr, 2048);

    REQUIRE (std::abs (dbDiff (outMag, inMag)) < 0.05);
}

TEST_CASE ("J-A frequency response is flat (not dark)", "[hysteresis-ja][dsp]")
{
    // The 2026-05-25 attempt sounded "very very dark". The H-domain solver has
    // no inherent frequency dependence (the trapezoidal dt cancels), so the
    // small-signal response must be flat across the band — no HF rolloff. This
    // is the regression guard against darkness. Measured low-level so we sit in
    // the linear region where tonal balance of detail matters most.
    constexpr double sr = 48000.0;
    HysteresisProcessor hp;
    hp.prepare (sr, 2);
    configureJA (hp, sr);

    auto gainAt = [&] (double freq) {
        hp.reset();
        configureJA (hp, sr);
        auto buf = makeSine (std::pow (10.0, -40.0 / 20.0), freq, sr, 8192);
        const double in = fundamentalMag (buf, freq, sr, 4096);
        hp.process (buf);
        return fundamentalMag (buf, freq, sr, 4096) / in;
    };

    const double ref = gainAt (1000.0);
    for (double freq : { 100.0, 500.0, 5000.0, 10000.0, 15000.0 })
    {
        const double tilt = dbDiff (gainAt (freq), ref);
        INFO ("freq " << freq << " Hz, tilt vs 1 kHz = " << tilt << " dB");
        REQUIRE (std::abs (tilt) < 0.15);
    }
}

TEST_CASE ("J-A is peak-safe at full scale yet shapes the waveform",
           "[hysteresis-ja][dsp]")
{
    // Why this matters: the J-A stage must be level-matched against asinh BY
    // CONSTRUCTION (gentle, non-expanding saturation), NOT by a dynamic makeup.
    // The earlier broadband-RMS makeup boosted transients past full scale and
    // overloaded the Reaper track. The honest A/B invariant is therefore:
    //   1. the output PEAK never exceeds the input peak (a saturator compresses,
    //      it never expands) — this is the regression guard for the overload;
    //   2. the waveform is still shaped (harmonics) — not a bypass.
    // Note RMS may rise slightly as the soft knee fattens the sine toward a
    // squarer shape (sine RMS 0.707·peak → square RMS = peak); that is real
    // tape behaviour and is fine SO LONG AS the peak stays bounded.
    constexpr double sr = 48000.0;
    constexpr int samples = 48000;   // 1 s — converge to steady-state minor loop
    HysteresisProcessor hp;
    hp.prepare (sr, 2);
    configureJA (hp, sr);

    auto buf = makeSine (1.0, 1000.0, sr, samples);
    juce::AudioBuffer<float> original;
    original.makeCopyOf (buf);

    const double inPeak = peakAbs (original);
    hp.process (buf);
    const double outPeak = peakAbs (buf);

    INFO ("in/out peak " << inPeak << " / " << outPeak
          << " (" << dbDiff (outPeak, inPeak) << " dB)");
    REQUIRE (outPeak <= inPeak * 1.05);   // peak-safe: never expands (≤ +0.4 dB)

    // The model genuinely distorts: the output must differ materially from the
    // clean input — proving it is not a transparent bypass. Compare the
    // converged tail.
    double maxDiff = 0.0;
    for (int n = samples - 8192; n < samples; ++n)
        maxDiff = std::max (maxDiff,
                            std::abs (static_cast<double> (buf.getSample (0, n)
                                      - original.getSample (0, n))));
    INFO ("max |out - in| over tail = " << maxDiff);
    REQUIRE (maxDiff > 0.02);
}

TEST_CASE ("J-A output is finite and bounded at full-scale and over-range",
           "[hysteresis-ja][dsp]")
{
    constexpr double sr = 48000.0;
    HysteresisProcessor hp;
    hp.prepare (sr, 2);

    for (double amp : { 1.0, 4.0 })
    {
        hp.reset();
        configureJA (hp, sr);
        auto buf = makeSine (amp, 1000.0, sr, 4096);
        hp.process (buf);
        INFO ("amp " << amp << " peak " << peakAbs (buf));
        REQUIRE (allFinite (buf));
        // A saturator must NEVER expand: output peak ≤ input peak (+5% margin
        // for the soft-knee overshoot near the iteration). The old +18 dBFS
        // (peak<8.0) ceiling let the +65 dB transient blow-up pass; this is the
        // tight bound that catches it.
        REQUIRE (peakAbs (buf) <= amp * 1.05);
    }
}

TEST_CASE ("J-A produces no NaN/Inf on hard transients", "[hysteresis-ja][dsp]")
{
    constexpr double sr = 48000.0;
    HysteresisProcessor hp;
    hp.prepare (sr, 2);
    configureJA (hp, sr);

    // Full-scale square wave = maximum per-sample ΔH; plus DC and an impulse.
    juce::AudioBuffer<float> buf (2, 4096);
    for (int n = 0; n < 4096; ++n)
    {
        float v = (n % 2 == 0) ? 1.0f : -1.0f;   // alternating full-scale
        if (n < 512) v = 1.0f;                    // DC block
        if (n == 1000) v = 4.0f;                  // over-range impulse
        buf.setSample (0, n, v);
        buf.setSample (1, n, v);
    }
    hp.process (buf);
    REQUIRE (allFinite (buf));
}

TEST_CASE ("J-A has no gross fold-back on a slow ascending ramp", "[hysteresis-ja][dsp]")
{
    // A slow one-directional ramp traces the ascending branch of the loop. The
    // transfer must be essentially non-decreasing (no fold-back / instability).
    // The fixed-point solver adds tiny (~1e-4, inaudible) ripple, so the
    // tolerance guards against gross fold-back, not micro-ripple.
    constexpr double sr = 48000.0;
    constexpr int samples = 8192;
    HysteresisProcessor hp;
    hp.prepare (sr, 2);
    configureJA (hp, sr);

    juce::AudioBuffer<float> buf (2, samples);
    for (int n = 0; n < samples; ++n)
    {
        const auto v = static_cast<float> (-2.0 + 4.0 * n / (samples - 1));
        buf.setSample (0, n, v);
        buf.setSample (1, n, v);
    }
    hp.process (buf);

    REQUIRE (allFinite (buf));
    float prev = buf.getSample (0, 0);
    for (int n = 1; n < samples; ++n)
    {
        const float cur = buf.getSample (0, n);
        REQUIRE (cur >= prev - 1.0e-3f);
        prev = cur;
    }
}

TEST_CASE ("J-A reset clears persistent state", "[hysteresis-ja][dsp]")
{
    constexpr double sr = 48000.0;

    // Drive one processor hard, reset, then process a quiet probe.
    HysteresisProcessor used;
    used.prepare (sr, 2);
    configureJA (used, sr);
    auto loud = makeSine (1.0, 500.0, sr, 4096);
    used.process (loud);
    used.reset();
    configureJA (used, sr);
    auto probeA = makeSine (1.0e-3, 1000.0, sr, 1024);
    used.process (probeA);

    // A fresh processor given the same quiet probe must match sample-for-sample.
    HysteresisProcessor fresh;
    fresh.prepare (sr, 2);
    configureJA (fresh, sr);
    auto probeB = makeSine (1.0e-3, 1000.0, sr, 1024);
    fresh.process (probeB);

    for (int n = 0; n < 1024; ++n)
        REQUIRE (probeA.getSample (0, n) == Catch::Approx (probeB.getSample (0, n)).margin (1.0e-7));
}

TEST_CASE ("J-A is stable and unity across oversampling rates", "[hysteresis-ja][dsp]")
{
    // The solver runs inside the oversampling wrap; effectiveSampleRate is
    // base × osFactor. Small-signal unity is rate-invariant (the trapezoidal
    // dt cancels in the H domain); output stays finite/bounded at every tier.
    HysteresisProcessor hp;
    hp.prepare (48000.0, 2);

    for (double esr : { 48000.0, 96000.0, 192000.0 })
    {
        hp.reset();
        hp.updateParameters (2, 1, 0.5f, 0.5f, esr, SaturationModel::JilesAtherton);

        auto quiet = makeSine (1.0e-3, 1000.0, esr, 8192);
        const double inMag = fundamentalMag (quiet, 1000.0, esr, 4096);
        hp.process (quiet);
        const double outMag = fundamentalMag (quiet, 1000.0, esr, 4096);
        INFO ("esr " << esr << " gain " << dbDiff (outMag, inMag) << " dB");
        REQUIRE (std::abs (dbDiff (outMag, inMag)) < 0.1);

        hp.reset();
        hp.updateParameters (2, 1, 0.5f, 0.5f, esr, SaturationModel::JilesAtherton);
        auto loud = makeSine (1.0, 1000.0, esr, 8192);
        hp.process (loud);
        REQUIRE (allFinite (loud));
        REQUIRE (peakAbs (loud) <= 1.05);   // peak-safe at every rate (never expands)
    }
}
