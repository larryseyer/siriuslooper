#include "ida/TransportPlayhead.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace ida;

TEST_CASE ("advancePlayedSamples advances only while playing", "[tape-playback][playhead]")
{
    std::int64_t pos = 0;
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/true);
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/false); // stopped: holds
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 256, /*isPlaying=*/true);
    REQUIRE (pos == 768);
}

TEST_CASE ("advancePlayedSamples ignores non-positive blocks", "[tape-playback][playhead]")
{
    REQUIRE (advancePlayedSamples (100, 0,   true) == 100);
    REQUIRE (advancePlayedSamples (100, -8,  true) == 100);
}

TEST_CASE ("playheadSeconds is identity-calibrated", "[tape-playback][playhead]")
{
    REQUIRE (playheadSeconds (48000, 48000.0) == Catch::Approx (1.0));
    REQUIRE (playheadSeconds (0, 48000.0) == Catch::Approx (0.0));
    REQUIRE (playheadSeconds (24000, 0.0) == Catch::Approx (0.0)); // sr==0 guard
}
