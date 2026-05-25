// V9 Slice 3 monitor lifecycle — engine-level lifecycle of the per-channel
// MonitorMode (Off / On) and its synchronization with the attached
// OutputMixer's channel registry. V9 collapses the previous tri-state
// (Off / Raw / Processed) to a binary toggle; the InputMixer no longer
// drives DirectLayer routes — instead MON-on auto-creates an OutputMixer
// channel whose audio source reads this input's post-strip buffer.
//
// These tests pin the message-thread state machine that the UI's Monitor
// button drives via `InputMixer::setChannelMonitorMode`. The mute-leak
// contract still lives in `InputMixerMonitorMuteLeakTests.cpp` (DirectLayer
// level — that path remains until Slice 4 deletes DirectLayer entirely).
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

TEST_CASE ("InputMixer: newly-added channels default to MonitorMode::Off",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
    CHECK (output.channelCount() == 0);
}

TEST_CASE ("InputMixer: unknown channel id reads Off and ignores setter (no crash)",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const ChannelId nonExistent (42);
    CHECK (mixer.channelMonitorMode (nonExistent) == MonitorMode::Off);
    mixer.setChannelMonitorMode (nonExistent, MonitorMode::On);
    CHECK (mixer.channelMonitorMode (nonExistent) == MonitorMode::Off);
    CHECK (output.channelCount() == 0);
}

TEST_CASE ("InputMixer: setting On creates exactly one OutputMixer channel",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::On);

    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::On);
    CHECK (output.channelCount() == 1);
    CHECK (mixer.channelMonitorOutputChannel (ch).has_value());
}

TEST_CASE ("InputMixer: setting Off after On removes the OutputMixer channel",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::On);
    REQUIRE (output.channelCount() == 1);

    mixer.setChannelMonitorMode (ch, MonitorMode::Off);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
    CHECK (output.channelCount() == 0);
    CHECK_FALSE (mixer.channelMonitorOutputChannel (ch).has_value());
}

TEST_CASE ("InputMixer: MON on is idempotent — does not mint a duplicate channel",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);

    mixer.setChannelMonitorMode (ch, MonitorMode::On);
    REQUIRE (output.channelCount() == 1);
    const auto firstId = mixer.channelMonitorOutputChannel (ch);

    mixer.setChannelMonitorMode (ch, MonitorMode::On);
    CHECK (output.channelCount() == 1);
    CHECK (mixer.channelMonitorOutputChannel (ch) == firstId);
}

TEST_CASE ("InputMixer: removeChannel tears down the channel's MON OutputMixer channel first",
           "[input-mixer][monitor]")
{
    OutputMixer output;
    InputMixer  mixer;
    mixer.attachOutputMixer (&output);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::On);
    REQUIRE (output.channelCount() == 1);

    mixer.removeChannel (ch);
    CHECK (output.channelCount() == 0);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
}

TEST_CASE ("InputMixer: destructor removes every MON channel from the OutputMixer",
           "[input-mixer][monitor]")
{
    // Mirrors the project-load path: the mixer is destroyed while the
    // OutputMixer (owned by MainComponent) survives. Without the dtor
    // sweep, OutputMixer's setChannelAudioSource pointers would outlive
    // the InputMixer's postStrip_ storage and the next audio-callback
    // resume would dereference dangling memory.
    OutputMixer output;
    {
        InputMixer mixer;
        mixer.attachOutputMixer (&output);
        const auto a = mixer.addChannel (InputId (0), SignalType::Audio);
        const auto b = mixer.addChannel (InputId (1), SignalType::Audio);
        mixer.setChannelMonitorMode (a, MonitorMode::On);
        mixer.setChannelMonitorMode (b, MonitorMode::On);
        REQUIRE (output.channelCount() == 2);
    } // mixer destructs here
    CHECK (output.channelCount() == 0);
}

TEST_CASE ("InputMixer: setChannelMonitorMode without an attached OutputMixer tracks mode but mints nothing",
           "[input-mixer][monitor]")
{
    // The collaborator is optional at the engine level — without the
    // OutputMixer attached, the mode is still tracked (so a later attach +
    // replay can engage MON channels), but no OutputMixer channel is minted
    // in the meantime.
    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::On);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::On);
    CHECK_FALSE (mixer.channelMonitorOutputChannel (ch).has_value());
}
