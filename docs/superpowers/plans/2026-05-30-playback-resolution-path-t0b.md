# T0b — Playback-Resolution Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a captured phrase play back through its Output-Mixer strip — its per-phrase meter moves — with the playhead driven by OTTO's transport, by streaming audio from T0a's on-disk tape record store into each phrase channel's scratch buffer.

**Architecture:** A control-worker thread reads an OTTO-published playhead, runs `RenderPipeline::activeReadsAt` off the audio thread, and publishes a pre-resolved lock-free snapshot of which phrase-channel slots are sounding and where on tape. Per-slot `TapePrefetcher` workers decode tape records ahead of the playhead into lock-free PCM rings. A new audio-callback step (between OTTO render and OutputMixer dispatch) does only atomic-read + ring-pull + memcpy into each phrase channel's stable scratch — no alloc, lock, I/O, decode, or tree-walk on the audio thread.

**Tech Stack:** C++20, JUCE (`juce_core` FileInputStream / AudioFormat), Catch2 (`[tape-playback]` tag), existing IDA `LockFreeSpscQueue`, `RenderPipeline`, `TapeRecordReader` (T0a), `OutputMixer`, `OttoHost`.

---

## Context

T0a landed the durable, media-agnostic tape record store (writer + reader + crash-recovery + concurrent read). The write side flows: physical inputs → InputMixer → `TapeRecordWriter` → `tape-<id>.idatape`. **The read side is unused outside tests, and every OutputMixer phrase channel processes silence** — `setChannelAudioSource` is called only for OTTO/MON strips; phrase channels' source pointers stay `nullptr`. T0b closes that gap.

Design spec (approved): `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md` (Phase T0b, lines 155–229). Whitepaper anchors: §3.6 (rendering pipeline), §5.2 / §6.6 ("everything the output mixer renders is sourced from tape"), §5.7 (OTTO is transport/tempo source), §8.1–8.5 (tape).

### Pinned code facts (verified against HEAD `7330ebd`)

| Fact | Location |
|---|---|
| Reader random-access opens a `FileInputStream` per call (the perf todo) | `audio/src/TapeRecordReader.cpp:250` |
| `readAudioRecord(position, PcmBlock&, TapeRecordHeader&) const` | `audio/include/ida/TapeRecordReader.h:48` |
| `refresh(TapeTruncationReport&)` re-scans appended records | `audio/include/ida/TapeRecordReader.h:54` |
| `PcmBlock { std::vector<float> left, right; int numFrames(); }` (stereo) | `engine/include/ida/IPayloadCodec.h:12` |
| Tape file path = `tapesDir/tape-<id>.idatape` | `audio/src/TapeRecordWriter.cpp:104` |
| `OttoHost::renderBlock` advances OTTO conductor; `getConductor().isPlaying()` available | `otto-bridge/src/OttoHost.cpp:241`; `external/OTTO/.../Conductor.h:209` |
| OttoHost already publishes `MasterSnapshot` via atomic load (pattern to mirror) | `otto-bridge/include/ida/OttoHost.h:182`; `otto-bridge/src/OttoHost.cpp:356` |
| Audio-callback insertion point: between OTTO render (line 112) and OutputMixer dispatch (line 118) | `audio/src/AudioCallback.cpp:112`↔`118` |
| MON template: mint channel → `setChannelAudioSource(id, postStripPointer(0), postStripPointer(1))` | `engine/src/InputMixer.cpp:236` |
| `OutputMixer::setChannelAudioSource(OutputChannelId, const float* l, const float* r) noexcept` | `engine/src/OutputMixer.cpp:127` |
| OutputMixer has **no** stable per-channel scratch (its `channelScratch_` is block-transient) | `engine/src/OutputMixer.cpp:683` |
| `RenderPipeline::activeReadsAt(Rational) const → std::vector<ActiveRead>` (allocates, walks tree — NOT RT-safe) | `engine/src/RenderPipeline.cpp:122` |
| `ActiveRead { ConstituentId loop; TapeId tape; Rational tapePosition; std::int64_t cycle; }` | `engine/include/ida/RenderPipeline.h:18` |
| Loop→channel map `phraseChannelByConstituent_` (`unordered_map<int64, OutputChannelId>`) | `app/MainComponent.h:516` |
| Phrase-strip index → ConstituentId vector `phraseStripConstituentIds_` | `app/MainComponent.h:506` |
| SPSC ring `push/pop` (wait-free, drop-on-full, `capacity+1` slots) | `engine/include/ida/LockFreeSpscQueue.h` |
| NotificationBus = per-category SPSC rings + atomic overflow counters (publish template) | `engine/include/ida/NotificationBus.h` |

---

## Resolved decisions (the spec's open questions + one discovered)

Engine-internal calls, resolved per "default to professional / elegant"; documented so the engineer doesn't re-litigate them.

1. **Playhead source — OttoHost accumulates elapsed *playing* samples.** Inside `renderBlock`, advance an `std::int64_t playedSamples_` by `numSamples` only when `getConductor().isPlaying()` is true; publish `TransportPlayhead { double positionInSeconds = playedSamples_ / preparedSampleRate_; bool isPlaying; }` as a lock-free atomic. **Why over beats×60/bpm:** monotonic-while-playing / holds-while-stopped *by construction* (the spec's accessor test contract), identity calibration makes `round(seconds·sr) == playedSamples_` exactly (no float drift through bpm), and it sidesteps OTTO's pattern-null position-advance quirk (`Conductor::processBlock` early-returns when `!hasPattern()`, which is OTTO's whole song-playback lifetime). This **is** "OTTO's positionInSeconds" in the identity sense §T0b means. Real OTTO ppq-position mapping + loopback calibration remain deferred.

2. **Resolution cadence — dedicated control-worker thread**, not per-audio-block. The worker waits on a condition variable with a timed wakeup (`kResolveIntervalMs = 10`, the FlacTapeSink/TapeRecordWriter worker pattern), reads the playhead atomic, runs `activeReadsAt` off-thread, and publishes the snapshot. Per-block resolution is rejected — it would drag the allocating tree-walk next to the audio thread.

3. **Snapshot is pre-resolved to dense phrase-channel slots.** `activeReadsAt` yields `ConstituentId`s; resolving `ConstituentId → OutputChannelId → prefetcher slot` happens *on the worker* before publish. The audio thread reads a fixed-capacity array indexed by phrase-channel slot — no hash lookup, no tree-walk on the hot path. Capacity `kMaxPhraseSlots = 64` (v1; phrase count is small).

4. **Prefetch ring depth — fixed 1.0 s of frames per channel for v1.** Sized once in `prepare`. Tier-coupling (§15.2) is a noted refinement, not v1.

5. **Reader stream caching — one cached read stream, invalidated on `refresh`.** `readAudioRecord` reuses a `mutable std::unique_ptr<juce::FileInputStream>`; `refresh()` resets it (a `FileInputStream` caches total length at construction, so a stream opened before an append cannot see post-append records — resetting on refresh is the correctness boundary).

---

## File structure (T0b)

| File | Responsibility |
|---|---|
| `audio/include/ida/TapeRecordReader.h` + `audio/src/TapeRecordReader.cpp` (modify) | Cache the read stream; reuse across `readAudioRecord`; reset on `refresh`. Add a test-only reopen counter. |
| `otto-bridge/include/ida/OttoHost.h` + `otto-bridge/src/OttoHost.cpp` (modify) | `TransportPlayhead` POD + `advancePlayedSamples` pure helper + atomic publish in `renderBlock` + `snapshotPlayhead()` accessor. |
| `engine/include/ida/TransportPlayhead.h` (new, JUCE-free) | The `TransportPlayhead` POD + the pure `advancePlayedSamples` helper (so it is unit-testable without OTTOProcessor). |
| `engine/include/ida/ActiveReadsSnapshot.h` (new, JUCE-free) | `PhraseSlotRead` POD + `ActiveReadsSnapshot` (fixed-cap array) + `ActiveReadsPublisher` (seqlock double-buffer: worker writes, audio thread reads lock-free). |
| `audio/include/ida/TapePrefetcher.h` + `audio/src/TapePrefetcher.cpp` (new) | Per-phrase-channel worker: decode tape records ahead of the playhead via T0a's reader into a lock-free PCM ring; loop-span / cycle sample arithmetic. Audio-thread `pull(float* l, float* r, int n)`. |
| `engine/include/ida/PlaybackResolver.h` + `engine/src/PlaybackResolver.cpp` (new) | The control-worker thread: read playhead → `activeReadsAt` → map to slots → publish snapshot + steer prefetchers. |
| `engine/include/ida/OutputMixer.h` + `engine/src/OutputMixer.cpp` (modify) | Stable per-channel phrase scratch (mirror of InputMixer `postStrip_`): `ensurePhraseScratch(id)` + `phraseScratchPointer(id, side)`; the playback step writes into it, `setChannelAudioSource` points at it. |
| `audio/include/ida/AudioCallback.h` + `audio/src/AudioCallback.cpp` (modify) | New playback step between OTTO render and OutputMixer dispatch: atomic playhead read + snapshot read + per-slot ring pull → memcpy into phrase scratch. Set-once collaborators `setPlaybackResolver` / `setActiveReadsSnapshot`. |
| `app/MainComponent.cpp` + `app/MainComponent.h` (modify) | Own a `RenderPipeline` + `PlaybackResolver`; create a `TapePrefetcher` per phrase channel; wire scratch + `setChannelAudioSource`; connect the OTTO playhead; start/stop the resolver with the device. |
| `tests/TapePlaybackTests.cpp` (new, Catch2 `[tape-playback]`) | All T0b headless tests below. Register in `tests/CMakeLists.txt`. |

Reuse, with paths: `engine/include/ida/LockFreeSpscQueue.h`, `engine/include/ida/NotificationBus.h` (publish template), `core/include/ida/Rational.h`, `core/include/ida/ConstituentId.h`, `core/include/ida/TapeId.h`, `audio/src/TapeRecordWriter.cpp` (worker CV pattern + tape-path helper to mirror), the `[tape-record]` test helpers in `tests/TapeRecordStoreTests.cpp` (write a known tape to disk for round-trip).

---

## Tasks — TDD, headless, commit per task

Each task is red → green → commit. Work on `master`; push is authorized. Stage only the task's files (never `git add -A`). Single-line commit `feat:`/`fix: T0b — …`.

---

### Task 1: Reader — cache the read stream (kill per-call file-open)

**Files:**
- Modify: `audio/include/ida/TapeRecordReader.h`
- Modify: `audio/src/TapeRecordReader.cpp:237-278`
- Test: `tests/TapeRecordStoreTests.cpp` (append to the existing `[tape-record]` file)

- [ ] **Step 1: Write the failing test**

Add to `tests/TapeRecordStoreTests.cpp`. (`writeKnownTape` / the registry helper already exist in this file from T0a — reuse them; if a helper writes N records and returns the file, use it.)

```cpp
TEST_CASE ("readAudioRecord reuses one file stream across many reads",
           "[tape-record][reader-stream]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry;
    registerAudioCodecs (registry);          // existing T0a helper

    constexpr int kRecords = 8;
    writeKnownTape (file, registry, kRecords); // existing T0a helper: 8 stereo blocks

    TapeTruncationReport report;
    auto reader = TapeRecordReader::open (file, registry, report, /*recover=*/false);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == kRecords);

    PcmBlock block;
    TapeRecordHeader hdr;
    for (int pass = 0; pass < 10; ++pass)
        for (std::uint64_t i = 0; i < reader->recordCount(); ++i)
            REQUIRE (reader->readAudioRecord (i, block, hdr));

    // 80 reads must have opened the file exactly once.
    REQUIRE (reader->testReadStreamOpenCount() == 1);
}

TEST_CASE ("readAudioRecord sees records appended after a refresh",
           "[tape-record][reader-stream]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry;
    registerAudioCodecs (registry);

    writeKnownTape (file, registry, 4);        // 4 records on disk

    TapeTruncationReport report;
    auto reader = TapeRecordReader::open (file, registry, report, /*recover=*/false);
    REQUIRE (reader->recordCount() == 4);

    PcmBlock block; TapeRecordHeader hdr;
    REQUIRE (reader->readAudioRecord (3, block, hdr));   // caches a stream (len=4 records)

    appendKnownRecords (file, registry, /*startSeq=*/4, /*count=*/3); // existing T0a helper
    reader->refresh (report);
    REQUIRE (reader->recordCount() == 7);

    // The cached stream's length is stale; the reader must have reset it on refresh,
    // so reading a post-append record succeeds.
    REQUIRE (reader->readAudioRecord (6, block, hdr));
}
```

> If `appendKnownRecords` doesn't exist in the T0a helpers, add it next to `writeKnownTape`: open a `FileOutputStream` in append mode, frame `count` records starting at `startSeq` via the same record-framing the writer uses, flush. Keep it in the test file.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[reader-stream]"`
Expected: FAIL — `testReadStreamOpenCount` undefined (compile error), confirming the API is missing.

- [ ] **Step 3: Add the cached stream + counter to the header**

In `audio/include/ida/TapeRecordReader.h`, add to the public section (after `refresh`):

```cpp
    // Test-only: how many times the random-access read stream has been opened.
    // A single sequential read session over the whole tape must report 1.
    std::uint64_t testReadStreamOpenCount() const noexcept;
```

In the private section (after `scannedTo_`):

```cpp
    // Cached random-access read stream. Lazily opened by readAudioRecord and
    // reused across calls; reset by refresh() because juce::FileInputStream
    // caches total length at construction (a stream opened before an append
    // cannot see post-append records).
    mutable std::unique_ptr<juce::FileInputStream> readStream_;
    mutable std::uint64_t readStreamOpens_ { 0 };
```

- [ ] **Step 4: Reuse the stream in `readAudioRecord`; reset it in `refresh`**

In `audio/src/TapeRecordReader.cpp`, replace the per-call open in `readAudioRecord` (the `juce::FileInputStream fis (file_);` block at ~line 250) with:

```cpp
    if (readStream_ == nullptr)
    {
        readStream_ = std::make_unique<juce::FileInputStream> (file_);
        ++readStreamOpens_;
        if (! readStream_->openedOk())
        {
            readStream_.reset();
            return false;
        }
    }
    juce::FileInputStream& fis = *readStream_;
```

(The rest of `readAudioRecord` — `bodyOffset`, `readBytesAt (fis, …)`, `decodeRecordBody`, `codec->decode` — is unchanged.)

In `refresh`, before the `scanFrom` call, reset the cached stream:

```cpp
void TapeRecordReader::refresh (TapeTruncationReport& reportOut)
{
    // The cached read stream's total length is snapshotted at construction;
    // drop it so the next readAudioRecord reopens against the grown file.
    readStream_.reset();
    scanFrom (scannedTo_, /*recover=*/false, reportOut);
}
```

Add the accessor at the end of the file (before the closing namespace):

```cpp
std::uint64_t TapeRecordReader::testReadStreamOpenCount() const noexcept
{
    return readStreamOpens_;
}
```

- [ ] **Step 5: Run test to verify it passes + no regression**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[tape-record]"`
Expected: PASS — all `[tape-record]` cases green (T0a's 48 + the 2 new), single-open assertion holds.

- [ ] **Step 6: Commit**

```bash
git add audio/include/ida/TapeRecordReader.h audio/src/TapeRecordReader.cpp tests/TapeRecordStoreTests.cpp
git commit -m "fix: T0b — cache reader file stream; invalidate on refresh"
```

---

### Task 2: OttoHost transport playhead accessor

**Files:**
- Create: `engine/include/ida/TransportPlayhead.h`
- Modify: `otto-bridge/include/ida/OttoHost.h`, `otto-bridge/src/OttoHost.cpp`
- Test: `tests/TapePlaybackTests.cpp` (new file)

- [ ] **Step 1: Write the failing test for the pure helper**

Create `tests/TapePlaybackTests.cpp`:

```cpp
#include "ida/TransportPlayhead.h"
#include <catch2/catch_test_macros.hpp>

using namespace ida;

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
    // 48000 played samples at 48 kHz == 1.0 s, and round(1.0 * 48000) == 48000.
    REQUIRE (playheadSeconds (48000, 48000.0) == Catch::Approx (1.0));
    REQUIRE (playheadSeconds (0, 48000.0) == Catch::Approx (0.0));
    REQUIRE (playheadSeconds (24000, 0.0) == Catch::Approx (0.0)); // sr==0 guard
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: `ida/TransportPlayhead.h` not found. (Register `tests/TapePlaybackTests.cpp` in `tests/CMakeLists.txt` alongside the existing test sources first; copy the `[tape-record]` registration line.)

- [ ] **Step 3: Write `TransportPlayhead.h`**

Create `engine/include/ida/TransportPlayhead.h`:

```cpp
#pragma once

#include <cstdint>

namespace ida {

/// Lock-free transport playhead snapshot published by OttoHost each block and
/// read by the playback-resolution worker + the audio-callback playback step.
/// Trivially copyable POD.
struct TransportPlayhead {
    double positionInSeconds { 0.0 };
    bool   isPlaying         { false };
};

/// Advance an elapsed-played-samples counter. Advances by `numSamples` only
/// while playing; holds while stopped; ignores non-positive blocks. Pure.
inline std::int64_t advancePlayedSamples (std::int64_t prev,
                                          int          numSamples,
                                          bool         isPlaying) noexcept
{
    if (isPlaying && numSamples > 0)
        return prev + static_cast<std::int64_t> (numSamples);
    return prev;
}

/// Identity calibration: played-samples → LMC seconds. Guards sr<=0 → 0.
inline double playheadSeconds (std::int64_t playedSamples, double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return 0.0;
    return static_cast<double> (playedSamples) / sampleRate;
}

} // namespace ida
```

- [ ] **Step 4: Run to verify the helper tests pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[playhead]"`
Expected: PASS (5 assertions across 3 cases).

- [ ] **Step 5: Publish from OttoHost::renderBlock + expose the accessor**

In `otto-bridge/include/ida/OttoHost.h`, add the include `#include "ida/TransportPlayhead.h"` and a public accessor near `snapshotMaster`:

```cpp
    /// Transport playhead snapshot. Any-thread (atomic load). Advances while
    /// OTTO's conductor reports isPlaying(), holds while stopped. v1 identity
    /// calibration — positionInSeconds is elapsed playing-seconds (see plan RD1).
    TransportPlayhead snapshotPlayhead() const noexcept;
```

In `OttoHost::Impl` (the struct in `OttoHost.cpp`), add:

```cpp
    std::int64_t                 playedSamples_ { 0 };          // audio-thread private
    std::atomic<double>          playheadSeconds_ { 0.0 };      // published
    std::atomic<bool>            playheadPlaying_ { false };    // published
```

At the **end** of `OttoHost::renderBlock` (after `processBlockAfterRouting`), add the publish (read isPlaying from the conductor, advance, store):

```cpp
    const bool playing = proc.getConductor().isPlaying();
    impl_->playedSamples_ = advancePlayedSamples (impl_->playedSamples_, numSamples, playing);
    impl_->playheadSeconds_.store (playheadSeconds (impl_->playedSamples_, impl_->sampleRate),
                                   std::memory_order_release);
    impl_->playheadPlaying_.store (playing, std::memory_order_release);
```

> `impl_->sampleRate` is the prepared sample rate already stored by `prepare()` (confirm the field name in `Impl`; it is the same value `getPreparedSampleRate()` returns — reuse that field). Reset `playedSamples_ = 0` in `prepare()` so a re-prepare restarts the clock.

Add the accessor definition:

```cpp
TransportPlayhead OttoHost::snapshotPlayhead() const noexcept
{
    return { impl_->playheadSeconds_.load (std::memory_order_acquire),
             impl_->playheadPlaying_.load (std::memory_order_acquire) };
}
```

Add `#include <atomic>` to `OttoHost.cpp` if not present.

- [ ] **Step 6: Build the app target to confirm the OttoHost edit compiles**

Run: `cmake --build build --target IDA 2>&1 | tail -3`
Expected: link success (no operator launch needed — this is a compile check).

- [ ] **Step 7: Commit**

```bash
git add engine/include/ida/TransportPlayhead.h otto-bridge/include/ida/OttoHost.h otto-bridge/src/OttoHost.cpp tests/TapePlaybackTests.cpp tests/CMakeLists.txt
git commit -m "feat: T0b — OttoHost transport playhead accessor (identity calibration)"
```

---

### Task 3: Active-reads snapshot + lock-free publisher

**Files:**
- Create: `engine/include/ida/ActiveReadsSnapshot.h`
- Test: `tests/TapePlaybackTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePlaybackTests.cpp`:

```cpp
#include "ida/ActiveReadsSnapshot.h"

TEST_CASE ("ActiveReadsPublisher round-trips a snapshot", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;

    ActiveReadsSnapshot in;
    in.count = 2;
    in.slots[0] = { /*slot=*/3, /*tapeSampleStart=*/1000, /*active=*/true };
    in.slots[1] = { /*slot=*/5, /*tapeSampleStart=*/2048, /*active=*/true };
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: header not found.

- [ ] **Step 3: Write `ActiveReadsSnapshot.h` (seqlock double-buffer)**

Create `engine/include/ida/ActiveReadsSnapshot.h`:

```cpp
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ida {

/// Max simultaneously-sounding phrase channels resolved per block (v1 = 64).
inline constexpr int kMaxPhraseSlots = 64;

/// One pre-resolved active read: which phrase-channel slot is sounding and the
/// absolute tape sample index it should read from this block. The worker has
/// already mapped ConstituentId → OutputChannelId → slot, so the audio thread
/// needs no map lookup.
struct PhraseSlotRead {
    int          slot            { -1 };   ///< dense phrase-channel slot index
    std::int64_t tapeSampleStart { 0 };    ///< absolute tape sample for this block's start
    bool         active          { false };
};

static_assert (std::is_trivially_copyable_v<PhraseSlotRead>);

/// Fixed-capacity snapshot. Trivially copyable so the publisher can byte-copy it.
struct ActiveReadsSnapshot {
    std::array<PhraseSlotRead, kMaxPhraseSlots> slots {};
    int count { 0 };

    void clear() noexcept { count = 0; }

    void add (const PhraseSlotRead& r) noexcept
    {
        if (count < kMaxPhraseSlots)
            slots[static_cast<std::size_t> (count++)] = r;
    }
};

static_assert (std::is_trivially_copyable_v<ActiveReadsSnapshot>);

/// Single-producer (worker) / single-consumer (audio thread) seqlock publisher.
/// The consumer read is wait-free and retries only if a write was concurrent;
/// the producer never blocks. No allocation on either side.
class ActiveReadsPublisher {
public:
    void publish (const ActiveReadsSnapshot& s) noexcept
    {
        const std::uint32_t seq = seq_.load (std::memory_order_relaxed);
        seq_.store (seq + 1, std::memory_order_release);   // odd: write in progress
        std::atomic_thread_fence (std::memory_order_release);
        buffer_ = s;                                       // POD copy
        std::atomic_thread_fence (std::memory_order_release);
        seq_.store (seq + 2, std::memory_order_release);   // even: write complete
    }

    void read (ActiveReadsSnapshot& out) const noexcept
    {
        for (;;)
        {
            const std::uint32_t before = seq_.load (std::memory_order_acquire);
            if (before & 1u) continue;                     // writer mid-write
            std::atomic_thread_fence (std::memory_order_acquire);
            out = buffer_;                                 // POD copy
            std::atomic_thread_fence (std::memory_order_acquire);
            const std::uint32_t after = seq_.load (std::memory_order_acquire);
            if (before == after) return;                   // stable read
        }
    }

private:
    mutable ActiveReadsSnapshot buffer_ {};
    std::atomic<std::uint32_t>  seq_ { 0 };
};

} // namespace ida
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[snapshot]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/ActiveReadsSnapshot.h tests/TapePlaybackTests.cpp
git commit -m "feat: T0b — pre-resolved active-reads snapshot + seqlock publisher"
```

---

### Task 4: OutputMixer stable per-phrase scratch

**Files:**
- Modify: `engine/include/ida/OutputMixer.h`, `engine/src/OutputMixer.cpp`
- Test: `tests/TapePlaybackTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePlaybackTests.cpp`:

```cpp
#include "ida/OutputMixer.h"

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
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>>());
    mixer.ensurePhraseScratch (ch);
    mixer.setChannelAudioSource (ch,
                                 mixer.phraseScratchPointer (ch, 0),
                                 mixer.phraseScratchPointer (ch, 1));

    // Fill the scratch with a DC value, render, expect non-silence at master.
    constexpr int n = 64;
    float* l = mixer.mutablePhraseScratch (ch, 0);
    float* r = mixer.mutablePhraseScratch (ch, 1);
    for (int i = 0; i < n; ++i) { l[i] = 0.5f; r[i] = 0.5f; }

    std::array<std::vector<float>, 2> out { std::vector<float> (n, 0.0f),
                                            std::vector<float> (n, 0.0f) };
    float* outPtrs[2] = { out[0].data(), out[1].data() };
    mixer.renderBuffer (outPtrs, 2, n);     // existing render entry (confirm signature)

    REQUIRE (std::abs (out[0][0]) > 0.0f);
}
```

> Confirm the exact render entry signature in `OutputMixer.h` (the audio-callback dispatch calls it via `dispatchOutputMixer`); if it is `renderBuffer(float* const*, int, int)` use that, otherwise adapt the call. The point of the second test is only that a scratch-sourced channel reaches master.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: `ensurePhraseScratch` / `phraseScratchPointer` / `mutablePhraseScratch` undefined.

- [ ] **Step 3: Add the stable scratch (mirror of InputMixer postStrip_)**

In `engine/include/ida/OutputMixer.h`, public section:

```cpp
    /// Allocate (once) a stable, zero-initialized stereo scratch buffer owned by
    /// this channel, sized to the max block. The T0b playback step writes decoded
    /// tape samples here on the audio thread; the channel's audio source points
    /// at it via setChannelAudioSource. Idempotent — a second call does not move
    /// the buffer. Message-thread only (allocates).
    void ensurePhraseScratch (OutputChannelId id);

    /// Read pointer into the per-channel phrase scratch (side 0=L, 1=R). Stable
    /// for the channel's lifetime. nullptr for unknown id / side out of [0,1] /
    /// scratch not yet ensured. Audio-thread safe (no alloc).
    const float* phraseScratchPointer (OutputChannelId id, int side) const noexcept;

    /// Write pointer into the per-channel phrase scratch. Same contract; the
    /// playback step memcpys decoded samples through this. Audio-thread safe.
    float* mutablePhraseScratch (OutputChannelId id, int side) noexcept;
```

Private section (near the other per-channel storage):

```cpp
    // Stable per-phrase-channel scratch: [channelId] -> stereo pair -> samples.
    // Mirror of InputMixer::postStrip_. Sized to kMaxBlockSamples per side.
    std::unordered_map<std::int64_t, std::array<std::vector<float>, 2>> phraseScratch_;
```

(Confirm `<unordered_map>`, `<array>`, `<vector>` includes are present in `OutputMixer.h`; add any missing.)

In `engine/src/OutputMixer.cpp`:

```cpp
void OutputMixer::ensurePhraseScratch (OutputChannelId id)
{
    auto& pair = phraseScratch_[id.value()];
    for (auto& side : pair)
        if (side.empty())
            side.assign (static_cast<std::size_t> (kMaxBlockSamples), 0.0f);
}

const float* OutputMixer::phraseScratchPointer (OutputChannelId id, int side) const noexcept
{
    if (side < 0 || side > 1) return nullptr;
    auto it = phraseScratch_.find (id.value());
    if (it == phraseScratch_.end()) return nullptr;
    const auto& v = it->second[static_cast<std::size_t> (side)];
    return v.empty() ? nullptr : v.data();
}

float* OutputMixer::mutablePhraseScratch (OutputChannelId id, int side) noexcept
{
    if (side < 0 || side > 1) return nullptr;
    auto it = phraseScratch_.find (id.value());
    if (it == phraseScratch_.end()) return nullptr;
    auto& v = it->second[static_cast<std::size_t> (side)];
    return v.empty() ? nullptr : v.data();
}
```

> `kMaxBlockSamples` is already used by `OutputMixer`'s `channelScratch_` constructor — reuse the same constant.

- [ ] **Step 4: Run to verify it passes + no regression**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[phrase-scratch]" && ./build/tests/IdaTests "[output-mixer]"`
Expected: PASS — new cases green, existing OutputMixer suite unchanged.

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp tests/TapePlaybackTests.cpp
git commit -m "feat: T0b — OutputMixer stable per-phrase scratch buffers"
```

---

### Task 5: TapePrefetcher — decode-ahead PCM ring

**Files:**
- Create: `audio/include/ida/TapePrefetcher.h`, `audio/src/TapePrefetcher.cpp`
- Test: `tests/TapePlaybackTests.cpp`

**Responsibility.** One per active phrase channel. Owns a `TapeRecordReader` (recover=false) on a tape file and a lock-free PCM ring (interleaved-free: two `LockFreeSpscQueue<float>`-style rings, or one `std::vector<float>` ring per side with atomic head/tail). A worker thread, steered by a target tape-sample position, decodes records via `readAudioRecord`, slices the exact sample span (record covers `[recordStartSample, recordStartSample + numFrames)`), and pushes frames into the ring. The audio thread calls `pull(float* l, float* r, int n)` — wait-free, fills with available frames, zero-fills the remainder (underrun = architectural silence, not a glitch crash).

**v1 sample model.** Each record's first sample is `seq * framesPerRecord` (the writer appends fixed-size blocks; `framesPerRecord` is read from record 0's `numFrames`). Tape sample S lives in record `S / framesPerRecord` at intra-record offset `S % framesPerRecord`. FreeRunning leaf loops: the prefetcher is told a `loopLengthSamples` and wraps the target position modulo it (cycle handling) — but **v1 wires only the non-looping forward read**; looping wrap is implemented and unit-tested here so T0b end-to-end can exercise a single cycle, with multi-cycle wrap proven by the ring test.

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePlaybackTests.cpp`:

```cpp
#include "ida/TapePrefetcher.h"

TEST_CASE ("prefetcher yields recorded samples from a target position",
           "[tape-playback][prefetch]")
{
    juce::TemporaryFile tmp (".idatape");
    const juce::File file = tmp.getFile();
    TapeCodecRegistry registry;
    registerAudioCodecs (registry);

    // Write a ramp tape: record r, frame f -> sample value (r*framesPerRecord+f).
    constexpr int framesPerRecord = 256;
    constexpr int records = 8;
    writeRampTape (file, registry, records, framesPerRecord); // PCM (Lavish) -> bit-exact

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
    TapeCodecRegistry registry; registerAudioCodecs (registry);
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
    TapeCodecRegistry registry; registerAudioCodecs (registry);
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
```

> Add a `writeRampTape (file, registry, records, framesPerRecord)` helper to the test file (mirror `writeKnownTape` but fill each frame with its absolute sample index). Use the **PCM** codec (Lavish) so the ramp round-trips bit-exact.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: `ida/TapePrefetcher.h` not found.

- [ ] **Step 3: Write `TapePrefetcher.h`**

Create `audio/include/ida/TapePrefetcher.h`:

```cpp
#pragma once

#include "ida/TapeRecordReader.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace ida {

/// Per-phrase-channel decode-ahead reader. A worker decodes tape records via a
/// TapeRecordReader into a lock-free stereo PCM ring; the audio thread pulls
/// frames wait-free (underrun zero-fills). See plan Task 5 for the sample model.
class TapePrefetcher {
public:
    TapePrefetcher() = default;
    ~TapePrefetcher();

    TapePrefetcher (const TapePrefetcher&) = delete;
    TapePrefetcher& operator= (const TapePrefetcher&) = delete;

    /// Open the tape file. framesPerRecord is the writer's fixed block size;
    /// loopLengthSamples==0 means non-looping forward read. Message thread.
    bool open (const juce::File& file, TapeCodecRegistry& registry,
               int framesPerRecord, std::int64_t loopLengthSamples);

    /// Size the ring (message thread, before start()).
    void prepare (int ringFrames);

    /// Start / stop the worker thread.
    void start();
    void stop();

    /// Steer the decode-ahead target (the next tape sample the audio thread will
    /// consume). Lock-free; the worker observes it and refills. Any thread.
    void setTargetSample (std::int64_t tapeSample) noexcept;

    /// Audio thread: pull n stereo frames from the ring into l/r. Returns the
    /// number of real frames written; zero-fills [returned, n). Wait-free.
    int pull (float* l, float* r, int n) noexcept;

    /// Test hook: run one synchronous decode-into-ring pass (no worker thread).
    void serviceForTest();

private:
    void workerLoop();
    void fillRing();                       // decode from targetSample_ until ring full

    std::unique_ptr<TapeRecordReader> reader_;
    TapeCodecRegistry*                registry_ { nullptr };
    int                               framesPerRecord_ { 0 };
    std::int64_t                      loopLengthSamples_ { 0 };

    // Stereo ring: parallel L/R float buffers, SPSC head/tail.
    std::vector<float>          ringL_, ringR_;
    std::atomic<std::size_t>    head_ { 0 };   // consumer (audio thread)
    std::atomic<std::size_t>    tail_ { 0 };   // producer (worker)
    std::int64_t                nextDecodeSample_ { 0 };  // worker-private cursor

    std::atomic<std::int64_t>   targetSample_ { 0 };
    std::atomic<bool>           running_ { false };
    std::thread                 worker_;
    std::mutex                  cvMutex_;
    std::condition_variable     cv_;
};

} // namespace ida
```

- [ ] **Step 4: Write `TapePrefetcher.cpp`**

Create `audio/src/TapePrefetcher.cpp`. Implement: ring capacity `ringFrames+1`; `available()` = `(tail-head) mod cap`; `space()` = `cap-1-available()`. `fillRing()` loop while `space() >= framesPerRecord_` and there is a record covering `nextDecodeSample_`: decode that record, push the frames at/after the intra-record offset, advance `nextDecodeSample_`; when `setTargetSample` jumps, reset `head_=tail_` (drop stale) and `nextDecodeSample_ = target`. `pull` copies `min(n, available)` frames advancing `head_`, zero-fills the rest. Wrap modulo `loopLengthSamples_` when non-zero.

```cpp
#include "ida/TapePrefetcher.h"

#include "ida/IPayloadCodec.h"
#include "ida/TapeRecord.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ida {

TapePrefetcher::~TapePrefetcher() { stop(); }

bool TapePrefetcher::open (const juce::File& file, TapeCodecRegistry& registry,
                           int framesPerRecord, std::int64_t loopLengthSamples)
{
    registry_          = &registry;
    framesPerRecord_   = framesPerRecord;
    loopLengthSamples_ = loopLengthSamples;
    TapeTruncationReport report;
    reader_ = TapeRecordReader::open (file, registry, report, /*recover=*/false);
    return reader_ != nullptr;
}

void TapePrefetcher::prepare (int ringFrames)
{
    ringL_.assign (static_cast<std::size_t> (ringFrames) + 1, 0.0f);
    ringR_.assign (static_cast<std::size_t> (ringFrames) + 1, 0.0f);
    head_.store (0); tail_.store (0);
}

void TapePrefetcher::setTargetSample (std::int64_t tapeSample) noexcept
{
    targetSample_.store (tapeSample, std::memory_order_release);
    cv_.notify_one();
}

namespace {
inline std::size_t ringAvail (std::size_t head, std::size_t tail, std::size_t cap) noexcept
{ return (tail + cap - head) % cap; }
}

int TapePrefetcher::pull (float* l, float* r, int n) noexcept
{
    const std::size_t cap  = ringL_.size();
    if (cap == 0) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); return 0; }
    std::size_t head = head_.load (std::memory_order_relaxed);
    const std::size_t tail = tail_.load (std::memory_order_acquire);
    const std::size_t avail = ringAvail (head, tail, cap);
    const int got = static_cast<int> (std::min<std::size_t> (avail, static_cast<std::size_t> (n)));
    for (int i = 0; i < got; ++i)
    {
        l[i] = ringL_[head]; r[i] = ringR_[head];
        head = (head + 1) % cap;
    }
    head_.store (head, std::memory_order_release);
    for (int i = got; i < n; ++i) { l[i] = 0.0f; r[i] = 0.0f; }
    return got;
}

void TapePrefetcher::fillRing()
{
    if (reader_ == nullptr || framesPerRecord_ <= 0) return;
    const std::size_t cap = ringL_.size();
    if (cap == 0) return;

    // Honor a target jump: drop stale ring contents and reposition the cursor.
    const std::int64_t target = targetSample_.load (std::memory_order_acquire);
    if (nextDecodeSample_ != target && ringAvail (head_.load(), tail_.load(), cap) == 0)
        nextDecodeSample_ = target;

    PcmBlock block; TapeRecordHeader hdr;
    for (;;)
    {
        std::size_t tail = tail_.load (std::memory_order_relaxed);
        const std::size_t head = head_.load (std::memory_order_acquire);
        const std::size_t space = (cap - 1) - ringAvail (head, tail, cap);
        if (space < static_cast<std::size_t> (framesPerRecord_)) break;

        std::int64_t s = nextDecodeSample_;
        if (loopLengthSamples_ > 0) s %= loopLengthSamples_;
        const std::uint64_t rec = static_cast<std::uint64_t> (s / framesPerRecord_);
        const int off          = static_cast<int> (s % framesPerRecord_);
        if (rec >= reader_->recordCount()) break;        // nothing more to decode
        if (! reader_->readAudioRecord (rec, block, hdr)) break;

        const int n = block.numFrames();
        for (int i = off; i < n; ++i)
        {
            ringL_[tail] = block.left[static_cast<std::size_t> (i)];
            ringR_[tail] = block.right[static_cast<std::size_t> (i)];
            tail = (tail + 1) % cap;
        }
        tail_.store (tail, std::memory_order_release);
        nextDecodeSample_ += (n - off);
    }
}

void TapePrefetcher::serviceForTest() { nextDecodeSample_ = targetSample_.load(); fillRing(); }

void TapePrefetcher::workerLoop()
{
    while (running_.load (std::memory_order_acquire))
    {
        fillRing();
        std::unique_lock<std::mutex> lk (cvMutex_);
        cv_.wait_for (lk, std::chrono::milliseconds (5));
    }
}

void TapePrefetcher::start()
{
    if (running_.exchange (true)) return;
    nextDecodeSample_ = targetSample_.load();
    worker_ = std::thread ([this] { workerLoop(); });
}

void TapePrefetcher::stop()
{
    if (! running_.exchange (false)) return;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

} // namespace ida
```

> Add `#include <chrono>` and the `<thread>/<mutex>/<condition_variable>` members to the header's includes (move the worker-sync members there). Register `audio/src/TapePrefetcher.cpp` in the `audio` target's CMakeLists.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[prefetch]"`
Expected: PASS — ramp matches, mid-tape seek lands on 1000.0, underrun zero-fills.

- [ ] **Step 6: Commit**

```bash
git add audio/include/ida/TapePrefetcher.h audio/src/TapePrefetcher.cpp tests/TapePlaybackTests.cpp audio/CMakeLists.txt
git commit -m "feat: T0b — TapePrefetcher decode-ahead PCM ring"
```

---

### Task 6: PlaybackResolver — control-worker resolution

**Files:**
- Create: `engine/include/ida/PlaybackResolver.h`, `engine/src/PlaybackResolver.cpp`
- Test: `tests/TapePlaybackTests.cpp`

**Responsibility.** A control-worker thread that, on each wakeup (`kResolveIntervalMs = 10`): reads a playhead provider → `Rational lmcTime` → `RenderPipeline::activeReadsAt(lmcTime)` → for each `ActiveRead`, look up `ConstituentId → slot` (via an injected map function), compute `tapeSampleStart = round(tapePosition.toDouble() * sampleRate)`, steer that slot's `TapePrefetcher::setTargetSample`, and accumulate a `PhraseSlotRead`. Publish the assembled `ActiveReadsSnapshot` via `ActiveReadsPublisher`. The provider/map/prefetcher accessors are injected (`std::function`s set at wiring time) so the resolver is testable without MainComponent.

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePlaybackTests.cpp`:

```cpp
#include "ida/PlaybackResolver.h"
#include "ida/RenderPipeline.h"

TEST_CASE ("resolver publishes a slot per active read", "[tape-playback][resolver]")
{
    // Build a RenderPipeline with one FreeRunning leaf loop (reuse the
    // [render-pipeline] test fixtures for a known tree + sessionToLmc).
    RenderPipeline pipeline = makeSingleLoopPipeline(); // existing test helper
    ActiveReadsPublisher publisher;

    PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setPlayheadProvider ([] { return TransportPlayhead { /*sec=*/0.0, /*playing=*/true }; });
    resolver.setSlotForConstituent ([] (ConstituentId c) { return static_cast<int> (c.value()); });
    int steered = -1; std::int64_t steeredTo = -1;
    resolver.setSteerPrefetcher ([&] (int slot, std::int64_t s) { steered = slot; steeredTo = s; });

    resolver.resolveOnceForTest();         // synchronous resolve+publish

    ActiveReadsSnapshot snap;
    publisher.read (snap);
    REQUIRE (snap.count >= 1);
    REQUIRE (snap.slots[0].active);
    REQUIRE (steered == snap.slots[0].slot);   // the prefetcher for that slot was steered
}

TEST_CASE ("resolver publishes empty snapshot when stopped", "[tape-playback][resolver]")
{
    RenderPipeline pipeline = makeSingleLoopPipeline();
    ActiveReadsPublisher publisher;
    PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setPlayheadProvider ([] { return TransportPlayhead { 0.0, /*playing=*/false }; });
    resolver.setSlotForConstituent ([] (ConstituentId c) { return static_cast<int> (c.value()); });
    resolver.setSteerPrefetcher ([] (int, std::int64_t) {});

    resolver.resolveOnceForTest();
    ActiveReadsSnapshot snap; publisher.read (snap);
    REQUIRE (snap.count == 0);              // not playing -> nothing sounding
}
```

> If `makeSingleLoopPipeline()` doesn't exist, lift the smallest fixture from the existing `[render-pipeline]` tests that builds a root with one FreeRunning leaf loop bound to a TapeId and a `sessionToLmc` identity TempoMap. Keep it as a local helper in `tests/TapePlaybackTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: `ida/PlaybackResolver.h` not found.

- [ ] **Step 3: Write `PlaybackResolver.h`**

Create `engine/include/ida/PlaybackResolver.h`:

```cpp
#pragma once

#include "ida/ActiveReadsSnapshot.h"
#include "ida/TransportPlayhead.h"
#include "ida/Rational.h"
#include "ida/ConstituentId.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace ida {

class RenderPipeline;

/// Off-audio-thread resolver: playhead -> activeReadsAt -> pre-resolved snapshot.
/// All collaborators are injected so it is testable in isolation.
class PlaybackResolver {
public:
    static constexpr int kResolveIntervalMs = 10;

    ~PlaybackResolver();

    void setPipeline (RenderPipeline* p) noexcept             { pipeline_ = p; }
    void setPublisher (ActiveReadsPublisher* p) noexcept      { publisher_ = p; }
    void setSampleRate (double sr) noexcept                   { sampleRate_ = sr; }
    void setPlayheadProvider (std::function<TransportPlayhead()> f) { playhead_ = std::move (f); }
    void setSlotForConstituent (std::function<int(ConstituentId)> f) { slotFor_ = std::move (f); }
    void setSteerPrefetcher (std::function<void(int, std::int64_t)> f) { steer_ = std::move (f); }

    void start();
    void stop();

    /// Synchronous one-shot resolve+publish (test + start() warmup).
    void resolveOnceForTest() { resolveOnce(); }

private:
    void resolveOnce();
    void workerLoop();

    RenderPipeline*       pipeline_  { nullptr };
    ActiveReadsPublisher* publisher_ { nullptr };
    double                sampleRate_ { 48000.0 };
    std::function<TransportPlayhead()>      playhead_;
    std::function<int(ConstituentId)>       slotFor_;
    std::function<void(int, std::int64_t)>  steer_;

    std::atomic<bool>       running_ { false };
    std::thread             worker_;
    std::mutex              cvMutex_;
    std::condition_variable cv_;
};

} // namespace ida
```

- [ ] **Step 4: Write `PlaybackResolver.cpp`**

Create `engine/src/PlaybackResolver.cpp`:

```cpp
#include "ida/PlaybackResolver.h"
#include "ida/RenderPipeline.h"

#include <chrono>
#include <cmath>
#include <vector>

namespace ida {

PlaybackResolver::~PlaybackResolver() { stop(); }

void PlaybackResolver::resolveOnce()
{
    if (pipeline_ == nullptr || publisher_ == nullptr || ! playhead_) return;

    ActiveReadsSnapshot snap;             // stack POD; worker thread, alloc is fine
    const TransportPlayhead ph = playhead_();
    if (ph.isPlaying)
    {
        const Rational lmc = Rational::fromDouble (ph.positionInSeconds); // existing helper
        const std::vector<ActiveRead> reads = pipeline_->activeReadsAt (lmc);
        for (const auto& ar : reads)
        {
            const int slot = slotFor_ ? slotFor_ (ar.loop) : -1;
            if (slot < 0) continue;
            const std::int64_t tapeSample =
                static_cast<std::int64_t> (std::llround (ar.tapePosition.toDouble() * sampleRate_));
            if (steer_) steer_ (slot, tapeSample);
            snap.add ({ slot, tapeSample, /*active=*/true });
        }
    }
    publisher_->publish (snap);
}

void PlaybackResolver::workerLoop()
{
    while (running_.load (std::memory_order_acquire))
    {
        resolveOnce();
        std::unique_lock<std::mutex> lk (cvMutex_);
        cv_.wait_for (lk, std::chrono::milliseconds (kResolveIntervalMs));
    }
}

void PlaybackResolver::start()
{
    if (running_.exchange (true)) return;
    worker_ = std::thread ([this] { workerLoop(); });
}

void PlaybackResolver::stop()
{
    if (! running_.exchange (false)) return;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

} // namespace ida
```

> Confirm `Rational::fromDouble` and `Rational::toDouble` exist (check `core/include/ida/Rational.h`); if the helpers are named differently (e.g. `Rational::approximate` / `asDouble`), use those. Register `engine/src/PlaybackResolver.cpp` in the `engine` target's CMakeLists.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[resolver]"`
Expected: PASS — playing yields ≥1 active slot + a steer; stopped yields empty.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/PlaybackResolver.h engine/src/PlaybackResolver.cpp tests/TapePlaybackTests.cpp engine/CMakeLists.txt
git commit -m "feat: T0b — PlaybackResolver control-worker (playhead -> active-reads snapshot)"
```

---

### Task 7: Audio-callback playback step

**Files:**
- Modify: `audio/include/ida/AudioCallback.h`, `audio/src/AudioCallback.cpp`
- Test: `tests/TapePlaybackTests.cpp`

**Responsibility.** A new RT-safe step between OTTO render (line 112) and OutputMixer dispatch (line 118): read the active-reads snapshot (lock-free), and for each active slot, pull frames from that slot's prefetcher into the slot's phrase scratch (write pointer). Channels with no active read are left as their scratch already holds (zeroed once per block by the step for active-then-inactive transitions). No alloc/lock/IO/decode/tree-walk — only `publisher.read`, `prefetcher.pull` (wait-free), and memcpy.

The callback needs: the `ActiveReadsPublisher*` (set once), and a slot→(prefetcher, scratchL, scratchR) table (set once at wiring time as a fixed `std::array<SlotSink, kMaxPhraseSlots>` of plain pointers — no per-block lookup).

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePlaybackTests.cpp`. This test drives the step directly via a test entry (`runPlaybackStepForTest`) so it needs no audio device:

```cpp
#include "ida/AudioCallback.h"

TEST_CASE ("playback step pulls active slot into its scratch; inactive stays zero",
           "[tape-playback][callback]")
{
    // One prefetcher primed with a ramp at sample 0.
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry; registerAudioCodecs (registry);
    writeRampTape (tmp.getFile(), registry, /*records=*/4, /*fpr=*/256);
    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, 256, 0));
    pre.prepare (4096);
    pre.setTargetSample (0);
    pre.serviceForTest();

    std::vector<float> scratchL (256, -1.0f), scratchR (256, -1.0f);

    ActiveReadsPublisher publisher;
    AudioCallback cb;
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (/*slot=*/0, &pre, scratchL.data(), scratchR.data());

    ActiveReadsSnapshot snap;
    snap.add ({ /*slot=*/0, /*tapeSampleStart=*/0, /*active=*/true });
    publisher.publish (snap);

    cb.runPlaybackStepForTest (/*numSamples=*/128);
    REQUIRE (scratchL[0] == Catch::Approx (0.0f));
    REQUIRE (scratchL[1] == Catch::Approx (1.0f));   // ramp landed in scratch

    // Now publish an empty snapshot: the step zeroes the previously-active slot.
    ActiveReadsSnapshot empty;
    publisher.publish (empty);
    cb.runPlaybackStepForTest (128);
    REQUIRE (scratchL[1] == 0.0f);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL: `setActiveReadsPublisher` / `bindPlaybackSlotForTest` / `runPlaybackStepForTest` undefined.

- [ ] **Step 3: Add the slot table + step to AudioCallback**

In `audio/include/ida/AudioCallback.h`, add includes `#include "ida/ActiveReadsSnapshot.h"` and forward-declare `class TapePrefetcher;`. Add to the public setters block:

```cpp
    /// Set-once (message thread, before the device starts). The playback step
    /// reads this snapshot each block.
    void setActiveReadsPublisher (ActiveReadsPublisher* p) noexcept { activeReads_ = p; }

    /// Bind a phrase-channel slot to its prefetcher + destination scratch
    /// pointers (the OutputMixer phrase scratch). Set-once at wiring time.
    void bindPlaybackSlot (int slot, TapePrefetcher* pre, float* scratchL, float* scratchR) noexcept;

    /// Test entry points (no audio device).
    void bindPlaybackSlotForTest (int slot, TapePrefetcher* pre, float* l, float* r) noexcept
    { bindPlaybackSlot (slot, pre, l, r); }
    void runPlaybackStepForTest (int numSamples) noexcept { renderPlaybackStep (numSamples); }
```

Private section:

```cpp
    struct PlaybackSlot { TapePrefetcher* pre { nullptr }; float* l { nullptr }; float* r { nullptr };
                          bool wasActive { false }; };
    ActiveReadsPublisher* activeReads_ { nullptr };
    std::array<PlaybackSlot, kMaxPhraseSlots> playbackSlots_ {};
    ActiveReadsSnapshot   playbackSnapshot_ {};   // reused, no per-block alloc

    void renderPlaybackStep (int numSamples) noexcept;
```

In `audio/src/AudioCallback.cpp`:

```cpp
void AudioCallback::bindPlaybackSlot (int slot, TapePrefetcher* pre,
                                      float* scratchL, float* scratchR) noexcept
{
    if (slot < 0 || slot >= kMaxPhraseSlots) return;
    playbackSlots_[static_cast<std::size_t> (slot)] = { pre, scratchL, scratchR, false };
}

void AudioCallback::renderPlaybackStep (int numSamples) noexcept
{
    if (activeReads_ == nullptr) return;
    activeReads_->read (playbackSnapshot_);          // lock-free, into reused member

    // Mark which slots are active this block.
    std::array<bool, kMaxPhraseSlots> active {};
    for (int i = 0; i < playbackSnapshot_.count; ++i)
    {
        const auto& s = playbackSnapshot_.slots[static_cast<std::size_t> (i)];
        if (s.active && s.slot >= 0 && s.slot < kMaxPhraseSlots)
            active[static_cast<std::size_t> (s.slot)] = true;
    }

    for (int slot = 0; slot < kMaxPhraseSlots; ++slot)
    {
        auto& ps = playbackSlots_[static_cast<std::size_t> (slot)];
        if (ps.l == nullptr) continue;
        if (active[static_cast<std::size_t> (slot)] && ps.pre != nullptr)
        {
            ps.pre->pull (ps.l, ps.r, numSamples);   // fills + zero-fills underrun
            ps.wasActive = true;
        }
        else if (ps.wasActive)
        {
            std::fill (ps.l, ps.l + numSamples, 0.0f);   // active -> inactive: silence once
            std::fill (ps.r, ps.r + numSamples, 0.0f);
            ps.wasActive = false;
        }
    }
}
```

Call the step from `audioDeviceIOCallbackWithContext`, **between line 112 (OTTO render) and line 114 (OutputMixer dispatch)**:

```cpp
    // Step 2c: T0b playback-resolution. Fill each sounding phrase channel's
    // stable scratch from its prefetch ring (atomic snapshot read + lock-free
    // ring pull + memcpy). RT-safe: no alloc/lock/IO/decode/tree-walk.
    renderPlaybackStep (numSamples);
```

Add `#include <algorithm>` and `#include <array>` if missing.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[callback]"`
Expected: PASS — ramp lands in scratch; empty snapshot zeroes the slot.

- [ ] **Step 5: RT-safety alloc test for the step**

Append a no-alloc assertion (reuse the `operator new` override pattern from the T0a writer test — `tests/TapeRecordStoreTests.cpp` has a `ScopedAllocGuard`/counter; if it's file-local, lift it into a shared `tests/AllocCounter.h` to avoid an ODR clash):

```cpp
TEST_CASE ("playback step performs zero allocations", "[tape-playback][callback][rt-safety]")
{
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry; registerAudioCodecs (registry);
    writeRampTape (tmp.getFile(), registry, 4, 256);
    TapePrefetcher pre; pre.open (tmp.getFile(), registry, 256, 0);
    pre.prepare (4096); pre.setTargetSample (0); pre.serviceForTest();

    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    ActiveReadsPublisher publisher;
    AudioCallback cb;
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &pre, l.data(), r.data());
    ActiveReadsSnapshot snap; snap.add ({ 0, 0, true }); publisher.publish (snap);

    cb.runPlaybackStepForTest (128);          // warm up

    AllocCounter::arm();
    for (int i = 0; i < 1000; ++i) cb.runPlaybackStepForTest (128);
    const auto allocs = AllocCounter::disarm();
    REQUIRE (allocs == 0);
}
```

Run: `./build/tests/IdaTests "[rt-safety]"` → PASS (zero allocs).

- [ ] **Step 6: Commit**

```bash
git add audio/include/ida/AudioCallback.h audio/src/AudioCallback.cpp tests/TapePlaybackTests.cpp
git commit -m "feat: T0b — audio-callback playback step (RT-safe scratch fill from prefetch ring)"
```

---

### Task 8: MainComponent wiring + end-to-end test

**Files:**
- Modify: `app/MainComponent.h`, `app/MainComponent.cpp`
- Test: `tests/TapePlaybackTests.cpp`

**Responsibility.** Own a `RenderPipeline` mirroring the live phrase tree, a `PlaybackResolver`, and a `TapePrefetcher` per phrase channel. At phrase-channel creation: `ensurePhraseScratch`, `setChannelAudioSource(ch, phraseScratchPointer(ch,0/1))`, allocate a slot, create+open+start the prefetcher on `tape-<id>.idatape`, and `audioCallback_.bindPlaybackSlot(slot, prefetcher, mutablePhraseScratch(ch,0/1))`. Wire the resolver: `setPlayheadProvider([this]{ return ottoHost_.snapshotPlayhead(); })`, `setSlotForConstituent` (ConstituentId → slot via `phraseChannelByConstituent_` → a slot table), `setSteerPrefetcher` (slot → prefetcher), `setPublisher(&activeReadsPublisher_)`, and `audioCallback_.setActiveReadsPublisher(&activeReadsPublisher_)`. Start the resolver when the device starts; stop it (and the prefetchers) on teardown — reverse acquisition order.

This task is integration glue; its correctness is proven by a headless end-to-end test that exercises the *engine* path (resolver + prefetcher + step + mixer) without the GUI, plus an operator eyes-on at the end.

- [ ] **Step 1: Write the failing end-to-end test**

Append to `tests/TapePlaybackTests.cpp`. This wires the components by hand (the same wiring MainComponent does) against a known tape:

```cpp
TEST_CASE ("end-to-end: advancing the playhead sounds the phrase, past it is silent",
           "[tape-playback][e2e]")
{
    // 1. A tape with a known ramp.
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry; registerAudioCodecs (registry);
    constexpr int fpr = 256, records = 8;          // 2048 frames total
    writeRampTape (tmp.getFile(), registry, records, fpr);

    // 2. A pipeline with one FreeRunning leaf loop bound to that tape, length 2048.
    auto [pipeline, loopId, tapeId] = makeSingleLoopPipelineBound (2048); // test helper

    // 3. OutputMixer phrase channel + scratch + prefetcher + slot binding.
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>>());
    mixer.ensurePhraseScratch (ch);
    mixer.setChannelAudioSource (ch, mixer.phraseScratchPointer (ch, 0),
                                     mixer.phraseScratchPointer (ch, 1));

    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, fpr, /*loopLengthSamples=*/2048));
    pre.prepare (8192);

    ActiveReadsPublisher publisher;
    AudioCallback cb;
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &pre, mixer.mutablePhraseScratch (ch, 0),
                                          mixer.mutablePhraseScratch (ch, 1));

    PlaybackResolver resolver;
    resolver.setPipeline (&pipeline);
    resolver.setPublisher (&publisher);
    resolver.setSampleRate (48000.0);
    resolver.setSlotForConstituent ([&] (ConstituentId c) { return c == loopId ? 0 : -1; });
    resolver.setSteerPrefetcher ([&] (int, std::int64_t s) { pre.setTargetSample (s); });

    // 4. Playhead at t=0, playing -> resolve -> prefetch -> step -> scratch non-silent.
    resolver.setPlayheadProvider ([] { return TransportPlayhead { 0.0, true }; });
    resolver.resolveOnceForTest();
    pre.serviceForTest();
    cb.runPlaybackStepForTest (128);
    const float* l = mixer.phraseScratchPointer (ch, 0);
    bool nonSilent = false;
    for (int i = 0; i < 128; ++i) nonSilent |= (l[i] != 0.0f);
    REQUIRE (nonSilent);
    REQUIRE (l[1] == Catch::Approx (1.0f));        // ramp sample 1

    // 5. Playhead past the loop, not playing -> empty snapshot -> scratch silent.
    resolver.setPlayheadProvider ([] { return TransportPlayhead { 999.0, false }; });
    resolver.resolveOnceForTest();
    cb.runPlaybackStepForTest (128);
    bool silent = true;
    for (int i = 0; i < 128; ++i) silent &= (l[i] == 0.0f);
    REQUIRE (silent);
}
```

> `makeSingleLoopPipelineBound(lengthSamples)` returns `{RenderPipeline, ConstituentId loop, TapeId tape}` — extend the Task 6 helper to also report the loop/tape ids and set the loop length so `activeReadsAt(0)` yields exactly that loop with `tapePosition≈0`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests` → FAIL (helper/bindings missing) — implement the helper, then the test goes red on the assertions until wiring is correct, then green. This task's "implementation" is the helper + confirming the already-built components compose; no new production type is required for the test to pass.

- [ ] **Step 3: Wire MainComponent (production glue)**

In `app/MainComponent.h`, add members:

```cpp
    ida::RenderPipeline                                   renderPipeline_;   // mirrors phrase tree
    ida::PlaybackResolver                                 playbackResolver_;
    ida::ActiveReadsPublisher                             activeReadsPublisher_;
    std::vector<std::unique_ptr<ida::TapePrefetcher>>     phrasePrefetchers_;
    std::unordered_map<std::int64_t, int>                 slotByConstituent_;  // ConstituentId -> slot
```

In `app/MainComponent.cpp`, at the phrase-channel creation site (where `phraseChannelByConstituent_[cid] = ch` is set), add the per-channel wiring:

```cpp
    outputMixer_->ensurePhraseScratch (ch);
    outputMixer_->setChannelAudioSource (ch,
        outputMixer_->phraseScratchPointer (ch, 0),
        outputMixer_->phraseScratchPointer (ch, 1));

    const int slot = static_cast<int> (phrasePrefetchers_.size());
    if (slot < ida::kMaxPhraseSlots)
    {
        auto pre = std::make_unique<ida::TapePrefetcher>();
        if (pre->open (tapeFileForConstituent (cid), tapeRegistry_,
                       kFramesPerRecord, /*loopLengthSamples=*/0))
        {
            pre->prepare (static_cast<int> (preparedSampleRate_)); // ~1.0 s ring (RD4)
            pre->start();
            audioCallback_.bindPlaybackSlot (slot, pre.get(),
                outputMixer_->mutablePhraseScratch (ch, 0),
                outputMixer_->mutablePhraseScratch (ch, 1));
            slotByConstituent_[cid.value()] = slot;
            phrasePrefetchers_.push_back (std::move (pre));
        }
    }
```

Once, after collaborators are constructed (device-prepare path):

```cpp
    playbackResolver_.setPipeline (&renderPipeline_);
    playbackResolver_.setPublisher (&activeReadsPublisher_);
    playbackResolver_.setSampleRate (preparedSampleRate_);
    playbackResolver_.setPlayheadProvider ([this] { return ottoHost_->snapshotPlayhead(); });
    playbackResolver_.setSlotForConstituent ([this] (ida::ConstituentId c) {
        auto it = slotByConstituent_.find (c.value());
        return it == slotByConstituent_.end() ? -1 : it->second; });
    playbackResolver_.setSteerPrefetcher ([this] (int slot, std::int64_t s) {
        if (slot >= 0 && slot < static_cast<int> (phrasePrefetchers_.size()))
            phrasePrefetchers_[static_cast<std::size_t> (slot)]->setTargetSample (s); });
    audioCallback_.setActiveReadsPublisher (&activeReadsPublisher_);
    playbackResolver_.start();
```

Teardown (reverse order — resolver before prefetchers before mixer):

```cpp
    playbackResolver_.stop();
    for (auto& p : phrasePrefetchers_) p->stop();
    phrasePrefetchers_.clear();
```

> `tapeFileForConstituent`, `tapeRegistry_`, `kFramesPerRecord`, `preparedSampleRate_` may need small accessors/fields — `kFramesPerRecord` is the writer's fixed block size (read it from the writer's constant; the writer already defines it). Mirror the tape-path helper from `TapeRecordWriter.cpp:104`. Keep `renderPipeline_` synced to the phrase tree at the same point the existing code rebuilds phrase strips (the session-load / add-phrase path) — assign it from the live root via the existing Constituent tree the phrase strips are built from.

- [ ] **Step 4: Run the full suite + clean build**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[tape-playback]"`
Expected: PASS — all `[tape-playback]` cases including `[e2e]`.

Run: `ctest --test-dir build` → full suite green at the established baseline (the documented `MainComponentPluginEditorTests_NOT_BUILT` is the only non-pass).

- [ ] **Step 5: Clean build before operator handoff**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA && cmake --build build --target IdaTests && ctest --test-dir build`
Expected: app + tests build clean; suite green.

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp tests/TapePlaybackTests.cpp
git commit -m "feat: T0b — wire playback-resolution path into MainComponent (phrase audio sounds)"
```

---

## Honest v1 limits (carry into continue.md + a code comment at each site)

- **FreeRunning leaf loops only** — `RenderPipeline::activeReadsAt`'s M3 reality; other triggers are correctly dormant (await subsystems not in this slice).
- **Identity device calibration** — `positionInSeconds → sampleIndex = round(seconds·sr)`; real loopback calibration deferred.
- **Playhead = elapsed-playing-seconds** (RD1), not OTTO's ppq position; relocate/seek within OTTO's timeline is not yet reflected. Documented v1 simplification.
- **Prefetch ring = fixed ~1.0 s** (RD4); tier-coupled depth (§15.2) deferred.
- **One tape per phrase channel, fixed `framesPerRecord`** — multi-tape phrase routing + variable block size are out of scope.

## Verification

- **Headless:** `cmake --build build --target IdaTests && ./build/tests/IdaTests "[tape-playback]"` — all new cases pass; `ctest --test-dir build` stays green at baseline.
- **RT-safety:** the `[tape-playback][rt-safety]` alloc test proves the playback step is zero-alloc; inspect `renderPlaybackStep` + `TapePrefetcher::pull` + `ActiveReadsPublisher::read` against `docs/RT_SAFETY_CONTRACT.md` — atomic reads, lock-free ring pull, memcpy only; no `new`/lock/IO/decode/tree-walk; `noexcept`.
- **Clean build before operator handoff:** `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`.
- **Operator (end of T0b):** launch `IDA` (Desktop alias), load/record a phrase loop, hit play (OTTO transport), hear the phrase through its output strip, watch its per-phrase meter move; stop → silence.

## Self-review notes (done while writing)

- **Spec coverage:** transport accessor (T2 ✓), resolution snapshot off-thread (T6 ✓), TapeReader prefetch (T5 ✓), audio-callback playback step between OTTO render and OutputMixer (T7 ✓), all five T0b test bullets (playhead monotonic/holds — T2; resolution snapshot — T6; reader random-access — T1/T5; end-to-end non-silent then silent — T8; RT-safety alloc — T7) ✓. The reader per-call file-open pre-work is T1 ✓.
- **Type consistency:** `TransportPlayhead`, `PhraseSlotRead`, `ActiveReadsSnapshot`, `kMaxPhraseSlots`, `phraseScratchPointer`/`mutablePhraseScratch`/`ensurePhraseScratch`, `TapePrefetcher::{open,prepare,setTargetSample,pull,serviceForTest}`, `PlaybackResolver::{setPipeline,setPublisher,setPlayheadProvider,setSlotForConstituent,setSteerPrefetcher}`, `AudioCallback::{setActiveReadsPublisher,bindPlaybackSlot,renderPlaybackStep}` are used consistently across tasks.
- **Unverified-at-write-time call sites to confirm during execution (flagged inline with `>`):** `OttoHost::Impl::sampleRate` field name; `OutputMixer` render entry signature; `Rational::fromDouble`/`toDouble` names; the `[tape-record]` test helpers (`writeKnownTape`/`registerAudioCodecs`/alloc counter) and whether to lift the alloc counter into a shared header; the existing `[render-pipeline]` single-loop fixture to base `makeSingleLoopPipeline(Bound)` on; `kFramesPerRecord` location in the writer. Each is a named, bounded lookup — not a design hole.

## Execution approach

Subagent-driven (per `continue.md` method note): one `general-purpose` implementer (model `sonnet`) per task with the FULL task text pasted in. Per task: implement → spec-compliance review → code-quality review → fix loop → git-verify (real `git log` + re-run the named suite whose summary line you read) before advancing. Trust the real Ninja build over stale clangd diagnostics. Stage only the task's files. After Task 8, run a holistic whole-subsystem review and refresh `continue.md`.
