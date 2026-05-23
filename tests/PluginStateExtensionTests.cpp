// Tests for plug-in state IPC (M8 S2). Round-trip 4 fixed bytes
// through the synthetic CLAP's state extension via the new
// state shm region.
#include "ida/OutOfProcessPluginInstance.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <span>
#include <sys/types.h>
#include <thread>
#include <vector>

#ifndef IDA_HOST_BINARY_PATH
    #error "IDA_HOST_BINARY_PATH required"
#endif
#ifndef IDA_SYNTHETIC_CLAP_PATH
    #error "IDA_SYNTHETIC_CLAP_PATH required"
#endif

using ida::OutOfProcessPluginInstance;

namespace
{
    juce::File hostBinary()      { return juce::File (IDA_HOST_BINARY_PATH); }
    juce::File syntheticBundle() { return juce::File (IDA_SYNTHETIC_CLAP_PATH); }
}

TEST_CASE ("requestStateSave returns the synthetic CLAP's 4-byte payload",
           "[plugin-state-extension]")
{
    OutOfProcessPluginInstance inst (hostBinary(), "state-save", syntheticBundle());
    REQUIRE (inst.isRunning());

    // Give the child a moment to attach the state region and start
    // servicing — same warmup the GUI request tests use.
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    std::vector<std::byte> bytes;
    const bool ok = inst.requestStateSave (bytes,
                                           std::chrono::milliseconds (1000));
    REQUIRE (ok);
    REQUIRE (bytes.size() == 4);
    CHECK (std::to_integer<int> (bytes[0]) == 0xCA);
    CHECK (std::to_integer<int> (bytes[1]) == 0xFE);
    CHECK (std::to_integer<int> (bytes[2]) == 0xBA);
    CHECK (std::to_integer<int> (bytes[3]) == 0xBE);
}

TEST_CASE ("requestStateLoad returns true for the canonical payload",
           "[plugin-state-extension]")
{
    OutOfProcessPluginInstance inst (hostBinary(), "state-load-ok", syntheticBundle());
    REQUIRE (inst.isRunning());
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const std::byte bytes[] = {
        std::byte { 0xCA }, std::byte { 0xFE },
        std::byte { 0xBA }, std::byte { 0xBE }
    };
    const bool ok = inst.requestStateLoad (
        std::span<const std::byte> (bytes, 4),
        std::chrono::milliseconds (1000));
    CHECK (ok);
}

TEST_CASE ("requestStateLoad returns false for wrong bytes",
           "[plugin-state-extension]")
{
    OutOfProcessPluginInstance inst (hostBinary(), "state-load-bad", syntheticBundle());
    REQUIRE (inst.isRunning());
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const std::byte bytes[] = {
        std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }
    };
    const bool ok = inst.requestStateLoad (
        std::span<const std::byte> (bytes, 4),
        std::chrono::milliseconds (1000));
    CHECK_FALSE (ok);
}

TEST_CASE ("requestStateSave times out if child is wedged",
           "[plugin-state-extension]")
{
    OutOfProcessPluginInstance inst (hostBinary(), "state-timeout", syntheticBundle());
    REQUIRE (inst.isRunning());
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // SIGSTOP the child so it stops servicing.
    ::kill (static_cast<pid_t> (inst.childPidForTesting()), SIGSTOP);

    std::vector<std::byte> bytes;
    const bool ok = inst.requestStateSave (bytes,
                                           std::chrono::milliseconds (250));
    CHECK_FALSE (ok);

    // Unfreeze so the destructor can reap cleanly.
    ::kill (static_cast<pid_t> (inst.childPidForTesting()), SIGCONT);
}
