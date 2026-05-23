// Tests for ida::RetroactiveRing — the in-memory window of recent tape data
// (white paper Part 6.4). These pin down the behaviour that lets a boundary be
// pulled backward in time: the ring always holds the most recent N events, and
// when it overflows the oldest falls off.
#include "ida/RetroactiveRing.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using ida::RetroactiveRing;

TEST_CASE ("a new ring is empty and rejects zero capacity", "[retroring]")
{
    RetroactiveRing<int> ring (4);
    CHECK (ring.capacity() == 4);
    CHECK (ring.size() == 0);
    CHECK (ring.empty());
    CHECK_FALSE (ring.full());
    CHECK_THROWS_AS (RetroactiveRing<int> (0), std::invalid_argument);
}

TEST_CASE ("under capacity, the ring keeps every event oldest-first", "[retroring]")
{
    RetroactiveRing<int> ring (4);
    ring.push (1);
    ring.push (2);
    ring.push (3);

    CHECK (ring.size() == 3);
    CHECK_FALSE (ring.full());
    CHECK (ring.snapshot() == std::vector<int> { 1, 2, 3 });
}

TEST_CASE ("at capacity the ring reports full", "[retroring]")
{
    RetroactiveRing<int> ring (3);
    ring.push (1);
    ring.push (2);
    ring.push (3);
    CHECK (ring.full());
    CHECK (ring.size() == 3);
}

TEST_CASE ("overflowing the ring drops the oldest event", "[retroring]")
{
    // White paper Part 6.4: ring depth is bounded by the capability tier. When
    // it overflows, the oldest event falls off and the window slides forward.
    RetroactiveRing<int> ring (3);
    for (int i = 1; i <= 5; ++i)
        ring.push (i);

    CHECK (ring.size() == 3);
    CHECK (ring.full());
    // Only the three most recent events survive, still oldest-first.
    CHECK (ring.snapshot() == std::vector<int> { 3, 4, 5 });
}

TEST_CASE ("the window keeps sliding as events keep arriving", "[retroring]")
{
    RetroactiveRing<int> ring (2);
    ring.push (10);
    CHECK (ring.snapshot() == std::vector<int> { 10 });
    ring.push (20);
    CHECK (ring.snapshot() == std::vector<int> { 10, 20 });
    ring.push (30);
    CHECK (ring.snapshot() == std::vector<int> { 20, 30 });
    ring.push (40);
    CHECK (ring.snapshot() == std::vector<int> { 30, 40 });
}
