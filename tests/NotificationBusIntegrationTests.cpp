// Integration tests for the M6 Session 3 NotificationBus → message-thread
// rolling-history → UI surface plumbing. The MainComponent code path is:
// (1) audio (or any) thread posts → NotificationBus per-category SPSC ring,
// (2) the 30Hz message-thread timer drains into a flat vector, (3) the
// vector entries are appended to a std::deque<Notification> rolling history,
// (4) the deque is trimmed to `kNotificationHistorySize = 20` so the front
// (oldest) entries fall off, (5) the deque is rendered into the Preparation
// pane's read-only multi-line TextEditor.
//
// These tests pin down the deque-trim + per-category FIFO behaviour without
// needing JUCE — the trim policy is the only behaviour S3 adds beyond what
// NotificationBusTests.cpp already covers for the bus itself.
#include "sirius/NotificationBus.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

using sirius::Category;
using sirius::Notification;
using sirius::NotificationBus;
using sirius::NotificationLevel;

namespace
{
    /// Mirror of `MainComponent::kNotificationHistorySize`. Duplicated here
    /// because pulling MainComponent.h into the test target would drag JUCE
    /// transitively; the constant is small and the M6 spec freezes it.
    constexpr std::size_t kNotificationHistorySize = 20;

    /// One drain → trim cycle. Matches the body inside
    /// `MainComponent::timerCallback` (M6 S3) line-for-line so the test
    /// exercises the production trim policy, not a paraphrase.
    void drainAndTrim (NotificationBus&            bus,
                       std::vector<Notification>&  drainBuffer,
                       std::deque<Notification>&   history)
    {
        bus.drain (drainBuffer);
        for (const auto& n : drainBuffer)
        {
            history.push_back (n);
            if (history.size() > kNotificationHistorySize)
                history.pop_front();
        }
    }
}

TEST_CASE ("rolling history trims to most-recent kNotificationHistorySize",
           "[notification-bus][integration]")
{
    NotificationBus bus;
    std::vector<Notification> drainBuffer;
    drainBuffer.reserve (sirius::kCategoryCount * NotificationBus::kRingCapacity);
    std::deque<Notification> history;

    // Post 30 distinct notifications on a single category — overflow won't
    // fire (ring holds 256), so all 30 land in the bus. The history must
    // bound at 20, retaining the most-recent 20 (messages "n10".."n29") in
    // posted order. Embed the index in the message body so the assertion
    // can identify which entries were dropped from the front.
    constexpr int totalPosts = 30;
    for (int i = 0; i < totalPosts; ++i)
    {
        const std::string msg = "n" + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Info, Category::TapeRotation, msg.c_str()));
    }

    drainAndTrim (bus, drainBuffer, history);

    REQUIRE (history.size() == kNotificationHistorySize);

    // The first retained entry is "n10" (entries n0..n9 fell off the front);
    // the last is "n29" (most recent at the back).
    CHECK (std::string (history.front().message.data()) == "n10");
    CHECK (std::string (history.back().message.data())  == "n29");

    // Per-entry FIFO across the retained window.
    int expectedIndex = 10;
    for (const auto& n : history)
    {
        const std::string expected = "n" + std::to_string (expectedIndex++);
        CHECK (std::string (n.message.data()) == expected);
    }
}

TEST_CASE ("empty drain leaves the history empty for the empty-state UI",
           "[notification-bus][integration]")
{
    NotificationBus bus;
    std::vector<Notification> drainBuffer;
    drainBuffer.reserve (sirius::kCategoryCount * NotificationBus::kRingCapacity);
    std::deque<Notification> history;

    drainAndTrim (bus, drainBuffer, history);

    CHECK (history.empty());
    CHECK (drainBuffer.empty());
}

TEST_CASE ("interleaved multi-category posts preserve per-category FIFO inside the trimmed window",
           "[notification-bus][integration]")
{
    NotificationBus bus;
    std::vector<Notification> drainBuffer;
    drainBuffer.reserve (sirius::kCategoryCount * NotificationBus::kRingCapacity);
    std::deque<Notification> history;

    // Post 15 entries each across two categories (30 total — same overflow
    // threshold as the first test, but spread across two rings). The
    // NotificationBus drain walks categories in declaration order, so the
    // drained vector order is "all DiskPressure, then all CpuPressure".
    // After trim to 20: 10 DiskPressure entries fall off the front
    // (d0..d9 dropped, d10..d14 retained), then all 15 CpuPressure entries
    // remain. Net: 5 disk + 15 cpu = 20. The bounded-window contract is
    // "newest 20 across the whole drain stream," not "newest 20 per
    // category" — operator gets a global recency view.
    constexpr int perCategory = 15;
    for (int i = 0; i < perCategory; ++i)
    {
        const std::string diskMsg = "d" + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Warning, Category::DiskPressure, diskMsg.c_str()));
    }
    for (int i = 0; i < perCategory; ++i)
    {
        const std::string cpuMsg = "c" + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Warning, Category::CpuPressure, cpuMsg.c_str()));
    }

    drainAndTrim (bus, drainBuffer, history);

    REQUIRE (history.size() == kNotificationHistorySize);

    // Bucket the retained entries per category and verify each category's
    // surviving messages are in posted order (FIFO within the trimmed window).
    std::vector<std::string> diskRetained;
    std::vector<std::string> cpuRetained;
    for (const auto& n : history)
    {
        const std::string body (n.message.data());
        if (n.category == Category::DiskPressure)      diskRetained.push_back (body);
        else if (n.category == Category::CpuPressure)  cpuRetained.push_back (body);
    }

    // 5 disk entries retained (d10..d14), 15 cpu entries retained (c0..c14).
    REQUIRE (diskRetained.size() == 5);
    REQUIRE (cpuRetained.size()  == 15);

    for (int i = 0; i < 5; ++i)
    {
        const std::string expected = "d" + std::to_string (10 + i);
        CHECK (diskRetained[static_cast<std::size_t> (i)] == expected);
    }
    for (int i = 0; i < 15; ++i)
    {
        const std::string expected = "c" + std::to_string (i);
        CHECK (cpuRetained[static_cast<std::size_t> (i)] == expected);
    }
}

TEST_CASE ("multiple drain cycles roll the history as new posts arrive",
           "[notification-bus][integration]")
{
    NotificationBus bus;
    std::vector<Notification> drainBuffer;
    drainBuffer.reserve (sirius::kCategoryCount * NotificationBus::kRingCapacity);
    std::deque<Notification> history;

    // First batch: 5 posts → history holds 5 entries.
    for (int i = 0; i < 5; ++i)
    {
        const std::string msg = "a" + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Info, Category::DeviceEvent, msg.c_str()));
    }
    drainAndTrim (bus, drainBuffer, history);
    REQUIRE (history.size() == 5);

    // Second batch: 20 more posts → total seen = 25, history trims to 20,
    // first 5 entries (a0..a4) fall off the front, the 20 most-recent
    // entries (b0..b19) remain in posted order.
    for (int i = 0; i < 20; ++i)
    {
        const std::string msg = "b" + std::to_string (i);
        REQUIRE (bus.post (NotificationLevel::Info, Category::DeviceEvent, msg.c_str()));
    }
    drainAndTrim (bus, drainBuffer, history);

    REQUIRE (history.size() == kNotificationHistorySize);
    CHECK (std::string (history.front().message.data()) == "b0");
    CHECK (std::string (history.back().message.data())  == "b19");
}
