// Tests for sirius::ChannelStrip<SignalType> — M5 Session 1 (per V7
// alignment plan amendment §3). The `Audio` specialization carries real
// gain/pan DSP; the other specializations are no-op stubs until their
// real-DSP milestones (M9 / M12 / M13).
//
// The signature is JUCE-free (raw float* const* + counts) because the
// engine layer's public API is JUCE-free per engine/CMakeLists.txt.
#include "sirius/ChannelStrip.h"
#include "sirius/ProcessingChain.h"
#include "sirius/SignalType.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <utility>

using sirius::ChannelStrip;
using sirius::FileChain;
using sirius::MidiChain;
using sirius::SignalType;
using sirius::VideoChain;

using AudioStrip = ChannelStrip<SignalType::Audio>;

// Compile-time invariant — `ChannelStrip<Audio>::process` MUST be noexcept
// (audio-thread surface per docs/RT_SAFETY_CONTRACT.md §6). The header itself
// holds the same static_assert via std::declval; this one is duplicated here
// so a test-build verifies it in isolation.
static_assert (noexcept (std::declval<AudioStrip&>().process (
                   static_cast<float* const*> (nullptr), 0, 0)),
               "ChannelStrip<Audio>::process must be noexcept");

TEST_CASE ("ChannelStrip<Audio> constructs with default gain 1.0 + pan 0.5 (center)",
           "[channel-strip]")
{
    AudioStrip strip;
    CHECK (strip.signalType() == SignalType::Audio);
    CHECK (strip.gain() == Catch::Approx (1.0f));
    CHECK (strip.pan()  == Catch::Approx (0.5f));
}

TEST_CASE ("ChannelStrip<Audio> applies gain to a mono buffer of all-1.0f samples",
           "[channel-strip][gain]")
{
    AudioStrip strip;
    strip.setGain (0.5f);

    std::array<float, 8> mono;
    mono.fill (1.0f);
    float* channelData[1] { mono.data() };

    strip.process (channelData, 1, static_cast<int> (mono.size()));

    for (float v : mono) CHECK (v == Catch::Approx (0.5f));
}

TEST_CASE ("ChannelStrip<Audio> pan=0.0 zeros the right channel; pan=1.0 zeros the left",
           "[channel-strip][pan]")
{
    SECTION ("pan = 0.0 → hard left, right silent")
    {
        AudioStrip strip;
        strip.setPan (0.0f);

        std::array<float, 4> left, right;
        left.fill (1.0f);
        right.fill (1.0f);
        float* channelData[2] { left.data(), right.data() };

        strip.process (channelData, 2, static_cast<int> (left.size()));

        for (float v : left)  CHECK (v == Catch::Approx (1.0f));   // cos(0) = 1
        for (float v : right) CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    }

    SECTION ("pan = 1.0 → hard right, left silent")
    {
        AudioStrip strip;
        strip.setPan (1.0f);

        std::array<float, 4> left, right;
        left.fill (1.0f);
        right.fill (1.0f);
        float* channelData[2] { left.data(), right.data() };

        strip.process (channelData, 2, static_cast<int> (left.size()));

        for (float v : left)  CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
        for (float v : right) CHECK (v == Catch::Approx (1.0f));   // sin(pi/2) = 1
    }

    SECTION ("pan = 0.5 → equal-power center (both ~0.707)")
    {
        AudioStrip strip;
        strip.setPan (0.5f);

        std::array<float, 4> left, right;
        left.fill (1.0f);
        right.fill (1.0f);
        float* channelData[2] { left.data(), right.data() };

        strip.process (channelData, 2, static_cast<int> (left.size()));

        const float expected = std::cos (sirius::kHalfPi * 0.5f); // ~0.7071
        for (float v : left)  CHECK (v == Catch::Approx (expected));
        for (float v : right) CHECK (v == Catch::Approx (expected));
    }
}

TEST_CASE ("ChannelStrip<Audio> ignores pan on a mono buffer (gain only)",
           "[channel-strip][pan]")
{
    AudioStrip strip;
    strip.setGain (2.0f);
    strip.setPan (0.0f); // would be hard-left on stereo

    std::array<float, 4> mono;
    mono.fill (1.0f);
    float* channelData[1] { mono.data() };

    strip.process (channelData, 1, static_cast<int> (mono.size()));

    // Mono path multiplies by gain only — no pan attenuation.
    for (float v : mono) CHECK (v == Catch::Approx (2.0f));
}

// =============================================================================
// [width] — stereo width (mid/side) control. Net-new in the pan+width detail
// panel slice (OTTO MixerChannel parity): width 0 = mono-collapse, 1 = unity
// (the side signal is untouched), 2 = double-wide. Width is applied AFTER the
// equal-power pan, so every expectation below folds in the pan gain.
// =============================================================================

TEST_CASE ("ChannelStrip<Audio> defaults to unity width 1.0 and clamps the setter to [0, 2]",
           "[channel-strip][width]")
{
    AudioStrip strip;
    CHECK (strip.width() == Catch::Approx (1.0f));   // unity by default — no stereo change

    strip.setWidth (-1.0f);
    CHECK (strip.width() == Catch::Approx (0.0f));    // a stray caller cannot drive width negative

    strip.setWidth (5.0f);
    CHECK (strip.width() == Catch::Approx (2.0f));    // clamped to the double-wide ceiling
}

TEST_CASE ("ChannelStrip<Audio> width=1.0 leaves the panned block byte-identical to pan-only",
           "[channel-strip][width]")
{
    // Why: the unity fast-path must not perturb the existing pan DSP. Same input,
    // same pan, width left at its 1.0 default → the pan-only expectation holds.
    AudioStrip strip;
    strip.setPan (0.5f);          // equal-power center
    // width stays 1.0 (default)

    std::array<float, 4> left, right;
    left.fill (1.0f);
    right.fill (0.5f);
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    const float centerGain = std::cos (sirius::kHalfPi * 0.5f); // ~0.7071
    for (float v : left)  CHECK (v == Catch::Approx (1.0f * centerGain));
    for (float v : right) CHECK (v == Catch::Approx (0.5f * centerGain));
}

TEST_CASE ("ChannelStrip<Audio> width=0.0 collapses a side-only signal to silence",
           "[channel-strip][width]")
{
    // Why: width 0 keeps only the mid (L+R)/2. An anti-phase pair (pure side) is
    // exactly what mono-collapse must null. Pan is centered so leftGain==rightGain.
    AudioStrip strip;
    strip.setPan  (0.5f);
    strip.setWidth (0.0f);

    std::array<float, 4> left, right;
    left.fill  ( 1.0f);
    right.fill (-1.0f);   // anti-phase → mid = 0
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    for (float v : left)  CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    for (float v : right) CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ChannelStrip<Audio> width=2.0 doubles the side component",
           "[channel-strip][width]")
{
    // Why: width 2 must amplify the side (L-R)/2 by 2 while preserving the mid.
    // Input is hard-content-left/silent-right (post equal-power center pan).
    AudioStrip strip;
    strip.setPan  (0.5f);
    strip.setWidth (2.0f);

    std::array<float, 4> left, right;
    left.fill  (1.0f);
    right.fill (0.0f);
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    const float cg = std::cos (sirius::kHalfPi * 0.5f); // center pan gain ~0.7071
    const float l  = cg, r = 0.0f;                       // post-pan
    const float mid = (l + r) * 0.5f;
    const float side = (l - r) * 0.5f * 2.0f;
    for (float v : left)  CHECK (v == Catch::Approx (mid + side));
    for (float v : right) CHECK (v == Catch::Approx (mid - side));
}

TEST_CASE ("ChannelStrip<Audio> width composes with gain and pan",
           "[channel-strip][width]")
{
    // Why: width is the last stereo stage — gain and pan must apply first, then
    // the mid/side fold. gain 2 × center pan, then width 0 collapses to the mid.
    AudioStrip strip;
    strip.setGain  (2.0f);
    strip.setPan   (0.5f);
    strip.setWidth (0.0f);

    std::array<float, 4> left, right;
    left.fill  (1.0f);
    right.fill (0.0f);
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    const float postPanLeft = 2.0f * std::cos (sirius::kHalfPi * 0.5f); // gain×panL
    const float mid = postPanLeft * 0.5f;                                // (l+0)/2
    for (float v : left)  CHECK (v == Catch::Approx (mid));
    for (float v : right) CHECK (v == Catch::Approx (mid));
}

TEST_CASE ("ChannelStrip<Audio> mute silences output regardless of width",
           "[channel-strip][width][mute]")
{
    // Why: the mute early-return precedes the width stage; width must not be able
    // to resurrect a muted channel.
    AudioStrip strip;
    strip.setMuted (true);
    strip.setWidth (2.0f);

    std::array<float, 4> left, right;
    left.fill  (0.7f);
    right.fill (-0.7f);
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    for (float v : left)  CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    for (float v : right) CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (strip.peakLeft()  == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (strip.peakRight() == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ChannelStrip<Audio> peak meters reflect the post-width signal",
           "[channel-strip][width][meter]")
{
    // Why: metering taps the final L/R, so a width change must move the peaks.
    AudioStrip strip;
    strip.setPan   (0.5f);
    strip.setWidth (2.0f);

    std::array<float, 3> left  { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> right { 0.0f, 0.0f, 0.0f };
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, 3);

    const float cg = std::cos (sirius::kHalfPi * 0.5f);
    const float mid = cg * 0.5f, side = cg * 0.5f * 2.0f;
    CHECK (strip.peakLeft()  == Catch::Approx (std::fabs (mid + side)));
    CHECK (strip.peakRight() == Catch::Approx (std::fabs (mid - side)));
}

TEST_CASE ("ChannelStrip<Audio> ignores width on a mono buffer",
           "[channel-strip][width]")
{
    // Why: a mono buffer has no stereo field; width is a no-op (gain only),
    // exactly as pan is ignored on the mono path.
    AudioStrip strip;
    strip.setGain  (2.0f);
    strip.setWidth (0.0f);   // would collapse stereo, but there is no side here

    std::array<float, 4> mono;
    mono.fill (1.0f);
    float* channelData[1] { mono.data() };

    strip.process (channelData, 1, static_cast<int> (mono.size()));

    for (float v : mono) CHECK (v == Catch::Approx (2.0f));
}

TEST_CASE ("ChannelStrip<Audio> defaults to un-muted with zero meters",
           "[channel-strip][mute][meter]")
{
    const AudioStrip strip;
    CHECK_FALSE (strip.muted());
    CHECK (strip.peakLeft()  == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (strip.peakRight() == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ChannelStrip<Audio> mute silences the output and zeroes the meters",
           "[channel-strip][mute]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    strip.setMuted (true);
    CHECK (strip.muted());

    std::array<float, 4> left, right;
    left.fill (0.8f);
    right.fill (-0.6f);
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, static_cast<int> (left.size()));

    for (float v : left)  CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    for (float v : right) CHECK (v == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (strip.peakLeft()  == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (strip.peakRight() == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ChannelStrip<Audio> meters the post-fader peak per side",
           "[channel-strip][meter]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    strip.setPan (0.0f); // hard left: leftGain = 1, rightGain = 0

    std::array<float, 3> left  { 0.3f, -0.8f, 0.5f };
    std::array<float, 3> right { 0.2f,  0.2f, 0.2f };
    float* channelData[2] { left.data(), right.data() };

    strip.process (channelData, 2, 3);

    CHECK (strip.peakLeft()  == Catch::Approx (0.8f));                 // max|left| post-fader
    CHECK (strip.peakRight() == Catch::Approx (0.0f).margin (1e-6f));  // panned out
}

TEST_CASE ("ChannelStrip<Audio> meter reflects the current block, not a running max",
           "[channel-strip][meter]")
{
    AudioStrip strip;
    strip.setPan (0.0f); // hard left for a clean left-side reading

    std::array<float, 2> loudL { 0.9f, 0.9f }, loudR { 0.0f, 0.0f };
    float* loud[2] { loudL.data(), loudR.data() };
    strip.process (loud, 2, 2);
    CHECK (strip.peakLeft() == Catch::Approx (0.9f));

    std::array<float, 2> quietL { 0.1f, 0.1f }, quietR { 0.0f, 0.0f };
    float* quiet[2] { quietL.data(), quietR.data() };
    strip.process (quiet, 2, 2);
    CHECK (strip.peakLeft() == Catch::Approx (0.1f)); // current block, not the prior 0.9
}

TEST_CASE ("ChannelStrip Midi / Video / File stubs construct and report SignalType",
           "[channel-strip][stubs]")
{
    const ChannelStrip<SignalType::Midi>  midi;
    const ChannelStrip<SignalType::Video> video;
    const ChannelStrip<SignalType::File>  file;

    CHECK (midi.signalType()  == SignalType::Midi);
    CHECK (video.signalType() == SignalType::Video);
    CHECK (file.signalType()  == SignalType::File);

    // Legacy M3-era chains (still in ProcessingChain.h until M9/M12/M13)
    // continue to construct and report their SignalType too.
    const MidiChain  m;
    const VideoChain v;
    const FileChain  f;
    CHECK (m.signalType() == SignalType::Midi);
    CHECK (v.signalType() == SignalType::Video);
    CHECK (f.signalType() == SignalType::File);
}

// =============================================================================
// [lufs] — per-channel EBU R128 loudness, the LUFS half of the dual OTTO-style
// channel meter. Every audio channel carries it (ChannelStrip<Audio>); the UI
// reads lufsIntegrated() on its timer.
// =============================================================================

namespace
{
    // Runs `blocks` 512-sample blocks of a 1 kHz sine at `amp` through the strip
    // (process applies gain/pan in place AND feeds the loudness meter), and
    // returns the strip's integrated LUFS afterwards.
    float feedSineGetLufs (AudioStrip& strip, float amp, double sampleRate, int blocks)
    {
        constexpr int kBlock = 512;
        std::array<float, kBlock> left {}, right {};
        float* data[2] { left.data(), right.data() };
        double phase = 0.0;
        const double inc = 2.0 * M_PI * 1000.0 / sampleRate;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = amp * static_cast<float> (std::sin (phase));
                phase += inc;
                left[static_cast<std::size_t> (i)]  = s;
                right[static_cast<std::size_t> (i)] = s;
            }
            strip.process (data, 2, kBlock);
        }
        return strip.lufsIntegrated();
    }
}

TEST_CASE ("ChannelStrip<Audio> reports integrated loudness above the gate for a "
           "loud signal and stays gated on silence", "[channel-strip][lufs]")
{
    constexpr double kSampleRate = 48000.0;

    AudioStrip loud;
    loud.prepare (kSampleRate, 512);
    const float loudLufs = feedSineGetLufs (loud, 0.5f, kSampleRate, 300);  // ~3.2 s
    CHECK (std::isfinite (loudLufs));
    CHECK (loudLufs > -40.0f);          // a half-scale tone is well above the -70 gate

    AudioStrip silent;
    silent.prepare (kSampleRate, 512);
    const float silentLufs = feedSineGetLufs (silent, 0.0f, kSampleRate, 300);
    CHECK (silentLufs <= -70.0f);       // absolute gate → no integration on silence
}

TEST_CASE ("ChannelStrip<Audio> integrated loudness tracks level — louder reads higher",
           "[channel-strip][lufs]")
{
    constexpr double kSampleRate = 48000.0;

    AudioStrip loud;  loud.prepare  (kSampleRate, 512);
    AudioStrip quiet; quiet.prepare (kSampleRate, 512);

    const float loudLufs  = feedSineGetLufs (loud,  0.5f,  kSampleRate, 300);
    const float quietLufs = feedSineGetLufs (quiet, 0.05f, kSampleRate, 300);  // ~20 dB down

    CHECK (loudLufs > quietLufs + 6.0f);   // a 20 dB level drop reads clearly lower
}

TEST_CASE ("ChannelStrip<Audio> loudness is silent + safe before prepare() is called",
           "[channel-strip][lufs]")
{
    AudioStrip strip;   // no prepare()
    const float lufs = feedSineGetLufs (strip, 0.5f, 48000.0, 50);  // must not crash
    CHECK (lufs <= -70.0f);   // unprepared meter reports silence
}
