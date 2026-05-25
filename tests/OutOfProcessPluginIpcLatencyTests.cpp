// Round-trip latency smoke for OutOfProcessPluginInstance's shared-memory
// SPSC ring transport (M7 S2c). Hidden by default ([.rt-smoke] convention,
// matching OutputMixer's RT smoke); runs only on explicit Catch2 filter
// or when the operator selects the [plugin-ipc] tag.
//
// The test spawns an identity-mode host, runs warmup + timed round-trips
// over a single 8-byte payload, and asserts the p99 round-trip stays
// inside the observed-on-dev-machine threshold. Observed dev-machine p99
// (Apple Silicon M-series, 2026-05-18) was 134 µs; the 300 µs ceiling
// below gives ~2× headroom against CI scheduler variance while still
// catching a real architectural regression. The V7 plan's <10 µs target
// is aspirational — that level requires the lock-free spin transport the
// S4+ watchdog work introduces; the S2c kRingPollMicroseconds backoff
// dominates the current baseline.

#include "ida/OutOfProcessPluginInstance.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
}

TEST_CASE ("identity-mode round-trip latency stays under the observed dev-machine ceiling",
           "[plugin-ipc][.rt-smoke]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    ida::OutOfProcessPluginInstance instance (binary, "ipc-latency");
    REQUIRE (instance.isRunning());

    // Single 8-byte payload — small enough to fit in one PluginIpcMessage
    // with zero risk of fragmentation, so the timing isolates the ring
    // push/pop + scheduling cost, not the memcpy of a large buffer.
    std::array<std::byte, 8> payload {};
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::byte> (0xA0 + i);

    std::array<std::byte, 8> echoed {};

    constexpr int kWarmupRoundTrips = 100;
    constexpr int kTimedRoundTrips  = 1000;

    // Warm-up: cold-cache, scheduler-warming round-trips that we discard.
    for (int i = 0; i < kWarmupRoundTrips; ++i)
    {
        REQUIRE (instance.sendBytes (payload.data(), payload.size()));
        std::size_t got = 0;
        while (got < payload.size())
        {
            const auto chunk = instance.readBytes (echoed.data() + got,
                                                   payload.size() - got,
                                                   1000);
            REQUIRE (chunk > 0);
            got += chunk;
        }
    }

    // Timed round-trips. Each iteration's start-to-finish duration is the
    // single-payload round-trip latency.
    std::vector<std::int64_t> nanos;
    nanos.reserve (static_cast<std::size_t> (kTimedRoundTrips));

    for (int i = 0; i < kTimedRoundTrips; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE (instance.sendBytes (payload.data(), payload.size()));
        std::size_t got = 0;
        while (got < payload.size())
        {
            const auto chunk = instance.readBytes (echoed.data() + got,
                                                   payload.size() - got,
                                                   1000);
            REQUIRE (chunk > 0);
            got += chunk;
        }
        const auto t1 = std::chrono::steady_clock::now();
        nanos.push_back (
            std::chrono::duration_cast<std::chrono::nanoseconds> (t1 - t0).count());
    }

    std::sort (nanos.begin(), nanos.end());
    const auto medianNs = nanos[nanos.size() / 2];
    const auto p99Ns    = nanos[static_cast<std::size_t> (nanos.size() * 0.99)];
    const auto maxNs    = nanos.back();

    std::printf ("[plugin-ipc.rt-smoke] round-trip latency over %d samples:\n"
                 "  median = %lld ns (%lld µs)\n"
                 "  p99    = %lld ns (%lld µs)\n"
                 "  max    = %lld ns (%lld µs)\n",
                 kTimedRoundTrips,
                 static_cast<long long> (medianNs), static_cast<long long> (medianNs / 1000),
                 static_cast<long long> (p99Ns),    static_cast<long long> (p99Ns / 1000),
                 static_cast<long long> (maxNs),    static_cast<long long> (maxNs / 1000));

    // Per-RT_SAFETY_CONTRACT §6 row for plugin IPC: 300 µs p99 ceiling.
    // Observed dev-machine p99 = 134 µs (Apple Silicon, 2026-05-18); the
    // ~2× headroom prevents CI flake on a busy runner while still catching
    // a real algorithmic regression.
    constexpr std::int64_t kP99CeilingNs = 300'000;
    CHECK (p99Ns < kP99CeilingNs);

    instance.shutdown();
    CHECK_FALSE (instance.isRunning());
}
