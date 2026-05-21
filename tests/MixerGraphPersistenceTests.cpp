#include "sirius/InputMixer.h"
#include "sirius/OutputMixer.h"
#include "sirius/MixerGraphState.h"
#include "sirius/SessionFormat.h"
#include "sirius/ChannelStrip.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>

using namespace sirius;

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
    EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (drums, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (InputId (1), SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (source.processingChainFor (ch));
    EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (EffectChain{}.withAppended (eq));

    const auto original = source.exportGraphState();
    const auto json     = persistence::serializeMixerGraphState (original);

    InputMixer loaded;
    loaded.importGraphState (persistence::deserializeInputMixerGraphState (json));

    const auto reexported = loaded.exportGraphState();
    CHECK (reexported == original);
    // Insert chains specifically survived end-to-end (the user bus + the channel).
    bool foundDrumsChain = false;
    for (const auto& b : reexported.buses)
        if (b.busId == drums.value()) { foundDrumsChain = b.inserts.size() == 1; }
    CHECK (foundDrumsChain);
    REQUIRE (reexported.channels.size() == 1);
    CHECK (reexported.channels[0].inserts.size() == 1);
}

TEST_CASE ("OutputMixer survives export -> serialize -> deserialize -> import", "[sessionformat][mixer]")
{
    OutputMixer source;
    const auto aux = source.addBus (BusConfig { 2, "Aux", BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, BusId (0)));
    EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (aux, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (SignalType::Audio);
    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
    EffectChainEntry eq; eq.displayName = "eq";
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
    // mixer with its dedicated RVB+DLY FX returns, the output mixer with master.
    InputMixer  input;
    OutputMixer output;

    CHECK (input.busCount() == 2);          // ctor RVB (busId 1) + DLY (busId 2)
    const auto outState = output.exportGraphState();
    REQUIRE (outState.buses.size() >= 1);   // master at index 0
    CHECK (outState.buses[0].busId == 0);
    CHECK (outState.channels.empty());

    // A channel added to the fresh input mixer default-routes to its tape terminal.
    input.registerInput (InputId (1), makeInputDescriptor());
    const auto chId = input.addChannel (InputId (1), SignalType::Audio);
    CHECK (input.channelMainOut (chId) == InputMixer::MainOutDest::Tape);
}
