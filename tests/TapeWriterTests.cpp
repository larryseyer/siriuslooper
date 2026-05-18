// Tests for TapeWriter — the audio-thread / writer-thread boundary added
// in M3 Session 2. The audio thread is the sole producer (calls
// tryEnqueue); the worker thread is the sole consumer (drains to per-
// channel <channelId>.tape.partial files at the given flush interval).
//
// Tests cover the boundary in three planes: (1) the SPSC enqueue/dequeue
// round-trip including the queue-full path; (2) the per-channel partial-
// file output (bytes written match bytes enqueued); (3) the dtor lifecycle
// (worker drains pending messages before joining).
//
// Note: TapeWriter takes std::chrono::milliseconds for the flush interval.
// The caller (app layer) converts CapabilityTier → interval; TapeWriter
// is intentionally free of the app-layer CapabilityTier header.
#include "sirius/Channel.h"
#include "sirius/TapeWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

using sirius::ChannelId;
using sirius::kMaxTapeWriteMessageBytes;
using sirius::Rational;
using sirius::TapeWriteMessage;
using sirius::TapeWriter;

namespace
{
    std::filesystem::path freshTempDir (const juce::String& tag)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-tapewriter-" + tag
                                      + "-" + juce::String (juce::Time::getMillisecondCounterHiRes()));
        dir.createDirectory();
        return std::filesystem::path (dir.getFullPathName().toStdString());
    }

    TapeWriteMessage makeMessage (ChannelId id, std::size_t sampleCount, std::byte fill)
    {
        TapeWriteMessage m;
        m.id = id;
        m.lmcTime = Rational (0);
        m.sampleCount = sampleCount;
        for (std::size_t i = 0; i < sampleCount; ++i)
            m.samples[i] = fill;
        return m;
    }
}

TEST_CASE ("TapeWriter writes enqueued bytes to a per-channel partial file",
           "[tape-writer]")
{
    auto tempDir = freshTempDir ("write");

    {
        // 1 ms flush interval (equivalent to Lavish tier)
        TapeWriter writer (tempDir, std::chrono::milliseconds (1), 64);
        const auto msg = makeMessage (ChannelId (7), 128, std::byte { 0xAB });
        REQUIRE (writer.tryEnqueue (msg));

        // 1 ms flush — give the worker generous slack so the assertion isn't
        // timing-sensitive on loaded test runners.
        for (int i = 0; i < 100; ++i)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            const juce::File partial (juce::String ((tempDir / "7.tape.partial").string()));
            if (partial.existsAsFile() && partial.getSize() >= 128)
                break;
        }
    } // dtor joins worker and final-flushes

    const juce::File partial (juce::String ((tempDir / "7.tape.partial").string()));
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 128);

    juce::MemoryBlock bytes;
    REQUIRE (partial.loadFileAsData (bytes));
    for (std::size_t i = 0; i < 128; ++i)
        CHECK (static_cast<unsigned char> (bytes[i]) == 0xAB);

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

TEST_CASE ("tryEnqueue returns false when the queue is full",
           "[tape-writer]")
{
    auto tempDir = freshTempDir ("full");

    // Capacity = 2 so we can pin the queue in the full state from a single thread.
    // 1000 ms flush interval (equivalent to Survival tier) — keeps the worker
    // asleep long enough for all three push attempts to complete before any drain.
    TapeWriter writer (tempDir, std::chrono::milliseconds (1000), 2);
    const auto msg = makeMessage (ChannelId (1), 4, std::byte { 0x01 });

    const bool a = writer.tryEnqueue (msg);
    const bool b = writer.tryEnqueue (msg);
    const bool c = writer.tryEnqueue (msg); // expected: full

    CHECK (a);
    CHECK (b);
    CHECK_FALSE (c);

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

TEST_CASE ("TapeWriter destructor drains pending messages and joins cleanly",
           "[tape-writer][lifecycle]")
{
    auto tempDir = freshTempDir ("drain");

    {
        // 200 ms flush interval (equivalent to Tight tier)
        TapeWriter writer (tempDir, std::chrono::milliseconds (200), 16);
        for (int i = 0; i < 5; ++i)
            REQUIRE (writer.tryEnqueue (makeMessage (ChannelId (3), 8, std::byte { 0x55 })));
        // No sleep — fall straight into dtor. The dtor must drain rather
        // than truncating the in-flight messages.
    }

    const juce::File partial (juce::String ((tempDir / "3.tape.partial").string()));
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 5 * 8);

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

TEST_CASE ("flushChannel finalizes the partial file so subsequent reads see all bytes",
           "[tape-writer][finalize]")
{
    auto tempDir = freshTempDir ("flush");

    // 1 ms flush interval (equivalent to Lavish tier)
    TapeWriter writer (tempDir, std::chrono::milliseconds (1), 64);
    const auto msg = makeMessage (ChannelId (9), 256, std::byte { 0xCD });
    REQUIRE (writer.tryEnqueue (msg));

    const juce::File partial (juce::String (writer.flushChannel (ChannelId (9)).string()));
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 256);
    CHECK (writer.errorCountForChannel (ChannelId (9)) == 0);

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}
