// Tests for the latency-timing math (white paper Part 5.5). These pin down the
// two transforms that make latency compensation architectural: a captured
// sample's true capture time, and an output sample's true presentation time —
// both exact, both relative to the audio callback. M2 Session 1 renamed the
// conceptual "membrane" surface to "latency"; assertions are preserved.
#include "ida/LatencyTiming.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::Rational;
using ida::SampleClock;
using ida::latency::inboundCaptureTime;
using ida::latency::outboundPresentTime;

TEST_CASE ("inbound capture time backdates each sample from the callback", "[latency]")
{
    const SampleClock device (Rational (48000));
    const Rational callbackTime (10);          // callback fired at LMC t = 10 s
    const Rational inputLatency (1, 100);      // 10 ms of input latency

    // The newest sample (index bufferSize-1) was captured exactly the input
    // latency before the callback.
    CHECK (inboundCaptureTime (callbackTime, inputLatency, 4, 3, device)
           == callbackTime - inputLatency);

    // Earlier samples were captured proportionally earlier still.
    CHECK (inboundCaptureTime (callbackTime, inputLatency, 4, 2, device)
           == callbackTime - inputLatency - Rational (1, 48000));
    CHECK (inboundCaptureTime (callbackTime, inputLatency, 4, 0, device)
           == callbackTime - inputLatency - Rational (3, 48000));
}

TEST_CASE ("captured samples carry strictly increasing capture times", "[latency][conceptual-time]")
{
    // White paper Part 5.5: every tape entry carries its true conceptual time
    // of capture, not its arrival time. Within one buffer, capture time must
    // rise monotonically from the oldest sample to the newest.
    const SampleClock device (Rational (48000));
    const Rational callbackTime (5);
    const Rational inputLatency (0);

    Rational previous = inboundCaptureTime (callbackTime, inputLatency, 8, 0, device);
    for (std::int64_t i = 1; i < 8; ++i)
    {
        const Rational current = inboundCaptureTime (callbackTime, inputLatency, 8, i, device);
        CHECK (current > previous);
        previous = current;
    }
}

TEST_CASE ("outbound presentation time projects each sample into the future", "[latency]")
{
    const SampleClock device (Rational (48000));
    const Rational callbackTime (10);
    const Rational outputLatency (1, 100);     // 10 ms of output latency

    // Sample 0 is heard exactly the output latency after the callback.
    CHECK (outboundPresentTime (callbackTime, outputLatency, 0, device)
           == callbackTime + outputLatency);

    // Later samples are heard proportionally later.
    CHECK (outboundPresentTime (callbackTime, outputLatency, 48000, device)
           == callbackTime + outputLatency + Rational (1));
}

TEST_CASE ("the latency math rejects out-of-range sample indices", "[latency]")
{
    const SampleClock device (Rational (48000));

    CHECK_THROWS_AS (inboundCaptureTime (Rational (0), Rational (0), 4, 4, device),
                     std::out_of_range);
    CHECK_THROWS_AS (inboundCaptureTime (Rational (0), Rational (0), 4, -1, device),
                     std::out_of_range);
    CHECK_THROWS_AS (outboundPresentTime (Rational (0), Rational (0), -1, device),
                     std::out_of_range);
}
