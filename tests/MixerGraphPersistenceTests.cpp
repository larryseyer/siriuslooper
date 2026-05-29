#include "ida/InputMixer.h"
#include "ida/OutputMixer.h"
#include "ida/MixerGraphState.h"
#include "ida/SessionFormat.h"
#include "ida/ChannelStrip.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <optional>

using namespace ida;

namespace
{
InputDescriptor makeInputDescriptor()
{
    return InputDescriptor {
        TapeId (1), InputKind::Audio, std::string ("In 1"), std::optional<int> (0)
    };
}
} // namespace

TEST_CASE ("InputMixer survives export -> serialize -> deserialize -> import", "[sessionformat][mixer]")
{
    InputMixer source;
    source.registerInput (InputId (1), makeInputDescriptor());
    const auto drums  = source.addBus (BusConfig { 2, "Drums", BusKind::Bus });
    const auto reverb = source.addFxReturn ("Reverb");
    const auto comp = EffectChainEntry::makePlugin (PluginDescriptor{}, "comp", "");
    source.setBusEffectChain (drums, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (InputId (1), SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (source.processingChainFor (ch));
    REQUIRE (strip != nullptr);
    const auto eq = EffectChainEntry::makePlugin (PluginDescriptor{}, "eq", "");
    strip->setEffectChain (EffectChain{}.withAppended (eq));

    const auto original = source.exportGraphState();
    const auto json     = persistence::serializeMixerGraphState (original);

    InputMixer loaded;
    loaded.importGraphState (persistence::deserializeInputMixerGraphState (json));

    const auto reexported = loaded.exportGraphState();
    CHECK (reexported == original);
    // Insert chains specifically survived end-to-end (the user bus + the channel).
    const auto drumsIt = std::find_if (reexported.buses.begin(), reexported.buses.end(),
        [&] (const ida::MixerBusState& b) { return b.busId == drums.value(); });
    REQUIRE (drumsIt != reexported.buses.end());   // the user bus must survive
    CHECK (drumsIt->inserts.size() == 1);          // its insert chain must survive
    REQUIRE (reexported.channels.size() == 1);
    CHECK (reexported.channels[0].inserts.size() == 1);
}

TEST_CASE ("OutputMixer survives export -> serialize -> deserialize -> import", "[sessionformat][mixer]")
{
    OutputMixer source;
    const auto aux = source.addBus (BusConfig { 2, "Aux", BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, BusId (0)));
    const auto comp = EffectChainEntry::makePlugin (PluginDescriptor{}, "comp", "");
    source.setBusEffectChain (aux, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (SignalType::Audio);
    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
    const auto eq = EffectChainEntry::makePlugin (PluginDescriptor{}, "eq", "");
    strip->setEffectChain (EffectChain{}.withAppended (eq));
    source.setChannelStrip (ch, std::move (strip));
    source.routeChannelToBus (ch, BusId (0), 1.0f);
    source.routeChannelToBus (ch, aux, 0.4f);

    const auto original = source.exportGraphState();
    const auto json     = persistence::serializeMixerGraphState (original);

    OutputMixer loaded;
    loaded.importGraphState (persistence::deserializeOutputMixerGraphState (json));

    CHECK (loaded.exportGraphState() == original);
}

TEST_CASE ("a pre-graph session (no mixer-graph JSON) loads as ctor-default mixers", "[sessionformat][mixer]")
{
    // Forward-compat: a pre-graph session never serialized a mixer graph, so
    // nothing is imported. Both mixers stay at their ctor defaults: the input
    // mixer is empty (minimal-defaults rule), the output mixer carries master.
    InputMixer  input;
    OutputMixer output;

    CHECK (input.busCount() == 0);          // minimal-defaults: no ctor-seeded buses
    const auto outState = output.exportGraphState();
    REQUIRE (outState.buses.size() >= 1);   // master at index 0
    CHECK (outState.buses[0].busId == 0);
    CHECK (outState.channels.empty());

    // A channel added to the fresh input mixer default-routes to its tape terminal.
    input.registerInput (InputId (1), makeInputDescriptor());
    const auto chId = input.addChannel (InputId (1), SignalType::Audio);
    CHECK (input.channelMainOut (chId) == InputMixer::MainOutDest::Tape);
}

TEST_CASE ("input mixer graph serializes a non-primary tape route", "[sessionformat][mixer][tape]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.addTape (ida::TapeId { 3 }));
    REQUIRE (mixer.setChannelMainOutToTape (ch, ida::TapeId { 3 }));

    const auto json    = ida::persistence::serializeMixerGraphState (mixer.exportGraphState());
    const auto decoded = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (decoded.channels.size() == 1);
    CHECK (decoded.channels[0].mainOut.terminal == ida::MixerTerminalKind::Tape);
    CHECK (decoded.channels[0].mainOut.tapeId   == 3);
}

TEST_CASE ("InputMixer survives serialize -> deserialize with an Internal-FX insert chain",
           "[sessionformat][mixer][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    InputMixer source;
    // Seed an FX return bus with an Internal-EQ insert.
    const auto busId = source.addFxReturn ("drums");
    source.setBusEffectChain (busId,
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq)));

    // Seed a channel with an Internal-CMP insert.
    source.registerInput (InputId (1), makeInputDescriptor());
    const auto ch = source.addChannel (InputId (1), SignalType::Audio);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (source.processingChainFor (ch));
    REQUIRE (strip != nullptr);
    strip->setEffectChain (
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp)));

    const auto json  = persistence::serializeMixerGraphState (source.exportGraphState());
    const auto round = persistence::deserializeInputMixerGraphState (json);

    REQUIRE (round.buses.size() == 1);
    REQUIRE (round.buses[0].inserts.size() == 1);
    CHECK (round.buses[0].inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round.buses[0].inserts.entries()[0].internalId == InternalFxId::kEq);

    REQUIRE (round.channels.size() == 1);
    REQUIRE (round.channels[0].inserts.size() == 1);
    CHECK (round.channels[0].inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round.channels[0].inserts.entries()[0].internalId == InternalFxId::kCmp);
}

TEST_CASE ("OutputMixer survives serialize -> deserialize with an Internal-FX insert chain",
           "[sessionformat][mixer][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    OutputMixer source;
    // OutputMixer has no addFxReturn; use addBus + routeBusToBus (same shape as
    // the existing round-trip test at line 58).
    // Seed an aux bus (BusId{1}) with an Internal-RVB insert.
    // BusId{0} is the master, auto-created in the ctor.
    const auto auxId = source.addBus (BusConfig { 2, "Aux", BusKind::Bus });
    REQUIRE (source.routeBusToBus (auxId, BusId (0)));
    source.setBusEffectChain (auxId,
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kRvb)));

    // Seed a channel with an Internal-DLY insert.
    const auto ch = source.addChannel (SignalType::Audio);
    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
    strip->setEffectChain (
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly)));
    // No routeChannelToBus — this test covers insert-chain persistence only,
    // not the routing matrix (already covered by the test at line 58 above).
    source.setChannelStrip (ch, std::move (strip));

    const auto json  = persistence::serializeMixerGraphState (source.exportGraphState());
    const auto round = persistence::deserializeOutputMixerGraphState (json);

    // buses[0] is master (BusId 0); buses[1] is the aux we added.
    REQUIRE (round.buses.size() == 2);
    const auto auxIt = std::find_if (round.buses.begin(), round.buses.end(),
        [&] (const ida::MixerBusState& b) { return b.busId == auxId.value(); });
    REQUIRE (auxIt != round.buses.end());
    const auto masterIt = std::find_if (round.buses.begin(), round.buses.end(),
        [&] (const ida::MixerBusState& b) { return b.busId != auxId.value(); });
    REQUIRE (masterIt != round.buses.end());
    // Slice EC-Polish — Bus ctor auto-seeds [EQ, CMP] so master defaults
    // to those two inserts after round-trip. The bleed check now verifies
    // master's chain is EXACTLY the default seed (no RVB or DLY drifted in).
    REQUIRE (masterIt->inserts.size() == 2);
    CHECK (masterIt->inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (masterIt->inserts.entries()[0].internalId == InternalFxId::kEq);
    CHECK (masterIt->inserts.entries()[1].kind == ida::EffectChainSlotKind::Internal);
    CHECK (masterIt->inserts.entries()[1].internalId == InternalFxId::kCmp);
    REQUIRE (auxIt->inserts.size() == 1);
    CHECK (auxIt->inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (auxIt->inserts.entries()[0].internalId == InternalFxId::kRvb);

    REQUIRE (round.channels.size() == 1);
    REQUIRE (round.channels[0].inserts.size() == 1);
    CHECK (round.channels[0].inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round.channels[0].inserts.entries()[0].internalId == InternalFxId::kDly);
}

TEST_CASE ("OutputChannelState round-trips ottoSource (-1, 0..31, -2 reserved)",
           "[output-channel-state][round-trip][otto-source]")
{
    using namespace ida;

    SECTION ("default ottoSource is -1")
    {
        OutputChannelState a;
        REQUIRE (a.ottoSource == -1);
    }

    SECTION ("operator== includes ottoSource")
    {
        OutputChannelState a; a.channelId = 7;
        OutputChannelState b; b.channelId = 7;
        REQUIRE (a == b);
        b.ottoSource = 3;
        REQUIRE (a != b);
        a.ottoSource = 3;
        REQUIRE (a == b);
    }

    SECTION ("a few representative values survive copy-construct + assign")
    {
        for (int src : { -1, 0, 17, 31, -2 })
        {
            OutputChannelState a; a.channelId = 1; a.ottoSource = src;
            OutputChannelState copy = a;
            REQUIRE (copy.ottoSource == src);
            OutputChannelState assigned;
            assigned = a;
            REQUIRE (assigned.ottoSource == src);
        }
    }
}
