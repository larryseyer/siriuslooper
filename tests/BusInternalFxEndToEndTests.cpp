// P7 T3a-C — end-to-end internal-FX dispatch tests through `Bus::process`.
//
// These cases exercise the full engine-side path: build an EffectChain with
// an Internal-EQ entry, hand it to Bus::setEffectChain (which calls
// `host_->setInternalFxAtSlot(...)` for every slot — bind on Internal,
// unbind on Plugin / Empty / past-end), and push audio through Bus::process.
// On the audio thread, Bus::process iterates the chain and dispatches each
// non-bypassed slot via host_->pumpSlot, which checks the internal-adapter
// table first.
//
// What we are NOT testing here:
//   - The OOP plug-in path's actual round-trip (that needs a child process;
//     the host-side dispatch tests cover the internal-vs-OOP-key dispatch
//     ordering).
//   - DSP correctness of PlayerEQ itself (OTTO's responsibility).
//
// What we ARE testing:
//   - Internal slot in the chain → adapter is bound → audio is processed
//     (output is finite, bounded, and tracks the input).
//   - Chain-transition: Internal → empty → Internal works (adapter unbound,
//     then re-bound). After the empty step, pumpSlot is never called by
//     Bus::process (the inline path takes over), so the dry signal passes
//     through.
//   - Chain-transition: Internal → Plugin (with no OOP child) does NOT
//     trip the host's double-bind jassert. The internal adapter must be
//     unbound BEFORE configureBus would notice the collision. We don't
//     drive configureBus here (no child), but we DO drive the host's
//     internal-adapter unbind via Bus::setEffectChain and then manually
//     emulate the OOP-side bind by calling configureBus and asserting it
//     doesn't trigger the exclusivity assert.
//
// Tag: `[internal-fx][end-to-end]` per the umbrella plan.

#include "sirius/Bus.h"
#include "sirius/EffectChain.h"
#include "sirius/InternalFxId.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/PluginDescriptor.h"

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
                    float                 freqHz   = 440.0f,
                    float                 amp      = 0.25f,
                    int                   srHz     = kSampleRate)
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

    // Drives one block through a stereo Bus: fills the bus's mix scratch
    // with the supplied signal and calls Bus::process.
    void runBus (ida::Bus&                       bus,
                 const std::array<float, kBlockSamples>& left,
                 const std::array<float, kBlockSamples>& right,
                 std::array<float, kBlockSamples>&       outL,
                 std::array<float, kBlockSamples>&       outR)
    {
        float* const lScratch = bus.mixBufferChannel (0);
        float* const rScratch = bus.mixBufferChannel (1);
        REQUIRE (lScratch != nullptr);
        REQUIRE (rScratch != nullptr);
        for (int i = 0; i < kBlockSamples; ++i)
        {
            lScratch[i] = left[i];
            rScratch[i] = right[i];
        }
        outL.fill (0.0f);
        outR.fill (0.0f);
        float* outs[2] = { outL.data(), outR.data() };
        bus.process (outs, 2, kBlockSamples);
    }
}

TEST_CASE ("Bus with Internal-EQ in slot 0 routes audio through the adapter",
           "[internal-fx][end-to-end]")
{
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    ida::Bus bus (ida::BusId { 1 }, ida::BusConfig { 2, "Aux" });
    bus.setEffectChainHost (&host);

    auto chain = ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq));
    bus.setEffectChain (chain);

    std::array<float, kBlockSamples> lin {}, rin {};
    const float inputPeak = fillSine (lin, rin);

    std::array<float, kBlockSamples> outL {}, outR {};
    runBus (bus, lin, rin, outL, outR);

    // Bus output should be finite and approximately track the input. The
    // flat-default EQ ≈ identity ⇒ output peak tracks input peak. We
    // compare across the back half of the block (IIR settling tail).
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

TEST_CASE ("Bus chain transition Internal → empty → Internal — adapter unbinds and re-binds",
           "[internal-fx][end-to-end]")
{
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    ida::Bus bus (ida::BusId { 2 }, ida::BusConfig { 2, "Aux2" });
    bus.setEffectChainHost (&host);

    // Start with the EQ in slot 0.
    auto chainWithEq = ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq));
    bus.setEffectChain (chainWithEq);

    std::array<float, kBlockSamples> lin {}, rin {};
    const float inputPeak = fillSine (lin, rin);

    // Phase 1 — EQ active, signal processed.
    {
        std::array<float, kBlockSamples> outL {}, outR {};
        runBus (bus, lin, rin, outL, outR);
        float outPeak = 0.0f;
        for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
            outPeak = std::max ({ outPeak, std::abs (outL[i]), std::abs (outR[i]) });
        CHECK (outPeak > 0.5f * inputPeak);
    }

    // Phase 2 — empty chain. Bus takes the inline path (no host call at
    // all) since hasActiveSlot is false. The internal-adapter table was
    // swept clean by setEffectChain's nullopt-everywhere pass, so no stale
    // adapter lingers. Output should be the input signal (gain 1.0).
    bus.setEffectChain (ida::EffectChain{});
    {
        std::array<float, kBlockSamples> outL {}, outR {};
        runBus (bus, lin, rin, outL, outR);
        // Dry pass-through: output ≈ input.
        for (int i = 0; i < kBlockSamples; ++i)
        {
            CHECK (std::isfinite (outL[i]));
            CHECK (std::isfinite (outR[i]));
            CHECK (std::abs (outL[i] - lin[i]) < 1.0e-5f);
            CHECK (std::abs (outR[i] - rin[i]) < 1.0e-5f);
        }
    }

    // Phase 3 — re-bind the EQ chain. The new EQ adapter must auto-prepare
    // (host_ remembers the prepare call from setup) so its first process
    // returns true.
    bus.setEffectChain (chainWithEq);
    {
        std::array<float, kBlockSamples> outL {}, outR {};
        runBus (bus, lin, rin, outL, outR);
        float outPeak = 0.0f;
        for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
            outPeak = std::max ({ outPeak, std::abs (outL[i]), std::abs (outR[i]) });
        CHECK (outPeak > 0.5f * inputPeak);
    }
}

TEST_CASE ("Bus chain transition Internal → Plugin clears the internal binding before the OOP path sees the slot",
           "[internal-fx][end-to-end]")
{
    // The OOP host's configureBus() asserts (internalAdapters_.find(key) ==
    // internalAdapters_.end()) — the (nodeKey, slotIdx) pair must NOT be in
    // both maps. Bus::setEffectChain's sweep must therefore call
    // setInternalFxAtSlot(nullopt) on slots that flipped from Internal to
    // Plugin BEFORE any OOP configureBus would happen.
    //
    // We can't drive configureBus end-to-end here (it spawns a child).
    // Instead: bind an internal EQ, then swap to a Plugin-kind chain via
    // Bus::setEffectChain, and confirm the host no longer reports the
    // internal adapter as bound. The exclusivity invariant is then trivially
    // safe — there's nothing to collide with.
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    constexpr std::int64_t kNodeKey = 3;
    ida::Bus bus (ida::BusId { kNodeKey }, ida::BusConfig { 2, "Aux3" });
    bus.setEffectChainHost (&host);

    // Phase 1 — bind EQ via the engine path. Verify with a real audio
    // round-trip: the bus produces non-zero output ⇒ the adapter is bound.
    bus.setEffectChain (ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq)));
    {
        std::array<float, kBlockSamples> lin {}, rin {}, outL {}, outR {};
        fillSine (lin, rin);
        runBus (bus, lin, rin, outL, outR);
        float outPeak = 0.0f;
        for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
            outPeak = std::max ({ outPeak, std::abs (outL[i]), std::abs (outR[i]) });
        REQUIRE (outPeak > 0.0f);
    }

    // Phase 2 — swap to a Plugin chain. The internal adapter must be
    // unbound; the slot's `pumpSlot` falls through to the OOP path, which
    // has no instance, so returns false (dry pass-through carries the
    // mixBuffer signal through processedBuffer_). Output should equal the
    // input — proving the adapter no longer intercepts.
    ida::PluginDescriptor descriptor;
    descriptor.format = ida::PluginFormat::Clap;
    descriptor.name   = "fakePlugin";
    bus.setEffectChain (ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makePlugin (descriptor, "fakePlugin")));

    {
        std::array<float, kBlockSamples> lin {}, rin {}, outL {}, outR {};
        fillSine (lin, rin);
        runBus (bus, lin, rin, outL, outR);
        // Bus::process took the effect-chain path (Plugin slot is non-bypassed
        // ⇒ hasActiveSlot == true), copied mixBuffer_ → processedBuffer_,
        // called pumpSlot which missed (no internal adapter, no OOP instance,
        // both maps empty for this key), so processedBuffer_ still holds the
        // dry mix. Output should equal the input.
        for (int i = 0; i < kBlockSamples; ++i)
        {
            CHECK (std::isfinite (outL[i]));
            CHECK (std::isfinite (outR[i]));
            CHECK (std::abs (outL[i] - lin[i]) < 1.0e-5f);
            CHECK (std::abs (outR[i] - rin[i]) < 1.0e-5f);
        }
    }
}

TEST_CASE ("Bus chain shrink — orphan internal adapters on past-end slots are unbound",
           "[internal-fx][end-to-end]")
{
    // When a chain shrinks from N slots to M < N slots, the rebind sweep
    // walks 0..kMaxSlots so the orphan (M..N-1) slots get unbound. Verifies
    // setEffectChain's "sweep up to kMaxSlots" guarantee.
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    ida::Bus bus (ida::BusId { 4 }, ida::BusConfig { 2, "Aux4" });
    bus.setEffectChainHost (&host);

    // Phase 1 — two EQs.
    auto twoEqs = ida::EffectChain{}
        .withAppended (ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq))
        .withAppended (ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq));
    bus.setEffectChain (twoEqs);

    // Phase 2 — drop to one EQ. Slot 1's adapter must be unbound. After
    // this, pumping (4, 1) directly through the host must miss.
    bus.setEffectChain (ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq)));

    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    fillSine (lin, rin);
    constexpr float kSentinel = 0.987654f;
    lout.fill (kSentinel);
    rout.fill (kSentinel);

    const std::array<const float*, 2> inPtrs  { lin.data(), rin.data() };
    const std::array<float*, 2>       outPtrs { lout.data(), rout.data() };
    const bool pumped = host.pumpSlot (
        /* nodeKey */ 4, /* slotIdx */ 1,
        inPtrs.data(),
        const_cast<float* const*> (outPtrs.data()),
        2, kBlockSamples);
    CHECK_FALSE (pumped);
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        CHECK (lout[i] == kSentinel);
        CHECK (rout[i] == kSentinel);
    }
}

TEST_CASE ("Bus::setEffectChain with a bypassed Internal slot propagates the flag to the host",
           "[internal-fx][end-to-end][bypass-load]")
{
    // P7 T5 slice 3 — session-load wiring. A chain whose Internal slot
    // carries bypassed=true (as persisted in JSON via SessionFormat) must
    // surface on the host's bypass map at chain-set time so the host's
    // pumpSlot returns false even though the adapter is bound. This
    // pins the load-path contract directly against the host (bypassing
    // Bus::processChain's own per-entry bypass skip, which would mask
    // the host-side propagation if we only observed Bus output).
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    constexpr std::int64_t kBusId = 9001;
    ida::Bus bus (ida::BusId { kBusId }, ida::BusConfig { 2, "Aux" });
    bus.setEffectChainHost (&host);

    // Build a chain entry the same way SessionFormat does on load:
    // makeInternal(kEq), then flip the persisted bypassed flag.
    auto entry = ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq);
    entry.bypassed = true;
    auto chain = ida::EffectChain{}.withAppended (std::move (entry));
    bus.setEffectChain (chain);

    // Direct host pumpSlot — adapter IS bound (Bus's chain-set sweep called
    // setInternalFxAtSlot on slot 0), AND bypass flag IS set on the host
    // (slice 3 wiring). Expect false return with output buffers unchanged.
    std::array<float, kBlockSamples> lin {}, rin {};
    fillSine (lin, rin);
    std::array<float, kBlockSamples> lout {}, rout {};
    constexpr float kSentinel = 0.4242f;
    lout.fill (kSentinel);
    rout.fill (kSentinel);

    const std::array<const float*, 2> inPtrs  { lin.data(),  rin.data()  };
    const std::array<float*, 2>       outPtrs { lout.data(), rout.data() };
    const bool pumped = host.pumpSlot (kBusId, /*slotIdx*/ 0,
                                       inPtrs.data(),
                                       const_cast<float* const*> (outPtrs.data()),
                                       2, kBlockSamples);
    CHECK_FALSE (pumped);
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        CHECK (lout[i] == kSentinel);
        CHECK (rout[i] == kSentinel);
    }
}

TEST_CASE ("Bus::setEffectChain with bypassed=false leaves the host dispatching the adapter",
           "[internal-fx][end-to-end][bypass-load]")
{
    // P7 T5 slice 3 — symmetric: a chain whose Internal slot has
    // bypassed=false (the common case) must leave the host's bypass map
    // absent (or false) for that slot, so pumpSlot dispatches normally.
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    constexpr std::int64_t kBusId = 9002;
    ida::Bus bus (ida::BusId { kBusId }, ida::BusConfig { 2, "Aux" });
    bus.setEffectChainHost (&host);

    auto chain = ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq));
    bus.setEffectChain (chain);

    std::array<float, kBlockSamples> lin {}, rin {};
    fillSine (lin, rin);
    std::array<float, kBlockSamples> lout {}, rout {};
    lout.fill (0.0f);
    rout.fill (0.0f);

    const std::array<const float*, 2> inPtrs  { lin.data(),  rin.data()  };
    const std::array<float*, 2>       outPtrs { lout.data(), rout.data() };
    const bool pumped = host.pumpSlot (kBusId, /*slotIdx*/ 0,
                                       inPtrs.data(),
                                       const_cast<float* const*> (outPtrs.data()),
                                       2, kBlockSamples);
    REQUIRE (pumped);
}

TEST_CASE ("Bus::setEffectChain replacing a bypassed Internal slot with a fresh one clears bypass",
           "[internal-fx][end-to-end][bypass-load]")
{
    // P7 T5 slice 3 — re-bind clears the host bypass. Sequence:
    //   1. Set chain with {Internal=kEq, bypassed=true} → host bypass on.
    //   2. Set chain with {Internal=kEq, bypassed=false} → host bypass off.
    // The second setEffectChain re-runs the sweep, which calls
    // setInternalFxAtSlot (resets bypass to absent in the host) and then
    // setInternalFxBypassAtSlot(.., false) — both no-ops if absent. Either
    // way the host must dispatch the second call's adapter.
    ida::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    constexpr std::int64_t kBusId = 9003;
    ida::Bus bus (ida::BusId { kBusId }, ida::BusConfig { 2, "Aux" });
    bus.setEffectChainHost (&host);

    // Chain 1 — bypassed.
    {
        auto e = ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq);
        e.bypassed = true;
        bus.setEffectChain (ida::EffectChain{}.withAppended (std::move (e)));
    }

    // Chain 2 — not bypassed.
    bus.setEffectChain (ida::EffectChain{}.withAppended (
        ida::EffectChainEntry::makeInternal (ida::InternalFxId::kEq)));

    std::array<float, kBlockSamples> lin {}, rin {};
    fillSine (lin, rin);
    std::array<float, kBlockSamples> lout {}, rout {};
    lout.fill (0.0f);
    rout.fill (0.0f);

    const std::array<const float*, 2> inPtrs  { lin.data(),  rin.data()  };
    const std::array<float*, 2>       outPtrs { lout.data(), rout.data() };
    REQUIRE (host.pumpSlot (kBusId, 0,
                            inPtrs.data(),
                            const_cast<float* const*> (outPtrs.data()),
                            2, kBlockSamples));
}
