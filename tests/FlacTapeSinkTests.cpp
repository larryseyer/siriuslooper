#include "sirius/FlacTapeSink.h"
#include "sirius/TapeId.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
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
    out.left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + out.numSamples);
    if (out.numChannels > 1)
        out.right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + out.numSamples);
    return out;
}
} // namespace

TEST_CASE ("FlacTapeSink writes one growing FLAC per delivered tape", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        sirius::FlacTapeSink sink (dir, 48000.0, 256);
        std::vector<float> l (480), r (480);
        for (int i = 0; i < 480; ++i) { l[i] = static_cast<float> (i) / 480.0f * 0.5f;
                                        r[i] = -l[i]; }
        for (int block = 0; block < 4; ++block)
            sink.deliverTapeBlock (sirius::TapeId { 1 }, l.data(), r.data(), 480);
    }
    const auto f = dir.getChildFile ("tape-1.flac");
    REQUIRE (f.existsAsFile());
    const auto decoded = decodeFlac (f);
    CHECK (decoded.numChannels == 2);
    CHECK (decoded.sampleRate == 48000.0);
    CHECK (decoded.numSamples == 480 * 4);
    CHECK_THAT (decoded.left[10], Catch::Matchers::WithinAbs (10.0f / 480.0f * 0.5f, 1.0e-4f));
    CHECK_THAT (decoded.right[10], Catch::Matchers::WithinAbs (-10.0f / 480.0f * 0.5f, 1.0e-4f));
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink writes distinct tapes to distinct files in parallel", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    {
        sirius::FlacTapeSink sink (dir, 44100.0, 256);
        std::vector<float> a (256, 0.25f), b (256, -0.25f);
        sink.deliverTapeBlock (sirius::TapeId { 1 }, a.data(), a.data(), 256);
        sink.deliverTapeBlock (sirius::TapeId { 7 }, b.data(), b.data(), 256);
    }
    CHECK (dir.getChildFile ("tape-1.flac").existsAsFile());
    CHECK (dir.getChildFile ("tape-7.flac").existsAsFile());
    CHECK (decodeFlac (dir.getChildFile ("tape-7.flac")).numSamples == 256);
    dir.deleteRecursively();
}
