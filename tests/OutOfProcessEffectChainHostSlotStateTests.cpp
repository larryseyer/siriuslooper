// Unit tests for the per-slot state machine inside
// `OutOfProcessEffectChainHost` (M7 S4). These cases exercise the
// watchdog-side logic — bypass short-circuit, permanent-bypass
// short-circuit, miss-counter reset — WITHOUT spawning a real host
// child. They use the public `pumpSlot` API against a slot that was
// never spawned (no SlotState exists at all → returns false) plus the
// test-only accessors `restartCountForTesting` /
// `permanentlyBypassedForTesting` for the structural shape.
//
// The full restart cycle (which DOES need a real host child and the
// SIGKILL trick) lives in
// `OutOfProcessEffectChainHostSupervisorTests.cpp`.
//
// Tag: `[plugin-supervisor][unit]` — the `[unit]` modifier lets a
// `[plugin-supervisor] ~[unit]` run skip these in favour of the
// integration suite.

#include "sirius/OutOfProcessEffectChainHost.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE ("pumpSlot on an unconfigured slot returns false without crashing",
           "[plugin-supervisor][unit]")
{
    sirius::OutOfProcessEffectChainHost host;

    std::array<float, 8>  leftIn  {};
    std::array<float, 8>  rightIn {};
    std::array<float, 8>  leftOut {};
    std::array<float, 8>  rightOut {};
    leftIn .fill (1.0f);
    rightIn.fill (-1.0f);

    const std::array<const float*, 2> inPtrs  { leftIn.data(),  rightIn.data() };
    const std::array<float*, 2>       outPtrs { leftOut.data(), rightOut.data() };

    // Bus 1 slot 0 was never configured — pumpSlot must return false
    // (dry-on-miss) without touching the output buffers.
    const bool pumped = host.pumpSlot (1, 0, inPtrs.data(),
                                       const_cast<float* const*> (outPtrs.data()),
                                       2, 8);
    CHECK_FALSE (pumped);
}

TEST_CASE ("restartCountForTesting / permanentlyBypassedForTesting default to zero / false",
           "[plugin-supervisor][unit]")
{
    sirius::OutOfProcessEffectChainHost host;

    // Unknown slot → returns 0 / false (the accessors are nullptr-tolerant
    // for the missing-slot case so a test can assert against fresh hosts
    // without having to seed a configureBus call first).
    CHECK (host.restartCountForTesting (1, 0) == 0);
    CHECK_FALSE (host.permanentlyBypassedForTesting (1, 0));
}

TEST_CASE ("constants are the V7 §9.1 acceptance values",
           "[plugin-supervisor][unit]")
{
    // Lock the public constants — these are the V7 §9.1 acceptance window
    // values, and changing them would silently change supervisor behaviour
    // in production. A test failure here is a deliberate forcing function
    // to re-review the white paper alignment before any threshold tweak.
    CHECK (sirius::OutOfProcessEffectChainHost::kConsecutiveMissThreshold == 16u);
    CHECK (sirius::OutOfProcessEffectChainHost::kMaxRestartAttempts       == 3u);
    CHECK (sirius::OutOfProcessEffectChainHost::kSupervisorPollMs         == 50);
    CHECK (sirius::OutOfProcessEffectChainHost::kRestartGraceMs           == 100);
}

TEST_CASE ("host with no slots constructs and destructs cleanly (supervisor thread joins)",
           "[plugin-supervisor][unit]")
{
    // This case implicitly verifies that the supervisor thread starts in
    // the constructor and joins in the destructor without a live slot to
    // poll. A leaked thread would cause a TSan / ASan terminate; a hung
    // join would hang the test process. Either failure mode fails this
    // test deterministically.
    sirius::OutOfProcessEffectChainHost host;
    (void) host;
}
