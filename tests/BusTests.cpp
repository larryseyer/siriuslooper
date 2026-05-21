// Tests for sirius::Bus + sirius::BusConfig — M5 Session 2. The Bus is the
// session-level effect-bus data model behind V3 Step 7 / V7 alignment plan
// M5 (lines 384-388). S2 establishes the configuration surfaces; S3 wires
// the audio-thread mix pipeline that consumes Bus::process.
#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/EffectChain.h"
#include "sirius/LufsMeter.h"
#include "sirius/PluginDescriptor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

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

TEST_CASE ("LufsMeter is move-constructible and a moved meter still measures", "[lufs][move]")
{
    static_assert (std::is_move_constructible_v<sirius::LufsMeter>,
                   "LufsMeter must be movable so Bus can live in std::vector<Bus>");

    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 512;

    sirius::LufsMeter source;
    source.prepare (kSampleRate, kBlock);

    sirius::LufsMeter moved (std::move (source));   // move-construct after prepare()
    // `source` is now in a moved-from (unspecified) state — not used again below.

    // Feed a 1 kHz sine (NOT DC — K-weighting's high-pass would kill a constant);
    // integrated loudness must rise above the -70 LUFS absolute gate, proving the
    // moved-into meter is fully functional.
    std::array<float, static_cast<std::size_t> (kBlock)> buf {};
    double phase = 0.0;
    const double inc = 2.0 * M_PI * 1000.0 / kSampleRate;
    for (int i = 0; i < 300; ++i)
    {
        for (int n = 0; n < kBlock; ++n)
        {
            buf[static_cast<std::size_t> (n)] = 0.5f * static_cast<float> (std::sin (phase));
            phase += inc;
        }
        moved.process (buf.data(), buf.data(), kBlock);
    }

    REQUIRE (moved.getIntegrated() > -70.0f);
}

TEST_CASE ("BusConfig defaults to BusKind::Bus and carries FxReturn through a Bus",
           "[bus][bus-kind]")
{
    using sirius::Bus;
    using sirius::BusConfig;
    using sirius::BusId;
    using sirius::BusKind;

    SECTION ("default kind is Bus")
    {
        const BusConfig cfg;
        CHECK (cfg.kind == BusKind::Bus);
    }

    SECTION ("FxReturn kind round-trips through Bus::config()")
    {
        Bus bus (BusId { 7 }, BusConfig { 2, "Reverb", BusKind::FxReturn });
        CHECK (bus.config().kind == BusKind::FxReturn);
        CHECK (bus.config().channelCount == 2);
    }
}

TEST_CASE ("Bus is movable (std::vector<Bus> requirement) and move preserves gain/mute",
           "[bus][move]")
{
    static_assert (std::is_move_constructible_v<sirius::Bus>,
                   "Bus must be MoveInsertable for std::vector<Bus>");

    sirius::Bus source (sirius::BusId { 7 }, sirius::BusConfig {});
    source.setGain (0.25f);
    source.setMuted (true);

    sirius::Bus moved (std::move (source));
    REQUIRE (moved.id().value() == 7);
    REQUIRE (moved.gain()  == 0.25f);
    REQUIRE (moved.muted() == true);
}

TEST_CASE ("Bus default gain is unity and unmuted (inline path stays bit-identical)",
           "[bus][gain]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    REQUIRE (bus.gain()  == 1.0f);
    REQUIRE (bus.muted() == false);

    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.5f;
    bus.mixBufferChannel (1)[0] = 0.5f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == 0.5f);
    REQUIRE (r[0] == 0.5f);
}

TEST_CASE ("Bus::setGain scales the output (inline path)", "[bus][gain]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setGain (0.5f);

    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.8f;
    bus.mixBufferChannel (1)[0] = 0.8f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == Catch::Approx (0.4f));
    REQUIRE (r[0] == Catch::Approx (0.4f));
}

TEST_CASE ("Bus::setMuted silences the output (inline path)", "[bus][mute]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setMuted (true);

    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.9f;
    bus.mixBufferChannel (1)[0] = 0.9f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == 0.0f);
    REQUIRE (r[0] == 0.0f);
}

TEST_CASE ("Bus peak reflects the post-fader signal (inline path)", "[bus][meter]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setGain (0.5f);

    constexpr int kSamples = 8;
    for (int s = 0; s < kSamples; ++s)
    {
        bus.mixBufferChannel (0)[s] = 0.8f;   // pre-fader 0.8
        bus.mixBufferChannel (1)[s] = 0.4f;
    }
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);

    REQUIRE (bus.peakLeft()  == Catch::Approx (0.4f));   // 0.8 * 0.5
    REQUIRE (bus.peakRight() == Catch::Approx (0.2f));   // 0.4 * 0.5
}

TEST_CASE ("Bus peak reads silence when muted", "[bus][meter][mute]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    constexpr int kSamples = 8;
    const auto fillMix = [&]
    {
        for (int s = 0; s < kSamples; ++s)
        {
            bus.mixBufferChannel (0)[s] = 0.9f;
            bus.mixBufferChannel (1)[s] = 0.9f;
        }
    };
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };

    // First, un-muted, to leave a NON-zero peak in the atomics — otherwise the
    // post-mute check would pass vacuously against a fresh Bus's 0.0 defaults
    // and could not catch a muted path that fails to update the meter.
    fillMix();
    bus.process (out, 2, kSamples);
    REQUIRE (bus.peakLeft()  > 0.0f);
    REQUIRE (bus.peakRight() > 0.0f);

    // Now mute and process a fresh block: the meter must fall to silence.
    bus.setMuted (true);
    fillMix();   // process() zeroed the mix buffer; refill it
    bus.process (out, 2, kSamples);
    REQUIRE (bus.peakLeft()  == 0.0f);
    REQUIRE (bus.peakRight() == 0.0f);
}

TEST_CASE ("Bus LUFS reads silence floor before prepare()", "[bus][lufs]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    constexpr int kSamples = 64;
    for (int s = 0; s < kSamples; ++s)
    {
        bus.mixBufferChannel (0)[s] = 0.5f;
        bus.mixBufferChannel (1)[s] = 0.5f;
    }
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);

    // Un-prepared meter self-no-ops → integrated stays at the silence floor.
    REQUIRE (bus.lufsIntegrated() <= -70.0f);
}

TEST_CASE ("Bus LUFS rises with a 1 kHz post-fader signal after prepare()", "[bus][lufs]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 512;
    bus.prepare (kSampleRate, kBlock);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float* out[2] = { l.data(), r.data() };
    double phase = 0.0;
    const double inc = 2.0 * M_PI * 1000.0 / kSampleRate;
    for (int i = 0; i < 300; ++i)   // ~3.2 s — past the integration ramp
    {
        // 1 kHz sine, NOT DC — K-weighting's high-pass would kill a constant.
        for (int s = 0; s < kBlock; ++s)
        {
            const float v = 0.5f * static_cast<float> (std::sin (phase));
            phase += inc;
            bus.mixBufferChannel (0)[s] = v;
            bus.mixBufferChannel (1)[s] = v;
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        bus.process (out, 2, kBlock);
    }
    REQUIRE (bus.lufsIntegrated() > -70.0f);
}
