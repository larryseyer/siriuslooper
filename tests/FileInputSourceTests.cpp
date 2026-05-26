#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "ida/FileInputSource.h"

#include <cmath>
#include <memory>

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
