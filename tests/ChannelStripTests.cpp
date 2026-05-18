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
