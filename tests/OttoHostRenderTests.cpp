// Tests for ida::OttoHost::renderBlock + per-output pointer accessors
// (M-OTTO-4 slice 1). The host wraps OTTO's PlayerManager::processGlobalMixer
// and dispatches the 32 stereo-pair output index space across three OTTO
// accessor families (24 instrument channels + 4 FX returns + 4 player buses).
//
// These tests do NOT exercise actual audio (OTTO's transport isn't rolled
// here, so the buffers stay silent) — they pin the WIRING: the call doesn't
// crash before or after prepare, accessors return non-null after prepare for
// every in-range index and nullptr for every out-of-range index, and the
// returned pointers are stable across renderBlock calls (OTTO owns the
// buffers; we just observe them).
//
// `juce::ScopedJuceInitialiser_GUI` is required because OttoHost owns a
// `juce::Timer` whose ctor touches the MessageManager (same as
// OttoHostTransportTests.cpp).

#include "ida/OttoHost.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <climits>

using ida::OttoHost;

namespace
{

constexpr double kTestSampleRate = 48000.0;
constexpr int    kTestBlockSize  = 256;

} // namespace

TEST_CASE ("OttoHost::renderBlock before prepare is a safe no-op",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    REQUIRE_FALSE (host.isPrepared());

    juce::MidiBuffer midi;

    // Must not crash. Must not throw (it's declared noexcept).
    host.renderBlock (kTestBlockSize, midi);
    host.renderBlock (0, midi);
    host.renderBlock (-1, midi);

    // Accessors still nullptr — neither prepared nor rendered.
    for (int i = 0; i < OttoHost::kNumOttoOutputs; ++i)
    {
        CHECK (host.getOttoOutputLeft  (i) == nullptr);
        CHECK (host.getOttoOutputRight (i) == nullptr);
    }
}

TEST_CASE ("OttoHost::getOttoOutput{Left,Right} returns non-null for every in-range index after prepare",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    REQUIRE (host.isPrepared());

    juce::MidiBuffer midi;
    host.renderBlock (kTestBlockSize, midi);

    for (int i = 0; i < OttoHost::kNumOttoOutputs; ++i)
    {
        CHECK (host.getOttoOutputLeft  (i) != nullptr);
        CHECK (host.getOttoOutputRight (i) != nullptr);
    }
}

TEST_CASE ("OttoHost::getOttoOutput{Left,Right} returns nullptr for out-of-range indices",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    juce::MidiBuffer midi;
    host.renderBlock (kTestBlockSize, midi);

    // Negatives.
    CHECK (host.getOttoOutputLeft  (-1)         == nullptr);
    CHECK (host.getOttoOutputRight (-1)         == nullptr);
    CHECK (host.getOttoOutputLeft  (INT_MIN)    == nullptr);
    CHECK (host.getOttoOutputRight (INT_MIN)    == nullptr);

    // At / past the upper bound.
    CHECK (host.getOttoOutputLeft  (OttoHost::kNumOttoOutputs)     == nullptr);
    CHECK (host.getOttoOutputRight (OttoHost::kNumOttoOutputs)     == nullptr);
    CHECK (host.getOttoOutputLeft  (OttoHost::kNumOttoOutputs + 1) == nullptr);
    CHECK (host.getOttoOutputRight (INT_MAX)                       == nullptr);
}

TEST_CASE ("OttoHost output pointers are stable across renderBlock calls",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);

    juce::MidiBuffer midi;
    host.renderBlock (kTestBlockSize, midi);
    const float* l0_first  = host.getOttoOutputLeft  (0);
    const float* l23_first = host.getOttoOutputLeft  (23);
    const float* l28_first = host.getOttoOutputLeft  (28);
    const float* r24_first = host.getOttoOutputRight (24);

    host.renderBlock (kTestBlockSize, midi);
    CHECK (host.getOttoOutputLeft  (0)  == l0_first);
    CHECK (host.getOttoOutputLeft  (23) == l23_first);
    CHECK (host.getOttoOutputLeft  (28) == l28_first);
    CHECK (host.getOttoOutputRight (24) == r24_first);

    // Pointer stability holds across many blocks — OTTO's GlobalMixer
    // allocates its per-channel buffers once in prepare() and never
    // reallocates them.
    for (int i = 0; i < 32; ++i)
        host.renderBlock (kTestBlockSize, midi);
    CHECK (host.getOttoOutputLeft  (0)  == l0_first);
    CHECK (host.getOttoOutputLeft  (23) == l23_first);
    CHECK (host.getOttoOutputLeft  (28) == l28_first);
    CHECK (host.getOttoOutputRight (24) == r24_first);
}

TEST_CASE ("OttoHost dispatches each index range to a distinct underlying buffer family",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    juce::MidiBuffer midi;
    host.renderBlock (kTestBlockSize, midi);

    // Sanity: a representative pointer from each of the three families
    // (channel / FX-return / player-bus) must point at three distinct
    // buffers. Catches a copy-paste bug that would route an FX-return
    // index to a channel accessor (or vice versa) and silently merge
    // the audio of two different OTTO outputs into the same IDA strip.
    const float* channel0   = host.getOttoOutputLeft (OttoHost::kOttoChannelRangeBegin);
    const float* fxReturn0  = host.getOttoOutputLeft (OttoHost::kOttoFxReturnRangeBegin);
    const float* playerBus0 = host.getOttoOutputLeft (OttoHost::kOttoPlayerBusRangeBegin);

    REQUIRE (channel0   != nullptr);
    REQUIRE (fxReturn0  != nullptr);
    REQUIRE (playerBus0 != nullptr);

    CHECK (channel0   != fxReturn0);
    CHECK (channel0   != playerBus0);
    CHECK (fxReturn0  != playerBus0);

    // And L != R within a single output (stereo pair, not dual mono).
    const float* channel0L = host.getOttoOutputLeft  (OttoHost::kOttoChannelRangeBegin);
    const float* channel0R = host.getOttoOutputRight (OttoHost::kOttoChannelRangeBegin);
    REQUIRE (channel0L != nullptr);
    REQUIRE (channel0R != nullptr);
    CHECK   (channel0L != channel0R);
}

TEST_CASE ("OttoHost::renderBlock tolerates zero / negative numSamples without crashing",
           "[otto-host-render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);

    juce::MidiBuffer midi;
    host.renderBlock (0, midi);
    host.renderBlock (-1, midi);
    host.renderBlock (INT_MIN, midi);
    host.renderBlock (kTestBlockSize, midi);

    // Still healthy after the no-op + negative calls.
    CHECK (host.getOttoOutputLeft  (0) != nullptr);
    CHECK (host.getOttoOutputRight (0) != nullptr);
}

TEST_CASE ("OttoHost embeds OTTOProcessor — getPlayerManager() reaches GlobalMixer through the processor",
           "[otto-host-render][processor-embed]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    REQUIRE (host.isPrepared());

    // First renderBlock populates GlobalMixer's per-channel output pointers
    // (which getOttoOutputLeft/Right read). The S1 wire change routes
    // processGlobalMixer through the embedded OTTOProcessor's
    // getPlayerManager() accessor instead of the bare PlayerManager member
    // pre-S1 used. Identical observable behavior — the accessor chain
    // returns the same GlobalMixer instance the processor manages.
    juce::MidiBuffer midi;
    host.renderBlock (kTestBlockSize, midi);

    for (int i = 0; i < OttoHost::kNumOttoOutputs; ++i)
    {
        CHECK (host.getOttoOutputLeft  (i) != nullptr);
        CHECK (host.getOttoOutputRight (i) != nullptr);
    }

    // Sustained per-block driving — the OTTOProcessor embed must survive
    // many renderBlock calls without destabilizing GlobalMixer's per-channel
    // buffer pointers (OTTO's GlobalMixer allocates its buffers once in
    // prepare() and reuses them). This pins the IDA-side WIRING, not the
    // OTTO-side audio invariants.
    const float* l0_first   = host.getOttoOutputLeft  (0);
    const float* lFx_first  = host.getOttoOutputLeft  (OttoHost::kOttoFxReturnRangeBegin);
    const float* rBus_first = host.getOttoOutputRight (OttoHost::kOttoPlayerBusRangeBegin);

    REQUIRE (l0_first   != nullptr);
    REQUIRE (lFx_first  != nullptr);
    REQUIRE (rBus_first != nullptr);

    for (int block = 0; block < 100; ++block)
        host.renderBlock (kTestBlockSize, midi);

    CHECK (host.getOttoOutputLeft  (0)                                  == l0_first);
    CHECK (host.getOttoOutputLeft  (OttoHost::kOttoFxReturnRangeBegin)  == lFx_first);
    CHECK (host.getOttoOutputRight (OttoHost::kOttoPlayerBusRangeBegin) == rBus_first);
}
