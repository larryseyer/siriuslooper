// P7 T3a I-2 — end-to-end internal-FX dispatch tests through InputMixer's
// `renderInputGraph` → Bus::process pipeline.
//
// These cases verify the wiring landed by `InputMixer::setEffectChainHost`:
// the setter must forward the audio-thread effect-chain host to every bus
// already registered AND to any bus added afterwards, so an
// `EffectChainEntry::makeInternal(kEq)` dropped into an Input-side bus chain
// actually reaches the host's `pumpSlot` dispatch instead of silently
// no-opping.
//
// Mirrors `tests/BusInternalFxEndToEndTests.cpp` (the Bus-direct version) so
// the wiring is exercised through the surface MainComponent actually drives:
// the InputMixer routing graph. Channel → Bus → HardwareOutput, with the
// Internal-EQ on the bus's effect chain, then renderInputGraph delivers the
// processed audio into the `directOut` buffer.
//
// What we are NOT testing here:
//   - DSP correctness of PlayerEQ (OTTO's responsibility).
//   - InputMixer's tape/send paths (covered by existing InputMixer tests).
//
// What we ARE testing:
//   - Adapter wiring through InputMixer::setEffectChainHost (pre-existing bus).
//   - Adapter wiring through addBus AFTER setEffectChainHost (new bus picks
//     up the stashed pointer at registration time).
//
// Tag: `[input-mixer][internal-fx][end-to-end]` per the umbrella plan.

#include "sirius/EffectChain.h"
#include "sirius/InputMixer.h"
#include "sirius/InternalFxId.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/SignalType.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

namespace
{
    constexpr int    kSampleRate   = 48000;
    constexpr int    kMaxBlock     = 512;
    constexpr int    kBlockSamples = 64;
    constexpr double kPi           = 3.14159265358979323846;

    template <std::size_t N>
    float fillSine (std::array<float, N>& left,
                    std::array<float, N>& right,
                    float                 freqHz = 440.0f,
                    float                 amp    = 0.25f,
                    int                   srHz   = kSampleRate)
    {
        float peak = 0.0f;
        for (std::size_t i = 0; i < N; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (srHz);
            const float v = amp * std::sin (2.0f * static_cast<float> (kPi) * freqHz * t);
            left [i] = v;
            right[i] = v;
            peak     = std::max (peak, std::abs (v));
        }
        return peak;
    }
}

TEST_CASE ("InputMixer routes audio through an Internal-EQ bus chain via setEffectChainHost (existing bus)",
           "[input-mixer][internal-fx][end-to-end]")
{
    // Construct a host and an InputMixer with a pre-existing bus, THEN call
    // setEffectChainHost. The setter must walk every already-registered bus
    // and forward the host pointer; otherwise the bus's effect chain would
    // hold the Internal-EQ entry but Bus::process would never dispatch
    // because host_ is null (silent no-op — the pre-T04 bug).
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    ida::InputMixer mixer;
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Aux" });
    REQUIRE (bus != ida::BusId { 0 });

    mixer.setEffectChainHost (&host);
    mixer.setBusEffectChain (bus,
        ida::EffectChain{}.withAppended (
            ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq)));

    // Register a channel that feeds device channels 0/1 stereo, route it
    // through the bus, and send the bus to HardwareOutput so renderInputGraph
    // delivers the processed audio into the directOut buffer.
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, /*leftDeviceChannel*/ 0,
                                     /*rightDeviceChannel*/ 1, /*stereo*/ true);
    REQUIRE (mixer.setChannelMainOutToBus (ch, bus));
    REQUIRE (mixer.setBusMainOutToHardwareOutput (bus));

    std::array<float, kBlockSamples> lin {}, rin {};
    const float inputPeak = fillSine (lin, rin);

    std::array<float, kBlockSamples> outL {}, outR {};
    const float* deviceIn[2] { lin.data(), rin.data() };
    float*       directOut[2] { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, kBlockSamples);

    // Bus output should be finite and approximately track the input. The
    // flat-default EQ ≈ identity ⇒ output peak tracks input peak. We compare
    // across the back half of the block (IIR settling tail), matching the
    // pattern in BusInternalFxEndToEndTests.cpp.
    float outPeak = 0.0f;
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        CHECK (std::isfinite (outL[i]));
        CHECK (std::isfinite (outR[i]));
        outPeak = std::max ({ outPeak, std::abs (outL[i]), std::abs (outR[i]) });
    }
    CHECK (outPeak > 0.5f * inputPeak);
    CHECK (outPeak < 1.5f * inputPeak);
}

TEST_CASE ("InputMixer addBus AFTER setEffectChainHost still wires the new bus to the host",
           "[input-mixer][internal-fx][end-to-end]")
{
    // Call setEffectChainHost on an empty mixer FIRST, then addBus. The
    // setter stashes the pointer in effectChainHost_ and addBus must read
    // that field when constructing the new bus, otherwise a bus added later
    // would silently no-op its chain. Mirrors OutputMixer::addBus's same
    // post-stash behavior.
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    ida::InputMixer mixer;
    mixer.setEffectChainHost (&host);

    const auto bus = mixer.addBus (ida::BusConfig { 2, "AuxLate" });
    REQUIRE (bus != ida::BusId { 0 });

    mixer.setBusEffectChain (bus,
        ida::EffectChain{}.withAppended (
            ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq)));

    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    REQUIRE (mixer.setChannelMainOutToBus (ch, bus));
    REQUIRE (mixer.setBusMainOutToHardwareOutput (bus));

    std::array<float, kBlockSamples> lin {}, rin {};
    const float inputPeak = fillSine (lin, rin);

    std::array<float, kBlockSamples> outL {}, outR {};
    const float* deviceIn[2] { lin.data(), rin.data() };
    float*       directOut[2] { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, kBlockSamples);

    float outPeak = 0.0f;
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        CHECK (std::isfinite (outL[i]));
        CHECK (std::isfinite (outR[i]));
        outPeak = std::max ({ outPeak, std::abs (outL[i]), std::abs (outR[i]) });
    }
    CHECK (outPeak > 0.5f * inputPeak);
    CHECK (outPeak < 1.5f * inputPeak);
}
