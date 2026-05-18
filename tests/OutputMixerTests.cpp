// Skeleton tests for sirius::OutputMixer — the M2 Session 2 stub of the V3
// §2.2 output-side mixer. Same rationale as InputMixerTests.cpp: every
// public method body in OutputMixer.cpp asserts false per V7 alignment
// plan M2 Risks note line 257, so the only safe surface to exercise today
// is the default ctor / dtor. Real behavioural tests (channel-from-
// Constituent registration, bus sends, render_buffer routing) land with
// M3 once the stubs are replaced.
#include "sirius/OutputMixer.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using sirius::OutputMixer;

static_assert (std::is_default_constructible_v<OutputMixer>,
               "OutputMixer must remain default-constructible until M3 designs its config");
static_assert (std::is_destructible_v<OutputMixer>);

TEST_CASE ("OutputMixer is default-constructible and destructible without crashing",
           "[output-mixer]")
{
    OutputMixer mixer;
    (void) mixer;
}
