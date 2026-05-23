// Tests for ida::LockFreeSpscQueue — the real-time-safe tape-write path.
// The plan calls proving this path the riskiest single piece of M2, so these
// tests cover both single-threaded correctness and a concurrent producer/
// consumer stress run that would expose loss, duplication, or reordering.
#include "ida/LockFreeSpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <thread>
#include <vector>

using ida::LockFreeSpscQueue;

TEST_CASE ("a queue reports its usable capacity and rejects zero", "[lockfree]")
{
    LockFreeSpscQueue<int> queue (8);
    CHECK (queue.capacity() == 8);
    CHECK (queue.empty());
    CHECK_THROWS_AS (LockFreeSpscQueue<int> (0), std::invalid_argument);
}

TEST_CASE ("push and pop preserve FIFO order", "[lockfree]")
{
    LockFreeSpscQueue<int> queue (4);
    CHECK (queue.push (10));
    CHECK (queue.push (20));
    CHECK (queue.push (30));

    int out = 0;
    REQUIRE (queue.pop (out)); CHECK (out == 10);
    REQUIRE (queue.pop (out)); CHECK (out == 20);
    REQUIRE (queue.pop (out)); CHECK (out == 30);
    CHECK_FALSE (queue.pop (out)); // empty
}

TEST_CASE ("a full queue refuses further pushes without blocking", "[lockfree]")
{
    LockFreeSpscQueue<int> queue (3);
    CHECK (queue.push (1));
    CHECK (queue.push (2));
    CHECK (queue.push (3));
    CHECK_FALSE (queue.push (4)); // full — returns false, never blocks

    int out = 0;
    REQUIRE (queue.pop (out)); CHECK (out == 1);
    CHECK (queue.push (4));     // a slot freed up
}

TEST_CASE ("the ring wraps correctly over many cycles", "[lockfree]")
{
    LockFreeSpscQueue<int> queue (2);
    int out = 0;
    // Far more items than the capacity pass through, exercising wrap-around.
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (queue.push (i));
        REQUIRE (queue.pop (out));
        CHECK (out == i);
    }
    CHECK (queue.empty());
}

TEST_CASE ("a concurrent producer and consumer lose nothing", "[lockfree][concurrency]")
{
    // One producer thread, one consumer thread — the queue's guarantee. The
    // consumer must receive exactly 0..N-1, in order, with nothing dropped or
    // duplicated, no matter how the two threads interleave.
    constexpr int count = 200'000;
    LockFreeSpscQueue<int> queue (1024);

    std::thread producer ([&]
    {
        for (int i = 0; i < count; ++i)
            while (! queue.push (i))
                std::this_thread::yield(); // spin until a slot frees
    });

    std::vector<int> received;
    received.reserve (count);
    std::thread consumer ([&]
    {
        int value = 0;
        while (static_cast<int> (received.size()) < count)
        {
            if (queue.pop (value))
                received.push_back (value);
            else
                std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();

    REQUIRE (received.size() == static_cast<std::size_t> (count));
    bool inOrder = true;
    for (int i = 0; i < count; ++i)
        if (received[static_cast<std::size_t> (i)] != i)
            inOrder = false;
    CHECK (inOrder);
}
