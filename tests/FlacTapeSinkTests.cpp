#include "ida/FlacTapeSink.h"
#include "ida/TapeId.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

namespace
{
juce::File makeTempTapesDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("sirius-flac-test-" + juce::String (juce::Time::getHighResolutionTicks()));
    dir.createDirectory();
    return dir;
}

struct DecodedFlac { int numChannels = 0; juce::int64 numSamples = 0;
                     std::vector<float> left, right; double sampleRate = 0; };

DecodedFlac decodeFlac (const juce::File& f)
{
    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> reader (
        fmt.createReaderFor (new juce::FileInputStream (f), true));
    REQUIRE (reader != nullptr);
    DecodedFlac out;
    out.numChannels = static_cast<int> (reader->numChannels);
    out.numSamples  = reader->lengthInSamples;
    out.sampleRate  = reader->sampleRate;
    juce::AudioBuffer<float> buf (out.numChannels, static_cast<int> (out.numSamples));
    reader->read (&buf, 0, static_cast<int> (out.numSamples), 0, true, true);
    const auto nSamples = static_cast<std::size_t> (out.numSamples);
    out.left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + nSamples);
    if (out.numChannels > 1)
        out.right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + nSamples);
    return out;
}
} // namespace

TEST_CASE ("FlacTapeSink writes one growing FLAC per delivered tape", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        ida::FlacTapeSink sink (dir, 48000.0, 256);
        std::vector<float> l (480), r (480);
        for (std::size_t i = 0; i < 480; ++i) { l[i] = static_cast<float> (i) / 480.0f * 0.5f;
                                             r[i] = -l[i]; }
        for (int block = 0; block < 4; ++block)
            sink.deliverTapeBlock (ida::TapeId { 1 }, l.data(), r.data(), 480);
    }
    const auto f = dir.getChildFile ("tape-1.flac");
    REQUIRE (f.existsAsFile());
    const auto decoded = decodeFlac (f);
    CHECK (decoded.numChannels == 2);
    CHECK_THAT (decoded.sampleRate, Catch::Matchers::WithinAbs (48000.0, 0.5));
    CHECK (decoded.numSamples == 480 * 4);
    CHECK_THAT (decoded.left[10], Catch::Matchers::WithinAbs (10.0f / 480.0f * 0.5f, 1.0e-4f));
    CHECK_THAT (decoded.right[10], Catch::Matchers::WithinAbs (-10.0f / 480.0f * 0.5f, 1.0e-4f));
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink writes distinct tapes to distinct files in parallel", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        ida::FlacTapeSink sink (dir, 44100.0, 256);
        std::vector<float> a (256, 0.25f), b (256, -0.25f);
        sink.deliverTapeBlock (ida::TapeId { 1 }, a.data(), a.data(), 256);
        sink.deliverTapeBlock (ida::TapeId { 7 }, b.data(), b.data(), 256);
    }
    CHECK (dir.getChildFile ("tape-1.flac").existsAsFile());
    CHECK (dir.getChildFile ("tape-7.flac").existsAsFile());
    CHECK (decodeFlac (dir.getChildFile ("tape-7.flac")).numSamples == 256);
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink closeTape finalizes a complete re-readable file mid-session", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        ida::FlacTapeSink sink (dir, 48000.0, 256);
        std::vector<float> l (480, 0.1f), r (480, -0.1f);
        for (int block = 0; block < 3; ++block)
            sink.deliverTapeBlock (ida::TapeId { 2 }, l.data(), r.data(), 480);
        sink.closeTape (ida::TapeId { 2 });

        // closeTape runs through the worker asynchronously; poll up to ~1s for the
        // file to be finalized. Test-only polling — production code never polls.
        const auto f = dir.getChildFile ("tape-2.flac");
        bool found = false;
        for (int attempt = 0; attempt < 200 && !found; ++attempt)
        {
            juce::Thread::sleep (5);
            found = f.existsAsFile() && f.getSize() > 0;
        }
        REQUIRE (found);
        CHECK (decodeFlac (f).numSamples == 480 * 3);
    }
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink dropped-block accounting increments when queue is full", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        // Capacity 1 forces queue-full on any rapid burst from the producer thread.
        ida::FlacTapeSink sink (dir, 48000.0, 1);
        std::vector<float> l (480, 0.2f), r (480, -0.2f);
        for (int i = 0; i < 5000; ++i)
            sink.deliverTapeBlock (ida::TapeId { 4 }, l.data(), r.data(), 480);
        CHECK (sink.droppedBlockCount() > 0);
    }
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink rate-0 safety net: no file created, blocks safely dropped, no crash", "[flac-tape-sink]")
{
    // Verifies the writerFor() rate-0 safety net: if the sample rate is never set
    // (stays 0.0), writerFor returns nullptr for every dequeued block, no writer
    // is ever created, and the sink destructs cleanly (joins worker, drains queue).
    // In production, setSampleRate MUST be called before capture begins; an unset
    // rate drops all blocks rather than creating a malformed writer.
    auto dir = makeTempTapesDir();
    {
        ida::FlacTapeSink sink (dir, 0.0, 256);
        std::vector<float> l (480, 0.3f), r (480, -0.3f);

        // Deliver 4 blocks — all silently dropped (writerFor returns nullptr, sr <= 0).
        for (int i = 0; i < 4; ++i)
            sink.deliverTapeBlock (ida::TapeId { 3 }, l.data(), r.data(), 480);
    } // RAII: destructor joins the worker, which drains and finds rate still 0.

    // No FLAC file should have been created.
    CHECK (! dir.getChildFile ("tape-3.flac").existsAsFile());
    dir.deleteRecursively();
}
