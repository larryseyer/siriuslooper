// P7 T3a-B — internal-FX dispatch tests for OutOfProcessEffectChainHost.
//
// These cases exercise the new `setInternalFxAtSlot(...)` + `prepare(...)`
// message-thread API and the RT-side internal-table-first dispatch branch
// in `pumpSlot`. They deliberately DO NOT spawn any OOP child — every
// case operates on a fresh host where the OOP `instances_` map is empty,
// so any non-false return MUST have come from the internal-FX path.
//
// Tag: `[internal-fx-host]` per the umbrella plan
// (~/.claude/plans/read-continue-and-proceed-structured-acorn.md,
// "Modified" section in the T3a body + "Subagent B" sequence row).

#include "sirius/OutOfProcessEffectChainHost.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

namespace
{
    constexpr int    kSampleRate   = 48000;
    constexpr int    kMaxBlock     = 512;
    constexpr int    kBlockSamples = 64;
    constexpr double kPi           = 3.14159265358979323846;

    // Fills `left`/`right` with a low-amplitude sine. Returns the peak so
    // callers can compare against an output peak (a flat-default EQ should
    // pass the signal through unchanged after the IIR settling tail).
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

    // Calls pumpSlot once with a stereo block. Returns the (bool) result.
    bool pump (sirius::OutOfProcessEffectChainHost& host,
               std::int64_t                         nodeKey,
               std::size_t                          slotIdx,
               const float*                         lin,
               const float*                         rin,
               float*                               lout,
               float*                               rout,
               int                                  numSamples = kBlockSamples)
    {
        const std::array<const float*, 2> inPtrs  { lin, rin };
        const std::array<float*, 2>       outPtrs { lout, rout };
        return host.pumpSlot (nodeKey, slotIdx, inPtrs.data(),
                              const_cast<float* const*> (outPtrs.data()),
                              2, numSamples);
    }
}

TEST_CASE ("pumpSlot on an unbound (nodeKey, slot) returns false — OOP miss path preserved",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;

    // No setInternalFxAtSlot call, no configureBus call — both the
    // internal-FX table and the OOP instances_ table are empty. The new
    // internal-first branch must miss and fall through to the existing
    // OOP path, which also misses (no instance), so pumpSlot returns false
    // without modifying the output buffers. This case confirms the new
    // code did NOT change today's behavior on the miss path.
    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    lin .fill (0.5f);
    rin .fill (-0.5f);
    constexpr float kSentinel = 0.123456f;
    lout.fill (kSentinel);
    rout.fill (kSentinel);

    const bool pumped = pump (host, /* nodeKey */ 42, /* slotIdx */ 0,
                              lin.data(), rin.data(),
                              lout.data(), rout.data());
    CHECK_FALSE (pumped);

    // Outputs left untouched — sentinel still present.
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        CHECK (lout[i] == kSentinel);
        CHECK (rout[i] == kSentinel);
    }
}

TEST_CASE ("setInternalFxAtSlot + prepare + pumpSlot — EQ adapter dispatch hits internal path",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;

    // Prepare first, then bind — the bound adapter must be auto-prepared
    // so its first `process` call returns true (not the unprepared-miss
    // false). This is the "prepare-then-bind" leg of Case 5 in the
    // umbrella plan's Subagent B test matrix.
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);
    host.setInternalFxAtSlot (42, 0, sirius::InternalFxId::kEq);

    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    const float inputPeak = fillSine (lin, rin);
    lout.fill (0.0f);
    rout.fill (0.0f);

    const bool pumped = pump (host, 42, 0,
                              lin.data(), rin.data(),
                              lout.data(), rout.data());
    REQUIRE (pumped);

    // Output is finite, bounded, and (because the EQ's default config is
    // flat) approximately equal to the input. The IIR cascade has a brief
    // settling tail on the first few samples, so we compare the peak
    // across the back half of the block where the filter has settled.
    float outPeak = 0.0f;
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        CHECK (std::isfinite (lout[i]));
        CHECK (std::isfinite (rout[i]));
        outPeak = std::max ({ outPeak, std::abs (lout[i]), std::abs (rout[i]) });
    }
    CHECK (outPeak > 0.5f * inputPeak);
    CHECK (outPeak < 1.5f * inputPeak);
}

TEST_CASE ("setInternalFxAtSlot(nullopt) unbinds — subsequent pumpSlot misses through OOP path",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    // Bind, confirm the bind worked, then unbind.
    host.setInternalFxAtSlot (42, 0, sirius::InternalFxId::kEq);

    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    fillSine (lin, rin);
    {
        lout.fill (0.0f); rout.fill (0.0f);
        const bool boundPump = pump (host, 42, 0,
                                     lin.data(), rin.data(),
                                     lout.data(), rout.data());
        REQUIRE (boundPump); // adapter is present
    }

    // Unbind. The internal table now has no entry at (42, 0); the OOP
    // path also has no instance; pumpSlot must return false and leave
    // outputs untouched.
    host.setInternalFxAtSlot (42, 0, std::nullopt);

    constexpr float kSentinel = -0.987654f;
    lout.fill (kSentinel);
    rout.fill (kSentinel);
    const bool unboundPump = pump (host, 42, 0,
                                   lin.data(), rin.data(),
                                   lout.data(), rout.data());
    CHECK_FALSE (unboundPump);
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        CHECK (lout[i] == kSentinel);
        CHECK (rout[i] == kSentinel);
    }
}

TEST_CASE ("two adapters at distinct (nodeKey, slot) keys do not cross-talk",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    // Bind two independent adapters at different keys. Each owns its own
    // PlayerEQ state — pumping (42, 0) must not touch (99, 3)'s adapter
    // (and vice versa). Easiest verification: drive each with a different
    // input pattern through a flat-default EQ and assert each output
    // matches its own input within IIR settling.
    host.setInternalFxAtSlot (42, 0, sirius::InternalFxId::kEq);
    host.setInternalFxAtSlot (99, 3, sirius::InternalFxId::kEq);

    std::array<float, kBlockSamples> lin1 {}, rin1 {}, lout1 {}, rout1 {};
    std::array<float, kBlockSamples> lin2 {}, rin2 {}, lout2 {}, rout2 {};
    const float peak1 = fillSine (lin1, rin1, /*freqHz*/ 220.0f, /*amp*/ 0.20f);
    const float peak2 = fillSine (lin2, rin2, /*freqHz*/ 880.0f, /*amp*/ 0.10f);
    lout1.fill (0.0f); rout1.fill (0.0f);
    lout2.fill (0.0f); rout2.fill (0.0f);

    REQUIRE (pump (host, 42, 0,
                   lin1.data(), rin1.data(),
                   lout1.data(), rout1.data()));
    REQUIRE (pump (host, 99, 3,
                   lin2.data(), rin2.data(),
                   lout2.data(), rout2.data()));

    // Each output should track its own input's peak across the settled
    // tail. If they were sharing one adapter, the (99, 3) pump would
    // either see (42, 0)'s state or vice versa, and the peaks would not
    // line up with the matching inputs.
    float outPeak1 = 0.0f, outPeak2 = 0.0f;
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        outPeak1 = std::max ({ outPeak1, std::abs (lout1[i]), std::abs (rout1[i]) });
        outPeak2 = std::max ({ outPeak2, std::abs (lout2[i]), std::abs (rout2[i]) });
    }
    CHECK (outPeak1 > 0.5f * peak1);
    CHECK (outPeak1 < 1.5f * peak1);
    CHECK (outPeak2 > 0.5f * peak2);
    CHECK (outPeak2 < 1.5f * peak2);

    // And the adapters' amplitudes are distinguishable (a sanity check
    // that the two outputs aren't both magically holding the same buffer).
    CHECK (std::abs (outPeak1 - outPeak2) > 0.01f);
}

TEST_CASE ("bind-then-prepare also auto-prepares — adapter is ready on first pumpSlot",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;

    // Bind FIRST, then prepare — the prepare(...) sweep must walk the
    // already-bound adapter so its first pumpSlot returns true. This is
    // the "bind-then-prepare" leg of Case 5 in the umbrella plan's
    // Subagent B test matrix.
    host.setInternalFxAtSlot (7, 1, sirius::InternalFxId::kEq);
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    fillSine (lin, rin);
    lout.fill (0.0f);
    rout.fill (0.0f);

    const bool pumped = pump (host, 7, 1,
                              lin.data(), rin.data(),
                              lout.data(), rout.data());
    REQUIRE (pumped);
}

TEST_CASE ("setInternalFxAtSlot with an un-shipped id (kCmp) is a no-op rebind",
           "[internal-fx-host]")
{
    sirius::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);

    // T3a ships only kEq. The factory returns nullptr for kCmp/kRvb/kDly
    // until T3b/c/d land. setInternalFxAtSlot(kCmp) should erase any
    // existing entry without storing a null adapter; subsequent pumpSlot
    // must miss (and leave outputs untouched).
    host.setInternalFxAtSlot (5, 0, sirius::InternalFxId::kEq);
    host.setInternalFxAtSlot (5, 0, sirius::InternalFxId::kCmp);

    std::array<float, kBlockSamples> lin {}, rin {}, lout {}, rout {};
    fillSine (lin, rin);
    constexpr float kSentinel = 0.314159f;
    lout.fill (kSentinel);
    rout.fill (kSentinel);

    const bool pumped = pump (host, 5, 0,
                              lin.data(), rin.data(),
                              lout.data(), rout.data());
    CHECK_FALSE (pumped);
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        CHECK (lout[i] == kSentinel);
        CHECK (rout[i] == kSentinel);
    }
}

TEST_CASE ("in-place aliasing through the host — inChannels == outChannels survives the round trip",
           "[internal-fx-host]")
{
    // The PlayerEQ adapter handles aliasing internally; the host forwards
    // pointers verbatim. This case pins that contract end-to-end: if a
    // future host-side change sneaks in a scratch copy that breaks
    // aliasing, this case fails. Compares the aliased run against a
    // parallel non-aliased run with the same input to confirm the host
    // doesn't perturb the result either way.
    sirius::OutOfProcessEffectChainHost host;
    host.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);
    host.setInternalFxAtSlot (42, 0, sirius::InternalFxId::kEq);

    // Reference (non-aliased) run on a freshly-bound second slot — gives
    // us an apples-to-apples baseline at the same point in adapter state.
    sirius::OutOfProcessEffectChainHost refHost;
    refHost.prepareInternalFx (static_cast<double> (kSampleRate), kMaxBlock);
    refHost.setInternalFxAtSlot (42, 0, sirius::InternalFxId::kEq);

    std::array<float, kBlockSamples> lin {}, rin {};
    const float inputPeak = fillSine (lin, rin);

    // Reference: separate in/out buffers, no aliasing.
    std::array<float, kBlockSamples> refLout {}, refRout {};
    refLout.fill (0.0f);
    refRout.fill (0.0f);
    const bool refPumped = pump (refHost, 42, 0,
                                 lin.data(), rin.data(),
                                 refLout.data(), refRout.data());
    REQUIRE (refPumped);

    // Aliased: in/out point at the SAME buffers. Both rows of the pointer
    // arrays carry identical pointers per channel — true in-place processing.
    std::array<float, kBlockSamples> aliasL {}, aliasR {};
    for (std::size_t i = 0; i < kBlockSamples; ++i)
    {
        aliasL[i] = lin[i];
        aliasR[i] = rin[i];
    }
    const std::array<const float*, 2> inPtrs  { aliasL.data(), aliasR.data() };
    const std::array<float*, 2>       outPtrs { aliasL.data(), aliasR.data() };
    REQUIRE (inPtrs[0] == outPtrs[0]);
    REQUIRE (inPtrs[1] == outPtrs[1]);

    const bool aliasedPumped = host.pumpSlot (
        /* nodeKey */ 42, /* slotIdx */ 0,
        inPtrs.data(),
        const_cast<float* const*> (outPtrs.data()),
        /* numChannels */ 2, /* numSamples */ kBlockSamples);
    REQUIRE (aliasedPumped);

    // The in-place buffers are now the adapter's output. Finite, bounded,
    // and approximately equal to the non-aliased reference output across
    // the settled tail. (The first samples include the IIR settling tail;
    // we compare the back half where the filter has converged.)
    float aliasPeak = 0.0f;
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        CHECK (std::isfinite (aliasL[i]));
        CHECK (std::isfinite (aliasR[i]));
        aliasPeak = std::max ({ aliasPeak, std::abs (aliasL[i]), std::abs (aliasR[i]) });
    }
    // Flat-default EQ ≈ identity ⇒ output peak tracks input peak.
    CHECK (aliasPeak > 0.5f * inputPeak);
    CHECK (aliasPeak < 1.5f * inputPeak);

    // Aliased and non-aliased outputs match sample-for-sample across the
    // settled tail. A host-side scratch-copy regression that breaks aliasing
    // would show up as drift between the two buffers.
    for (std::size_t i = kBlockSamples / 2; i < kBlockSamples; ++i)
    {
        CHECK (std::abs (aliasL[i] - refLout[i]) < 1.0e-5f);
        CHECK (std::abs (aliasR[i] - refRout[i]) < 1.0e-5f);
    }
}
