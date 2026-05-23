#include "ida/WetCaptureWriter.h"

#include "ida/Bus.h"
#include "ida/Channel.h"
#include "ida/EffectChain.h"
#include "ida/IEffectChainHost.h"
#include "ida/NotificationBus.h"
#include "ida/PluginDescriptor.h"
#include "ida/Rational.h"
#include "ida/TapeStore.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace ida;
using namespace std::chrono_literals;

namespace
{
std::filesystem::path makeTempDir (const char* tag)
{
    auto dir = std::filesystem::temp_directory_path()
             / ("ida-wet-" + std::string (tag) + "-"
                + std::to_string (juce::Random::getSystemRandom().nextInt64()));
    std::filesystem::create_directories (dir);
    return dir;
}

std::vector<float> readPartialFloats (const std::filesystem::path& path)
{
    juce::File f (juce::String (path.string()));
    juce::MemoryBlock mb;
    REQUIRE (f.loadFileAsData (mb));
    std::vector<float> out (mb.getSize() / sizeof (float));
    std::memcpy (out.data(), mb.getData(), out.size() * sizeof (float));
    return out;
}
} // namespace

TEST_CASE ("WetCaptureWriter enqueue flushes interleaved partial bytes", "[wet-capture]")
{
    const auto dir = makeTempDir ("enqueue");
    WetCaptureWriter writer (dir, 5ms, 64);

    std::vector<float> left  { 0.10f, 0.20f, 0.30f };
    std::vector<float> right { 0.40f, 0.50f, 0.60f };
    const float* chans[2] { left.data(), right.data() };

    REQUIRE (writer.tryEnqueueWet (ChannelId { 7 }, Rational { 0 }, chans, 2, 3));

    const auto path = writer.flushChannel (ChannelId { 7 });
    REQUIRE (std::filesystem::exists (path));
    REQUIRE (path.filename().string() == "7.wet.tape.partial");

    const auto got = readPartialFloats (path);
    const std::vector<float> expected { 0.10f, 0.40f, 0.20f, 0.50f, 0.30f, 0.60f };
    REQUIRE (got == expected);

    std::filesystem::remove_all (dir);
}

TEST_CASE ("WetCaptureWriter rejects an oversized buffer", "[wet-capture]")
{
    const auto dir = makeTempDir ("oversize");
    WetCaptureWriter writer (dir, 5ms, 64);

    const int tooManySamples = (kMaxTapeWriteMessageBytes / sizeof (float)) / 2 + 1;
    std::vector<float> ch (static_cast<std::size_t> (tooManySamples), 0.0f);
    const float* chans[2] { ch.data(), ch.data() };

    REQUIRE_FALSE (writer.tryEnqueueWet (ChannelId { 1 }, Rational { 0 }, chans, 2, tooManySamples));

    const auto path = writer.flushChannel (ChannelId { 1 });
    REQUIRE_FALSE (std::filesystem::exists (path));

    std::filesystem::remove_all (dir);
}

TEST_CASE ("WetCaptureWriter flushChannel returns a complete ordered partial", "[wet-capture]")
{
    const auto dir = makeTempDir ("ordered");
    WetCaptureWriter writer (dir, 5ms, 64);

    for (int i = 0; i < 4; ++i)
    {
        std::vector<float> mono { static_cast<float> (i) };
        const float* chans[1] { mono.data() };
        REQUIRE (writer.tryEnqueueWet (ChannelId { 3 }, Rational { 0 }, chans, 1, 1));
    }

    const auto path = writer.flushChannel (ChannelId { 3 });
    const auto got = readPartialFloats (path);
    const std::vector<float> expected { 0.0f, 1.0f, 2.0f, 3.0f };
    REQUIRE (got == expected);

    std::filesystem::remove_all (dir);
}

TEST_CASE ("WetCaptureWriter finalizeToStore round-trips through TapeStore", "[wet-capture]")
{
    const auto dir = makeTempDir ("finalize");
    const auto storeDir = dir / "tapes";
    persistence::TapeStore store (juce::File (juce::String (storeDir.string())));

    WetCaptureWriter writer (dir, 5ms, 64);

    std::vector<float> left  { 0.25f, 0.50f };
    std::vector<float> right { 0.75f, 1.00f };
    const float* chans[2] { left.data(), right.data() };
    REQUIRE (writer.tryEnqueueWet (ChannelId { 9 }, Rational { 0 }, chans, 2, 2));

    const juce::String hash = writer.finalizeToStore (ChannelId { 9 }, store);
    REQUIRE (hash.isNotEmpty());
    REQUIRE (store.exists (hash));

    // Partial is gone after finalize.
    REQUIRE_FALSE (std::filesystem::exists (dir / "9.wet.tape.partial"));

    // Stored bytes equal the interleaved payload.
    juce::MemoryBlock back;
    REQUIRE (store.read (hash, back));
    std::vector<float> got (back.getSize() / sizeof (float));
    std::memcpy (got.data(), back.getData(), got.size() * sizeof (float));
    const std::vector<float> expected { 0.25f, 0.75f, 0.50f, 1.00f };
    REQUIRE (got == expected);

    std::filesystem::remove_all (dir);
}

TEST_CASE ("WetCaptureWriter finalizeToStore returns empty when nothing captured", "[wet-capture]")
{
    const auto dir = makeTempDir ("finalize-empty");
    const auto storeDir = dir / "tapes";
    persistence::TapeStore store (juce::File (juce::String (storeDir.string())));
    WetCaptureWriter writer (dir, 5ms, 64);

    REQUIRE (writer.finalizeToStore (ChannelId { 2 }, store).isEmpty());

    std::filesystem::remove_all (dir);
}

TEST_CASE ("WetCaptureWriter posts DiskPressure on open failure", "[wet-capture]")
{
    const auto dir = makeTempDir ("iofail");
    WetCaptureWriter writer (dir, 5ms, 64);

    NotificationBus bus;
    writer.setNotificationBus (&bus);

    // Occupy the partial path with a directory → FileOutputStream open fails.
    std::filesystem::create_directories (dir / "5.wet.tape.partial");

    std::vector<float> mono { 0.5f };
    const float* chans[1] { mono.data() };
    REQUIRE (writer.tryEnqueueWet (ChannelId { 5 }, Rational { 0 }, chans, 1, 1));

    (void) writer.flushChannel (ChannelId { 5 }); // forces a worker drain pass

    CHECK (writer.errorCountForChannel (ChannelId { 5 }) >= 1u);

    std::vector<Notification> drained;
    bus.drain (drained);
    bool sawDiskPressure = false;
    for (const auto& n : drained)
        if (n.level == NotificationLevel::Error && n.category == Category::DiskPressure)
            sawDiskPressure = true;
    CHECK (sawDiskPressure);

    std::filesystem::remove_all (dir);
}

namespace
{
// Doubles every sample in-place; proves the tap is post-effects.
struct DoublingHost : ida::IEffectChainHost
{
    bool pumpSlot (std::int64_t, std::size_t,
                   const float* const* in, float* const* out,
                   int numChannels, int numSamples) noexcept override
    {
        for (int c = 0; c < numChannels; ++c)
            for (int s = 0; s < numSamples; ++s)
                out[c][s] = in[c][s] * 2.0f;
        return true;
    }
};

ida::EffectChain chainWithOneSlot()
{
    ida::EffectChainEntry entry;
    entry.descriptor.name = "doubler";
    return ida::EffectChain{}.withAppended (entry);
}
} // namespace

TEST_CASE ("Bus with no wet sink does not capture", "[wet-capture]")
{
    const auto dir = makeTempDir ("bus-inert");
    WetCaptureWriter writer (dir, 5ms, 64);

    Bus bus (BusId { 1 }, BusConfig { 2, "Reverb" });
    // No setWetCaptureSink call.

    const int n = 4;
    for (int c = 0; c < 2; ++c)
    {
        float* mix = bus.mixBufferChannel (c);
        for (int s = 0; s < n; ++s) mix[s] = 0.1f;
    }
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    float* out[2] { outL.data(), outR.data() };
    bus.process (out, 2, n);

    REQUIRE_FALSE (std::filesystem::exists (writer.flushChannel (ChannelId { 1 })));
    std::filesystem::remove_all (dir);
}

TEST_CASE ("Bus wet sink captures the post-effects signal", "[wet-capture]")
{
    const auto dir = makeTempDir ("bus-seam");
    const auto storeDir = dir / "tapes";
    persistence::TapeStore store (juce::File (juce::String (storeDir.string())));
    WetCaptureWriter writer (dir, 5ms, 64);

    DoublingHost host;
    Bus bus (BusId { 1 }, BusConfig { 2, "Reverb" });
    bus.setEffectChain (chainWithOneSlot());
    bus.setEffectChainHost (&host);
    bus.setWetCaptureSink (&writer, ChannelId { 11 });

    const int n = 3;
    for (int c = 0; c < 2; ++c)
    {
        float* mix = bus.mixBufferChannel (c);
        for (int s = 0; s < n; ++s) mix[s] = 0.25f; // dry input
    }
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    float* out[2] { outL.data(), outR.data() };
    bus.process (out, 2, n);

    const juce::String hash = writer.finalizeToStore (ChannelId { 11 }, store);
    REQUIRE (hash.isNotEmpty());

    juce::MemoryBlock back;
    REQUIRE (store.read (hash, back));
    std::vector<float> got (back.getSize() / sizeof (float));
    std::memcpy (got.data(), back.getData(), got.size() * sizeof (float));

    // Wet = dry * 2 = 0.5 on every interleaved sample (6 = 3 frames × 2 ch).
    REQUIRE (got.size() == static_cast<std::size_t> (n * 2));
    for (float v : got) REQUIRE (v == 0.5f);

    std::filesystem::remove_all (dir);
}
