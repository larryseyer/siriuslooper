// Tests for ida::SharedMemorySpscQueue<T> — the cross-process SPSC
// variant of LockFreeSpscQueue. These exercise correctness in a single
// process (placement-new'd over a heap allocation); the actual shm_open
// round-trip is exercised in SharedMemoryRegionTests and at integration
// time through OutOfProcessPluginInstance (M7 S2c+).

#include "sirius/SharedMemorySpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

using ida::SharedMemorySpscQueue;

namespace
{
    /// Allocates a zero-filled region large enough to back a queue of
    /// `capacity` items, with stable lifetime for the duration of one
    /// test case. heap-backed is fine — the SPSC class itself doesn't
    /// care that the bytes happen to live in this process's heap rather
    /// than in a shm mapping.
    template <typename T>
    std::vector<std::byte> makeRegion (std::size_t capacity)
    {
        return std::vector<std::byte> (SharedMemorySpscQueue<T>::bytesNeeded (capacity));
    }
}

TEST_CASE ("create reports usable capacity and rejects zero", "[shm-spsc]")
{
    auto region = makeRegion<int> (8);
    auto queue  = SharedMemorySpscQueue<int>::create (region.data(), 8);
    CHECK (queue.capacity() == 8);
    CHECK (queue.empty());

    auto small = makeRegion<int> (1);
    CHECK_THROWS_AS (SharedMemorySpscQueue<int>::create (small.data(), 0),
                     std::invalid_argument);
    CHECK_THROWS_AS (SharedMemorySpscQueue<int>::create (nullptr, 8),
                     std::invalid_argument);
}

TEST_CASE ("push and pop preserve FIFO order", "[shm-spsc]")
{
    auto region = makeRegion<int> (4);
    auto queue  = SharedMemorySpscQueue<int>::create (region.data(), 4);

    CHECK (queue.push (10));
    CHECK (queue.push (20));
    CHECK (queue.push (30));

    int out = 0;
    REQUIRE (queue.pop (out)); CHECK (out == 10);
    REQUIRE (queue.pop (out)); CHECK (out == 20);
    REQUIRE (queue.pop (out)); CHECK (out == 30);
    CHECK_FALSE (queue.pop (out)); // empty
}

TEST_CASE ("a full queue refuses further pushes without blocking", "[shm-spsc]")
{
    auto region = makeRegion<int> (3);
    auto queue  = SharedMemorySpscQueue<int>::create (region.data(), 3);

    CHECK (queue.push (1));
    CHECK (queue.push (2));
    CHECK (queue.push (3));
    // The N+1 sentinel slot means the 4th push must fail; matches
    // LockFreeSpscQueue semantics so callers can swap one for the other.
    CHECK_FALSE (queue.push (4));

    int out = 0;
    REQUIRE (queue.pop (out)); CHECK (out == 1);
    CHECK (queue.push (4)); // freed one slot — push now succeeds
}

TEST_CASE ("capacity 1 still round-trips a single item", "[shm-spsc]")
{
    auto region = makeRegion<int> (1);
    auto queue  = SharedMemorySpscQueue<int>::create (region.data(), 1);

    CHECK (queue.push (42));
    CHECK_FALSE (queue.push (43)); // full at 1
    int out = 0;
    REQUIRE (queue.pop (out));
    CHECK (out == 42);
    CHECK_FALSE (queue.pop (out)); // empty again
}

TEST_CASE ("attach sees the same state the producer wrote", "[shm-spsc]")
{
    // Shared bytes; producer creates and pushes, consumer attaches and pops.
    // This is the single-process analogue of the actual cross-process use:
    // the bytes are the same region viewed from two queue handles.
    struct StereoSample
    {
        std::uint32_t frame;
        float l;
        float r;
    };

    auto region = makeRegion<StereoSample> (16);
    auto producer = SharedMemorySpscQueue<StereoSample>::create (region.data(), 16);
    for (std::uint32_t i = 0; i < 5; ++i)
        REQUIRE (producer.push (StereoSample { i, 0.1f * static_cast<float> (i),
                                                  -0.1f * static_cast<float> (i) }));

    auto consumer = SharedMemorySpscQueue<StereoSample>::attach (region.data(), 16);
    StereoSample out {};
    for (std::uint32_t i = 0; i < 5; ++i)
    {
        REQUIRE (consumer.pop (out));
        CHECK (out.frame == i);
        CHECK (out.l == 0.1f * static_cast<float> (i));
        CHECK (out.r == -0.1f * static_cast<float> (i));
    }
    CHECK_FALSE (consumer.pop (out));
}

TEST_CASE ("attach rejects capacity mismatch with producer", "[shm-spsc]")
{
    auto region = makeRegion<int> (8);
    (void) SharedMemorySpscQueue<int>::create (region.data(), 8);
    // Re-attaching with a different capacity is the cross-process equivalent
    // of two sides disagreeing on the segment shape — fail loud rather than
    // silently corrupting indexing.
    CHECK_THROWS_AS (SharedMemorySpscQueue<int>::attach (region.data(), 7),
                     std::invalid_argument);
}
