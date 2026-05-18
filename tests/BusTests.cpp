// Tests for sirius::Bus + sirius::BusConfig — M5 Session 2. The Bus is the
// session-level effect-bus data model behind V3 Step 7 / V7 alignment plan
// M5 (lines 384-388). S2 establishes the configuration surfaces; S3 wires
// the audio-thread mix pipeline that consumes Bus::process.
#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/EffectChain.h"
#include "sirius/PluginDescriptor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>

using sirius::Bus;
using sirius::BusConfig;
using sirius::BusId;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::PluginDescriptor;
using sirius::PluginFormat;

// Compile-time invariant — `Bus::process` MUST be noexcept (audio-thread
// surface per docs/RT_SAFETY_CONTRACT.md §6). The header itself holds the
// stub body; this static_assert verifies the contract in test-build
// isolation matching the `ChannelStripTests.cpp` convention.
static_assert (noexcept (std::declval<Bus&>().process (
                   static_cast<float* const*> (nullptr), 0, 0)),
               "Bus::process must be noexcept (RT-safety contract §6)");

TEST_CASE ("BusConfig defaults to stereo with empty name", "[bus][bus-config]")
{
    BusConfig config;
    CHECK (config.channelCount == 2);
    CHECK (config.name.empty());
}

TEST_CASE ("Bus constructor stores id and config", "[bus]")
{
    Bus bus (BusId { 7 }, BusConfig { 1, "Aux Mono" });
    CHECK (bus.id().value() == 7);
    CHECK (bus.config().channelCount == 1);
    CHECK (bus.config().name == "Aux Mono");
}

TEST_CASE ("Bus::setEffectChain copies the chain in", "[bus][effect-chain]")
{
    Bus bus (BusId { 1 }, BusConfig { 2, "Reverb" });
    CHECK (bus.effectChain().empty());

    PluginDescriptor descriptor;
    descriptor.format = PluginFormat::Vst3;
    descriptor.name   = "ConvolutionReverb";

    EffectChainEntry entry;
    entry.descriptor = descriptor;
    entry.displayName = "ConvolutionReverb";

    EffectChain chain;
    chain = chain.withAppended (entry);

    bus.setEffectChain (chain);
    REQUIRE (bus.effectChain().size() == 1);
    CHECK (bus.effectChain().at (0).displayName == "ConvolutionReverb");

    // Source chain is independent — proves "copies in," not "moves in."
    CHECK (chain.size() == 1);
}

TEST_CASE ("Bus::process is noexcept and additively writes mixBuffer into output (M5 S3 body)",
           "[bus][rt-safety]")
{
    Bus bus (BusId { 0 }, BusConfig { 2, "Master" });

    // Populate the bus's mixBuffer via the audio-thread write accessor.
    // Left channel = 0.25, right channel = 0.75 across 8 samples.
    float* const busLeft  = bus.mixBufferChannel (0);
    float* const busRight = bus.mixBufferChannel (1);
    REQUIRE (busLeft  != nullptr);
    REQUIRE (busRight != nullptr);
    for (int s = 0; s < 8; ++s) { busLeft[s] = 0.25f; busRight[s] = 0.75f; }

    // Output pre-loaded to 0.1 so we can prove the write is ADDITIVE.
    std::array<float, 8> left;  left.fill (0.1f);
    std::array<float, 8> right; right.fill (0.1f);
    std::array<float*, 2> output { left.data(), right.data() };

    bus.process (output.data(), 2, static_cast<int> (left.size()));

    for (float v : left)  CHECK (v == Catch::Approx (0.35f));
    for (float v : right) CHECK (v == Catch::Approx (0.85f));

    // After process, mixBuffer is zeroed so the next buffer starts fresh.
    for (int s = 0; s < 8; ++s)
    {
        CHECK (busLeft[s]  == Catch::Approx (0.0f));
        CHECK (busRight[s] == Catch::Approx (0.0f));
    }
}

TEST_CASE ("Bus::process handles defensive guards without crashing",
           "[bus][rt-safety]")
{
    Bus bus (BusId { 0 }, BusConfig { 2, "Master" });

    bus.process (nullptr, 2, 64);
    std::array<float*, 2> nullChannels { nullptr, nullptr };
    bus.process (nullChannels.data(), 2, 64);
    bus.process (nullChannels.data(), 0, 64);
    bus.process (nullChannels.data(), 2, 0);
}
