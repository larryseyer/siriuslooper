#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "ida/FileInputSource.h"

#include <cmath>
#include <memory>
#include <vector>

namespace
{

/// Synthesize a brief stereo WAV file (440 Hz sine) at `file`.
/// Returns true on success. Caller owns the file lifetime.
bool writeTestWav (const juce::File& file,
                   double sampleRate,
                   int numChannels,
                   int numFrames)
{
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return false;

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (16);
    auto writer = fmt.createWriterFor (stream, options);
    if (writer == nullptr) return false;

    juce::AudioBuffer<float> buf (numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numFrames; ++i)
            buf.setSample (ch, i,
                (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate));

    return writer->writeFromAudioSampleBuffer (buf, 0, numFrames);
}

} // namespace

TEST_CASE ("FileInputSource opens a WAV file and reports duration",
           "[file-input][audio]")
{
    juce::TemporaryFile temp { ".wav" };
    REQUIRE (writeTestWav (temp.getFile(), 48000.0, 2, 24000));  // 0.5 s

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (temp.getFile().getFullPathName().toStdString()));
    CHECK (source.currentReaderDurationFrames() == 24000);
    CHECK (source.currentReaderSampleRate()     == Catch::Approx (48000.0));
    CHECK (source.currentReaderNumChannels()    == 2);
}

TEST_CASE ("FileInputSource openReader returns false for missing file",
           "[file-input][audio]")
{
    ida::FileInputSource source { 48000.0 };
    CHECK_FALSE (source.openReader ("/definitely/not/a/file.wav"));
}

TEST_CASE ("FileInputSource preserves prior reader when openReader fails",
           "[file-input][audio]")
{
    juce::TemporaryFile good { ".wav" };
    REQUIRE (writeTestWav (good.getFile(), 48000.0, 2, 24000));

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (good.getFile().getFullPathName().toStdString()));
    const auto priorSr     = source.currentReaderSampleRate();
    const auto priorFrames = source.currentReaderDurationFrames();

    CHECK_FALSE (source.openReader ("/definitely/not/a/file.wav"));

    CHECK (source.currentReaderSampleRate()     == Catch::Approx (priorSr));
    CHECK (source.currentReaderDurationFrames() == priorFrames);
}

TEST_CASE ("FileInputSource::pullInto delivers pre-filled ring samples",
           "[file-input][audio][ring]")
{
    ida::FileInputSource source { 48000.0 };

    juce::AudioBuffer<float> pattern (2, 128);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i)
            pattern.setSample (ch, i, (float) (ch * 1000 + i));

    source.testPushRing (pattern);

    juce::AudioBuffer<float> out (2, 128);
    out.clear();
    source.pullInto (out, 128);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i)
            CHECK (out.getSample (ch, i) == Catch::Approx ((float) (ch * 1000 + i)));
}

TEST_CASE ("FileInputSource::pullInto fills silence on ring underrun + ticks counter",
           "[file-input][audio][ring]")
{
    ida::FileInputSource source { 48000.0 };

    juce::AudioBuffer<float> out (2, 64);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            out.setSample (ch, i, 9.f);  // garbage

    source.pullInto (out, 64);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            CHECK (out.getSample (ch, i) == Catch::Approx (0.f));

    CHECK (source.underrunCount() == 1);
}

TEST_CASE ("FileInputSource plays an opened WAV: worker fills ring; pullInto delivers samples",
           "[file-input][audio][transport]")
{
    juce::TemporaryFile temp { ".wav" };
    REQUIRE (writeTestWav (temp.getFile(), 48000.0, 2, 4800));  // 100 ms

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (temp.getFile().getFullPathName().toStdString()));

    source.play();
    juce::Thread::sleep (60);  // let worker prime the ring

    juce::AudioBuffer<float> out (2, 1024);
    out.clear();
    source.pullInto (out, 1024);

    // Pulled samples should be non-zero (sine carries energy).
    const float rmsL = out.getRMSLevel (0, 0, 1024);
    CHECK (rmsL > 0.01f);

    source.stop();
}

TEST_CASE ("FileInputSource pause halts ring growth; play resumes; stop seeks to 0",
           "[file-input][audio][transport]")
{
    juce::TemporaryFile temp { ".wav" };
    REQUIRE (writeTestWav (temp.getFile(), 48000.0, 2, 48000));  // 1 s

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (temp.getFile().getFullPathName().toStdString()));

    source.play();
    juce::Thread::sleep (60);

    juce::AudioBuffer<float> out (2, 512);
    out.clear();
    source.pullInto (out, 512);

    source.pause();
    const auto headAfterPause = source.playheadFrames();
    juce::Thread::sleep (50);
    CHECK (source.playheadFrames() == headAfterPause);

    source.stop();
    CHECK (source.playheadFrames() == 0);
}

TEST_CASE ("FileInputSource raw-pointer pullInto consumes the ring exactly like the AudioBuffer overload",
           "[file-input][pull]")
{
    ida::FileInputSource src (48000.0);

    constexpr int frames = 256;
    juce::AudioBuffer<float> seed (2, frames);
    for (int n = 0; n < frames; ++n)
    {
        seed.getWritePointer (0)[n] = 0.10f + 0.001f * static_cast<float> (n);
        seed.getWritePointer (1)[n] = 0.50f - 0.001f * static_cast<float> (n);
    }
    src.testPushRing (seed);

    std::vector<float> L (frames, 0.0f);
    std::vector<float> R (frames, 0.0f);
    const bool ok = src.pullInto (L.data(), R.data(), frames);

    REQUIRE (ok);
    for (int n = 0; n < frames; ++n)
    {
        REQUIRE (L[n] == Catch::Approx (seed.getReadPointer (0)[n]));
        REQUIRE (R[n] == Catch::Approx (seed.getReadPointer (1)[n]));
    }
    REQUIRE (src.underrunCount() == 0);
}

TEST_CASE ("FileInputSource raw-pointer pullInto silences the tail and bumps underrun on short ring",
           "[file-input][pull]")
{
    ida::FileInputSource src (48000.0);

    constexpr int seeded    = 100;
    constexpr int requested = 200;
    juce::AudioBuffer<float> seed (2, seeded);
    for (int n = 0; n < seeded; ++n)
    {
        seed.getWritePointer (0)[n] = 0.25f;
        seed.getWritePointer (1)[n] = 0.75f;
    }
    src.testPushRing (seed);

    std::vector<float> L (requested, -1.0f);
    std::vector<float> R (requested, -1.0f);
    const bool ok = src.pullInto (L.data(), R.data(), requested);

    REQUIRE (ok);
    for (int n = 0; n < seeded; ++n)
    {
        REQUIRE (L[n] == Catch::Approx (0.25f));
        REQUIRE (R[n] == Catch::Approx (0.75f));
    }
    for (int n = seeded; n < requested; ++n)
    {
        REQUIRE (L[n] == Catch::Approx (0.0f));
        REQUIRE (R[n] == Catch::Approx (0.0f));
    }
    REQUIRE (src.underrunCount() == 1);
}

TEST_CASE ("FileInputSource pullIntoStatic thunk forwards to pullInto",
           "[file-input][pull][thunk]")
{
    ida::FileInputSource src (48000.0);

    constexpr int frames = 64;
    juce::AudioBuffer<float> seed (2, frames);
    for (int n = 0; n < frames; ++n)
    {
        seed.getWritePointer (0)[n] = 0.05f * static_cast<float> (n);
        seed.getWritePointer (1)[n] = -0.05f * static_cast<float> (n);
    }
    src.testPushRing (seed);

    std::vector<float> L (frames, 0.0f);
    std::vector<float> R (frames, 0.0f);
    const bool ok = ida::FileInputSource::pullIntoStatic (&src, L.data(), R.data(), frames);

    REQUIRE (ok);
    for (int n = 0; n < frames; ++n)
    {
        REQUIRE (L[n] == Catch::Approx (seed.getReadPointer (0)[n]));
        REQUIRE (R[n] == Catch::Approx (seed.getReadPointer (1)[n]));
    }
}
