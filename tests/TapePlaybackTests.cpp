#include "ida/TransportPlayhead.h"
#include "ida/ActiveReadsSnapshot.h"
#include "ida/AudioCallback.h"
#include "ida/EngineConfig.h"
#include "ida/PlaybackResolver.h"
#include "ida/RenderPipeline.h"
#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"
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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

// The operator-new override + alloc counters are defined in
// IdaMasterSpectrumTests.cpp. TapePlaybackTests.cpp reuses them via extern
// (same pattern as TapeRecordStoreTests.cpp).
extern thread_local std::atomic<size_t> g_allocCount;
extern thread_local bool g_counting;

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

    int cumulativeFrames = 0;
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
        recHdr.seq          = static_cast<std::uint64_t> (rec);
        recHdr.type         = TapeRecordType::Audio;
        recHdr.codec        = TapeCodecId::AudioPcm;
        recHdr.lmcTs        = ida::Rational (cumulativeFrames, 48000);
        recHdr.conceptualTs = recHdr.lmcTs;

        std::vector<std::byte> encoded;
        encodeRecord (recHdr, payload.data(), payload.size(), encoded);
        fos.write (encoded.data(), encoded.size());
        cumulativeFrames += framesPerRecord;
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

// ---------------------------------------------------------------------------
// writeRampTapeVariable: writes `count` records with variable frame counts
// (sizes[k] frames each). Each frame's value == its absolute sample index
// (cumulative across records). lmcTs stamped as cumulativeFrames/48000.
// ---------------------------------------------------------------------------
void writeRampTapeVariable (const juce::File& file,
                            TapeCodecRegistry& registry,
                            const int* sizes,
                            int count)
{
    file.deleteFile();
    juce::FileOutputStream fos (file);
    REQUIRE (fos.openedOk());

    std::array<std::byte, kTapeFileHeaderBytes> hdrBuf {};
    writeFileHeader (hdrBuf.data());
    fos.write (hdrBuf.data(), hdrBuf.size());

    IPayloadCodec* codec = registry.codecFor (TapeCodecId::AudioPcm);
    REQUIRE (codec != nullptr);

    int cumulativeFrames = 0;
    for (int rec = 0; rec < count; ++rec)
    {
        const int nFrames = sizes[rec];
        std::vector<float> left  (static_cast<std::size_t> (nFrames));
        std::vector<float> right (static_cast<std::size_t> (nFrames));
        for (int f = 0; f < nFrames; ++f)
        {
            const float val = static_cast<float> (cumulativeFrames + f);
            left [static_cast<std::size_t> (f)] = val;
            right[static_cast<std::size_t> (f)] = val;
        }

        const auto payload = codec->encode (left.data(), right.data(), nFrames, 48000.0);
        REQUIRE_FALSE (payload.empty());

        TapeRecordHeader recHdr;
        recHdr.seq          = static_cast<std::uint64_t> (rec);
        recHdr.type         = TapeRecordType::Audio;
        recHdr.codec        = TapeCodecId::AudioPcm;
        recHdr.lmcTs        = ida::Rational (cumulativeFrames, 48000);
        recHdr.conceptualTs = recHdr.lmcTs;

        std::vector<std::byte> encoded;
        encodeRecord (recHdr, payload.data(), payload.size(), encoded);
        fos.write (encoded.data(), encoded.size());
        cumulativeFrames += nFrames;
    }

    fos.flush();
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
    REQUIRE (pre.open (file, registry, 48000.0, /*loopLengthSamples=*/0));
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
    REQUIRE (pre.open (file, registry, 48000.0, 0));
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
    REQUIRE (pre.open (file, registry, 48000.0, 0));
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
    REQUIRE (pre.open (file, registry, 48000.0, 0));
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
    REQUIRE (pre.open (file, registry, 48000.0, loopLen));
    pre.prepare (1024);
    pre.setTargetSample (loopLen - 32);   // start 32 frames before the loop end
    pre.serviceForTest();

    std::vector<float> l (64, -1.0f), r (64, -1.0f);
    REQUIRE (pre.pull (l.data(), r.data(), 64) == 64);
    REQUIRE (l[0]  == Catch::Approx (static_cast<float> (loopLen - 32))); // 224
    REQUIRE (l[31] == Catch::Approx (static_cast<float> (loopLen - 1)));  // 255 (loop end)
    REQUIRE (l[32] == Catch::Approx (0.0f));   // wrapped back to sample 0
}

// ---------------------------------------------------------------------------
// PlaybackResolver tests (T0b Task 6)
// Fixture helpers renamed to avoid ODR clash with RenderPipelineTests.cpp.
// ---------------------------------------------------------------------------
namespace {

inline std::shared_ptr<const ida::Constituent> makePlaybackLoop (
    std::int64_t id, ida::Rational placeIn, ida::Rational placeOut,
    std::int64_t tape, ida::Rational sliceIn, ida::Rational sliceOut)
{
    const ida::Constituent loop { ida::ConstituentId (id),
                                  ida::Position (placeIn), ida::Position (placeOut) };
    return std::make_shared<const ida::Constituent> (
        loop.withTapeReference (ida::TapeReference (ida::TapeId (tape), sliceIn, sliceOut)));
}

inline std::shared_ptr<const ida::Constituent> makePlaybackSession (
    ida::Rational lengthWholeNotes, std::shared_ptr<const ida::Constituent> child)
{
    const ida::Constituent session (ida::ConstituentId (1), ida::Position(),
                                    ida::Position (lengthWholeNotes));
    return std::make_shared<const ida::Constituent> (session.withChildAdded (std::move (child)));
}

inline ida::RenderPipeline makeSingleLoopPipeline()
{
    auto session = makePlaybackSession (ida::Rational (8),
        makePlaybackLoop (10, ida::Rational (0), ida::Rational (8),
                          100, ida::Rational (2), ida::Rational (4)));
    return ida::RenderPipeline (session, ida::TempoMap::fromBpm (ida::Rational (120)));
}

struct BoundLoopPipeline
{
    std::shared_ptr<const ida::Constituent> root;
    ida::ConstituentId loop;
    ida::TapeId        tape;
};

// A single leaf loop filling [0, 8) whole notes, reading tape slice [0, 8).
// At playhead t=0 its tapePosition is Rational(0), so the ramp read starts at
// absolute sample 0 (keeps the test tape small: 8 records * 256 frames = 2048 frames).
inline BoundLoopPipeline makeBoundLoop()
{
    const ida::ConstituentId loopId (10);
    const ida::TapeId        tapeId (100);
    auto child = makePlaybackLoop (loopId.value(), ida::Rational (0), ida::Rational (8),
                                   tapeId.value(), ida::Rational (0), ida::Rational (8));
    auto root  = makePlaybackSession (ida::Rational (8), child);
    return { root, loopId, tapeId };
}

} // namespace

TEST_CASE ("resolver publishes a slot per active read", "[tape-playback][resolver]")
{
    ida::RenderPipeline pipeline = makeSingleLoopPipeline();
    ida::ActiveReadsPublisher publisher;

    ida::PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setPlayheadProvider ([] { return ida::TransportPlayhead { /*positionInSeconds=*/0.0, /*isPlaying=*/true }; });
    resolver.setSlotForConstituent ([] (ida::ConstituentId c) { return static_cast<int> (c.value()); });
    int steered = -1; std::int64_t steeredTo = -1;
    resolver.setSteerPrefetcher ([&] (int slot, std::int64_t s) { steered = slot; steeredTo = s; });

    resolver.resolveOnceForTest();

    ida::ActiveReadsSnapshot snap;
    publisher.read (snap);
    REQUIRE (snap.count >= 1);
    REQUIRE (snap.slots[0].active);
    REQUIRE (steered == snap.slots[0].slot);
    REQUIRE (steeredTo == snap.slots[0].tapeSampleStart);
    REQUIRE (snap.slots[0].tapeSampleStart == 96000); // tapePosition 2s * 48000
}

TEST_CASE ("resolver publishes empty snapshot when stopped", "[tape-playback][resolver]")
{
    ida::RenderPipeline pipeline = makeSingleLoopPipeline();
    ida::ActiveReadsPublisher publisher;
    ida::PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setPlayheadProvider ([] { return ida::TransportPlayhead { 0.0, /*isPlaying=*/false }; });
    resolver.setSlotForConstituent ([] (ida::ConstituentId c) { return static_cast<int> (c.value()); });
    resolver.setSteerPrefetcher ([] (int, std::int64_t) {});

    resolver.resolveOnceForTest();
    ida::ActiveReadsSnapshot snap; publisher.read (snap);
    REQUIRE (snap.count == 0);
}

TEST_CASE ("resolver skips reads whose constituent has no slot", "[tape-playback][resolver]")
{
    ida::RenderPipeline pipeline = makeSingleLoopPipeline();
    ida::ActiveReadsPublisher publisher;
    ida::PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setPlayheadProvider ([] { return ida::TransportPlayhead { 0.0, true }; });
    resolver.setSlotForConstituent ([] (ida::ConstituentId) { return -1; }); // no slot
    resolver.setSteerPrefetcher ([] (int, std::int64_t) {});

    resolver.resolveOnceForTest();
    ida::ActiveReadsSnapshot snap; publisher.read (snap);
    REQUIRE (snap.count == 0); // unmapped reads are dropped
}

// ---------------------------------------------------------------------------
// AudioCallback playback step (T0b Task 7)
// ---------------------------------------------------------------------------

TEST_CASE ("playback step pulls active slot into its scratch; inactive stays zero",
           "[tape-playback][callback]")
{
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    writeRampTape (tmp.getFile(), registry, /*records=*/4, /*fpr=*/256);
    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, 48000.0, 0));
    pre.prepare (4096);
    pre.setTargetSample (0);
    pre.serviceForTest();

    std::vector<float> scratchL (256, -1.0f), scratchR (256, -1.0f);

    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (/*slot=*/0, &pre, scratchL.data(), scratchR.data());

    ActiveReadsSnapshot snap;
    snap.add ({ /*slot=*/0, /*tapeSampleStart=*/0, /*active=*/true });
    publisher.publish (snap);

    cb.runPlaybackStepForTest (/*numSamples=*/128);
    REQUIRE (scratchL[0] == Catch::Approx (0.0f));
    REQUIRE (scratchL[1] == Catch::Approx (1.0f));   // ramp landed in scratch
    REQUIRE (scratchR[1] == Catch::Approx (1.0f));   // R must match L (ramp writes both)

    // Now publish an empty snapshot: the step zeroes the previously-active slot.
    ActiveReadsSnapshot empty;
    publisher.publish (empty);
    cb.runPlaybackStepForTest (128);
    REQUIRE (scratchL[1] == 0.0f);
    REQUIRE (scratchR[1] == 0.0f);   // R must also be zeroed on deactivation
}

TEST_CASE ("playback step fills multiple active slots independently",
           "[tape-playback][callback]")
{
    juce::TemporaryFile tmpA (".idatape"), tmpB (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    writeRampTape (tmpA.getFile(), registry, 4, 256);
    writeRampTape (tmpB.getFile(), registry, 4, 256);

    TapePrefetcher preA, preB;
    REQUIRE (preA.open (tmpA.getFile(), registry, 48000.0, 0));
    REQUIRE (preB.open (tmpB.getFile(), registry, 48000.0, 0));
    preA.prepare (4096); preA.setTargetSample (0);   preA.serviceForTest();
    preB.prepare (4096); preB.setTargetSample (512); preB.serviceForTest(); // mid-tape

    std::vector<float> aL (256, -1.0f), aR (256, -1.0f);
    std::vector<float> bL (256, -1.0f), bR (256, -1.0f);

    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &preA, aL.data(), aR.data());
    cb.bindPlaybackSlotForTest (3, &preB, bL.data(), bR.data());   // non-adjacent slot

    ActiveReadsSnapshot snap;
    snap.add ({ 0, 0,   true });
    snap.add ({ 3, 512, true });
    publisher.publish (snap);

    cb.runPlaybackStepForTest (64);
    REQUIRE (aL[1] == Catch::Approx (1.0f));     // slot 0 ramp from sample 0
    REQUIRE (bL[0] == Catch::Approx (512.0f));   // slot 3 ramp from sample 512
    REQUIRE (bL[1] == Catch::Approx (513.0f));
}

TEST_CASE ("playback step performs zero allocations", "[tape-playback][callback][rt-safety]")
{
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    writeRampTape (tmp.getFile(), registry, 4, 256);
    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, 48000.0, 0));
    pre.prepare (4096); pre.setTargetSample (0); pre.serviceForTest();

    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &pre, l.data(), r.data());
    ActiveReadsSnapshot snap; snap.add ({ 0, 0, true }); publisher.publish (snap);

    cb.runPlaybackStepForTest (128);          // warm up (any lazy init outside measure)

    g_allocCount.store (0, std::memory_order_relaxed);
    g_counting = true;
    for (int i = 0; i < 1000; ++i) cb.runPlaybackStepForTest (128);
    g_counting = false;
    REQUIRE (g_allocCount.load (std::memory_order_relaxed) == 0u);
}

// ---------------------------------------------------------------------------
// End-to-end playback resolution (T0b Task 8a)
// Wires resolver -> prefetcher -> playback step -> output mixer scratch by hand.
// ---------------------------------------------------------------------------

TEST_CASE ("end-to-end: playing the playhead sounds the phrase; stopped is silent",
           "[tape-playback][e2e]")
{
    // 1. A known ramp tape (PCM, bit-exact): 8 records * 256 frames = 2048 frames.
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    constexpr int fpr = 256, records = 8;          // 2048 frames total
    writeRampTape (tmp.getFile(), registry, records, fpr);

    // 2. Pipeline with one FreeRunning leaf loop reading that tape from slice 0.
    //    sliceIn=0 -> tapePosition==Rational(0) at playhead t=0 -> tapeSampleStart==0.
    BoundLoopPipeline fixture = makeBoundLoop();
    ida::RenderPipeline pipeline (fixture.root, ida::TempoMap::fromBpm (ida::Rational (120)));

    // Sanity: the loop sounds at t=0 with tapePosition 0.
    {
        const auto reads = pipeline.activeReadsAt (ida::Rational (0));
        REQUIRE (reads.size() == 1);
        REQUIRE (reads[0].loop == fixture.loop);
        REQUIRE (reads[0].tapePosition == ida::Rational (0));
    }

    // 3. OutputMixer phrase channel with stable scratch; audio source points at it.
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>> ());
    mixer.ensurePhraseScratch (ch);
    mixer.setChannelAudioSource (ch,
                                 mixer.phraseScratchPointer (ch, 0),
                                 mixer.phraseScratchPointer (ch, 1));

    // 4. Prefetcher for the loop's tape (loopLength = 2048 samples matches the ramp tape).
    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, 48000.0, /*loopLengthSamples=*/records * fpr));
    pre.prepare (8192);

    // 5. AudioCallback playback step bound: slot 0 -> prefetcher -> phrase scratch.
    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &pre,
                                mixer.mutablePhraseScratch (ch, 0),
                                mixer.mutablePhraseScratch (ch, 1));

    // 6. Resolver: loop constituent -> slot 0; steer drives the prefetcher target.
    ida::PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setSlotForConstituent ([&] (ida::ConstituentId c)
        { return c == fixture.loop ? 0 : -1; });
    resolver.setSteerPrefetcher ([&] (int /*slot*/, std::int64_t s)
        { pre.setTargetSample (s); });

    // 7. PLAYING at t=0: resolve -> steer prefetcher -> prefetch -> step -> scratch non-silent.
    resolver.setPlayheadProvider ([] { return ida::TransportPlayhead { 0.0, /*isPlaying=*/true }; });
    resolver.resolveOnceForTest();

    ActiveReadsSnapshot snap;
    publisher.read (snap);
    REQUIRE (snap.count == 1);
    const std::int64_t tapeSampleStart = snap.slots[0].tapeSampleStart;  // == 0 here

    pre.serviceForTest();                  // synchronous decode-into-ring
    cb.runPlaybackStepForTest (128);

    const float* l = mixer.phraseScratchPointer (ch, 0);
    const float* r = mixer.phraseScratchPointer (ch, 1);
    bool nonSilent = false;
    for (int i = 0; i < 128; ++i) nonSilent |= (l[i] != 0.0f);
    REQUIRE (nonSilent);
    // Ramp value at absolute sample (tapeSampleStart + i): both channels match.
    REQUIRE (l[1] == Catch::Approx (static_cast<float> (tapeSampleStart + 1)));
    REQUIRE (r[1] == Catch::Approx (static_cast<float> (tapeSampleStart + 1)));

    // 8. STOPPED: empty snapshot -> step zeroes the previously-active scratch.
    resolver.setPlayheadProvider ([] { return ida::TransportPlayhead { 0.0, /*isPlaying=*/false }; });
    resolver.resolveOnceForTest();
    cb.runPlaybackStepForTest (128);
    bool silent = true;
    for (int i = 0; i < 128; ++i) silent &= (l[i] == 0.0f);
    REQUIRE (silent);
}

// ---------------------------------------------------------------------------
// Variable-size record test (T0b Task 5 correctness — the whole point)
// ---------------------------------------------------------------------------

TEST_CASE ("prefetcher locates samples across variable-size records",
           "[tape-playback][prefetch]")
{
    // Write 5 records with IRREGULAR frame counts (reality: one record per
    // audio-callback block, block size is NOT constant under JUCE/AUv3 hosts).
    // Record values: frame f of record r == its absolute sample index (cumulative).
    // Cumulative starts: {0, 100, 356, 406, 706}.
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry = makePlaybackRegistry();
    const int sizes[] = { 100, 256, 50, 300, 64 };
    writeRampTapeVariable (file, registry, sizes, 5);

    TapePrefetcher pre;
    REQUIRE (pre.open (file, registry, 48000.0, /*loopLengthSamples=*/0));
    pre.prepare (4096);

    // Target sample 400 sits in record 2 (start=356, 50 frames → covers 356..405)
    // at offset 44.  Pulling 128 frames crosses records 2→3 (start=406, 300 frames).
    pre.setTargetSample (400);
    pre.serviceForTest();

    std::vector<float> l (128, -1.0f), r (128, -1.0f);
    REQUIRE (pre.pull (l.data(), r.data(), 128) == 128);
    for (int i = 0; i < 128; ++i)
        REQUIRE (l[i] == Catch::Approx (static_cast<float> (400 + i)));
}
