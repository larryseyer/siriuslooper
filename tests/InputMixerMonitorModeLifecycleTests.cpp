// 2026-05-24 monitor slice — engine-level lifecycle of the per-channel
// MonitorMode (Off / Raw / Processed) and its synchronization with the
// DirectLayer route table. These tests do NOT exercise the audio thread
// or the mute-leak fix (see InputMixerMonitorMuteLeakTests.cpp for that);
// they pin the message-thread state machine that the UI's Monitor button
// drives via `InputMixer::setChannelMonitorMode`.
#include "ida/Channel.h"
#include "ida/DirectLayer.h"
#include "ida/InputMixer.h"
#include "ida/MonitorMode.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

using ida::ChannelId;
using ida::DirectLayer;
using ida::InputId;
using ida::InputMixer;
using ida::MonitorMode;
using ida::SignalType;

TEST_CASE ("InputMixer: newly-added channels default to MonitorMode::Off",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
    CHECK (layer.rawRouteCount() == 0u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("InputMixer: unknown channel id reads Off and ignores setter (no crash)",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const ChannelId nonExistent (42);
    CHECK (mixer.channelMonitorMode (nonExistent) == MonitorMode::Off);
    mixer.setChannelMonitorMode (nonExistent, MonitorMode::Processed);
    CHECK (mixer.channelMonitorMode (nonExistent) == MonitorMode::Off);
    CHECK (layer.rawRouteCount() == 0u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("InputMixer: setting Processed creates a processed route",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::Processed);

    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Processed);
    CHECK (layer.processedRouteCount() == 1u);
    CHECK (layer.rawRouteCount() == 0u);
}

TEST_CASE ("InputMixer: setting Raw creates a raw route",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::Raw);

    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Raw);
    CHECK (layer.rawRouteCount() == 1u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("InputMixer: setting Off removes the route",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::Processed);
    REQUIRE (layer.processedRouteCount() == 1u);

    mixer.setChannelMonitorMode (ch, MonitorMode::Off);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
    CHECK (layer.processedRouteCount() == 0u);
    CHECK (layer.rawRouteCount() == 0u);
}

TEST_CASE ("InputMixer: mode swap Raw → Processed swaps the route kind",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);

    mixer.setChannelMonitorMode (ch, MonitorMode::Raw);
    REQUIRE (layer.rawRouteCount() == 1u);
    REQUIRE (layer.processedRouteCount() == 0u);

    mixer.setChannelMonitorMode (ch, MonitorMode::Processed);
    CHECK (layer.rawRouteCount() == 0u);
    CHECK (layer.processedRouteCount() == 1u);

    mixer.setChannelMonitorMode (ch, MonitorMode::Raw);
    CHECK (layer.rawRouteCount() == 1u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("InputMixer: removeChannel tears down the channel's monitor route first",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::Processed);
    REQUIRE (layer.processedRouteCount() == 1u);

    mixer.removeChannel (ch);
    CHECK (layer.processedRouteCount() == 0u);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Off);
}

TEST_CASE ("InputMixer: destructor removes every monitor route from the DirectLayer",
           "[input-mixer][monitor]")
{
    // Mirrors the project-load path: the mixer is destroyed while the
    // DirectLayer (owned by MainComponent) survives. Without the dtor
    // sweep the routes would outlive the strips that own the mute atomic
    // and the next audio-callback read would dereference dangling memory.
    DirectLayer layer;
    {
        InputMixer mixer;
        mixer.setDirectLayer (&layer);
        const auto a = mixer.addChannel (InputId (0), SignalType::Audio);
        const auto b = mixer.addChannel (InputId (1), SignalType::Audio);
        mixer.setChannelMonitorMode (a, MonitorMode::Processed);
        mixer.setChannelMonitorMode (b, MonitorMode::Raw);
        REQUIRE (layer.processedRouteCount() == 1u);
        REQUIRE (layer.rawRouteCount() == 1u);
    } // mixer destructs here
    CHECK (layer.processedRouteCount() == 0u);
    CHECK (layer.rawRouteCount() == 0u);
}

TEST_CASE ("InputMixer: setChannelMonitorMode without a bound DirectLayer is no-op for routes",
           "[input-mixer][monitor]")
{
    // The collaborator is optional at the engine level — without the
    // DirectLayer bound, the mode is still tracked (so a later attach +
    // replay can engage routes), but no routes are minted in the meantime.
    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelMonitorMode (ch, MonitorMode::Processed);
    CHECK (mixer.channelMonitorMode (ch) == MonitorMode::Processed);
}

TEST_CASE ("InputMixer: monitor output pair round-trips on the per-channel state",
           "[input-mixer][monitor]")
{
    InputMixer mixer;
    DirectLayer layer;
    mixer.setDirectLayer (&layer);
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);

    mixer.setChannelMonitorMode (ch, MonitorMode::Processed, /*outputPair*/ 0);
    CHECK (mixer.channelMonitorOutputPair (ch) == 0);

    mixer.setChannelMonitorMode (ch, MonitorMode::Processed, /*outputPair*/ 3);
    CHECK (mixer.channelMonitorOutputPair (ch) == 3);

    mixer.setChannelMonitorMode (ch, MonitorMode::Off);
    CHECK (mixer.channelMonitorOutputPair (ch) == 0);
}
