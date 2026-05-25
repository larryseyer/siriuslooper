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
#include "ida/Channel.h"
#include "ida/InputMixer.h"
#include "ida/MonitorMode.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

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
}
