// V9 Slice 3 — the core "MON owns an OutputMixer channel" contract,
// asserted as five independent clauses:
//   1. MON off (the default for a freshly-added channel) → no OutputMixer
//      channel is auto-created.
//   2. MON on → exactly one OutputMixer channel exists.
//   3. MON off after MON on → that channel is removed.
//   4. MON on while already On → idempotent (no duplicate channel).
//   5. removeChannel while MON is on → the OutputMixer channel is cleaned
//      up too (no leak).
//
// These tests intentionally do NOT exercise the audio thread — they pin the
// message-thread state machine the UI's Monitor button drives. The fact
// that the OutputMixer channel's audio source is the InputMixer's
// post-strip buffer is covered by InputMixerPostStripBufferTests + the
// existing OutputMixer renderBuffer tests.
//
// V9 Slice 4 follow-up (2026-05-24): explicit MON+mute regression — muting
// a strip while MON is on must zero the post-strip buffer, and therefore
// silence the auto-created OutputMixer channel (which reads in-place from
// that same pointer). This was implicitly covered by the now-deleted
// InputMixerMonitorMuteLeakTests; we want it explicit again.
#include "ida/Channel.h"
#include "ida/ChannelStrip.h"
#include "ida/InputMixer.h"
#include "ida/MonitorMode.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using ida::ChannelId;
using ida::InputId;
using ida::InputMixer;
using ida::MonitorMode;
using ida::OutputMixer;
using ida::SignalType;

TEST_CASE ("InputMixer MON owns an OutputMixer channel",
           "[input-mixer][mon][output-mixer]")
{
    OutputMixer output;
    InputMixer  input;
    input.attachOutputMixer (&output);

    const auto chId = input.addChannel (InputId (0), SignalType::Audio);

    SECTION ("MON off → no OutputMixer channel created for this input")
    {
        REQUIRE (output.channelCount() == 0);
    }

    SECTION ("MON on → creates exactly one OutputMixer channel")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        REQUIRE (output.channelCount() == 1);
    }

    SECTION ("MON off after MON on → removes the OutputMixer channel")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        input.setChannelMonitorMode (chId, MonitorMode::Off);
        REQUIRE (output.channelCount() == 0);
    }

    SECTION ("MON on twice is idempotent")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        const auto firstCount = output.channelCount();
        input.setChannelMonitorMode (chId, MonitorMode::On);
        REQUIRE (output.channelCount() == firstCount);
    }

    SECTION ("removing the input channel while MON is on cleans up the output channel")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        input.removeChannel (chId);
        REQUIRE (output.channelCount() == 0);
    }

    SECTION ("MON on → the auto-created OutputMixer channel carries a ChannelStrip<Audio>")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        const auto monChId = input.channelMonitorOutputChannel (chId);
        REQUIRE (monChId.has_value());
        REQUIRE (output.audioStripForChannel (*monChId) != nullptr);
    }

    SECTION ("MON on + MON output strip muted → no signal at master after render")
    {
        using ida::ChannelStrip;

        input.setChannelInputSource (chId, 0, 1, /*stereo=*/true);
        input.setChannelMonitorMode (chId, MonitorMode::On);
        const auto monChId = input.channelMonitorOutputChannel (chId);
        REQUIRE (monChId.has_value());

        constexpr int n = 64;
        std::array<float, n> left {}, right {};
        for (int i = 0; i < n; ++i) { left[i] = 0.5f; right[i] = -0.5f; }
        const float* inputs[2] { left.data(), right.data() };

        std::array<float, n> outL {}, outR {};
        float* outputs[2] { outL.data(), outR.data() };

        // Unmuted: input flows through input strip → post-strip buffer
        // → MON output strip (unmuted) → master → outputs[].
        input.renderInputGraph (inputs, 2, nullptr, 0, n);
        output.renderBuffer (nullptr, 0, outputs, 2, n);
        const bool anyAudibleUnmuted =
            (outL[0] != 0.0f) || (outR[0] != 0.0f)
         || (outL[n / 2] != 0.0f) || (outR[n / 2] != 0.0f);
        REQUIRE (anyAudibleUnmuted);

        // Mute the MON OUTPUT strip (distinct from the input strip's mute —
        // that path is covered by the "MON+mute" test below). Re-render
        // with fresh output buffers and expect silence at master.
        auto* monStrip = output.audioStripForChannel (*monChId);
        REQUIRE (monStrip != nullptr);
        monStrip->setMuted (true);

        outL.fill (0.0f);
        outR.fill (0.0f);
        input.renderInputGraph (inputs, 2, nullptr, 0, n);
        output.renderBuffer (nullptr, 0, outputs, 2, n);
        REQUIRE (outL[0] == 0.0f);
        REQUIRE (outR[0] == 0.0f);
        REQUIRE (outL[n / 2] == 0.0f);
        REQUIRE (outR[n / 2] == 0.0f);
    }
}

TEST_CASE ("MON+mute: strip mute yields silence at the auto-created OutputMixer channel",
           "[input-mixer][mon][mute]")
{
    using ida::ChannelStrip;

    OutputMixer output;
    InputMixer  input;
    input.attachOutputMixer (&output);

    const auto chId = input.addChannel (InputId (0), SignalType::Audio);
    input.setChannelInputSource (chId, 0, 1, /*stereo=*/true);
    input.setChannelMonitorMode (chId, MonitorMode::On);
    REQUIRE (output.channelCount() == 1);

    // Drive a non-zero device input through the strip. Default strip gain
    // is unity (no mute), so the post-strip buffer must carry the signal —
    // and the auto-created OutputMixer channel reads in-place from that
    // same pointer, so it would render audibly were nothing muted.
    constexpr int n = 64;
    std::array<float, n> left {}, right {};
    for (int i = 0; i < n; ++i)
    {
        left[i]  = 0.5f;
        right[i] = -0.5f;
    }
    const float* inputs[2] { left.data(), right.data() };
    input.renderInputGraph (inputs, 2, nullptr, 0, n);

    // Sanity check: unmuted, the post-strip seam carries SOMETHING. The
    // exact post-strip value depends on default pan/width law applied to
    // the stereo source; what matters here is non-silence — so that the
    // post-mute zero is a real change, not a no-op.
    REQUIRE (input.postStripPointer (chId, 0) != nullptr);
    REQUIRE (input.postStripPointer (chId, 1) != nullptr);
    REQUIRE (input.postStripPointer (chId, 0)[0] != 0.0f);
    REQUIRE (input.postStripPointer (chId, 1)[0] != 0.0f);

    // Mute the strip via the same path the UI's mute button drives.
    auto* chain = input.processingChainFor (chId);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (chain);
    strip->setMuted (true);

    // Same non-zero device input, re-rendered.
    input.renderInputGraph (inputs, 2, nullptr, 0, n);

    // Post-strip is silent — and therefore the OutputMixer MON channel
    // (which sources from this pointer) is silent too. Spot-check the
    // first sample and a midpoint to catch a partial / fade scenario.
    REQUIRE (input.postStripPointer (chId, 0)[0]      == 0.0f);
    REQUIRE (input.postStripPointer (chId, 1)[0]      == 0.0f);
    REQUIRE (input.postStripPointer (chId, 0)[n / 2]  == 0.0f);
    REQUIRE (input.postStripPointer (chId, 1)[n / 2]  == 0.0f);
}
