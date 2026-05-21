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
#include "sirius/MixerGraphState.h"
#include "sirius/OutputMixer.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/SignalType.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <type_traits>
#include <vector>

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

// ---------------------------------------------------------------------------
// M5 Session 3 — integration tests through the full I/O path
// ---------------------------------------------------------------------------

TEST_CASE ("OutputMixer::renderBuffer routes input through strip+master at unity",
           "[output-mixer][render-buffer]")
{
    OutputMixer mixer;
    const auto chL = mixer.addChannel (SignalType::Audio);
    const auto chR = mixer.addChannel (SignalType::Audio);

    // Attach a unity-gain center-pan strip to each channel (the strip's
    // equal-power pan at p=0.5 yields ~0.707 per side, so use mono mode
    // via the gain-only mono path is not possible here — the test feeds
    // stereo inputs anyway). Set gain to 0.5 so the expected output is
    // input × stripGain × panFactor × masterSendLevel = 1.0 × 0.5 ×
    // ~0.707 × 1.0 ~ 0.354.
    auto stripL = std::make_unique<ChannelStrip<SignalType::Audio>>();
    stripL->setGain (0.5f);
    stripL->setPan  (0.5f); // center
    mixer.setChannelStrip (chL, std::move (stripL));

    auto stripR = std::make_unique<ChannelStrip<SignalType::Audio>>();
    stripR->setGain (0.5f);
    stripR->setPan  (0.5f);
    mixer.setChannelStrip (chR, std::move (stripR));

    constexpr int kFrames = 8;
    std::array<float, kFrames> inLeft;  inLeft.fill  (1.0f);
    std::array<float, kFrames> inRight; inRight.fill (1.0f);
    const float* inputs[2] = { inLeft.data(), inRight.data() };

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (inputs, 2, outputs, 2, kFrames);

    // Channel L: src=1.0 → strip(gain 0.5, pan 0.5 center) → left ~= 0.354,
    // right ~= 0.354. Channel R: same. Both channels' contributions are
    // summed at the master:
    //   master_left  = chL_left  + chR_left  = 0.354 + 0.354 = 0.707
    //   master_right = chL_right + chR_right = 0.354 + 0.354 = 0.707
    constexpr float kCenterFactor = 0.7071067811865476f; // cos(pi/4) = sin(pi/4)
    const float expected = 2.0f * 0.5f * kCenterFactor;  // ~= 0.707

    for (float v : outLeft)  CHECK (v == Catch::Approx (expected).margin (1e-5f));
    for (float v : outRight) CHECK (v == Catch::Approx (expected).margin (1e-5f));
}

TEST_CASE ("OutputMixer::renderBuffer is a no-op with no registered channels",
           "[output-mixer][render-buffer]")
{
    OutputMixer mixer; // empty — M5 default state.

    constexpr int kFrames = 8;
    std::array<float, kFrames> inLeft;  inLeft.fill  (1.0f);
    const float* inputs[1] = { inLeft.data() };

    std::array<float, kFrames> outLeft;  outLeft.fill (0.42f); // sentinel
    float* outputs[1] = { outLeft.data() };

    mixer.renderBuffer (inputs, 1, outputs, 1, kFrames);

    // Empty registry → no writes into outputs. Sentinel untouched.
    for (float v : outLeft) CHECK (v == Catch::Approx (0.42f));
}

TEST_CASE ("OutputMixer::renderBuffer accumulates aux-bus sends into master at unity",
           "[output-mixer][render-buffer][bus-send]")
{
    OutputMixer mixer;
    const auto ch     = mixer.addChannel (SignalType::Audio);
    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb" });

    // No strip on the channel — scratch carries unity-gain source.
    // Send 0.5 of the channel into the reverb bus. Master defaults to 1.0
    // (set by addChannel). Expected: master_out = ch_scratch * 1.0
    // (master send) + ch_scratch * 0.5 (reverb bus, identity effectChain,
    // accumulated into master) = ch_scratch * 1.5.
    mixer.routeChannelToBus (ch, reverb, 0.5f);

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill  (1.0f);
    const float* inputs[1] = { inLeft.data() };

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (inputs, 1, outputs, 2, kFrames);

    // Mono input → both scratch channels carry 1.0 → master accumulates
    // (1.0 + 0.5) * 1.0 = 1.5 on each channel.
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.5f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.5f));
}

TEST_CASE ("OutputMixer::renderBuffer writes additively into pre-populated outputs",
           "[output-mixer][render-buffer]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    (void) ch;

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill  (0.5f);
    const float* inputs[1] = { inLeft.data() };

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.25f); // pre-populated
    std::array<float, kFrames> outRight; outRight.fill (0.25f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (inputs, 1, outputs, 2, kFrames);

    // Pre-existing 0.25 + (input 0.5 * master send 1.0) = 0.75. Proves
    // DirectLayer's bypass writes are preserved when OutputMixer runs
    // after DirectLayer in the AudioCallback.
    for (float v : outLeft)  CHECK (v == Catch::Approx (0.75f));
    for (float v : outRight) CHECK (v == Catch::Approx (0.75f));
}

// ---------------------------------------------------------------------------
// M5 Session 3 — overflow regression tests (S2 code review carry-over)
// ---------------------------------------------------------------------------
//
// `addChannel` past kMaxOutputChannels and `addBus` past kMaxBuses both fire
// `jassertfalse` in debug builds — those assertions ARE the test in debug
// (operator gets a loud signal that the cap was hit). The release-build
// behavior is the sentinel return + a non-corrupted registry, which is
// what these tests verify. Guarded with `#if ! JUCE_DEBUG` so the debug
// build doesn't trigger the assert dialog during ctest runs.

#if ! JUCE_DEBUG
TEST_CASE ("OutputMixer::addChannel returns sentinel and preserves cap on overflow",
           "[output-mixer][overflow]")
{
    OutputMixer mixer;

    // Fill to the cap.
    for (int i = 0; i < OutputMixer::kMaxOutputChannels; ++i)
    {
        const auto id = mixer.addChannel (SignalType::Audio);
        CHECK (id.value() != 0);
    }

    // One past the cap — must return sentinel 0 and NOT extend the registry.
    const auto overflow = mixer.addChannel (SignalType::Audio);
    CHECK (overflow.value() == 0);

    // Registry stability — the matrix entry for the last valid channel
    // still reads the addChannel-default master send level of 1.0.
    const OutputChannelId lastValid { OutputMixer::kMaxOutputChannels };
    CHECK (mixer.sendLevelFor (lastValid, BusId { 0 }) == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer::addBus returns sentinel and preserves cap on overflow",
           "[output-mixer][overflow]")
{
    OutputMixer mixer;

    // Master is auto-created at construction → fill the remaining slots.
    for (int i = 1; i < OutputMixer::kMaxBuses; ++i)
    {
        const auto id = mixer.addBus (BusConfig { 2, "" });
        CHECK (id.value() != 0);
    }

    // One past the cap — must return sentinel BusId{0} (master fallback)
    // and NOT corrupt the registry.
    const auto overflow = mixer.addBus (BusConfig { 2, "" });
    CHECK (overflow.value() == 0);

    // Registry stability — adding a channel + routing it to the last valid
    // bus still works (proves the bus is reachable post-overflow attempt).
    const auto ch = mixer.addChannel (SignalType::Audio);
    const BusId lastValidBus { OutputMixer::kMaxBuses - 1 };
    mixer.routeChannelToBus (ch, lastValidBus, 0.5f);
    CHECK (mixer.sendLevelFor (ch, lastValidBus) == Catch::Approx (0.5f));
}
#endif // ! JUCE_DEBUG

// ---------------------------------------------------------------------------
// M5 Session 3 — RT-budget smoke test (32 channels × 8 buses)
// ---------------------------------------------------------------------------
//
// Per the Performance Benchmarker brief in continue.md operator TODOs:
// the 32-channel/8-bus configuration is the worst-case M5 OutputMixer
// load. Asserts the per-call elapsed stays well inside the 5.33ms
// 256-sample-at-48kHz buffer budget. Hidden by default (Catch2 leading-dot
// tag convention) — runs only when explicitly filtered, matching the
// DirectLayer `[.rt-smoke]` pattern.

TEST_CASE ("OutputMixer::renderBuffer 32-channel x 8-bus RT smoke",
           "[output-mixer][.rt-smoke]")
{
    OutputMixer mixer;

    // Register 32 channels, each with a unity strip + send into every aux
    // bus at 0.25. Pre-register 8 aux buses (master is auto, total 9).
    std::vector<OutputChannelId> channels;
    channels.reserve (32);
    for (int i = 0; i < 32; ++i)
    {
        const auto ch = mixer.addChannel (SignalType::Audio);
        channels.push_back (ch);
        auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
        strip->setGain (0.8f);
        strip->setPan  (0.5f);
        mixer.setChannelStrip (ch, std::move (strip));
    }

    std::vector<BusId> auxBuses;
    auxBuses.reserve (8);
    for (int b = 0; b < 8; ++b)
        auxBuses.push_back (mixer.addBus (BusConfig { 2, "" }));

    for (const auto ch : channels)
        for (const auto bus : auxBuses)
            mixer.routeChannelToBus (ch, bus, 0.25f);

    // Drive with 32 input channels of all-1.0f × 256 samples (the typical
    // dev-machine block size).
    constexpr int kFrames = 256;
    constexpr int kInputs = 32;
    std::vector<std::vector<float>> inputBuffers (static_cast<std::size_t> (kInputs),
                                                  std::vector<float> (static_cast<std::size_t> (kFrames), 1.0f));
    std::vector<const float*> inputs (static_cast<std::size_t> (kInputs));
    for (std::size_t i = 0; i < static_cast<std::size_t> (kInputs); ++i)
        inputs[i] = inputBuffers[i].data();

    std::array<std::vector<float>, 2> outputBuffers {
        std::vector<float> (kFrames, 0.0f),
        std::vector<float> (kFrames, 0.0f)
    };
    float* outputs[2] = { outputBuffers[0].data(), outputBuffers[1].data() };

    // Warmup 100 iterations to settle caches; measure 100 iterations and
    // take the median (via a simple max-bound — the assertion target is
    // generous so transient OS jitter doesn't flake the test).
    constexpr int kWarmup    = 100;
    constexpr int kMeasure   = 100;
    for (int i = 0; i < kWarmup; ++i)
        mixer.renderBuffer (inputs.data(), kInputs, outputs, 2, kFrames);

    double maxElapsedSec = 0.0;
    for (int i = 0; i < kMeasure; ++i)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        mixer.renderBuffer (inputs.data(), kInputs, outputs, 2, kFrames);
        const auto t1 = juce::Time::getHighResolutionTicks();
        const double elapsedSec = juce::Time::highResolutionTicksToSeconds (t1 - t0);
        if (elapsedSec > maxElapsedSec) maxElapsedSec = elapsedSec;
    }

    // 48kHz / 256 samples → 5.33ms buffer budget. OutputMixer is one of
    // several audio-thread steps; budget 1ms (~19% of buffer) here.
    // Measured 2026-05-18 on dev machine: max ~17.5µs (~0.33% of buffer).
    constexpr double kBudgetSec = 0.001;
    CHECK (maxElapsedSec < kBudgetSec);
}

namespace
{
    // Synchronous in-process effect host that halves every sample. Stands in
    // for a real plug-in so a bus applies an OBSERVABLE transform — the only
    // way to prove signal actually traversed a given bus in a Phase-1 unity
    // routing graph (where total signal reaching master is otherwise
    // path-invariant).
    struct HalvingEffectHost : sirius::IEffectChainHost
    {
        bool pumpSlot (std::int64_t, std::size_t,
                       const float* const* inChannels, float* const* outChannels,
                       int numChannels, int numSamples) noexcept override
        {
            for (int c = 0; c < numChannels; ++c)
                for (int s = 0; s < numSamples; ++s)
                    outChannels[c][s] = inChannels[c][s] * 0.5f;
            return true;
        }
    };
}

TEST_CASE ("OutputMixer bus->bus subgroup actually routes audio through the parent bus",
           "[output-mixer][subgroup]")
{
    using sirius::BusConfig;
    using sirius::EffectChain;
    using sirius::EffectChainEntry;
    using sirius::OutputMixer;
    using sirius::SignalType;

    OutputMixer mixer;
    const auto ch   = mixer.addChannel (SignalType::Audio);
    const auto busA = mixer.addBus (BusConfig { 2, "A" });
    const auto busB = mixer.addBus (BusConfig { 2, "B" });

    // busB carries a one-slot chain so the halving host transforms anything
    // that flows THROUGH busB. busA stays unity (empty chain).
    EffectChainEntry slot;
    slot.descriptor.name = "Halve";
    slot.displayName     = "Halve";
    mixer.setBusEffectChain (busB, EffectChain {}.withAppended (slot));
    HalvingEffectHost host;
    mixer.setEffectChainHost (&host);

    // Channel feeds ONLY busA (its default direct-to-master send is zeroed),
    // and busA subgroups into busB. So the signal path is ch -> busA -> busB.
    mixer.routeChannelToBus (ch, BusId { 0 }, 0.0f); // kill direct-to-master
    mixer.routeChannelToBus (ch, busA, 1.0f);
    REQUIRE (mixer.routeBusToBus (busA, busB));

    std::array<float, 4> in;  in.fill (0.5f);
    std::array<float, 4> out; out.fill (0.0f);
    const float* inPtrs[1]  = { in.data() };
    float*       outPtrs[2] = { out.data(), nullptr };
    mixer.renderBuffer (inPtrs, 1, outPtrs, 1, static_cast<int> (in.size()));

    // Signal traversed busB, so busB's halving applied exactly once:
    // 0.5 (input) -> busA (unity) -> busB (x0.5) -> master -> 0.25.
    // If routeBusToBus regressed to a no-op (busA -> master directly), busB
    // would never see the signal and the output would be 0.5 — so this
    // assertion FAILS if subgroup routing breaks.
    for (float v : out) CHECK (v == Catch::Approx (0.25f));
}

TEST_CASE ("OutputMixer routeBusToBus rejects master-as-source and unknown buses",
           "[output-mixer][subgroup]")
{
    using sirius::BusConfig;
    using sirius::OutputMixer;

    OutputMixer mixer;
    const auto busA = mixer.addBus (BusConfig { 2, "A" });

    // Master's main-out is fixed to the terminal — it can never be a subgroup source.
    CHECK_FALSE (mixer.routeBusToBus (BusId { 0 }, busA));
    // Out-of-range ids are rejected (no graph mutation, no crash).
    CHECK_FALSE (mixer.routeBusToBus (busA, BusId { 999 }));
    CHECK_FALSE (mixer.routeBusToBus (BusId { 999 }, busA));
}

TEST_CASE ("OutputMixer export/import round-trips buses, sends, subgroups, inserts", "[output-mixer][persistence]")
{
    sirius::OutputMixer source;
    const auto aux = source.addBus (sirius::BusConfig { 2, "Aux", sirius::BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, sirius::BusId (0)));   // aux -> master

    sirius::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (aux, sirius::EffectChain{}.withAppended (comp));

    const auto ch = source.addChannel (sirius::SignalType::Audio);
    auto strip = std::make_unique<sirius::ChannelStrip<sirius::SignalType::Audio>>();
    sirius::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (sirius::EffectChain{}.withAppended (eq));
    source.setChannelStrip (ch, std::move (strip));
    source.routeChannelToBus (ch, sirius::BusId (0), 0.7f);    // non-default master level
    source.routeChannelToBus (ch, aux, 0.4f);                 // send to aux

    const auto exported = source.exportGraphState();
    REQUIRE (exported.buses.size() == 2);              // master (0) + aux
    CHECK (exported.buses[0].busId == 0);              // master first

    sirius::OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("OutputMixer import of an empty snapshot keeps only the master bus", "[output-mixer][persistence]")
{
    sirius::OutputMixer mixer;
    mixer.importGraphState (sirius::OutputMixerGraphState{});
    const auto state = mixer.exportGraphState();
    REQUIRE (state.buses.size() == 1);
    CHECK (state.buses[0].busId == 0);
    CHECK (state.channels.empty());
}
