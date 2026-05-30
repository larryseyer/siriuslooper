#include "ida/TransportPlayhead.h"
#include "ida/ActiveReadsSnapshot.h"
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

TEST_CASE ("ActiveReadsPublisher round-trips a snapshot", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;

    ActiveReadsSnapshot in;
    in.add ({ /*slot=*/3, /*tapeSampleStart=*/1000, /*active=*/true });
    in.add ({ /*slot=*/5, /*tapeSampleStart=*/2048, /*active=*/true });
    pub.publish (in);

    ActiveReadsSnapshot out;
    pub.read (out);                       // lock-free consumer read
    REQUIRE (out.count == 2);
    REQUIRE (out.slots[0].slot == 3);
    REQUIRE (out.slots[0].tapeSampleStart == 1000);
    REQUIRE (out.slots[1].slot == 5);
    REQUIRE (out.slots[1].active);
}

TEST_CASE ("ActiveReadsSnapshot clamps to capacity", "[tape-playback][snapshot]")
{
    ActiveReadsSnapshot s;
    for (int i = 0; i < kMaxPhraseSlots + 10; ++i)
        s.add ({ i, /*tapeSampleStart=*/0, /*active=*/true });
    REQUIRE (s.count == kMaxPhraseSlots);     // never overruns the fixed array
}

TEST_CASE ("publisher read before any publish yields empty", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;
    ActiveReadsSnapshot out;
    pub.read (out);
    REQUIRE (out.count == 0);
}
