// Integration tests for the watchdog + supervisor pair in
// `OutOfProcessEffectChainHost` (M7 S4). These cases SPAWN a real
// `ida_plugin_host` child process running the synthetic identity
// CLAP, then exercise three lifecycle shapes:
//
//   1. Healthy host stays healthy — driving repeated pumps does NOT
//      trigger a spurious restart.
//   2. Dead host triggers a restart — externally SIGKILLing the child
//      makes pumpSlot start missing; within the watchdog threshold +
//      poll cadence + grace window the supervisor swaps in a new child
//      and posts an `Info / PluginEvent` to the bound notification sink.
//   3. Permanent bypass — repeatedly killing every replacement child
//      escalates after `kMaxRestartAttempts` and the slot enters the
//      permanently-bypassed state with an `Error / PluginEvent` post.
//
// These tests SKIP cleanly if the host binary or the .clap bundle is
// not built (mirrors the M7 S3 integration test pattern).
//
// Tag: `[plugin-supervisor]` — no `[unit]` modifier because these cases
// spawn real children and sleep through grace windows; they're slower
// than the structural unit suite.

#include "ida/EffectChain.h"
#include "ida/INotificationSink.h"
#include "ida/OutOfProcessEffectChainHost.h"
#include "ida/PluginDescriptor.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    juce::File hostBinary()
    {
       #ifdef IDA_PLUGIN_HOST_PATH
        return juce::File (IDA_PLUGIN_HOST_PATH);
       #else
        return juce::File();
       #endif
    }

    juce::File clapBundle()
    {
       #ifdef IDA_SYNTHETIC_CLAP_PATH
        return juce::File (IDA_SYNTHETIC_CLAP_PATH);
       #else
        return juce::File();
       #endif
    }

    /// Trivial recording sink — captures every post into a vector under a
    /// mutex so the test can assert on level + message contents after the
    /// supervisor cycle completes. Implements `INotificationSink` (the
    /// core port) instead of constructing a real `NotificationBus`,
    /// keeping the test lean and engine-independent.
    class RecordingSink : public ida::INotificationSink
    {
    public:
        struct Entry { ida::NotificationLevel level; std::string message; };

        bool post (ida::NotificationLevel level,
                   ida::Category /*category*/,
                   const char* message) noexcept override
        {
            std::scoped_lock lk (mutex_);
            entries_.push_back ({ level, message != nullptr ? message : std::string {} });
            return true;
        }

        std::vector<Entry> snapshot() const
        {
            std::scoped_lock lk (mutex_);
            return entries_;
        }

        std::size_t countAtLevel (ida::NotificationLevel level) const
        {
            std::scoped_lock lk (mutex_);
            std::size_t n = 0;
            for (const auto& e : entries_) if (e.level == level) ++n;
            return n;
        }

    private:
        mutable std::mutex  mutex_;
        std::vector<Entry>  entries_;
    };

    /// Drives one stereo pump through the host's pumpSlot. The caller
    /// usually doesn't care about the boolean return — the goal is to
    /// advance the watchdog counter, not to validate audio.
    bool pumpOnce (ida::OutOfProcessEffectChainHost& host,
                   std::int64_t busId,
                   std::size_t  slotIdx)
    {
        constexpr int  kBlock = 64;
        std::array<float, kBlock> left  {};
        std::array<float, kBlock> right {};
        std::array<float, kBlock> outL  {};
        std::array<float, kBlock> outR  {};
        left .fill (0.25f);
        right.fill (-0.25f);

        const std::array<const float*, 2> inPtrs  { left.data(),  right.data() };
        const std::array<float*, 2>       outPtrs { outL.data(),  outR.data() };

        return host.pumpSlot (busId, slotIdx, inPtrs.data(),
                              const_cast<float* const*> (outPtrs.data()),
                              2, kBlock);
    }

    /// Configures a single-slot bus + waits a beat for the host to dlopen
    /// + activate the CLAP. Returns the resulting host wrapped in a
    /// unique_ptr so the caller can move it across the SKIP boundary.
    void primeHost (ida::OutOfProcessEffectChainHost& host,
                    std::int64_t busId,
                    const juce::File& binary,
                    const juce::File& bundle)
    {
        ida::PluginDescriptor descriptor;
        descriptor.format   = ida::PluginFormat::Clap;
        descriptor.name     = "SyntheticTestPlugin";
        descriptor.filePath = bundle.getFullPathName().toStdString();

        ida::EffectChainEntry entry;
        entry.descriptor  = descriptor;
        entry.displayName = "Identity";

        ida::EffectChain chain;
        chain = chain.withAppended (entry);

        host.configureBus (busId, chain, binary, bundle);

        // CLAP dlopen + activate latency — same rationale as the M7 S3
        // integration test (it's host wake-up, not engine determinism).
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }
}

TEST_CASE ("healthy host: 50 pumps trigger zero restarts",
           "[plugin-supervisor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
   #ifdef __APPLE__
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");
   #else
    if (! bundle.existsAsFile())
        SKIP ("SyntheticTestPlugin .clap shared library not present at IDA_SYNTHETIC_CLAP_PATH");
   #endif

    RecordingSink sink;
    ida::OutOfProcessEffectChainHost host;
    host.setNotificationSink (&sink);

    primeHost (host, /* busId */ 1, binary, bundle);

    for (int i = 0; i < 50; ++i)
    {
        pumpOnce (host, 1, 0);
        // Brief inter-pump sleep so the host has time to respond and
        // pumpSlot resets the watchdog counter rather than accumulating
        // empty-pop misses under back-pressure.
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    // Give the supervisor a couple of poll cycles to observe the
    // healthy state — if it WERE going to restart, it would have by now.
    std::this_thread::sleep_for (std::chrono::milliseconds (
        ida::OutOfProcessEffectChainHost::kSupervisorPollMs * 3));

    CHECK (host.restartCountForTesting (1, 0) == 0u);
    CHECK_FALSE (host.permanentlyBypassedForTesting (1, 0));
    CHECK (sink.countAtLevel (ida::NotificationLevel::Info)  == 0);
    CHECK (sink.countAtLevel (ida::NotificationLevel::Error) == 0);
}

TEST_CASE ("dead host: SIGKILL the child, supervisor restarts and posts an Info event",
           "[plugin-supervisor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
   #ifdef __APPLE__
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");
   #else
    if (! bundle.existsAsFile())
        SKIP ("SyntheticTestPlugin .clap shared library not present at IDA_SYNTHETIC_CLAP_PATH");
   #endif

    RecordingSink sink;
    ida::OutOfProcessEffectChainHost host;
    host.setNotificationSink (&sink);

    primeHost (host, /* busId */ 2, binary, bundle);

    // Prime: a few good pumps so we know the round-trip is working
    // before we kill the child.
    for (int i = 0; i < 5; ++i)
    {
        pumpOnce (host, 2, 0);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    REQUIRE (host.restartCountForTesting (2, 0) == 0u);

    // Kill the child. The OutOfProcessPluginInstance's `isRunning()`
    // will eventually report false; `tryReadBytes` will return empty
    // forever; the watchdog counter accumulates.
    const long pidBefore = host.childPidForTestingAtSlot (2, 0);
    REQUIRE (pidBefore > 0);
    REQUIRE (::kill (static_cast<pid_t> (pidBefore), SIGKILL) == 0);

    // Drive enough pumps to cross the watchdog threshold. Each pump
    // increments consecutiveMisses by 1 (empty pop after the kill).
    // kConsecutiveMissThreshold = 16, so 20 is comfortably over.
    for (int i = 0; i < 20; ++i)
    {
        pumpOnce (host, 2, 0);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    // Wait for the supervisor to: observe the misses (≤ kSupervisorPollMs),
    // raise the bypass flag, sleep kRestartGraceMs, tear down + respawn.
    // Total worst case ≈ 50 + 100 + ~200ms spawn + dlopen ≈ 500ms.
    // Add slack for CI variance.
    std::this_thread::sleep_for (std::chrono::milliseconds (1500));

    CHECK (host.restartCountForTesting (2, 0) == 1u);
    CHECK_FALSE (host.permanentlyBypassedForTesting (2, 0));

    // The replacement instance has a different PID than the original.
    const long pidAfter = host.childPidForTestingAtSlot (2, 0);
    CHECK (pidAfter > 0);
    CHECK (pidAfter != pidBefore);

    // Exactly one Info post, zero Error posts.
    CHECK (sink.countAtLevel (ida::NotificationLevel::Info)  == 1);
    CHECK (sink.countAtLevel (ida::NotificationLevel::Error) == 0);

    // The Info message mentions "restarted".
    const auto entries = sink.snapshot();
    REQUIRE_FALSE (entries.empty());
    bool foundRestartMsg = false;
    for (const auto& e : entries)
        if (e.level == ida::NotificationLevel::Info
            && e.message.find ("restarted") != std::string::npos)
            foundRestartMsg = true;
    CHECK (foundRestartMsg);
}

TEST_CASE ("permanent bypass: kill every generation, slot bypasses after kMaxRestartAttempts",
           "[plugin-supervisor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
   #ifdef __APPLE__
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");
   #else
    if (! bundle.existsAsFile())
        SKIP ("SyntheticTestPlugin .clap shared library not present at IDA_SYNTHETIC_CLAP_PATH");
   #endif

    RecordingSink sink;
    ida::OutOfProcessEffectChainHost host;
    host.setNotificationSink (&sink);

    primeHost (host, /* busId */ 3, binary, bundle);

    // Drive `kMaxRestartAttempts` failure cycles, then one more cycle to
    // trigger permanent bypass. S1 fix: instead of sleeping a fixed
    // 1500 ms per iteration (which flakes on loaded CI), gate each
    // iteration on `restartCountForTesting` advancing past its prior
    // value — the supervisor is observably making progress or it isn't.
    // The per-iteration 3-second timeout fails LOUD when the supervisor
    // genuinely stalls; it doesn't fail quietly when CI happens to be
    // slow this minute.
    const auto kExpectedAttempts =
        ida::OutOfProcessEffectChainHost::kMaxRestartAttempts;

    for (std::uint32_t attempt = 0; attempt < kExpectedAttempts; ++attempt)
    {
        const auto priorCount = host.restartCountForTesting (3, 0);

        // Kill the current child so this attempt's pumps generate misses.
        const long pid = host.childPidForTestingAtSlot (3, 0);
        if (pid > 0)
            ::kill (static_cast<pid_t> (pid), SIGKILL);

        // Drive pumps + poll for the supervisor to observe + act. Pump
        // a small batch each loop pass so the miss counter keeps growing
        // even if the supervisor poll lands between batches.
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds (3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (int i = 0; i < 4; ++i)
                pumpOnce (host, 3, 0);
            std::this_thread::sleep_for (std::chrono::milliseconds (60));
            if (host.restartCountForTesting (3, 0) > priorCount)
                break;
        }
        REQUIRE (host.restartCountForTesting (3, 0) > priorCount);
    }

    // Final cycle: with `restartCount == kMaxRestartAttempts` the next
    // supervisor entry hits the permanent-bypass gate and posts an Error.
    // Kill the current child to drive that supervisor entry.
    const long lastPid = host.childPidForTestingAtSlot (3, 0);
    if (lastPid > 0)
        ::kill (static_cast<pid_t> (lastPid), SIGKILL);

    const auto bypassDeadline = std::chrono::steady_clock::now()
                                + std::chrono::seconds (3);
    while (std::chrono::steady_clock::now() < bypassDeadline)
    {
        for (int i = 0; i < 4; ++i)
            pumpOnce (host, 3, 0);
        std::this_thread::sleep_for (std::chrono::milliseconds (60));
        if (host.permanentlyBypassedForTesting (3, 0))
            break;
    }

    CHECK (host.permanentlyBypassedForTesting (3, 0));

    // pumpSlot on a permanently-bypassed slot returns false immediately.
    CHECK_FALSE (pumpOnce (host, 3, 0));

    // Exactly one Error post (the permanent-bypass announcement) and
    // kMaxRestartAttempts Info posts (one per successful restart).
    CHECK (sink.countAtLevel (ida::NotificationLevel::Error) == 1);
    CHECK (sink.countAtLevel (ida::NotificationLevel::Info)
           == ida::OutOfProcessEffectChainHost::kMaxRestartAttempts);

    // The Error message mentions "permanently bypassed".
    const auto entries = sink.snapshot();
    bool foundBypassMsg = false;
    for (const auto& e : entries)
        if (e.level == ida::NotificationLevel::Error
            && e.message.find ("permanently bypassed") != std::string::npos)
            foundBypassMsg = true;
    CHECK (foundBypassMsg);
}
