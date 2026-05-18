// Skeleton tests for sirius::InputMixer — the M2 Session 2 stub of the V3
// §2.1 input-side mixer. Every public method body in InputMixer.cpp is
// `assert(false && "M3-M5 stub")` per V7 alignment plan M2 Risks note line
// 257: stubs are loud, not silent, so a buggy caller in M1's audio path
// is impossible to miss. That means these tests can only verify what is
// safe to call today — the default ctor and the default dtor. The real
// behavioural tests (register_input + channel-driven tape allocation +
// process_buffer round-trip) land with M3.
//
// The point of this file isn't behavioural coverage — it's a regression
// floor: if a future refactor accidentally makes InputMixer non-default-
// constructible (e.g., adds a required ctor argument before the M3 design
// is settled), this file fails to compile, which is the right time to
// notice.
#include "sirius/InputMixer.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using sirius::InputMixer;

static_assert (std::is_default_constructible_v<InputMixer>,
               "InputMixer must remain default-constructible until M3 designs its config");
static_assert (std::is_destructible_v<InputMixer>);

TEST_CASE ("InputMixer is default-constructible and destructible without crashing",
           "[input-mixer]")
{
    // No member methods are called — every body asserts false. The M3
    // milestone replaces the stubs with real implementations and adds
    // the behavioural test cases that exercise them.
    InputMixer mixer;
    (void) mixer;
}
