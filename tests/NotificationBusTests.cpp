// Tests for ida::NotificationBus — the V5 §8.6 engine↔UI truthfulness
// channel. The audio thread posts wait-free into per-category SPSC rings; the
// message thread drains. These tests pin down: trivially-copyable layout,
// empty-drain no-op, single-post round-trip, per-category FIFO at scale,
// multi-category interleave preserving per-ring order, overflow drops + the
// per-category counter, message truncation with forced null termination, and
// a concurrent producer + consumer correctness stress.
#include "sirius/NotificationBus.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using ida::Category;
using ida::Notification;
using ida::NotificationBus;
using ida::NotificationLevel;

// Compile-time guarantee that the POD shape Holds — this is the contract the
// SPSC ring's in-place copy relies on for allocation-free pushes.
static_assert (std::is_trivially_copyable_v<Notification>,
               "Notification must be trivially copyable.");

TEST_CASE ("default ctor and empty drain are no-ops", "[notification-bus]")
{
    NotificationBus bus;
    std::vector<Notification> out;
    bus.drain (out);
    CHECK (out.empty());

    // All overflow counters start at zero across the nine categories.
    for (auto cat : { Category::DiskPressure, Category::CpuPressure, Category::RamPressure,
                      Category::DeviceEvent, Category::PluginEvent, Category::ClockEvent,
                      Category::NetworkEvent, Category::StateRepair, Category::TapeRotation })
    {
        CHECK (bus.overflowCount (cat) == 0);
    }
}

TEST_CASE ("a single post round-trips through drain", "[notification-bus]")
{
    NotificationBus bus;
    REQUIRE (bus.post (NotificationLevel::Warning, Category::CpuPressure,
                       "audio thread missed deadline"));

    std::vector<Notification> out;
    bus.drain (out);
    REQUIRE (out.size() == 1);
    CHECK (out[0].level == NotificationLevel::Warning);
    CHECK (out[0].category == Category::CpuPressure);
    CHECK (out[0].postedTicks > 0);
    CHECK (std::string (out[0].message.data()) == "audio thread missed deadline");
}

TEST_CASE ("post accepts a null message and produces an empty string", "[notification-bus]")
{
    NotificationBus bus;
    REQUIRE (bus.post (NotificationLevel::Info, Category::DeviceEvent, nullptr));

    std::vector<Notification> out;
    bus.drain (out);
    REQUIRE (out.size() == 1);
    CHECK (out[0].message[0] == '\0');
}

TEST_CASE ("per-category FIFO is preserved across 1000 posts", "[notification-bus]")
{
    NotificationBus bus;
    constexpr int total = 1000;

    // Use a category we won't overflow: post within capacity then drain, post
    // more, drain — verifying FIFO across drain boundaries.
    constexpr int batchSize = static_cast<int> (NotificationBus::kRingCapacity); // 256
    int posted = 0;
    std::vector<Notification> out;
    out.reserve (batchSize);

    int received = 0;
    while (posted < total)
    {
        const int toPost = std::min (batchSize, total - posted);
        for (int i = 0; i < toPost; ++i)
        {
            const std::string msg = "msg " + std::to_string (posted + i);
            REQUIRE (bus.post (NotificationLevel::Info, Category::TapeRotation, msg.c_str()));
        }
        bus.drain (out);
        REQUIRE (static_cast<int> (out.size()) == toPost);
        for (int i = 0; i < toPost; ++i)
        {
            const std::string expected = "msg " + std::to_string (received + i);
            CHECK (std::string (out[static_cast<std::size_t> (i)].message.data()) == expected);
        }
        received += toPost;
        posted += toPost;
    }

    CHECK (received == total);
    CHECK (bus.overflowCount (Category::TapeRotation) == 0);
}

TEST_CASE ("multi-category posts preserve per-category order", "[notification-bus]")
{
    NotificationBus bus;
    constexpr int perCategory = 50;

    // Interleave posts across three categories; each category should retain
    // its own posted order on drain. Cross-category order is not asserted.
    for (int i = 0; i < perCategory; ++i)
    {
        const std::string diskMsg = "disk " + std::to_string (i);
        const std::string cpuMsg = "cpu " + std::to_string (i);
        const std::string netMsg = "net " + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Warning, Category::DiskPressure, diskMsg.c_str()));
        REQUIRE (bus.post (NotificationLevel::Warning, Category::CpuPressure, cpuMsg.c_str()));
        REQUIRE (bus.post (NotificationLevel::Warning, Category::NetworkEvent, netMsg.c_str()));
    }

    std::vector<Notification> out;
    bus.drain (out);
    REQUIRE (out.size() == static_cast<std::size_t> (perCategory * 3));

    // Filter per category and assert each one is in posted order.
    std::vector<std::string> diskMsgs, cpuMsgs, netMsgs;
    for (const auto& n : out)
    {
        const std::string body (n.message.data());
        if (n.category == Category::DiskPressure)  diskMsgs.push_back (body);
        else if (n.category == Category::CpuPressure)   cpuMsgs.push_back (body);
        else if (n.category == Category::NetworkEvent)  netMsgs.push_back (body);
    }
    REQUIRE (diskMsgs.size() == static_cast<std::size_t> (perCategory));
    REQUIRE (cpuMsgs.size() == static_cast<std::size_t> (perCategory));
    REQUIRE (netMsgs.size() == static_cast<std::size_t> (perCategory));
    for (int i = 0; i < perCategory; ++i)
    {
        CHECK (diskMsgs[static_cast<std::size_t> (i)] == "disk " + std::to_string (i));
        CHECK (cpuMsgs[static_cast<std::size_t> (i)] == "cpu " + std::to_string (i));
        CHECK (netMsgs[static_cast<std::size_t> (i)] == "net " + std::to_string (i));
    }
}

TEST_CASE ("overflow drops new entries and bumps the per-category counter",
           "[notification-bus]")
{
    NotificationBus bus;
    constexpr int total = 300;
    constexpr int cap = static_cast<int> (NotificationBus::kRingCapacity); // 256

    int accepted = 0;
    int rejected = 0;
    for (int i = 0; i < total; ++i)
    {
        const std::string msg = "n" + std::to_string (i);
        const bool ok = bus.post (NotificationLevel::Error, Category::DiskPressure, msg.c_str());
        if (ok) ++accepted; else ++rejected;
    }

    CHECK (accepted == cap);
    CHECK (rejected == total - cap);
    CHECK (bus.overflowCount (Category::DiskPressure)
           == static_cast<std::uint64_t> (total - cap));

    // The 256 accepted ones are 0..255 in order; entries 256..299 were dropped.
    std::vector<Notification> out;
    bus.drain (out);
    REQUIRE (out.size() == static_cast<std::size_t> (cap));
    for (int i = 0; i < cap; ++i)
    {
        const std::string expected = "n" + std::to_string (i);
        CHECK (std::string (out[static_cast<std::size_t> (i)].message.data()) == expected);
    }

    // Drain clears the ring; subsequent posts should succeed and the overflow
    // counter is sticky (it is a lifetime count, not a current-pressure gauge).
    REQUIRE (bus.post (NotificationLevel::Info, Category::DiskPressure, "after drain"));
    CHECK (bus.overflowCount (Category::DiskPressure)
           == static_cast<std::uint64_t> (total - cap));
}

TEST_CASE ("messages longer than 128 bytes are truncated and null-terminated",
           "[notification-bus]")
{
    NotificationBus bus;
    // 200-char message of repeating 'A' guarantees we exceed the 128-byte buffer.
    const std::string longMessage (200, 'A');
    REQUIRE (bus.post (NotificationLevel::Warning, Category::RamPressure,
                       longMessage.c_str()));

    std::vector<Notification> out;
    bus.drain (out);
    REQUIRE (out.size() == 1);
    const auto& msg = out[0].message;

    // The last byte is forced to '\0' for the truncation case.
    CHECK (msg[127] == '\0');
    // The byte before the terminator is a real 'A' from the source — no
    // zero-padding past the truncation point.
    CHECK (msg[126] == 'A');
    // The first 127 chars are 'A' (the truncated payload).
    for (std::size_t i = 0; i < 127; ++i)
        CHECK (msg[i] == 'A');
}

TEST_CASE ("concurrent producer + consumer loses nothing per category",
           "[notification-bus][concurrency]")
{
    NotificationBus bus;
    constexpr int count = 10000;
    std::atomic<bool> producerDone { false };
    std::atomic<int> received { 0 };

    // Producer thread spams CpuPressure as fast as the ring can drain. Yields
    // on overflow so the consumer can catch up — overflow drops here would
    // break the strict-count assertion below.
    std::thread producer ([&]
    {
        for (int i = 0; i < count; ++i)
        {
            const std::string msg = "p" + std::to_string (i);
            while (! bus.post (NotificationLevel::Info, Category::CpuPressure, msg.c_str()))
                std::this_thread::yield();
        }
        producerDone.store (true, std::memory_order_release);
    });

    // Consumer thread drains repeatedly into the same vector. Per-category
    // order is preserved by SPSC; we collect all in order and verify the
    // sequence 0..count-1 lands intact.
    std::vector<std::string> collected;
    collected.reserve (count);
    std::vector<Notification> batch;
    batch.reserve (NotificationBus::kRingCapacity);

    while (received.load (std::memory_order_acquire) < count)
    {
        bus.drain (batch);
        for (const auto& n : batch)
        {
            if (n.category == Category::CpuPressure)
                collected.emplace_back (n.message.data());
        }
        received.store (static_cast<int> (collected.size()), std::memory_order_release);

        if (collected.size() < static_cast<std::size_t> (count)
            && producerDone.load (std::memory_order_acquire)
            && batch.empty())
        {
            // Producer is done and the last drain was empty — but the count
            // hasn't caught up. Yield once and re-check; the loop terminates
            // on the next iteration if the ring genuinely drained.
            std::this_thread::yield();
        }
    }

    producer.join();

    REQUIRE (collected.size() == static_cast<std::size_t> (count));
    bool inOrder = true;
    for (int i = 0; i < count; ++i)
    {
        const std::string expected = "p" + std::to_string (i);
        if (collected[static_cast<std::size_t> (i)] != expected)
        {
            inOrder = false;
            break;
        }
    }
    CHECK (inOrder);
    // Producer waited on overflow rather than dropping, so the counter stays 0.
    CHECK (bus.overflowCount (Category::CpuPressure) == 0);
}
