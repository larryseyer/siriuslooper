#include "ida/MixerGraphState.h"

#include <catch2/catch_test_macros.hpp>

using namespace sirius;

namespace
{
    EffectChainEntry makeEntry (const std::string& name)
    {
        EffectChainEntry e;
        e.displayName = name;
        return e;
    }

    InputMixerGraphState populatedInput()
    {
        InputMixerGraphState s;
        MixerBusState bus;
        bus.busId = 1; bus.name = "Drums"; bus.kind = MixerBusKind::Bus;
        bus.mainOut.kind = MixerMainOut::Kind::Terminal;
        bus.mainOut.terminal = MixerTerminalKind::Tape;
        bus.inserts = EffectChain{}.withAppended (makeEntry ("comp"));
        s.buses.push_back (bus);

        InputChannelState ch;
        ch.channelId = 5; ch.signalType = SignalType::Audio; ch.inputSourceId = 2;
        ch.source = { 2, 3, true };
        ch.tapeMode = TapeMode::CommitToTape;
        ch.mainOut.kind = MixerMainOut::Kind::Bus; ch.mainOut.busId = 1;
        ch.sends.push_back ({ 7, 0.5f });
        ch.inserts = EffectChain{}.withAppended (makeEntry ("eq"));
        s.channels.push_back (ch);

        s.nextBusId = 2; s.nextChannelId = 6;
        return s;
    }
}

TEST_CASE ("equal InputMixerGraphStates compare equal", "[mixergraphstate]")
{
    CHECK (populatedInput() == populatedInput());
}

TEST_CASE ("InputMixerGraphState inequality is detected per field", "[mixergraphstate]")
{
    SECTION ("bus name")        { auto s = populatedInput(); s.buses[0].name = "X";              CHECK (s != populatedInput()); }
    SECTION ("bus kind")        { auto s = populatedInput(); s.buses[0].kind = MixerBusKind::FxReturn; CHECK (s != populatedInput()); }
    SECTION ("bus main-out")    { auto s = populatedInput(); s.buses[0].mainOut.terminal = MixerTerminalKind::HardwareOutput; CHECK (s != populatedInput()); }
    SECTION ("bus inserts")     { auto s = populatedInput(); s.buses[0].inserts = EffectChain{}; CHECK (s != populatedInput()); }
    SECTION ("channel main-out"){ auto s = populatedInput(); s.channels[0].mainOut.busId = 99;  CHECK (s != populatedInput()); }
    SECTION ("send level")      { auto s = populatedInput(); s.channels[0].sends[0].level = 0.9f; CHECK (s != populatedInput()); }
    SECTION ("source")          { auto s = populatedInput(); s.channels[0].source.stereo = false; CHECK (s != populatedInput()); }
    SECTION ("tape mode")       { auto s = populatedInput(); s.channels[0].tapeMode = TapeMode::NoTape; CHECK (s != populatedInput()); }
    SECTION ("channel inserts") { auto s = populatedInput(); s.channels[0].inserts = EffectChain{}; CHECK (s != populatedInput()); }
    SECTION ("nextBusId")       { auto s = populatedInput(); s.nextBusId = 99;                   CHECK (s != populatedInput()); }
    SECTION ("nextChannelId")   { auto s = populatedInput(); s.nextChannelId = 99;               CHECK (s != populatedInput()); }
}

TEST_CASE ("OutputMixerGraphState equality and per-field inequality", "[mixergraphstate]")
{
    OutputMixerGraphState a;
    MixerBusState master; master.busId = 0; master.name = "Master";
    master.mainOut.kind = MixerMainOut::Kind::Terminal;
    master.mainOut.terminal = MixerTerminalKind::HardwareOutput;
    a.buses.push_back (master);
    OutputChannelState ch; ch.channelId = 1; ch.sends.push_back ({ 0, 1.0f });
    a.channels.push_back (ch);
    a.nextBusId = 1; a.nextChannelId = 2;

    CHECK (a == a);
    SECTION ("send")    { auto b = a; b.channels[0].sends[0].level = 0.3f; CHECK (a != b); }
    SECTION ("master name") { auto b = a; b.buses[0].name = "M"; CHECK (a != b); }
    SECTION ("nextChannelId") { auto b = a; b.nextChannelId = 9; CHECK (a != b); }
    SECTION ("master busId")     { auto b = a; b.buses[0].busId = 9;                          CHECK (a != b); }
    SECTION ("master kind")      { auto b = a; b.buses[0].kind = MixerBusKind::FxReturn;       CHECK (a != b); }
    SECTION ("master mainOut")   { auto b = a; b.buses[0].mainOut.terminal = MixerTerminalKind::Tape; CHECK (a != b); }
    SECTION ("master inserts")   { auto b = a; b.buses[0].inserts = EffectChain{}.withAppended (makeEntry ("x")); CHECK (a != b); }
    SECTION ("channel signalType") { auto b = a; b.channels[0].signalType = SignalType::Midi;  CHECK (a != b); }
    SECTION ("channel inserts")  { auto b = a; b.channels[0].inserts = EffectChain{}.withAppended (makeEntry ("y")); CHECK (a != b); }
    SECTION ("nextBusId")        { auto b = a; b.nextBusId = 9;                                CHECK (a != b); }
}
