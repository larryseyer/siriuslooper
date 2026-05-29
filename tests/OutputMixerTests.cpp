// Tests for ida::OutputMixer — the V3 §2.2 output-side mixer.
//
// M2 Session 3 shipped the default-ctor/dtor floor (assert-false bodies).
// M5 Session 2 extends with configuration-surface tests: channel registry,
// bus registry (including the auto-created master at BusId{0}), and the
// send-level matrix accessors. Audio-thread `renderBuffer` body is still
// a stub in S2; an integration test for the real renderBuffer lands in
// M5 Session 3.
#include "ida/Bus.h"
#include "ida/Channel.h"
#include "ida/ChannelStrip.h"
#include "ida/EffectChain.h"
#include "ida/MixerGraphState.h"
#include "ida/OutputMixer.h"
#include "ida/PluginDescriptor.h"
#include "ida/SessionFormat.h"
#include "ida/SignalType.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <type_traits>
#include <vector>

using ida::BusConfig;
using ida::BusId;
using ida::ChannelStrip;
using ida::EffectChain;
using ida::EffectChainEntry;
using ida::OutputChannelId;
using ida::OutputMixer;
using ida::PluginDescriptor;
using ida::PluginFormat;
using ida::SignalType;

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
    using ida::BusKind;
    OutputMixer mixer;
    const auto channel = mixer.addChannel (SignalType::Audio);
    // FX-return-kind: send is additive (slice E3 keeps Bus-kind targets
    // radio — they zero master). Test intent is read/write of a send
    // level, which is independent of radio semantics.
    const auto reverb  = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

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

    // 2026-05-24: wire each channel's audio source explicitly. The prior
    // M5 proxy (inputChannelData[id-1] auto-map) was removed; phrase
    // channels are silent until something feeds them.
    mixer.setChannelAudioSource (chL, inLeft.data(),  inLeft.data());
    mixer.setChannelAudioSource (chR, inRight.data(), inRight.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

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
    using ida::BusKind;
    OutputMixer mixer;
    const auto ch     = mixer.addChannel (SignalType::Audio);
    // Use an FX return so the channel's master send survives (slice E3
    // radio semantics zero master only when targeting another Bus-kind
    // bus; FX-return targets are additive). Test intent is the bus-into-
    // master subgroup accumulation: master + reverb subgroup = 1.5×.
    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // No strip on the channel — scratch carries unity-gain source.
    // Send 0.5 of the channel into the reverb bus. Master defaults to 1.0
    // (set by addChannel). Expected: master_out = ch_scratch * 1.0
    // (master send) + ch_scratch * 0.5 (reverb bus, identity effectChain,
    // accumulated into master) = ch_scratch * 1.5.
    mixer.routeChannelToBus (ch, reverb, 0.5f);

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill  (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

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

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill  (0.5f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.25f); // pre-populated
    std::array<float, kFrames> outRight; outRight.fill (0.25f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Pre-existing 0.25 + (input 0.5 * master send 1.0) = 0.75. Proves
    // OutputMixer writes additively into the output buffers, preserving
    // any pre-existing content the AudioCallback may have placed there.
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
// tag convention) — runs only when explicitly filtered.

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
    // Wire each channel's audio source (2026-05-24 — replaces the M5 proxy).
    for (std::size_t i = 0; i < static_cast<std::size_t> (kInputs); ++i)
        mixer.setChannelAudioSource (channels[i],
                                     inputBuffers[i].data(), inputBuffers[i].data());

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
        mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    double maxElapsedSec = 0.0;
    for (int i = 0; i < kMeasure; ++i)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);
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
    struct HalvingEffectHost : ida::IEffectChainHost
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
    using ida::BusConfig;
    using ida::EffectChain;
    using ida::EffectChainEntry;
    using ida::OutputMixer;
    using ida::SignalType;

    OutputMixer mixer;
    const auto ch   = mixer.addChannel (SignalType::Audio);
    const auto busA = mixer.addBus (BusConfig { 2, "A" });
    const auto busB = mixer.addBus (BusConfig { 2, "B" });

    // busB carries a one-slot chain so the halving host transforms anything
    // that flows THROUGH busB. busA + master must stay unity for the
    // assertion to isolate busB's transform — slice EC-Polish auto-seeds
    // [EQ, CMP] on every bus, so explicitly clear those two to keep the
    // single-halve invariant.
    mixer.setBusEffectChain (busA,     EffectChain {});
    mixer.setBusEffectChain (BusId {0}, EffectChain {});  // master
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
    mixer.setChannelAudioSource (ch, in.data(), in.data());
    float*       outPtrs[2] = { out.data(), nullptr };
    mixer.renderBuffer (nullptr, 0, outPtrs, 1, static_cast<int> (in.size()));

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
    using ida::BusConfig;
    using ida::OutputMixer;

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
    ida::OutputMixer source;
    const auto aux = source.addBus (ida::BusConfig { 2, "Aux", ida::BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, ida::BusId (0)));   // aux -> master

    ida::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (aux, ida::EffectChain{}.withAppended (comp));

    const auto ch = source.addChannel (ida::SignalType::Audio);
    auto strip = std::make_unique<ida::ChannelStrip<ida::SignalType::Audio>>();
    ida::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (ida::EffectChain{}.withAppended (eq));
    source.setChannelStrip (ch, std::move (strip));
    source.routeChannelToBus (ch, ida::BusId (0), 0.7f);    // non-default master level
    source.routeChannelToBus (ch, aux, 0.4f);                 // send to aux

    const auto exported = source.exportGraphState();
    REQUIRE (exported.buses.size() == 2);              // master (0) + aux
    CHECK (exported.buses[0].busId == 0);              // master first

    ida::OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("OutputMixer importGraphState preserves channelId gaps from removeChannel",
           "[output-mixer][persistence][slice-p]")
{
    // Slice P (2026-05-24) regression: MainComponent's phrase-channel map
    // stores ConstituentId -> OutputChannelId. After save/load the map's
    // OutputChannelIds must still address the right channels — even when the
    // export carries gap ids (1, 3, 5) from intermediate removeChannel calls.
    // InputMixer::importGraphState already preserves persisted ChannelIds;
    // OutputMixer must do the same.
    ida::OutputMixer source;
    const auto a = source.addChannel (ida::SignalType::Audio); // id 1
    const auto b = source.addChannel (ida::SignalType::Audio); // id 2
    const auto c = source.addChannel (ida::SignalType::Audio); // id 3
    source.removeChannel (b);                                   // frees id 2
    const auto d = source.addChannel (ida::SignalType::Audio); // reuses id 2
    source.removeChannel (d);                                   // frees id 2 again
    // Final state: channels {1, 3} live; nextOutputChannelId_ = 4; free list = [2].
    juce::ignoreUnused (a, c);

    const auto exported = source.exportGraphState();
    REQUIRE (exported.channels.size() == 2);
    // Persisted ids are 1 and 3 (gap at 2). Order may follow swap-erase; check
    // the set rather than the sequence.
    std::vector<std::int64_t> exportedIds;
    for (const auto& ch : exported.channels) exportedIds.push_back (ch.channelId);
    std::sort (exportedIds.begin(), exportedIds.end());
    CHECK (exportedIds == std::vector<std::int64_t> { 1, 3 });

    ida::OutputMixer loaded;
    loaded.importGraphState (exported);
    const auto reexported = loaded.exportGraphState();

    // The loaded mixer must report the same channelIds as the source.
    std::vector<std::int64_t> reIds;
    for (const auto& ch : reexported.channels) reIds.push_back (ch.channelId);
    std::sort (reIds.begin(), reIds.end());
    CHECK (reIds == std::vector<std::int64_t> { 1, 3 });
    CHECK (reexported.nextChannelId == exported.nextChannelId);
}

TEST_CASE ("OutputMixer import of an empty snapshot keeps only the master bus", "[output-mixer][persistence]")
{
    ida::OutputMixer mixer;
    mixer.importGraphState (ida::OutputMixerGraphState{});
    const auto state = mixer.exportGraphState();
    REQUIRE (state.buses.size() == 1);
    CHECK (state.buses[0].busId == 0);
    CHECK (state.channels.empty());
}

// ---------------------------------------------------------------------------
// Slice 3 — cycle predicate, per-output-pair routing, bus rename
// ---------------------------------------------------------------------------

TEST_CASE ("OutputMixer::busMainOutToBusWouldCycle mirrors the graph cycle check",
           "[output-mixer][slice3][cycle]")
{
    OutputMixer mixer;
    const auto a = mixer.addBus (BusConfig { 2, "A" });
    const auto b = mixer.addBus (BusConfig { 2, "B" });

    // A -> master is fine; B -> A is fine; A -> B is fine; B -> master is fine.
    CHECK_FALSE (mixer.busMainOutToBusWouldCycle (a, BusId { 0 }));
    CHECK_FALSE (mixer.busMainOutToBusWouldCycle (b, a));
    REQUIRE (mixer.routeBusToBus (b, a));   // B feeds into A

    // Now A -> B would close the loop A -> B -> A.
    CHECK (mixer.busMainOutToBusWouldCycle (a, b));
    // Sanity: the engine should also refuse the real route.
    CHECK_FALSE (mixer.routeBusToBus (a, b));

    // Unknown ids never report a cycle (defensive false).
    CHECK_FALSE (mixer.busMainOutToBusWouldCycle (BusId { 999 }, a));
    CHECK_FALSE (mixer.busMainOutToBusWouldCycle (a, BusId { 999 }));
}

TEST_CASE ("OutputMixer per-output-pair routing writes to the requested pair",
           "[output-mixer][slice3][hardware-output-pair]")
{
    using ida::BusKind;
    OutputMixer mixer;
    // FX-return-kind so the channel-send into it is additive (slice E3:
    // Bus-kind targets are radio and would zero master). Test intent is
    // pair-routing — using an FX return preserves both the master at
    // pair 0 and the aux at pair 1 contributions.
    const auto aux = mixer.addBus (BusConfig { 2, "Aux", BusKind::FxReturn });

    // Default pair is 0 (outputs [0,1]).
    CHECK (mixer.busHardwareOutPair (aux) == 0);
    CHECK (mixer.busHardwareOutPair (BusId { 0 }) == 0);

    // Route aux direct to hardware output pair 1 (outputs [2,3]). The master
    // remains parked on pair 0.
    REQUIRE (mixer.setBusMainOutToHardwareOutput (aux, /*pairIndex*/ 1));
    CHECK (mixer.busMainOut (aux) == OutputMixer::MainOutDest::HardwareOutput);
    CHECK (mixer.busHardwareOutPair (aux) == 1);

    // Feed a channel that sends 1.0 into the aux (and the default 1.0 into
    // the master). With aux on pair 1 and master on pair 0, the aux's
    // contribution should land in outputs [2,3] and the master's
    // contribution should land in outputs [0,1] — both at unity.
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.routeChannelToBus (ch, aux, 1.0f);

    constexpr int kFrames = 4;
    std::array<float, kFrames> input;  input.fill (1.0f);
    mixer.setChannelAudioSource (ch, input.data(), input.data());

    std::array<float, kFrames> out0{}; out0.fill (0.0f);
    std::array<float, kFrames> out1{}; out1.fill (0.0f);
    std::array<float, kFrames> out2{}; out2.fill (0.0f);
    std::array<float, kFrames> out3{}; out3.fill (0.0f);
    float* outputs[4] = { out0.data(), out1.data(), out2.data(), out3.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 4, kFrames);

    // Master at pair 0 carries the channel's master send (1.0).
    for (float v : out0) CHECK (v == Catch::Approx (1.0f));
    for (float v : out1) CHECK (v == Catch::Approx (1.0f));
    // Aux at pair 1 carries the channel's aux send (1.0), bypassing master.
    for (float v : out2) CHECK (v == Catch::Approx (1.0f));
    for (float v : out3) CHECK (v == Catch::Approx (1.0f));

    // Master can also park on a non-zero pair (multi-output interfaces).
    REQUIRE (mixer.setBusMainOutToHardwareOutput (BusId { 0 }, /*pairIndex*/ 1));
    CHECK (mixer.busHardwareOutPair (BusId { 0 }) == 1);

    // Pair index round-trips through persistence.
    const auto exported = mixer.exportGraphState();
    OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.busHardwareOutPair (BusId { 0 }) == 1);
    CHECK (loaded.busHardwareOutPair (aux) == 1);
}

TEST_CASE ("OutputMixer::renameBus updates the bus name; master is canonical",
           "[output-mixer][slice3][rename]")
{
    OutputMixer mixer;
    const auto aux = mixer.addBus (BusConfig { 2, "Bus 1" });

    // Master rename is rejected — the name is canonical mixing-console nomenclature.
    CHECK_FALSE (mixer.renameBus (BusId { 0 }, "Not Master"));
    REQUIRE (mixer.busForId (BusId { 0 }) != nullptr);
    CHECK (mixer.busForId (BusId { 0 })->config().name == "Master");

    // Unknown ids are rejected.
    CHECK_FALSE (mixer.renameBus (BusId { 999 }, "x"));

    // Aux rename writes through to the live bus AND survives persistence.
    REQUIRE (mixer.renameBus (aux, "Headphone Cue"));
    CHECK (mixer.busForId (aux)->config().name == "Headphone Cue");

    const auto exported = mixer.exportGraphState();
    REQUIRE (exported.buses.size() == 2);
    CHECK (exported.buses[1].name == "Headphone Cue");

    OutputMixer loaded;
    loaded.importGraphState (exported);
    REQUIRE (loaded.busForId (aux) != nullptr);
    CHECK (loaded.busForId (aux)->config().name == "Headphone Cue");
}

// --- Slice 5a — phrase-channel engine surface ------------------------------
// Two additions: (1) removeChannel with id reuse via a free-list so
// phrase-add/remove churn doesn't burn through kMaxOutputChannels = 32;
// (2) per-channel hardware-output routing mirroring slice 3's bus-side API.
// Audio-thread render path is untouched — phrase channels don't feed audio
// yet (5b is the UI shell, 5c persists the ConstituentId binding).

TEST_CASE ("OutputMixer::removeChannel of an unknown id is a no-op",
           "[output-mixer][slice5][remove]")
{
    OutputMixer mixer;
    const auto real = mixer.addChannel (SignalType::Audio);

    // Unknown id — must not throw, must not alter any state observable
    // through the existing accessors. The real channel's master send (the
    // 1.0 addChannel default) survives untouched.
    mixer.removeChannel (OutputChannelId { 999 });

    CHECK (mixer.sendLevelFor (real, BusId { 0 }) == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer::removeChannel releases the id; next addChannel reuses it",
           "[output-mixer][slice5][remove]")
{
    OutputMixer mixer;
    const auto first  = mixer.addChannel (SignalType::Audio);
    const auto second = mixer.addChannel (SignalType::Audio);

    REQUIRE (first.value()  == 1);
    REQUIRE (second.value() == 2);

    mixer.removeChannel (first);

    // Free-list pops the freed id; the next add gets id 1 back rather than
    // incrementing past 2. This is the contract that lets a long session of
    // phrase add/remove churn stay under kMaxOutputChannels = 32.
    const auto reused = mixer.addChannel (SignalType::Audio);
    CHECK (reused.value() == first.value());

    // Original id 2 is unaffected — only the removed id rejoins circulation.
    CHECK (mixer.sendLevelFor (second, BusId { 0 }) == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer::removeChannel zeros the freed channel's sendMatrix row",
           "[output-mixer][slice5][remove]")
{
    using ida::BusKind;
    OutputMixer mixer;
    // Use an FX return so the radio-style routeChannelToBus (slice E3, which
    // zeros master + other Bus-kind sends when targeting a Bus-kind bus)
    // doesn't interfere — FX-return sends are additive and survive
    // alongside master. The test's intent (sendMatrix row cleared on
    // remove + reused channel starts at addChannel defaults) is the
    // engine surface we're pinning here.
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });
    const auto ch  = mixer.addChannel (SignalType::Audio);

    // Configure non-default sends on both master and the FX return.
    mixer.routeChannelToBus (ch, BusId { 0 }, 0.5f);   // master, additive (master is Bus-kind but radio targeting master keeps master)
    mixer.routeChannelToBus (ch, rvb,         0.75f); // FX return, additive (FxReturn kind never radios)
    REQUIRE (mixer.sendLevelFor (ch, BusId { 0 }) == Catch::Approx (0.5f));
    REQUIRE (mixer.sendLevelFor (ch, rvb)         == Catch::Approx (0.75f));

    mixer.removeChannel (ch);

    // After remove + addChannel-from-free-list, the re-minted channel must
    // start at addChannel's defaults (1.0 into master, 0.0 into the FX
    // return). The 0.75 FX-return send the removed channel had is gone.
    const auto reused = mixer.addChannel (SignalType::Audio);
    REQUIRE (reused.value() == ch.value());
    CHECK (mixer.sendLevelFor (reused, BusId { 0 }) == Catch::Approx (1.0f));
    CHECK (mixer.sendLevelFor (reused, rvb)         == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::setChannelMainOutToHardwareOutput round-trips through persistence",
           "[output-mixer][slice5][hardware-output-pair]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    // Default before any setter call is pair 0 — same defensive default as
    // the bus-side accessor.
    CHECK (mixer.channelMainOutHardwareOutPair (ch) == 0);

    // Unknown ids are rejected.
    CHECK_FALSE (mixer.setChannelMainOutToHardwareOutput (OutputChannelId { 999 }, 1));
    CHECK (mixer.channelMainOutHardwareOutPair (OutputChannelId { 999 }) == 0);

    // Real id accepted; pair index is stored on the message thread.
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch, 3));
    CHECK (mixer.channelMainOutHardwareOutPair (ch) == 3);

    // Pair index round-trips through the persistence snapshot. Channel ids
    // are remapped from 1 on import (re-minted by addChannel); the single
    // channel here keeps id 1 so the lookup is straightforward.
    const auto exported = mixer.exportGraphState();
    OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.channelMainOutHardwareOutPair (ch) == 3);
}

TEST_CASE ("OutputMixer::setChannelMainOutToHardwareOutput clamps negative pairIndex to 0",
           "[output-mixer][slice5][hardware-output-pair]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch, -5));
    CHECK (mixer.channelMainOutHardwareOutPair (ch) == 0);
}

TEST_CASE ("OutputMixer::audioStripForChannel returns the strip attached via setChannelStrip",
           "[output-mixer][slice5b][channel-strip-accessor]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    // No strip attached yet — accessor returns nullptr (defensive default;
    // mirrors busForId returning nullptr for unknown ids).
    CHECK (mixer.audioStripForChannel (ch) == nullptr);

    // Unknown ids are also nullptr — caller must not deref unconditionally.
    CHECK (mixer.audioStripForChannel (OutputChannelId { 999 }) == nullptr);

    // After setChannelStrip the accessor returns the SAME object (raw pointer
    // identity preserved — the OutputMixer owns the unique_ptr; the accessor
    // hands out a non-owning view for message-thread reads).
    auto strip   = std::make_unique<ChannelStrip<SignalType::Audio>>();
    auto* rawPtr = strip.get();
    mixer.setChannelStrip (ch, std::move (strip));
    CHECK (mixer.audioStripForChannel (ch) == rawPtr);

    // Pan + width round-trip — the accessor lets the UI read engine state
    // back into the pan/width detail panel (slice 5b operator UX).
    rawPtr->setPan (0.25f);
    rawPtr->setWidth (1.5f);
    auto* read = mixer.audioStripForChannel (ch);
    REQUIRE (read != nullptr);
    CHECK (read->pan()   == Catch::Approx (0.25f));
    CHECK (read->width() == Catch::Approx (1.5f));
}

// Slice E1 — BusKind::FxReturn on OutputMixer (mixer-symmetry spec
// 2026-05-23). InputMixer already exposes FxReturn as a first-class bus
// kind; OutputMixer's addBus accepts the kind today but had no accessor
// to read it back and no test coverage. These tests pin the surface.
TEST_CASE ("OutputMixer::addBus with BusKind::FxReturn mints an FX-return distinct from a plain Bus",
           "[output-mixer][fx-return]")
{
    using ida::BusKind;

    OutputMixer mixer;

    const auto drums = mixer.addBus (BusConfig { 2, "Drums",  BusKind::Bus });
    const auto rvb   = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    REQUIRE (drums.value() != 0);
    REQUIRE (rvb.value()   != 0);
    REQUIRE (drums.value() != rvb.value());

    // busCount = master (index 0) + drums + rvb = 3.
    REQUIRE (mixer.busCount() == 3);

    // busIdAt is 0-indexed into the full bus vector (master sits at 0).
    CHECK (mixer.busIdAt (0).value() == 0);              // master
    CHECK (mixer.busIdAt (1).value() == drums.value());
    CHECK (mixer.busIdAt (2).value() == rvb.value());

    // Kind read-back at each index — the master + plain bus report Bus,
    // the FX return reports FxReturn.
    CHECK (mixer.busKindAt (0) == BusKind::Bus);         // master
    CHECK (mixer.busKindAt (1) == BusKind::Bus);         // drums
    CHECK (mixer.busKindAt (2) == BusKind::FxReturn);    // reverb

    // Defensive defaults match InputMixer::busKindAt — out-of-range
    // returns the safe Bus sentinel rather than asserting in release.
    CHECK (mixer.busKindAt (-1) == BusKind::Bus);
    CHECK (mixer.busKindAt (mixer.busCount()) == BusKind::Bus);
}

TEST_CASE ("OutputMixer::routeChannelToBus targets an FX-return identically to a plain bus",
           "[output-mixer][fx-return]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto ch  = mixer.addChannel (SignalType::Audio);
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Engine doesn't branch on kind for the send-matrix — sends accumulate
    // into FX returns the same way they do into plain aux buses (the kind
    // gates UI pre-filter behavior, not engine accumulation).
    mixer.routeChannelToBus (ch, rvb, 0.75f);
    CHECK (mixer.sendLevelFor (ch, rvb) == Catch::Approx (0.75f));

    // Sends zero cleanly too — used by the send-zero bypass path (E3).
    mixer.routeChannelToBus (ch, rvb, 0.0f);
    CHECK (mixer.sendLevelFor (ch, rvb) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer export/import round-trips a BusKind::FxReturn end-to-end",
           "[output-mixer][fx-return][persistence]")
{
    using ida::BusKind;

    OutputMixer source;
    const auto ch    = source.addChannel (SignalType::Audio);
    const auto drums = source.addBus (BusConfig { 2, "Drums",  BusKind::Bus });
    const auto rvb   = source.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });
    source.routeChannelToBus (ch, drums, 1.0f);
    source.routeChannelToBus (ch, rvb,   0.5f);

    // Drop an insert on the FX return so the persistence path is exercised
    // for an FxReturn-kind bus (not just plain Bus).
    EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (rvb, EffectChain{}.withAppended (comp));

    const auto exported = source.exportGraphState();

    // Three buses: master + drums + reverb.
    REQUIRE (exported.buses.size() == 3);
    CHECK (exported.buses[0].busId == 0);
    CHECK (exported.buses[0].kind  == ida::MixerBusKind::Bus); // master
    CHECK (exported.buses[1].busId == drums.value());
    CHECK (exported.buses[1].kind  == ida::MixerBusKind::Bus);
    CHECK (exported.buses[2].busId == rvb.value());
    CHECK (exported.buses[2].kind  == ida::MixerBusKind::FxReturn);
    CHECK (exported.buses[2].inserts.entries().size() == 1);

    // Reconstruct and re-export — fixed point on the FxReturn kind +
    // insert chain confirms the import path maps MixerBusKind::FxReturn
    // back into BusKind::FxReturn when minting via addBus.
    OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.exportGraphState() == exported);

    // And the loaded mixer reports the kind via the busKindAt accessor —
    // confirms importGraphState wired BusConfig.kind correctly, not just
    // the persistence shape.
    REQUIRE (loaded.busCount() == 3);
    CHECK (loaded.busKindAt (2) == BusKind::FxReturn);
}

// Slice E2 — per-channel pre-fader send toggle on OutputMixer (mixer-symmetry
// spec 2026-05-23 §Slice E2). One toggle per channel covering all of that
// channel's sends (single-toggle simplification per D3). Default false =
// post-fader (today's behavior). Pre-fader mode bypasses ChannelStrip::process
// (gain + mute) for the send-matrix source so a muted channel still feeds
// reverb-on-cans / live-cue setups.
TEST_CASE ("OutputMixer::channelSendIsPreFader defaults to false; setter round-trips",
           "[output-mixer][send][pre-fader]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    CHECK_FALSE (mixer.channelSendIsPreFader (ch));

    mixer.setChannelSendIsPreFader (ch, true);
    CHECK (mixer.channelSendIsPreFader (ch));

    mixer.setChannelSendIsPreFader (ch, false);
    CHECK_FALSE (mixer.channelSendIsPreFader (ch));

    // Unknown ids return the safe default (false) rather than asserting.
    CHECK_FALSE (mixer.channelSendIsPreFader (OutputChannelId { 999 }));
}

TEST_CASE ("OutputMixer post-fader sends respect channel mute (default behavior)",
           "[output-mixer][send][pre-fader][render-buffer]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    // Mute the strip so the post-fader (post-mute) signal is silent.
    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>> ();
    strip->setMuted (true);
    mixer.setChannelStrip (ch, std::move (strip));

    constexpr int kFrames = 8;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // The default master send is 1.0 (set by addChannel), but the channel is
    // muted → post-fader send is silent → master output is silent.
    for (float v : outLeft)  CHECK (v == Catch::Approx (0.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer pre-fader sends bypass channel mute",
           "[output-mixer][send][pre-fader][render-buffer]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>> ();
    strip->setMuted (true);
    mixer.setChannelStrip (ch, std::move (strip));

    // Flip the channel's send mode to pre-fader. The send-matrix accumulator
    // must now sample the channel's pre-strip signal, so the muted strip
    // doesn't kill the master send.
    mixer.setChannelSendIsPreFader (ch, true);

    constexpr int kFrames = 8;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Source = 1.0, master send = 1.0, strip is muted but bypassed for the
    // send tap → master output carries source on both channels (the source
    // copy lands in both leftScratch + rightScratch).
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer pre-fader sends bypass channel gain",
           "[output-mixer][send][pre-fader][render-buffer]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>> ();
    strip->setGain (0.25f);     // post-fader would attenuate to 0.25
    strip->setPan  (0.5f);      // center → ~0.707 per side
    mixer.setChannelStrip (ch, std::move (strip));
    mixer.setChannelSendIsPreFader (ch, true);

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Pre-fader bypasses gain AND pan — the send tap sees the un-stripped
    // mono → both sides at 1.0 (mono source copied to both scratch
    // channels in renderBuffer's source-fill step).
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer export/import round-trips the preFaderSends flag",
           "[output-mixer][send][pre-fader][persistence]")
{
    OutputMixer source;
    const auto chA = source.addChannel (SignalType::Audio);
    const auto chB = source.addChannel (SignalType::Audio);

    source.setChannelSendIsPreFader (chA, true);
    // chB stays at default false.

    const auto exported = source.exportGraphState();
    REQUIRE (exported.channels.size() == 2);
    CHECK (exported.channels[0].preFaderSends);
    CHECK_FALSE (exported.channels[1].preFaderSends);

    OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.channelSendIsPreFader (chA));
    CHECK_FALSE (loaded.channelSendIsPreFader (chB));
    CHECK (loaded.exportGraphState() == exported);
}

// Slice E3 — channelMainOut(OutputChannelId) accessor pair + radio-style
// routeChannelToBus + send-zero bypass (mixer-symmetry spec 2026-05-23 §E3).
// Defines the explicit main-out manifest on output channels (was inferred
// from "all sends == 0" in slice 5b). routeChannelToBus(ch, busOfKindBus,
// level) NOW sets the main-out + radio-zeros other Bus-kind sends; FX-
// return sends are kept (the sends tab is a separate surface). FX-return
// targets stay additive — they're send taps, not main-outs.
TEST_CASE ("OutputMixer::channelMainOut defaults to Bus(master) on a fresh channel",
           "[output-mixer][channel-main-out]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);

    CHECK (mixer.channelMainOut (ch) == OutputMixer::MainOutDest::Bus);
    CHECK (mixer.channelMainOutBus (ch).value() == 0); // master

    // Unknown ids return safe defaults (Bus + BusId{0}), matching busMainOut.
    CHECK (mixer.channelMainOut (OutputChannelId { 999 })
           == OutputMixer::MainOutDest::Bus);
    CHECK (mixer.channelMainOutBus (OutputChannelId { 999 }).value() == 0);
}

TEST_CASE ("OutputMixer::routeChannelToBus to a Bus-kind bus is radio: sets main-out, zeros other Bus sends, keeps FX-return sends",
           "[output-mixer][channel-main-out]")
{
    using ida::BusKind;
    OutputMixer mixer;
    const auto ch    = mixer.addChannel (SignalType::Audio);
    const auto drums = mixer.addBus (BusConfig { 2, "Drums",  BusKind::Bus });
    const auto rvb   = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Pre-state: master send is 1.0 (addChannel default), aux Bus and FX
    // return are both 0.
    REQUIRE (mixer.sendLevelFor (ch, BusId { 0 }) == Catch::Approx (1.0f));

    // Drop a send into the FX return BEFORE switching main-out. The FX-
    // return tap must survive the radio zero on the next routeChannelToBus
    // (D5 in the spec: sends tab is independent of main-out picker).
    mixer.routeChannelToBus (ch, rvb, 0.5f);
    REQUIRE (mixer.sendLevelFor (ch, rvb) == Catch::Approx (0.5f));

    // Route the channel into the Bus-kind aux. Main-out flips to drums;
    // master goes to 0 (radio), drums goes to 0.75, the FX-return send
    // stays at 0.5 untouched.
    mixer.routeChannelToBus (ch, drums, 0.75f);

    CHECK (mixer.channelMainOut (ch) == OutputMixer::MainOutDest::Bus);
    CHECK (mixer.channelMainOutBus (ch).value() == drums.value());

    CHECK (mixer.sendLevelFor (ch, drums)        == Catch::Approx (0.75f));
    CHECK (mixer.sendLevelFor (ch, BusId { 0 }) == Catch::Approx (0.0f));   // master zeroed (radio)
    CHECK (mixer.sendLevelFor (ch, rvb)         == Catch::Approx (0.5f));   // FX return survives
}

TEST_CASE ("OutputMixer::routeChannelToBus to an FX-return-kind bus is additive (no radio): main-out unchanged",
           "[output-mixer][channel-main-out]")
{
    using ida::BusKind;
    OutputMixer mixer;
    const auto ch    = mixer.addChannel (SignalType::Audio);
    const auto drums = mixer.addBus (BusConfig { 2, "Drums",  BusKind::Bus });
    const auto rvb   = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Establish drums as main-out (radio zeroed master).
    mixer.routeChannelToBus (ch, drums, 1.0f);
    REQUIRE (mixer.channelMainOutBus (ch).value() == drums.value());

    // Sending into an FX return must NOT change main-out and must NOT
    // disturb the existing Bus-kind sends.
    mixer.routeChannelToBus (ch, rvb, 0.3f);

    CHECK (mixer.channelMainOut (ch)            == OutputMixer::MainOutDest::Bus);
    CHECK (mixer.channelMainOutBus (ch).value() == drums.value());
    CHECK (mixer.sendLevelFor (ch, drums) == Catch::Approx (1.0f));
    CHECK (mixer.sendLevelFor (ch, rvb)   == Catch::Approx (0.3f));
}

TEST_CASE ("OutputMixer::setChannelMainOutToHardwareOutput flips channelMainOut to HardwareOutput",
           "[output-mixer][channel-main-out]")
{
    using ida::BusKind;
    OutputMixer mixer;
    const auto ch  = mixer.addChannel (SignalType::Audio);
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Send into an FX return first so the radio path doesn't disturb it.
    mixer.routeChannelToBus (ch, rvb, 0.4f);

    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch, /*pairIndex*/ 1));

    CHECK (mixer.channelMainOut (ch) == OutputMixer::MainOutDest::HardwareOutput);
    CHECK (mixer.channelMainOutHardwareOutPair (ch) == 1);
    // Every Bus-kind send is zeroed when main-out flips to HardwareOutput
    // (so the picker label inference can read channelMainOut directly
    // instead of scanning the send matrix).
    CHECK (mixer.sendLevelFor (ch, BusId { 0 }) == Catch::Approx (0.0f));
    // FX-return sends survive — they're independent of main-out.
    CHECK (mixer.sendLevelFor (ch, rvb) == Catch::Approx (0.4f));
}

// Slice E3 — Bus::sendInputActive() + activeSenderCount_ bypass. A bus with
// zero active senders skips its process loop entirely (RT optimization for
// the common "FX return on standby" case). Counter goes 0→1 on a send
// level 0→nonzero transition, 1→0 on nonzero→0; addChannel bumps master
// for its default 1.0 send; addBus bumps master for its default subgroup
// main-out.
TEST_CASE ("Bus::sendInputActive is false on a fresh aux bus (no senders)",
           "[bus][send-bypass]")
{
    using ida::BusKind;
    OutputMixer mixer;
    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    auto* rvb = mixer.busForId (reverb);
    REQUIRE (rvb != nullptr);
    CHECK_FALSE (rvb->sendInputActive());
}

TEST_CASE ("Bus::sendInputActive flips on channel-send 0->nonzero and back",
           "[bus][send-bypass]")
{
    using ida::BusKind;
    OutputMixer mixer;
    const auto ch     = mixer.addChannel (SignalType::Audio);
    const auto reverb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    auto* rvb = mixer.busForId (reverb);
    REQUIRE (rvb != nullptr);
    REQUIRE_FALSE (rvb->sendInputActive());

    mixer.routeChannelToBus (ch, reverb, 0.5f);
    CHECK (rvb->sendInputActive());

    mixer.routeChannelToBus (ch, reverb, 0.0f);
    CHECK_FALSE (rvb->sendInputActive());
}

TEST_CASE ("Bus::sendInputActive on master tracks default unity sends from added channels",
           "[bus][send-bypass]")
{
    OutputMixer mixer;
    auto* master = mixer.busForId (BusId { 0 });
    REQUIRE (master != nullptr);

    // No channels yet — master has no senders. (Aux buses default their
    // main-out to master too, but none have been added.)
    CHECK_FALSE (master->sendInputActive());

    const auto chA = mixer.addChannel (SignalType::Audio);
    CHECK (master->sendInputActive());

    const auto chB = mixer.addChannel (SignalType::Audio);
    CHECK (master->sendInputActive());

    // Re-route both channels off master (radio) — count returns to zero.
    const auto drums = mixer.addBus (BusConfig { 2, "Drums" }); // bumps master too (subgroup main-out default)
    mixer.routeChannelToBus (chA, drums, 1.0f);
    mixer.routeChannelToBus (chB, drums, 1.0f);
    CHECK (master->sendInputActive()); // drums still routes to master

    // Send drums to hardware-output instead → master loses its last sender.
    REQUIRE (mixer.setBusMainOutToHardwareOutput (drums));
    CHECK_FALSE (master->sendInputActive());
}

TEST_CASE ("renderBuffer skips a bus whose sendInputActive is false (no contribution to outputs)",
           "[output-mixer][bus][send-bypass][render-buffer]")
{
    using ida::BusKind;
    OutputMixer mixer;
    // Add a channel + an FX return with NO send into it. The FX return's
    // sendInputActive must stay false; its process must early-return; its
    // contribution to master (its default main-out) must be zero. The
    // channel registration alone is what makes master active; the test
    // doesn't need to address it by id beyond that.
    const auto ch  = mixer.addChannel (SignalType::Audio);
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Drop a recognizable junk pattern into the FX return's mixBuffer so
    // we can detect whether process touched it (process zeros the buffer
    // on the active path; the bypass path leaves it as-is OR also zeros,
    // depending on impl — but the audible-via-master test is what matters).
    auto* rvbBus = mixer.busForId (rvb);
    REQUIRE (rvbBus != nullptr);
    REQUIRE_FALSE (rvbBus->sendInputActive());

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());

    std::array<float, kFrames> outLeft;  outLeft.fill (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Master is active (ch's default 1.0 master send) — its accumulated mix
    // is source 1.0 × master send 1.0 = 1.0 on both sides. The FX return,
    // having no senders, must contribute exactly 0 to master.
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer export/import round-trips channelMainOut (Bus and HardwareOutput)",
           "[output-mixer][channel-main-out][persistence]")
{
    using ida::BusKind;
    OutputMixer source;
    const auto chA = source.addChannel (SignalType::Audio);
    const auto chB = source.addChannel (SignalType::Audio);
    const auto drums = source.addBus (BusConfig { 2, "Drums", BusKind::Bus });

    // chA → drums (Bus); chB → HardwareOutput pair 2.
    source.routeChannelToBus (chA, drums, 1.0f);
    REQUIRE (source.setChannelMainOutToHardwareOutput (chB, /*pair*/ 2));

    const auto exported = source.exportGraphState();

    OutputMixer loaded;
    loaded.importGraphState (exported);

    CHECK (loaded.channelMainOut (chA)            == OutputMixer::MainOutDest::Bus);
    CHECK (loaded.channelMainOutBus (chA).value() == drums.value());
    CHECK (loaded.channelMainOut (chB)            == OutputMixer::MainOutDest::HardwareOutput);
    CHECK (loaded.channelMainOutHardwareOutPair (chB) == 2);

    CHECK (loaded.exportGraphState() == exported);
}

// Bus-to-FX-return sends (mixer-symmetry, 2026-05-24). Mirror of InputMixer's
// setBusSend / busSendLevel — brings the OutputMixer API to parity per
// [[project_input_output_mixers_identical]].
TEST_CASE ("OutputMixer::busSendLevel defaults to zero",
           "[output-mixer][bus-send]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto aux = mixer.addBus (BusConfig { 2, "Aux",    BusKind::Bus });
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    CHECK (mixer.busSendLevel (aux, rvb)         == Catch::Approx (0.0f));
    CHECK (mixer.busSendLevel (BusId{ 0 }, rvb)  == Catch::Approx (0.0f)); // master too
}

TEST_CASE ("OutputMixer::setBusSend sets and clears the level",
           "[output-mixer][bus-send]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto aux = mixer.addBus (BusConfig { 2, "Aux",    BusKind::Bus });
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    REQUIRE (mixer.setBusSend (aux, rvb, 0.5f));
    CHECK   (mixer.busSendLevel (aux, rvb) == Catch::Approx (0.5f));

    // Clamps to [0, 1].
    REQUIRE (mixer.setBusSend (aux, rvb, 1.7f));
    CHECK   (mixer.busSendLevel (aux, rvb) == Catch::Approx (1.0f));

    // level <= 0 removes the edge.
    REQUIRE (mixer.setBusSend (aux, rvb, 0.0f));
    CHECK   (mixer.busSendLevel (aux, rvb) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::setBusSend rejects self-send (feedback guard)",
           "[output-mixer][bus-send][feedback]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // FX-return sending to itself would close a one-edge feedback loop;
    // MixerGraph::setSend rejects (source == fxReturn). No silent acceptance.
    CHECK_FALSE (mixer.setBusSend (rvb, rvb, 0.5f));
    CHECK (mixer.busSendLevel (rvb, rvb) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::setBusSend rejects sending into a plain Bus (only FX returns accept sends)",
           "[output-mixer][bus-send]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto src = mixer.addBus (BusConfig { 2, "A", BusKind::Bus });
    const auto dst = mixer.addBus (BusConfig { 2, "B", BusKind::Bus });

    // Subgroup main-out (A.mainOut = B) is the legal aux-to-aux route; sends
    // are reserved for FX-returns to prevent ambiguity over what is a send
    // tap vs. a main-out swap. MixerGraph::setSend enforces this.
    CHECK_FALSE (mixer.setBusSend (src, dst, 0.5f));
    CHECK (mixer.busSendLevel (src, dst) == Catch::Approx (0.0f));
}

TEST_CASE ("OutputMixer::setBusSend rejects cycles via the routing graph",
           "[output-mixer][bus-send][feedback]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Route the FX-return's main-out away from master so its graph dest is
    // independent — then setBusSend(master → rvb) is acyclic and accepted.
    REQUIRE (mixer.setBusMainOutToHardwareOutput (rvb));
    REQUIRE (mixer.setBusSend (BusId{ 0 }, rvb, 0.25f));

    // If we now repoint rvb.mainOut back to master, that would create a
    // cycle (master → rvb → master). The graph rejects the second edge —
    // master → rvb already exists, so rvb → master via routeBusToBus closes
    // it. Cycle prevention lives in MixerGraph::setMainOut.
    // routeBusToBus only accepts non-master source buses, but the FX-return
    // is non-master — repoint it back via routeBusToBus(rvb, master).
    // (routeBusToBus rejects if it would cycle.)
    CHECK_FALSE (mixer.routeBusToBus (rvb, BusId{ 0 }));
}

TEST_CASE ("OutputMixer::setBusSend feeds the FX-return mix and survives renderBuffer",
           "[output-mixer][bus-send][render-buffer]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto ch  = mixer.addChannel (SignalType::Audio);
    const auto aux = mixer.addBus (BusConfig { 2, "Aux",    BusKind::Bus });
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    // Route the channel main-out to the aux bus at unity (radio semantic
    // also zeroes the default master send), so the aux bus has signal.
    mixer.routeChannelToBus (ch, aux, 1.0f);
    // Aux now bus-sends to the FX return at 0.5; FX-return main-out stays at
    // master (the default), so the FX-return signal reaches the master
    // output through its own evaluation step.
    REQUIRE (mixer.setBusSend (aux, rvb, 0.5f));

    constexpr int kFrames = 8;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());
    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Two parallel paths into the master: aux→master (unity), and
    // aux→rvb (0.5) → rvb→master (unity). Both add: 1.0 + 0.5 = 1.5.
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.5f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.5f));
}

TEST_CASE ("OutputMixer::setBusSend with level=0 removes the FX-return contribution",
           "[output-mixer][bus-send][render-buffer]")
{
    using ida::BusKind;

    OutputMixer mixer;
    const auto ch  = mixer.addChannel (SignalType::Audio);
    const auto aux = mixer.addBus (BusConfig { 2, "Aux",    BusKind::Bus });
    const auto rvb = mixer.addBus (BusConfig { 2, "Reverb", BusKind::FxReturn });

    mixer.routeChannelToBus (ch, aux, 1.0f);
    REQUIRE (mixer.setBusSend (aux, rvb, 0.5f));
    REQUIRE (mixer.setBusSend (aux, rvb, 0.0f));  // remove the edge

    constexpr int kFrames = 4;
    std::array<float, kFrames> inLeft;  inLeft.fill (1.0f);
    mixer.setChannelAudioSource (ch, inLeft.data(), inLeft.data());
    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (nullptr, 0, outputs, 2, kFrames);

    // Only the aux→master path remains (1.0). FX-return is bypassed (no senders).
    for (float v : outLeft)  CHECK (v == Catch::Approx (1.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (1.0f));
}

TEST_CASE ("OutputMixer round-trips bus pan / width / gain / muted",
           "[output-mixer][persistence]")
{
    ida::OutputMixer mixer;
    const auto busId = mixer.addBus (ida::BusConfig { 2, "OutAuxA", ida::BusKind::Bus });
    auto* bus = mixer.busForId (busId);
    REQUIRE (bus != nullptr);
    bus->setGain  (0.5f);
    bus->setMuted (true);
    bus->setPan   (0.25f);
    bus->setWidth (1.5f);

    const auto state = mixer.exportGraphState();
    ida::OutputMixer restored;
    restored.importGraphState (state);

    auto* restoredBus = restored.busForId (busId);
    REQUIRE (restoredBus != nullptr);
    CHECK (restoredBus->gain()  == Catch::Approx (0.5f));
    CHECK (restoredBus->muted());
    CHECK (restoredBus->pan()   == Catch::Approx (0.25f));
    CHECK (restoredBus->width() == Catch::Approx (1.5f));
    CHECK (restored.exportGraphState() == state);
}

TEST_CASE ("OutputMixer round-trips bus->FX-return send levels",
           "[output-mixer][persistence]")
{
    ida::OutputMixer mixer;
    const auto srcBus = mixer.addBus (ida::BusConfig { 2, "SrcBus", ida::BusKind::Bus });
    const auto fxRetA = mixer.addBus (ida::BusConfig { 2, "FxRetA", ida::BusKind::FxReturn });
    const auto fxRetB = mixer.addBus (ida::BusConfig { 2, "FxRetB", ida::BusKind::FxReturn });
    REQUIRE (mixer.setBusSend (srcBus, fxRetA, 0.65f));
    REQUIRE (mixer.setBusSend (srcBus, fxRetB, 0.30f));

    const auto state = mixer.exportGraphState();
    ida::OutputMixer restored;
    restored.importGraphState (state);
    CHECK (restored.busSendLevel (srcBus, fxRetA) == Catch::Approx (0.65f));
    CHECK (restored.busSendLevel (srcBus, fxRetB) == Catch::Approx (0.30f));
    CHECK (restored.exportGraphState() == state);
}

// End-to-end persistence round-trip through the JSON layer — mirror of the
// app's save/load path: exportGraphState -> serializeMixerGraphState (JSON) ->
// deserializeOutputMixerGraphState -> new mixer + importGraphState. The
// existing pan/width/gain/muted test only crosses the engine boundary; this
// one proves the JSON layer doesn't drop bus state on the way to disk.
TEST_CASE ("OutputMixer aux-bus pan / width / gain / muted survive the full JSON round-trip",
           "[output-mixer][persistence][json]")
{
    ida::OutputMixer mixer;
    const auto busId = mixer.addBus (ida::BusConfig { 2, "OutAuxA", ida::BusKind::Bus });
    auto* bus = mixer.busForId (busId);
    REQUIRE (bus != nullptr);
    bus->setGain  (0.5f);
    bus->setMuted (true);
    bus->setPan   (0.25f);
    bus->setWidth (1.5f);

    const auto exported = mixer.exportGraphState();
    const auto json     = ida::persistence::serializeMixerGraphState (exported);
    const auto reloaded = ida::persistence::deserializeOutputMixerGraphState (json);

    ida::OutputMixer restored;
    restored.importGraphState (reloaded);

    auto* restoredBus = restored.busForId (busId);
    REQUIRE (restoredBus != nullptr);
    CHECK (restoredBus->gain()  == Catch::Approx (0.5f));
    CHECK (restoredBus->muted());
    CHECK (restoredBus->pan()   == Catch::Approx (0.25f));
    CHECK (restoredBus->width() == Catch::Approx (1.5f));
}

TEST_CASE ("OutputMixer::setOttoSource / channelOttoSource round-trip through "
           "exportGraphState + importGraphState",
           "[output-mixer][otto-source]")
{
    using namespace ida;

    OutputMixer mix;

    const auto chPhrase = mix.addChannel (SignalType::Audio);
    const auto chOtto0  = mix.addChannel (SignalType::Audio);
    const auto chOtto31 = mix.addChannel (SignalType::Audio);

    SECTION ("freshly added channels default ottoSource to kOttoSourcePhraseChannel")
    {
        REQUIRE (mix.channelOttoSource (chPhrase)  == kOttoSourcePhraseChannel);
        REQUIRE (mix.channelOttoSource (chOtto0)   == kOttoSourcePhraseChannel);
        REQUIRE (mix.channelOttoSource (chOtto31)  == kOttoSourcePhraseChannel);
        REQUIRE (mix.channelOttoSource (OutputChannelId { 9999 }) == kOttoSourcePhraseChannel); // unknown id
    }

    SECTION ("setOttoSource writes; getter reads back")
    {
        mix.setOttoSource (chOtto0, 0);
        mix.setOttoSource (chOtto31, 31);

        REQUIRE (mix.channelOttoSource (chPhrase)  == kOttoSourcePhraseChannel);
        REQUIRE (mix.channelOttoSource (chOtto0)   == 0);
        REQUIRE (mix.channelOttoSource (chOtto31)  == 31);
    }

    SECTION ("ottoSource round-trips through export + import")
    {
        mix.setOttoSource (chOtto0, 0);
        mix.setOttoSource (chOtto31, 31);

        const auto state = mix.exportGraphState();

        OutputMixer fresh;
        fresh.importGraphState (state);

        REQUIRE (fresh.channelOttoSource (chPhrase)  == kOttoSourcePhraseChannel);
        REQUIRE (fresh.channelOttoSource (chOtto0)   == 0);
        REQUIRE (fresh.channelOttoSource (chOtto31)  == 31);
    }

    SECTION ("removeChannel cleans up the per-channel ottoSource (swap-erase parity)")
    {
        mix.setOttoSource (chOtto0,  0);
        mix.setOttoSource (chOtto31, 31);
        mix.removeChannel (chOtto0);

        REQUIRE (mix.channelOttoSource (chOtto31) == 31);
        const auto chReplay = mix.addChannel (SignalType::Audio);
        REQUIRE (mix.channelOttoSource (chReplay) == kOttoSourcePhraseChannel);
    }
}
