/*
    TapeColorChainPeakTests — full-chain peak-safety regression.

    Copyright (C) 2026 Larry Seyer
    Licensed under the GNU Affero General Public License v3.0 (see LICENSE).

    The bare-HysteresisProcessor J-A tests run the solver at base rate on
    sine/ramp signals. The PLUGIN path is different in four ways that together
    produced an instant Reaper overload when the operator enabled the J-A model:
    oversampling (default Standard/2x), the record-emphasis HF pre-boost, the
    +3 dB default Level pre-drive, and real peaky program material. None of that
    was covered, so a transient-expanding makeup shipped green.

    This suite drives the FULL lsfx::TapeColorProcessor chain at its defaults
    with a program-like signal (a sustained loud bed plus periodic full-scale
    transients — the crest profile that makes a broadband-RMS makeup expand
    peaks) and pins the one invariant a tape saturation stage must never break:

        output true-peak <= input true-peak + a small margin

    for BOTH saturation models. asinh is the passing control; J-A is the
    regression that must hold once the operating point is fixed.
*/

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <lsfx_tapecolor/dsp/TapeColorProcessor.h>

using lsfx::TapeColorProcessor;
using TapeColorConfig = lsfx::tapecolor::TapeColorConfig;

namespace
{
    constexpr double kPi        = 3.14159265358979323846;
    constexpr double kSampleR   = 48000.0;
    constexpr int    kBlock     = 512;
    constexpr int    kSeconds   = 1;
    constexpr int    kTotal     = static_cast<int> (kSampleR) * kSeconds;

    bool allFinite (const juce::AudioBuffer<float>& buf)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int n = 0; n < buf.getNumSamples(); ++n)
                if (! std::isfinite (buf.getSample (ch, n)))
                    return false;
        return true;
    }

    float peakAbs (const juce::AudioBuffer<float>& buf)
    {
        float p = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int n = 0; n < buf.getNumSamples(); ++n)
                p = std::max (p, std::abs (buf.getSample (ch, n)));
        return p;
    }

    // Program-like signal: a loud sustained low-frequency bed (drives the tape
    // stage into compression, which is what winds a makeup gain up) with
    // periodic full-scale transient spikes riding on top (high crest factor).
    // A non-expanding stage keeps every output sample at or below the input
    // peak of 1.0; a transient-boosting makeup pushes the spikes past it.
    void fillProgram (juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double bed = 0.6 * std::sin (2.0 * kPi * 60.0 * i / kSampleR);
            // Full-scale two-sample spike every 2000 samples (~24 Hz click).
            const double spike = (i % 2000 < 2) ? 1.0 : 0.0;
            float v = static_cast<float> (juce::jlimit (-1.0, 1.0, bed + spike));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
    }

    // Run the full chain at defaults (Standard 2x, Level +3, enabled) with the
    // given saturation model, block by block, and return the output peak.
    float runChain (int saturationModel, float& inPeakOut)
    {
        TapeColorProcessor dsp;
        dsp.prepare (kSampleR, kBlock, 2);

        TapeColorConfig cfg;            // defaults: machine S821, tape T456,
        cfg.enabled         = true;     // Standard 2x, Level +3, mix 1.0
        cfg.saturationModel = saturationModel;
        dsp.scratchConfig() = cfg;
        dsp.commitConfig();

        juce::AudioBuffer<float> full (2, kTotal);
        fillProgram (full);
        inPeakOut = peakAbs (full);

        float outPeak = 0.0f;
        for (int start = 0; start < kTotal; start += kBlock)
        {
            const int len = juce::jmin (kBlock, kTotal - start);
            juce::AudioBuffer<float> block (2, len);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, full, ch, start, len);

            dsp.process (block);

            REQUIRE (allFinite (block));
            outPeak = std::max (outPeak, peakAbs (block));
        }
        return outPeak;
    }
} // namespace

TEST_CASE ("full chain: asinh never expands peaks (control)", "[tapecolor-chain][dsp]")
{
    float inPeak = 0.0f;
    const float outPeak = runChain (/*asinh*/ 0, inPeak);
    const float marginDb = 20.0f * std::log10 (outPeak / inPeak);
    INFO ("asinh in-peak " << inPeak << " out-peak " << outPeak
          << " (" << marginDb << " dB)");
    REQUIRE (marginDb < 0.5f);
}

TEST_CASE ("full chain: J-A never expands peaks (regression)", "[tapecolor-chain][dsp]")
{
    // The bug: enabling J-A in Reaper instantly overloaded the track because a
    // broadband-RMS makeup re-expanded transients above full scale. A tape
    // saturation stage must compress, never expand — output peak must not
    // exceed the input peak (small margin for oversampling halfband overshoot).
    float inPeak = 0.0f;
    const float outPeak = runChain (/*J-A*/ 1, inPeak);
    const float marginDb = 20.0f * std::log10 (outPeak / inPeak);
    INFO ("J-A in-peak " << inPeak << " out-peak " << outPeak
          << " (" << marginDb << " dB)");
    REQUIRE (marginDb < 0.5f);
}
