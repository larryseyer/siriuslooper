// V9 monitor slice — persistence round-trip + V8 back-compat for the
// per-channel MonitorMode field on InputChannelState. V9 collapses the V8
// tri-state (Off / Raw / Processed) to a binary toggle (Off / On); V8
// sessions ("Raw" / "Processed") must still load (both coerce to On).
//
// Token helpers (monitorModeToken / monitorModeFromString) live in an
// anonymous namespace inside SessionFormat.cpp, so these tests exercise
// the contract via the public serialize / deserialize round-trip — the
// same pattern as the rest of SessionFormatTests.
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
    // Off is the operator-opt-in default per whitepaper §7.2. Saved
    // projects should not carry the field when the channel is Off so the
    // JSON stays compact and old-version reads still decode cleanly.
    const auto original = singleChannelState (MonitorMode::Off);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    CHECK (json.indexOf ("monitorMode") < 0); // compact: no field emitted for Off
}

TEST_CASE ("MonitorMode On round-trips through JSON as \"On\"",
           "[sessionformat][monitor]")
{
    const auto original = singleChannelState (MonitorMode::On);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    // V9 emits the token "On" (not "Raw" / "Processed"). Substring check
    // matches regardless of JUCE's formatting spaces around `:`.
    CHECK (json.indexOf ("monitorMode") >= 0);
    CHECK (json.indexOf ("On") >= 0);
    CHECK (json.indexOf ("Raw") < 0);
    CHECK (json.indexOf ("Processed") < 0);
}

TEST_CASE ("monitorOutputPair round-trips when non-zero",
           "[sessionformat][monitor]")
{
    const auto original = singleChannelState (MonitorMode::On, /*pair*/ 2);
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);

    CHECK (restored == original);
    CHECK (json.indexOf ("monitorOutputPair") >= 0);
}

TEST_CASE ("Missing monitorMode key on load reads as Off (back-compat)",
           "[sessionformat][monitor]")
{
    // Pre-2026-05-24 sessions never wrote the field; we also omit it for
    // Off so the JSON stays compact. Synthesize a fresh Off-state and
    // confirm round-trip omission + restore-as-Off.
    const auto offState = singleChannelState (MonitorMode::Off);
    const auto json     = ida::persistence::serializeMixerGraphState (offState);

    REQUIRE (json.indexOf ("monitorMode") < 0);
    REQUIRE (json.indexOf ("monitorOutputPair") < 0);

    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    REQUIRE (restored.channels.size() == 1u);
    CHECK (restored.channels[0].monitorMode == MonitorMode::Off);
    CHECK (restored.channels[0].monitorOutputPair == 0);
}

TEST_CASE ("V8 legacy \"Processed\" token reads as On (V9 back-compat)",
           "[sessionformat][monitor]")
{
    // V8 emitted "Processed" for the post-strip mode. V9 collapses Raw /
    // Processed into a single On — V8 projects must still load. Synthesize
    // a V9 On-state JSON then string-swap the token to simulate a V8 file.
    const auto         onState = singleChannelState (MonitorMode::On);
    const juce::String v9Json   = ida::persistence::serializeMixerGraphState (onState);
    const juce::String v8Json   = v9Json.replace ("\"On\"", "\"Processed\"");
    REQUIRE (v8Json != v9Json);
    REQUIRE (v8Json.indexOf ("Processed") >= 0);

    const auto restored = ida::persistence::deserializeInputMixerGraphState (v8Json);
    REQUIRE (restored.channels.size() == 1u);
    CHECK (restored.channels[0].monitorMode == MonitorMode::On);
}

TEST_CASE ("V8 legacy \"Raw\" token reads as On (V9 back-compat — collapsed)",
           "[sessionformat][monitor]")
{
    // V8's "Raw" pre-strip mode is gone in V9. Sessions saved with "Raw"
    // must still load — V9 coerces both legacy tokens to On so the
    // operator's intent (the channel is monitoring) survives the version
    // bump; the loss of pre-strip semantics is documented in the
    // whitepaper V9 migration note.
    const auto         onState = singleChannelState (MonitorMode::On);
    const juce::String v9Json   = ida::persistence::serializeMixerGraphState (onState);
    const juce::String v8Json   = v9Json.replace ("\"On\"", "\"Raw\"");
    REQUIRE (v8Json != v9Json);
    REQUIRE (v8Json.indexOf ("Raw") >= 0);

    const auto restored = ida::persistence::deserializeInputMixerGraphState (v8Json);
    REQUIRE (restored.channels.size() == 1u);
    CHECK (restored.channels[0].monitorMode == MonitorMode::On);
}

TEST_CASE ("Unknown monitorMode token throws (fail-loud, no silent default)",
           "[sessionformat][monitor]")
{
    // Pinned wire format: an unrecognized token is a bug, not a value to
    // silently coerce. Matches the TapeColorMode (Slice 2) and SignalType
    // contracts already in SessionFormat. Start from a valid V9 On-mode
    // JSON and replace the token with garbage. `juce::String::replace`
    // returns a new string (it does not mutate the receiver).
    const auto         valid     = singleChannelState (MonitorMode::On);
    const juce::String json      = ida::persistence::serializeMixerGraphState (valid);
    const juce::String bogusJson = json.replace ("\"On\"", "\"Sideways\"");
    REQUIRE (bogusJson != json);

    CHECK_THROWS (ida::persistence::deserializeInputMixerGraphState (bogusJson));
}
