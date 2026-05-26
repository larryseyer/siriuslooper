#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "ida/FileInputSource.h"
#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"

#include <memory>

namespace
{

/// Synthesize a small constant-value WAV file at `file`.
/// Constant signal carries energy (so RMS > 0 holds after resample) but
/// has no per-sample variation, keeping the tests deterministic.
bool writeWav (const juce::File& file,
               double sampleRate,
               int numChannels,
               int numFrames,
               float fillSample)
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
            buf.setSample (ch, i, fillSample);

    return writer->writeFromAudioSampleBuffer (buf, 0, numFrames);
}

/// Poll-until-condition with a generous timeout. Returns true if the
/// predicate becomes true within `timeoutMs`, false on timeout. Lets
/// timing-sensitive tests avoid wall-clock dependence.
template <typename Pred>
bool waitUntil (Pred pred, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (pred()) return true;
        juce::Thread::sleep (5);
    }
    return pred();
}

} // namespace

TEST_CASE ("Playlist: LoopScope=Off advances to next entry, stops at end of list",
           "[file-input][playlist]")
{
    juce::TemporaryFile a { ".wav" };
    juce::TemporaryFile b { ".wav" };
    REQUIRE (writeWav (a.getFile(), 48000.0, 2, 4800, 0.5f));   // 100 ms
    REQUIRE (writeWav (b.getFile(), 48000.0, 2, 4800, -0.5f));

    ida::FileInputSource source { 48000.0 };
    source.addEntry (a.getFile().getFullPathName().toStdString());
    source.addEntry (b.getFile().getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Off);
    source.play();

    // Two 100-ms clips need ~200 ms of audio production; the ring stays
    // ahead of a phantom consumer so the worker traverses both then stops.
    // Generous 1500 ms timeout — poll-until-condition avoids wall-clock flake.
    const bool stopped = waitUntil ([&] { return ! source.isPlaying(); }, 1500);
    CHECK (stopped);
}

TEST_CASE ("Playlist: LoopScope=Track rewinds same entry on EOF",
           "[file-input][playlist]")
{
    juce::TemporaryFile loop { ".wav" };
    REQUIRE (writeWav (loop.getFile(), 48000.0, 2, 4800, 0.3f));

    ida::FileInputSource source { 48000.0 };
    auto e = source.addEntry (loop.getFile().getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Track);
    source.play();

    juce::Thread::sleep (250);  // 2.5x the file length
    CHECK (source.isPlaying());            // never stopped
    CHECK (source.currentEntry() == e);    // never advanced
}

TEST_CASE ("Playlist: LoopScope=List wraps last -> first",
           "[file-input][playlist]")
{
    juce::TemporaryFile a { ".wav" };
    juce::TemporaryFile b { ".wav" };
    REQUIRE (writeWav (a.getFile(), 48000.0, 2, 4800, 0.3f));
    REQUIRE (writeWav (b.getFile(), 48000.0, 2, 4800, -0.3f));

    ida::FileInputSource source { 48000.0 };
    auto e1 = source.addEntry (a.getFile().getFullPathName().toStdString());
    auto e2 = source.addEntry (b.getFile().getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::List);
    source.play();

    juce::Thread::sleep (350);  // ~3.5x a single clip -> wrapped at least once
    CHECK (source.isPlaying());
    // currentEntry is either e1 or e2 — both prove the worker is cycling.
    CHECK ((source.currentEntry() == e1 || source.currentEntry() == e2));
}

TEST_CASE ("Playlist: missing entry mid-list is skipped on advance",
           "[file-input][playlist]")
{
    juce::TemporaryFile a { ".wav" };
    juce::TemporaryFile c { ".wav" };
    REQUIRE (writeWav (a.getFile(), 48000.0, 2, 2400, 0.3f));  // 50 ms
    REQUIRE (writeWav (c.getFile(), 48000.0, 2, 2400, -0.3f));

    ida::FileInputSource source { 48000.0 };
    source.addEntry (a.getFile().getFullPathName().toStdString());
    auto eB = source.addEntry ("/nope/missing.wav");            // missing
    source.addEntry (c.getFile().getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Off);
    source.play();

    const bool stopped = waitUntil ([&] { return ! source.isPlaying(); }, 1500);
    CHECK (stopped);
    CHECK (source.entryMissing (eB));  // skipped, marked missing
}

TEST_CASE ("Playlist: reorder of currently-playing entry keeps reader uninterrupted",
           "[file-input][playlist]")
{
    juce::TemporaryFile a { ".wav" };
    juce::TemporaryFile b { ".wav" };
    REQUIRE (writeWav (a.getFile(), 48000.0, 2, 48000, 0.3f));  // 1 s
    REQUIRE (writeWav (b.getFile(), 48000.0, 2, 4800,  -0.3f));

    ida::FileInputSource source { 48000.0 };
    auto eA = source.addEntry (a.getFile().getFullPathName().toStdString());
    source.addEntry (b.getFile().getFullPathName().toStdString());
    source.play();

    juce::Thread::sleep (60);
    const auto headBefore = source.playheadFrames();

    REQUIRE (source.reorderEntry (eA, 1));   // move A to position 1
    juce::Thread::sleep (20);
    const auto headAfter = source.playheadFrames();

    CHECK (source.currentEntry() == eA);     // still on A (lookup is by id)
    CHECK (headAfter >= headBefore);         // reader uninterrupted
}

TEST_CASE ("Playlist: removeEntry refuses the currently-playing entry",
           "[file-input][playlist]")
{
    juce::TemporaryFile a { ".wav" };
    REQUIRE (writeWav (a.getFile(), 48000.0, 2, 48000, 0.3f));

    ida::FileInputSource source { 48000.0 };
    auto eA = source.addEntry (a.getFile().getFullPathName().toStdString());
    source.play();
    // Wait until the worker has opened the reader and adopted eA as the
    // current entry — only then is the "refuse remove of currently-playing"
    // invariant testable. Poll-until-condition (not a wall-clock sleep) so
    // a slow CI host doesn't flake.
    const bool adopted = waitUntil ([&] { return source.currentEntry() == eA; }, 1500);
    REQUIRE (adopted);

    CHECK_FALSE (source.removeEntry (eA));    // refused
    CHECK (source.isPlaying());
}

TEST_CASE ("Playlist: LoopScope=List with a single entry rewinds in place (does not stop)",
           "[file-input][playlist]")
{
    juce::TemporaryFile only { ".wav" };
    REQUIRE (writeWav (only.getFile(), 48000.0, 2, 4800, 0.3f));   // 100 ms

    ida::FileInputSource source { 48000.0 };
    auto e = source.addEntry (only.getFile().getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::List);
    source.play();

    juce::Thread::sleep (250);   // 2.5× the clip — must have wrapped at least once
    CHECK (source.isPlaying());
    CHECK (source.currentEntry() == e);
}

TEST_CASE ("Mono file is dual-mono'd at the reader stage",
           "[file-input][playlist]")
{
    juce::TemporaryFile mono { ".wav" };
    REQUIRE (writeWav (mono.getFile(), 48000.0, 1, 4800, 0.5f));

    ida::FileInputSource source { 48000.0 };
    source.addEntry (mono.getFile().getFullPathName().toStdString());
    source.play();
    juce::Thread::sleep (60);

    juce::AudioBuffer<float> out (2, 1024);
    out.clear();
    source.pullInto (out, 1024);

    for (int i = 0; i < 1024; ++i)
        CHECK (out.getSample (0, i) == Catch::Approx (out.getSample (1, i)));
}

TEST_CASE ("Sample-rate mismatch is transparently resampled",
           "[file-input][playlist]")
{
    juce::TemporaryFile wav44 { ".wav" };
    REQUIRE (writeWav (wav44.getFile(), 44100.0, 2, 4410, 0.4f));  // 100 ms @ 44.1 k

    ida::FileInputSource source { 48000.0 };
    source.addEntry (wav44.getFile().getFullPathName().toStdString());
    source.play();
    juce::Thread::sleep (60);

    juce::AudioBuffer<float> out (2, 1024);
    out.clear();
    source.pullInto (out, 1024);

    CHECK (out.getRMSLevel (0, 0, 1024) > 0.01f);   // resampled signal carries energy
}
