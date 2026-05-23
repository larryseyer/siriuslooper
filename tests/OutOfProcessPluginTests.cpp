// Tests for ida::OutOfProcessPluginInstance (M7 Session 1).
//
// These exercise the message-thread plumbing — process spawn, byte-stream
// round-trip over stdin/stdout (the S1 transport before shared memory),
// and clean shutdown / no-zombie behaviour. The child binary is the
// `ida_plugin_host` executable built by the host_process target; its
// path is plumbed in via IDA_PLUGIN_HOST_PATH (a CMake-side
// generator-expression define so the tests don't have to guess where the
// build dropped the binary).
//
// Skips cleanly if the binary isn't present — the test executable can in
// principle be invoked before host_process has finished building, and a
// missing binary is not a Sirius-side regression worth a hard failure.

#include "sirius/OutOfProcessPluginInstance.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace
{
    juce::File hostBinaryFile()
    {
       #ifdef IDA_PLUGIN_HOST_PATH
        return juce::File (IDA_PLUGIN_HOST_PATH);
       #else
        return juce::File();
       #endif
    }

    juce::File syntheticClapFile()
    {
       #ifdef IDA_SYNTHETIC_CLAP_PATH
        return juce::File (IDA_SYNTHETIC_CLAP_PATH);
       #else
        return juce::File();
       #endif
    }

    /// Reads exactly `expected` bytes from `instance`, draining as many
    /// reads as needed until either the deadline elapses or EOF is hit.
    /// readBytes() returns after the first non-empty read, so this helper
    /// loops to bridge the gap when the pipe arrives in multiple chunks.
    std::vector<std::byte> readExact (ida::OutOfProcessPluginInstance& instance,
                                      std::size_t expected,
                                      int totalTimeoutMs)
    {
        std::vector<std::byte> out;
        out.reserve (expected);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds (totalTimeoutMs);

        while (out.size() < expected)
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
                                       deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0)
                break;

            std::array<std::byte, 256> chunk {};
            const auto bytes = instance.readBytes (chunk.data(),
                                                   std::min (chunk.size(), expected - out.size()),
                                                   static_cast<int> (remaining));
            if (bytes == 0)
                break;
            out.insert (out.end(), chunk.data(), chunk.data() + bytes);
        }
        return out;
    }
}

TEST_CASE ("OutOfProcessPluginInstance round-trips bytes through the identity host",
           "[out-of-process-plugin]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    ida::OutOfProcessPluginInstance instance (binary, "identity-roundtrip");
    REQUIRE (instance.isRunning());

    constexpr std::size_t kPayloadBytes = 1024;
    std::vector<std::byte> payload (kPayloadBytes);

    std::mt19937 rng (0xC0FFEE);
    std::uniform_int_distribution<int> dist (0, 255);
    for (auto& b : payload)
        b = static_cast<std::byte> (dist (rng));

    REQUIRE (instance.sendBytes (payload.data(), payload.size()));

    const auto echoed = readExact (instance, kPayloadBytes, 2000);
    REQUIRE (echoed.size() == payload.size());
    CHECK (echoed == payload);

    instance.shutdown();
    CHECK_FALSE (instance.isRunning());
}

TEST_CASE ("OutOfProcessPluginInstance shutdown without any I/O exits cleanly",
           "[out-of-process-plugin]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    ida::OutOfProcessPluginInstance instance (binary, "shutdown-no-io");
    REQUIRE (instance.isRunning());

    instance.shutdown();
    CHECK_FALSE (instance.isRunning());
}

TEST_CASE ("OutOfProcessPluginInstance closing stdin lets the host exit on EOF",
           "[out-of-process-plugin]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    ida::OutOfProcessPluginInstance instance (binary, "shutdown-on-eof");
    REQUIRE (instance.isRunning());

    // shutdown() closes our write end of the child's stdin first, which
    // is precisely the EOF signal the host's identity loop drops out on
    // before the grace period elapses.
    instance.shutdown();
    CHECK_FALSE (instance.isRunning());
}

TEST_CASE ("OutOfProcessPluginInstance destructor reaps the child (no zombie)",
           "[out-of-process-plugin]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    long pidWhileAlive = -1;
    {
        ida::OutOfProcessPluginInstance instance (binary, "no-zombie");
        REQUIRE (instance.isRunning());
        pidWhileAlive = instance.childPidForTesting();
        REQUIRE (pidWhileAlive > 0);
    }
    // After the dtor: kill(pid, 0) probes the kernel for the PID's
    // existence without sending a real signal. ESRCH ("no such process")
    // is the proof the child was actually reaped, not just orphaned.
    // A short retry loop covers the rare race where the parent's waitpid
    // returned but the kernel's process-table cleanup is still pending.
    bool reaped = false;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (::kill (static_cast<pid_t> (pidWhileAlive), 0) == -1 && errno == ESRCH)
        {
            reaped = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    CHECK (reaped);
}

TEST_CASE ("OutOfProcessPluginInstance round-trips stereo audio through the synthetic CLAP identity plug-in",
           "[out-of-process-plugin][clap]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto clapBundle = syntheticClapFile();
   #ifdef __APPLE__
    if (! clapBundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");
   #else
    if (! clapBundle.existsAsFile())
        SKIP ("SyntheticTestPlugin .clap shared library not present at IDA_SYNTHETIC_CLAP_PATH");
   #endif

    ida::OutOfProcessPluginInstance instance (binary, "clap-identity", clapBundle);
    REQUIRE (instance.isRunning());

    // Wire format the CLAP-mode pump expects: uint32 frameCount followed
    // by frameCount × 2 × float (interleaved L,R). Pick 256 frames — far
    // below the host's 1024-frame initial capacity, so no re-activation
    // round trip is required.
    constexpr std::uint32_t kFrameCount       = 256;
    constexpr std::size_t   kInterleavedBytes = kFrameCount * 2 * sizeof (float);

    std::vector<float> input (kFrameCount * 2);
    std::mt19937 rng (0xDEADBEEF);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (auto& s : input)
        s = dist (rng);

    REQUIRE (instance.sendBytes (reinterpret_cast<const std::byte*> (&kFrameCount),
                                 sizeof (kFrameCount)));
    REQUIRE (instance.sendBytes (reinterpret_cast<const std::byte*> (input.data()),
                                 kInterleavedBytes));

    const auto echoed = readExact (instance, kInterleavedBytes, 4000);
    REQUIRE (echoed.size() == kInterleavedBytes);

    // Identity plug-in copies input → output byte-for-byte (the float
    // memcpy in SyntheticTestPlugin::pluginProcess is value-preserving
    // regardless of NaN/denormal payload).
    std::vector<float> echoedFloats (kFrameCount * 2);
    std::memcpy (echoedFloats.data(), echoed.data(), kInterleavedBytes);
    CHECK (echoedFloats == input);

    instance.shutdown();
    CHECK_FALSE (instance.isRunning());
}

TEST_CASE ("two OutOfProcessPluginInstances do not cross-talk on stdin/stdout",
           "[out-of-process-plugin]")
{
    const auto binary = hostBinaryFile();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    ida::OutOfProcessPluginInstance a (binary, "concurrent-a");
    ida::OutOfProcessPluginInstance b (binary, "concurrent-b");
    REQUIRE (a.isRunning());
    REQUIRE (b.isRunning());

    constexpr std::size_t kPayloadBytes = 256;
    std::vector<std::byte> payloadA (kPayloadBytes);
    std::vector<std::byte> payloadB (kPayloadBytes);
    for (std::size_t i = 0; i < kPayloadBytes; ++i)
    {
        payloadA[i] = static_cast<std::byte> (i & 0xFF);
        payloadB[i] = static_cast<std::byte> ((0xFF - i) & 0xFF);
    }

    REQUIRE (a.sendBytes (payloadA.data(), payloadA.size()));
    REQUIRE (b.sendBytes (payloadB.data(), payloadB.size()));

    const auto echoedA = readExact (a, kPayloadBytes, 2000);
    const auto echoedB = readExact (b, kPayloadBytes, 2000);

    REQUIRE (echoedA.size() == payloadA.size());
    REQUIRE (echoedB.size() == payloadB.size());
    CHECK (echoedA == payloadA);
    CHECK (echoedB == payloadB);
    CHECK (echoedA != echoedB); // sanity-check the payloads were distinct

    a.shutdown();
    b.shutdown();
    CHECK_FALSE (a.isRunning());
    CHECK_FALSE (b.isRunning());
}
