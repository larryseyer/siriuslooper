// Tests for sirius::OutputMixer — the V3 §2.2 output-side mixer.
//
// M2 Session 3 shipped the default-ctor/dtor floor (assert-false bodies).
// M5 Session 2 extends with configuration-surface tests: channel registry,
// bus registry (including the auto-created master at BusId{0}), and the
// send-level matrix accessors. Audio-thread `renderBuffer` body is still
// a stub in S2; an integration test for the real renderBuffer lands in
// M5 Session 3.
#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/ChannelStrip.h"
#include "sirius/EffectChain.h"
#include "sirius/OutputMixer.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/SignalType.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>

using sirius::BusConfig;
using sirius::BusId;
using sirius::ChannelStrip;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::OutputChannelId;
using sirius::OutputMixer;
using sirius::PluginDescriptor;
using sirius::PluginFormat;
using sirius::SignalType;

static_assert (std::is_default_constructible_v<OutputMixer>,
               "OutputMixer must remain default-constructible");
static_assert (std::is_destructible_v<OutputMixer>);

TEST_CASE ("OutputMixer is default-constructible and destructible without crashing",
           "[output-mixer]")
{
    OutputMixer mixer;
    (void) mixer;
}

TEST_CASE ("OutputMixer::addChannel hands out sequential OutputChannelIds starting at 1",
           "[output-mixer][channel-registry]")
{
    OutputMixer mixer;

    const auto first  = mixer.addChannel (SignalType::Audio);
    const auto second = mixer.addChannel (SignalType::Audio);
    const auto third  = mixer.addChannel (SignalType::Audio);

    CHECK (first.value()  == 1);
    CHECK (second.value() == 2);
    CHECK (third.value()  == 3);
    CHECK (first != second);
    CHECK (second != third);
}

TEST_CASE ("OutputMixer auto-creates the master bus at BusId{0} on construction",
           "[output-mixer][bus-registry]")
{
    OutputMixer mixer;

    // The master is observable through `addBus` returning BusId{1} for the
    // first explicit aux bus — proves the master occupies the BusId{0} slot
    // without exposing a direct accessor (none is needed for S2).
    const auto firstAux = mixer.addBus (BusConfig { 2, "Reverb" });
    CHECK (firstAux.value() == 1);

    // Master default direct level is 1.0 for newly registered channels —
    // assertable via `sendLevelFor` once a channel exists.
    const auto channel = mixer.addChannel (SignalType::Audio);
    CHECK (mixer.sendLevelFor (channel, BusId { 0 }) == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer::addBus hands out sequential BusIds starting at 1 (master is 0)",
           "[output-mixer][bus-registry]")
{
    OutputMixer mixer;

    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb" });
    const auto delay  = mixer.addBus (BusConfig { 2, "Delay" });
    const auto chorus = mixer.addBus (BusConfig { 1, "Chorus (mono)" });

    CHECK (reverb.value() == 1);
    CHECK (delay.value()  == 2);
    CHECK (chorus.value() == 3);
}

TEST_CASE ("OutputMixer::routeChannelToBus stores send levels retrievably",
           "[output-mixer][send-matrix]")
{
    OutputMixer mixer;
    const auto channel = mixer.addChannel (SignalType::Audio);
    const auto reverb  = mixer.addBus (BusConfig { 2, "Reverb" });

    // Default: a newly added channel has 0 send to a newly added bus.
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (0.0f));

    mixer.routeChannelToBus (channel, reverb, 0.5f);
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (0.5f));

    // Master default direct level is 1.0 from addChannel — overridable.
    CHECK (mixer.sendLevelFor (channel, BusId { 0 }) == Catch::Approx (1.0f));
    mixer.routeChannelToBus (channel, BusId { 0 }, 0.75f);
    CHECK (mixer.sendLevelFor (channel, BusId { 0 }) == Catch::Approx (0.75f));
}

TEST_CASE ("OutputMixer::routeChannelToBus clamps send levels to [0, 1]",
           "[output-mixer][send-matrix]")
{
    OutputMixer mixer;
    const auto channel = mixer.addChannel (SignalType::Audio);
    const auto reverb  = mixer.addBus (BusConfig { 2, "Reverb" });

    mixer.routeChannelToBus (channel, reverb, 2.5f);
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (1.0f));

    mixer.routeChannelToBus (channel, reverb, -0.25f);
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::routeChannelToBus is a no-op for unknown ids",
           "[output-mixer][send-matrix]")
{
    OutputMixer mixer;
    const auto channel = mixer.addChannel (SignalType::Audio);
    const auto reverb  = mixer.addBus (BusConfig { 2, "Reverb" });

    // Unknown channel — must not flip the matrix entry for the real channel.
    mixer.routeChannelToBus (OutputChannelId { 999 }, reverb, 0.5f);
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (0.0f));

    // Unknown bus — likewise must not affect any matrix entry.
    mixer.routeChannelToBus (channel, BusId { 999 }, 0.5f);
    CHECK (mixer.sendLevelFor (channel, reverb) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::setChannelStrip stores ownership and is a no-op for unknown ids",
           "[output-mixer][channel-strip]")
{
    OutputMixer mixer;
    const auto channel = mixer.addChannel (SignalType::Audio);

    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
    strip->setGain (0.5f);
    mixer.setChannelStrip (channel, std::move (strip));
    // No accessor for the strip in S2 — the contract is "no crash, no
    // double-free." Session 3 adds a const accessor when the audio thread
    // needs to invoke it.

    // Unknown id — must drop the strip silently rather than throw.
    auto orphan = std::make_unique<ChannelStrip<SignalType::Audio>>();
    mixer.setChannelStrip (OutputChannelId { 999 }, std::move (orphan));
}

TEST_CASE ("OutputMixer::setBusEffectChain copies the chain into the named bus",
           "[output-mixer][bus-effect-chain]")
{
    OutputMixer mixer;
    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb" });

    PluginDescriptor descriptor;
    descriptor.format = PluginFormat::Vst3;
    descriptor.name   = "ConvolutionReverb";

    EffectChainEntry entry;
    entry.descriptor = descriptor;
    entry.displayName = "ConvolutionReverb";

    EffectChain chain;
    chain = chain.withAppended (entry);
    REQUIRE (chain.size() == 1);

    mixer.setBusEffectChain (reverb, chain);
    // No accessor in S2 — the contract is "copy-in succeeds and doesn't
    // throw." Session 3's audio-thread traversal will read through the
    // bus's `effectChain()` accessor (already on Bus.h).

    // Unknown bus — must drop the chain silently.
    mixer.setBusEffectChain (BusId { 999 }, chain);
}

TEST_CASE ("OutputMixer constants expose the hard ceilings",
           "[output-mixer][constants]")
{
    // Public constants for tests + S3 traversal — verify the values
    // documented in the header.
    static_assert (OutputMixer::kMaxOutputChannels == 32);
    static_assert (OutputMixer::kMaxBuses == 64);
    CHECK (OutputMixer::kMaxOutputChannels == 32);
    CHECK (OutputMixer::kMaxBuses == 64);
}
