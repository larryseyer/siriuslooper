// 2026-05-24 monitor slice — persistence round-trip + back-compat for the
// per-channel MonitorMode field on InputChannelState. Mirrors the existing
// SessionFormatTests round-trip pattern; isolated into its own file so the
// fixture lives next to the field it covers.
#include "ida/MixerGraphState.h"
#include "ida/MonitorMode.h"
#include "ida/SessionFormat.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using namespace ida;

    InputMixerGraphState singleChannelState (MonitorMode mode, int outputPair = 0)
    {
        InputMixerGraphState s;
        InputChannelState c;
        c.channelId        = 1;
        c.signalType       = SignalType::Audio;
        c.inputSourceId    = 0;
        c.source           = { 0, 1, true };
        c.tapeMode         = TapeMode::CommitToTape;
        c.mainOut.kind     = MixerMainOut::Kind::Terminal;
        c.mainOut.terminal = MixerTerminalKind::Tape;
        c.mainOut.tapeId   = 1;
        c.monitorMode       = mode;
        c.monitorOutputPair = outputPair;
        s.channels.push_back (c);
        s.nextChannelId = 2;
        return s;
    }
}

TEST_CASE ("MonitorMode Off round-trips and is NOT emitted on the wire",
           "[sessionformat][monitor]")
{
    // Off is the operator-opt-in default per whitepaper §7.3. Saved
    // projects should not carry the field when the channel is Off so the
    // JSON stays compact and old-version reads still decode cleanly.
    const auto original = singleChannelState (MonitorMode::Off);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    CHECK (json.indexOf ("monitorMode") < 0); // compact: no field emitted for Off
}

TEST_CASE ("MonitorMode Raw round-trips through JSON",
           "[sessionformat][monitor]")
{
    const auto original = singleChannelState (MonitorMode::Raw);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    // Key is emitted (the substring matches regardless of JUCE's
    // formatting spaces around `:`); value is verified by round-trip.
    CHECK (json.indexOf ("monitorMode") >= 0);
    CHECK (json.indexOf ("Raw") >= 0);
}

TEST_CASE ("MonitorMode Processed round-trips through JSON",
           "[sessionformat][monitor]")
{
    const auto original = singleChannelState (MonitorMode::Processed);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    CHECK (json.indexOf ("monitorMode") >= 0);
    CHECK (json.indexOf ("Processed") >= 0);
}

TEST_CASE ("monitorOutputPair round-trips when non-zero",
           "[sessionformat][monitor]")
{
    const auto original = singleChannelState (MonitorMode::Processed, /*pair*/ 2);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    CHECK (json.indexOf ("monitorOutputPair") >= 0);
}

TEST_CASE ("Missing monitorMode key on load reads as Off (back-compat)",
           "[sessionformat][monitor]")
{
    // Synthesize a JSON document for a single channel with NO monitor key
    // — the byte-exact shape of pre-2026-05-24 session files. Reading must
    // succeed and the channel must come back with MonitorMode::Off.
    // Build the legacy state programmatically (with Off + zero pair so
    // nothing emits), serialize it, then check the JSON has no monitor key
    // and that re-deserializing returns Off. Synthesizing the JSON by hand
    // is brittle to other format evolutions (e.g. effectChain shape).
    const auto offState = singleChannelState (MonitorMode::Off);
    const auto json     = ida::persistence::serializeMixerGraphState (offState);

    // Pre-2026-05-24 sessions never wrote the field; we omit it for Off too.
    REQUIRE (json.indexOf ("monitorMode") < 0);
    REQUIRE (json.indexOf ("monitorOutputPair") < 0);

    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    REQUIRE (restored.channels.size() == 1u);
    CHECK (restored.channels[0].monitorMode == MonitorMode::Off);
    CHECK (restored.channels[0].monitorOutputPair == 0);
}

TEST_CASE ("Unknown monitorMode token throws (fail-loud, no silent default)",
           "[sessionformat][monitor]")
{
    // Pinned wire format: an unrecognized token is a bug, not a value to
    // silently coerce. Matches the TapeColorMode (Slice 2) and SignalType
    // contracts already in SessionFormat.
    // Start from a valid Processed-mode JSON (so the shape of every other
    // field matches whatever the current format is) and replace the value
    // string with an unknown token. `juce::String::replace` returns a new
    // string (it doesn't mutate the receiver).
    const auto       valid     = singleChannelState (MonitorMode::Processed);
    const juce::String json      = ida::persistence::serializeMixerGraphState (valid);
    const juce::String bogusJson = json.replace ("Processed", "Sideways");
    REQUIRE (bogusJson != json);

    CHECK_THROWS (ida::persistence::deserializeInputMixerGraphState (bogusJson));
}
