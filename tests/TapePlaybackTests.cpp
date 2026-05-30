#include "ida/TransportPlayhead.h"
#include "ida/ActiveReadsSnapshot.h"
#include "ida/Bus.h"
#include "ida/ChannelStrip.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"
#include "ida/TapePrefetcher.h"
#include "ida/AudioPayloadCodec.h"
#include "ida/TapeRecord.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

using namespace ida;

// ---------------------------------------------------------------------------
// writeRampTape: writes `records` PCM records, each `framesPerRecord` stereo
// frames, directly to disk using PcmAudioCodec + encodeRecord + FileOutputStream.
// Record r, frame f: both channels == static_cast<float>(r*framesPerRecord + f).
// PCM codec used for bit-exact round-trip.
// ---------------------------------------------------------------------------
namespace {

void writeRampTape (const juce::File& file,
                    TapeCodecRegistry& registry,
                    int records,
                    int framesPerRecord)
{
    file.deleteFile();
    juce::FileOutputStream fos (file);
    REQUIRE (fos.openedOk());

    // 12-byte file header
    std::array<std::byte, kTapeFileHeaderBytes> hdrBuf {};
    writeFileHeader (hdrBuf.data());
    fos.write (hdrBuf.data(), hdrBuf.size());

    IPayloadCodec* codec = registry.codecFor (TapeCodecId::AudioPcm);
    REQUIRE (codec != nullptr);

    for (int rec = 0; rec < records; ++rec)
    {
        // Build a ramp: sample absolute index = rec * framesPerRecord + frame
        std::vector<float> left  (static_cast<std::size_t> (framesPerRecord));
        std::vector<float> right (static_cast<std::size_t> (framesPerRecord));
        for (int f = 0; f < framesPerRecord; ++f)
        {
            const float val = static_cast<float> (rec * framesPerRecord + f);
            left [static_cast<std::size_t> (f)] = val;
            right[static_cast<std::size_t> (f)] = val;
        }

        const auto payload = codec->encode (left.data(), right.data(),
                                            framesPerRecord, 48000.0);
        REQUIRE_FALSE (payload.empty());

        TapeRecordHeader recHdr;
        recHdr.seq   = static_cast<std::uint64_t> (rec);
        recHdr.type  = TapeRecordType::Audio;
        recHdr.codec = TapeCodecId::AudioPcm;

        std::vector<std::byte> encoded;
        encodeRecord (recHdr, payload.data(), payload.size(), encoded);
        fos.write (encoded.data(), encoded.size());
    }

    fos.flush();
}

// Build a registry with both real audio codecs (same pattern as TapeRecordStoreTests).
TapeCodecRegistry makePlaybackRegistry()
{
    TapeCodecRegistry reg;
    reg.registerCodec (std::make_shared<PcmAudioCodec>());
    reg.registerCodec (std::make_shared<FlacAudioCodec>());
    return reg;
}

} // namespace

TEST_CASE ("advancePlayedSamples advances only while playing", "[tape-playback][playhead]")
{
    std::int64_t pos = 0;
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/true);
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/false); // stopped: holds
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 256, /*isPlaying=*/true);
    REQUIRE (pos == 768);
}

TEST_CASE ("advancePlayedSamples ignores non-positive blocks", "[tape-playback][playhead]")
{
    REQUIRE (advancePlayedSamples (100, 0,   true) == 100);
    REQUIRE (advancePlayedSamples (100, -8,  true) == 100);
}

TEST_CASE ("playheadSeconds is identity-calibrated", "[tape-playback][playhead]")
{
    REQUIRE (playheadSeconds (48000, 48000.0) == Catch::Approx (1.0));
    REQUIRE (playheadSeconds (0, 48000.0) == Catch::Approx (0.0));
    REQUIRE (playheadSeconds (24000, 0.0) == Catch::Approx (0.0)); // sr==0 guard
}

TEST_CASE ("ActiveReadsPublisher round-trips a snapshot", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;

    ActiveReadsSnapshot in;
    in.add ({ /*slot=*/3, /*tapeSampleStart=*/1000, /*active=*/true });
    in.add ({ /*slot=*/5, /*tapeSampleStart=*/2048, /*active=*/true });
    pub.publish (in);

    ActiveReadsSnapshot out;
    pub.read (out);                       // lock-free consumer read
    REQUIRE (out.count == 2);
    REQUIRE (out.slots[0].slot == 3);
    REQUIRE (out.slots[0].tapeSampleStart == 1000);
    REQUIRE (out.slots[1].slot == 5);
    REQUIRE (out.slots[1].active);
}

TEST_CASE ("ActiveReadsSnapshot clamps to capacity", "[tape-playback][snapshot]")
{
    ActiveReadsSnapshot s;
    for (int i = 0; i < kMaxPhraseSlots + 10; ++i)
        s.add ({ i, /*tapeSampleStart=*/0, /*active=*/true });
    REQUIRE (s.count == kMaxPhraseSlots);     // never overruns the fixed array
}

TEST_CASE ("publisher read before any publish yields empty", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;
    ActiveReadsSnapshot out;
    pub.read (out);
    REQUIRE (out.count == 0);
}

TEST_CASE ("phrase scratch pointer is stable and zero-initialized",
           "[tape-playback][phrase-scratch]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.ensurePhraseScratch (ch);

    const float* l0 = mixer.phraseScratchPointer (ch, 0);
    const float* r0 = mixer.phraseScratchPointer (ch, 1);
    REQUIRE (l0 != nullptr);
    REQUIRE (r0 != nullptr);
    REQUIRE (l0 != r0);
    REQUIRE (l0[0] == 0.0f);

    // Pointer must not move across a second ensure call (stable for lifetime).
    mixer.ensurePhraseScratch (ch);
    REQUIRE (mixer.phraseScratchPointer (ch, 0) == l0);

    // Out-of-range side and unknown channel return nullptr.
    REQUIRE (mixer.phraseScratchPointer (ch, 2) == nullptr);
    REQUIRE (mixer.phraseScratchPointer (OutputChannelId { 9999 }, 0) == nullptr);
}

TEST_CASE ("writing phrase scratch then rendering produces non-silent output",
           "[tape-playback][phrase-scratch]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>> ());
    mixer.ensurePhraseScratch (ch);
    mixer.setChannelAudioSource (ch,
                                 mixer.phraseScratchPointer (ch, 0),
                                 mixer.phraseScratchPointer (ch, 1));

    constexpr int n = 64;
    float* l = mixer.mutablePhraseScratch (ch, 0);
    float* r = mixer.mutablePhraseScratch (ch, 1);
    for (int i = 0; i < n; ++i) { l[i] = 0.5f; r[i] = 0.5f; }

    std::array<float, n> outLeft;  outLeft.fill  (0.0f);
    std::array<float, n> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    // No live input needed — phrase channels read from their audio source.
    mixer.renderBuffer (nullptr, 0, outputs, 2, n);

    REQUIRE (std::abs (outLeft[0]) > 0.0f);
}

// ---------------------------------------------------------------------------
// TapePrefetcher tests (T0b Task 5)
// ---------------------------------------------------------------------------

TEST_CASE ("prefetcher yields recorded samples from a target position",
           "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    auto registry = makePlaybackRegistry();

    constexpr int framesPerRecord = 256;
    constexpr int records = 8;
    writeRampTape (file, registry, records, framesPerRecord);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, framesPerRecord, /*loopLengthSamples=*/0));
    pre.prepare (/*ringFrames=*/4096);

    pre.setTargetSample (0);
    pre.serviceForTest();                 // synchronous decode-into-ring (test hook)

    std::vector<float> l (512, -1.0f), r (512, -1.0f);
    const int got = pre.pull (l.data(), r.data(), 512);
    REQUIRE (got == 512);
    for (int i = 0; i < 512; ++i)
        REQUIRE (l[i] == Catch::Approx (static_cast<float> (i))); // ramp matches
}

TEST_CASE ("prefetcher random-access seek mid-tape", "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    auto registry = makePlaybackRegistry();
    constexpr int fpr = 256, records = 8;
    writeRampTape (file, registry, records, fpr);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, fpr, 0));
    pre.prepare (4096);
    pre.setTargetSample (1000);            // mid-record
    pre.serviceForTest();

    std::vector<float> l (64, -1.0f), r (64, -1.0f);
    REQUIRE (pre.pull (l.data(), r.data(), 64) == 64);
    REQUIRE (l[0] == Catch::Approx (1000.0f));
}

TEST_CASE ("prefetcher underrun zero-fills, never crashes", "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    auto registry = makePlaybackRegistry();
    writeRampTape (file, registry, /*records=*/1, /*fpr=*/64);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, 64, 0));
    pre.prepare (256);
    pre.setTargetSample (0);
    pre.serviceForTest();

    std::vector<float> l (256, 7.0f), r (256, 7.0f);
    const int got = pre.pull (l.data(), r.data(), 256);
    REQUIRE (got == 64);                   // only 64 real frames available
    REQUIRE (l[64] == 0.0f);               // remainder zero-filled
}

TEST_CASE ("prefetcher worker thread fills the ring and stops cleanly",
           "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry = makePlaybackRegistry();
    constexpr int fpr = 256, records = 8;
    writeRampTape (file, registry, records, fpr);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, fpr, 0));
    pre.prepare (4096);
    pre.setTargetSample (0);
    pre.start();

    // Poll briefly for the worker to fill the ring (bounded wait, no flakiness:
    // up to ~500ms, succeeds as soon as frames are available).
    std::vector<float> l (256, -1.0f), r (256, -1.0f);
    int got = 0;
    for (int tries = 0; tries < 50 && got == 0; ++tries)
    {
        got = pre.pull (l.data(), r.data(), 256);
        if (got == 0) std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    pre.stop();   // must join cleanly
    REQUIRE (got > 0);
    REQUIRE (l[0] == Catch::Approx (0.0f));   // first sample of the ramp
}

TEST_CASE ("prefetcher loop mode wraps the decode cursor",
           "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry = makePlaybackRegistry();
    constexpr int fpr = 64, records = 4;          // 256 total frames
    constexpr std::int64_t loopLen = records * fpr; // 256
    writeRampTape (file, registry, records, fpr);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, fpr, loopLen));
    pre.prepare (1024);
    pre.setTargetSample (loopLen - 32);   // start 32 frames before the loop end
    pre.serviceForTest();

    std::vector<float> l (64, -1.0f), r (64, -1.0f);
    REQUIRE (pre.pull (l.data(), r.data(), 64) == 64);
    REQUIRE (l[0]  == Catch::Approx (static_cast<float> (loopLen - 32))); // 224
    REQUIRE (l[31] == Catch::Approx (static_cast<float> (loopLen - 1)));  // 255 (loop end)
    REQUIRE (l[32] == Catch::Approx (0.0f));   // wrapped back to sample 0
}
