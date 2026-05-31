# T0a — Durable Media-Agnostic Tape Record Store — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (or superpowers:executing-plans). Steps use checkbox (`- [ ]`) syntax. Follow superpowers:test-driven-development per task.

**Goal:** Replace IDA's monolithic-FLAC on-disk tape with an append-only, self-delimiting, checksummed, media-agnostic **record container** plus a random-access **reader** and **crash-recovery** scan — the durable foundation the T0b playback path reads from.

**Architecture:** A pure-C++ byte layer (`TapeRecord`) defines records `[u32 bodyLen][body][u32 crc32(body)]` after a 12-byte file header. A codec layer (`IPayloadCodec` + registry) decouples medium from durability; only audio codecs ship this sprint (FLAC-per-block + PCM). A worker-driven `TapeRecordWriter` (an `ITapeSink` reusing FlacTapeSink's audio→SPSC→worker pattern) frames/flushes records; a `TapeRecordReader` scans, recovers, indexes, and random-access-decodes. The live capture path swaps `FlacTapeSink` → `TapeRecordWriter`; FlacTapeSink is retired.

**Tech Stack:** C++20, JUCE (`juce::FlacAudioFormat`, `juce::File`, `juce::MemoryOutputStream`/`MemoryInputStream`, `juce::FileInputStream`/`FileOutputStream`), Catch2, CMake/Ninja.

---

## Source design

Spec: `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md`. Whitepaper §8.1–8.5, §15.2, §17.8, §17.9.

## On-disk format (authoritative — all tasks share this)

```
File header (written once at tape open, 12 bytes):
  magic[8]      = {'I','D','A','T','A','P','E','\0'}
  formatVersion u16  = 1            (little-endian)
  reserved      u16  = 0

Per record (append-only):
  bodyLen   u32                      (little-endian) = kRecordHeaderBytes + payloadLen
  body[bodyLen]:
    seq           u64                (per-tape, 0-based, monotonic)
    type          u16                (TapeRecordType: Audio=1)
    codec         u16                (TapeCodecId: AudioFlac=1, AudioPcm=2)
    conceptualNum i64                (Rational conceptual timestamp)
    conceptualDen i64
    lmcNum        i64                (Rational LMC timestamp)
    lmcDen        i64
    payload[payloadLen]              (codec-encoded bytes; independently decodable)
  crc        u32                     (little-endian) = crc32(body, bodyLen)

kRecordHeaderBytes = 8+2+2+8+8+8+8 = 44.
```

All integers little-endian regardless of host. CRC is standard CRC-32 (IEEE 802.3, poly 0xEDB88320, reflected). Append-only ⇒ any corruption is at the tail; length-prefix + trailing CRC make a torn/partial trailing record detectable, so recovery truncates at the first record whose bytes are incomplete or whose CRC fails.

## Public API surfaces (authoritative — keep consistent across tasks)

`core/include/ida/TapeRecord.h` (JUCE-free; may include `ida/Rational.h`):
```cpp
namespace ida {
enum class TapeRecordType : std::uint16_t { Audio = 1 };
enum class TapeCodecId    : std::uint16_t { None = 0, AudioFlac = 1, AudioPcm = 2 };

inline constexpr std::array<std::byte,8> tapeFileMagic();   // "IDATAPE\0"
inline constexpr std::uint16_t kTapeFormatVersion = 1;
inline constexpr std::size_t   kTapeFileHeaderBytes = 12;
inline constexpr std::size_t   kRecordHeaderBytes   = 44;

struct TapeRecordHeader {
    std::uint64_t  seq          { 0 };
    TapeRecordType type         { TapeRecordType::Audio };
    TapeCodecId    codec        { TapeCodecId::AudioFlac };
    Rational       conceptualTs { Rational{0} };
    Rational       lmcTs        { Rational{0} };
};

// little-endian primitives
void          writeLE16(std::byte* dst, std::uint16_t v) noexcept;
void          writeLE32(std::byte* dst, std::uint32_t v) noexcept;
void          writeLE64(std::byte* dst, std::uint64_t v) noexcept;
std::uint16_t readLE16(const std::byte* src) noexcept;
std::uint32_t readLE32(const std::byte* src) noexcept;
std::uint64_t readLE64(const std::byte* src) noexcept;

std::uint32_t crc32(const std::byte* data, std::size_t len) noexcept;

void writeFileHeader(std::byte* dst /*>= kTapeFileHeaderBytes*/) noexcept;
// returns true + sets versionOut iff magic matches and n>=12
bool readFileHeader(const std::byte* src, std::size_t n, std::uint16_t& versionOut) noexcept;

// Serialize header+payload into `out` as [bodyLen][body][crc]; out is resized. Returns total bytes.
std::size_t encodeRecord(const TapeRecordHeader& h,
                         const std::byte* payload, std::size_t payloadLen,
                         std::vector<std::byte>& out);

// Parse a body buffer (the `bodyLen` bytes, CRC already validated by caller) into header + payload view.
// Returns false if bodyLen < kRecordHeaderBytes.
bool decodeRecordBody(const std::byte* body, std::size_t bodyLen,
                      TapeRecordHeader& hOut,
                      const std::byte*& payloadOut, std::size_t& payloadLenOut) noexcept;
}
```

`engine/include/ida/IPayloadCodec.h` (JUCE-free; includes `ida/TapeRecord.h`):
```cpp
namespace ida {
struct PcmBlock {                  // decoded stereo PCM (worker/message thread)
    std::vector<float> left;
    std::vector<float> right;
    int numFrames() const noexcept { return (int) left.size(); }
};
class IPayloadCodec {
public:
    virtual ~IPayloadCodec() = default;
    virtual TapeCodecId codecId() const noexcept = 0;
    // worker-thread: encode one stereo block → payload bytes (independently decodable)
    virtual std::vector<std::byte> encode(const float* left, const float* right,
                                          int numFrames, double sampleRate) const = 0;
    // worker-thread: decode payload → PCM
    virtual bool decode(const std::byte* payload, std::size_t len, PcmBlock& out) const = 0;
};
class TapeCodecRegistry {
public:
    void registerCodec(std::shared_ptr<IPayloadCodec> codec);   // keyed by codec->codecId()
    IPayloadCodec* codecFor(TapeCodecId id) const noexcept;     // nullptr if absent
private:
    std::unordered_map<std::uint16_t, std::shared_ptr<IPayloadCodec>> codecs_;
};
}
```

`audio/include/ida/AudioPayloadCodec.h` (uses juce FLAC):
```cpp
namespace ida {
class FlacAudioCodec final : public IPayloadCodec {   // codecId() == AudioFlac
    // encode: one self-contained 24-bit stereo FLAC stream (header + one block) via
    //         juce::MemoryOutputStream + FlacAudioFormat (compression level 3).
    // decode: juce::MemoryInputStream + FlacAudioFormat::createReaderFor → PcmBlock.
};
class PcmAudioCodec final : public IPayloadCodec {    // codecId() == AudioPcm
    // encode: [u32 numFrames] then interleaved float32 LE (bit-exact, no loss).
    // decode: inverse.
};
}
```

`audio/include/ida/TapeRecordWriter.h` (an `ITapeSink`; **mirrors FlacTapeSink's surface for a drop-in swap**):
```cpp
namespace ida {
inline constexpr int kTapeRecordWriterMaxFramesPerMessage = 4096;
class TapeRecordWriter final : public ITapeSink {
public:
    TapeRecordWriter(juce::File tapesDir, double sampleRate, std::size_t queueCapacity,
                     TapeCodecId audioCodec, int flushIntervalMs);
    ~TapeRecordWriter() override;
    TapeRecordWriter(const TapeRecordWriter&) = delete;
    TapeRecordWriter& operator=(const TapeRecordWriter&) = delete;

    void          setSampleRate(double sampleRate) noexcept;
    void          closeTape(TapeId id);                 // same SINGLE-PRODUCER caveat as FlacTapeSink
    std::uint64_t droppedBlockCount() const noexcept;
    int           flushIntervalMs() const noexcept;

    void deliverTapeBlock(TapeId tape, const float* left, const float* right,
                          int numSamples) noexcept override;   // audio thread
    juce::File tapeFile(TapeId id) const;               // <tapesDir>/tape-<id>.idatape (test helper)
};
}
```
File extension: `tape-<id>.idatape` (distinct from the retired `.flac`). Timestamp policy v1: the writer derives `lmcTs = Rational(framesWrittenForTape, sampleRate-as-rational)` from the per-tape cumulative frame count (exact); `conceptualTs = lmcTs` at capture (the real conceptual↔LMC mapping is a T0b render-time concern via `TempoMap`). Document this in the header.

`audio/include/ida/TapeRecordReader.h`:
```cpp
namespace ida {
struct RecordIndexEntry {
    std::uint64_t  seq;
    std::uint64_t  fileOffset;     // offset of the bodyLen prefix
    std::uint32_t  bodyLen;
    TapeRecordType type;
    TapeCodecId    codec;
    Rational       lmcTs;
};
struct TapeTruncationReport {
    bool          truncated      { false };
    std::uint64_t recordsKept    { 0 };
    std::uint64_t truncatedAtOffset { 0 };
    std::string   reason;          // "" when not truncated
};
class TapeRecordReader {
public:
    // recover=true: exclusive crash-recovery open — truncates a bad/partial trailing record on disk.
    // recover=false: read-only live open — stops at the last complete record, never writes (concurrent read).
    static std::unique_ptr<TapeRecordReader> open(const juce::File& file,
                                                  TapeCodecRegistry& registry,
                                                  TapeTruncationReport& reportOut,
                                                  bool recover = true);
    std::uint64_t recordCount() const noexcept;
    const std::vector<RecordIndexEntry>& index() const noexcept;
    // random access by 0-based record position: decode payload via the record's codec.
    bool readAudioRecord(std::uint64_t position, PcmBlock& out, TapeRecordHeader& hOut) const;
    // live re-scan: pick up records flushed since the last open/refresh (recover=false readers).
    void refresh(TapeTruncationReport& reportOut);
private:
    juce::File file_;
    TapeCodecRegistry* registry_;
    std::vector<RecordIndexEntry> index_;
    std::uint64_t scannedTo_ { 0 };   // byte offset scanned up to
};
}
```

## Reuse (exact paths)

- SPSC ring: `engine/include/ida/LockFreeSpscQueue.h`.
- Audio→SPSC→worker pattern to copy verbatim: `audio/src/FlacTapeSink.cpp` (Message POD, `workerLoop`, `drainQueue`, `wakeCv_`/`wait_for`, dtor join+drain).
- Exact time: `core/include/ida/Rational.h` (`numerator()/denominator()`, exact ctor `Rational{n,d}`).
- `core/include/ida/TapeId.h`.
- Tier flush map source: `app/CapabilityTier.h` `TierPolicy` (extend with `flushIntervalMs`).
- FLAC decode reference for tests: the `decodeFlac()` helper in `tests/FlacTapeSinkTests.cpp` (move/adapt into the new test file before deleting that file in Task 9).
- Integration site: `app/MainComponent.cpp:4244` (FlacTapeSink construction), `:6078`/`:6085` (`setSampleRate`/`droppedBlockCount`), `app/MainComponent.h:371` (`flacTapeSink_` member, `:15` include).

## CMake wiring

- `core/include/ida/TapeRecord.h` is header-only? No — `crc32`, LE helpers, `encodeRecord`, `decodeRecordBody`, `writeFileHeader`/`readFileHeader` go in `core/src/TapeRecord.cpp`. Add to `core/CMakeLists.txt` sources.
- `engine/include/ida/IPayloadCodec.h` + `engine/src/IPayloadCodec.cpp` (registry impl) → `engine/CMakeLists.txt`.
- `audio/src/AudioPayloadCodec.cpp`, `audio/src/TapeRecordWriter.cpp`, `audio/src/TapeRecordReader.cpp` → `audio/CMakeLists.txt` (remove `src/FlacTapeSink.cpp` in Task 9).
- New test file `tests/TapeRecordStoreTests.cpp` registered in `tests/CMakeLists.txt` (mirror the `FlacTapeSinkTests.cpp` registration at `tests/CMakeLists.txt:88`).

---

## Task 1 — TapeRecord byte layer + CRC32 (pure C++)

**Files:** Create `core/include/ida/TapeRecord.h`, `core/src/TapeRecord.cpp`; modify `core/CMakeLists.txt`; Test `tests/TapeRecordStoreTests.cpp` (new, tag `[tape-record]`).

- [ ] Write failing tests: (a) `writeFileHeader`→`readFileHeader` returns version 1 and rejects wrong magic / short buffer; (b) LE helpers round-trip for 16/32/64 across known byte patterns and are byte-stable (assert exact bytes for a known value, e.g. `writeLE32(0x01020304)` → bytes `04 03 02 01`); (c) `crc32` matches a known vector (`crc32("123456789")==0xCBF43926`); (d) `encodeRecord` then manual parse: bodyLen at `[0..4)`, trailing 4 bytes == `crc32(body)`, body fields decode via `decodeRecordBody` bit-exact (seq, type, codec, both Rationals, payload bytes); (e) flipping any body byte makes the stored CRC mismatch a freshly-computed `crc32(body)`.
- [ ] Run: `cmake --build build --target IdaTests && ctest --test-dir build -R tape-record` → FAIL (undefined).
- [ ] Implement `TapeRecord.{h,cpp}`; register in `core/CMakeLists.txt`.
- [ ] Run tests → PASS.
- [ ] Commit: `feat: T0a — TapeRecord byte layout + CRC32 record container primitives`.

## Task 2 — IPayloadCodec interface + registry

**Files:** Create `engine/include/ida/IPayloadCodec.h`, `engine/src/IPayloadCodec.cpp`; modify `engine/CMakeLists.txt`; tests append to `tests/TapeRecordStoreTests.cpp`.

- [ ] Write failing tests with a trivial in-test fake codec (`codecId()==AudioPcm`): registry returns the registered codec for its id; returns `nullptr` for an unregistered id; re-registering the same id replaces.
- [ ] Run → FAIL.
- [ ] Implement registry (`std::unordered_map<std::uint16_t,...>` keyed by `(std::uint16_t)codec->codecId()`).
- [ ] Run → PASS. Commit: `feat: T0a — IPayloadCodec interface + codec registry`.

## Task 3 — Audio codecs (FLAC-per-block + PCM)

**Files:** Create `audio/include/ida/AudioPayloadCodec.h`, `audio/src/AudioPayloadCodec.cpp`; modify `audio/CMakeLists.txt`; tests append.

- [ ] Write failing tests: (a) `PcmAudioCodec` encode→decode of a known stereo ramp is **bit-exact** (every sample equal); (b) `FlacAudioCodec` encode→decode of a known stereo block returns the same `numFrames` and samples within 24-bit FLAC quantization tolerance (`|err| <= 1.0/(1<<23)` plus small margin); (c) each FLAC payload decodes **standalone** — encode block A and block B separately, decode B without ever touching A, samples match B (no cross-block state); (d) decode of garbage bytes returns `false` (no throw).
- [ ] Run → FAIL.
- [ ] Implement both codecs. FLAC encode: `juce::MemoryOutputStream` → `FlacAudioFormat::createWriterFor(streamBase, opts.withSampleRate(sr).withNumChannels(2).withBitsPerSample(24).withQualityOptionIndex(3))`, `writeFromFloatArrays`, destroy writer to finalize, return the MemoryBlock bytes. FLAC decode: `juce::MemoryInputStream` (copy) → `createReaderFor` → `read` into a `juce::AudioBuffer<float>` → fill `PcmBlock`. PCM: `[u32 numFrames]` + interleaved LE float32.
- [ ] Run → PASS. Commit: `feat: T0a — FLAC-per-block + PCM audio payload codecs`.

## Task 4 — TapeRecordWriter (append + framing + flush, RT-safe entry)

**Files:** Create `audio/include/ida/TapeRecordWriter.h`, `audio/src/TapeRecordWriter.cpp`; modify `audio/CMakeLists.txt`; tests append.

- [ ] Write failing tests (drive worker deterministically via a small `flushIntervalMs`, e.g. 5 ms, and `closeTape` before reading raw bytes): (a) deliver N blocks to one tape with `AudioPcm`, `closeTape`, then read the raw file: first 12 bytes are the file header; exactly N records follow, each `[bodyLen][body][crc]` with valid CRC and `seq` 0..N-1; (b) two tapes write to two distinct `.idatape` files; (c) per-record `lmcTs` increases by `blockFrames/sampleRate` (exact Rational) across consecutive records; (d) RT-safety inspection note: `deliverTapeBlock` constructs a stack POD `Message` and only `queue_.push` — assert no allocation by structure (mirror `FlacTapeSink`); (e) oversized block (> max frames) is dropped and bumps `droppedBlockCount()`.
- [ ] Run → FAIL.
- [ ] Implement: copy FlacTapeSink's Message/worker/CV/drain skeleton. Worker owns `std::unordered_map<TapeId-int, OpenTape>` where `OpenTape { juce::FileOutputStream*, std::uint64_t nextSeq, std::uint64_t framesWritten }`. On first block for a tape: open file, write file header. Per block: pick codec from registry by the configured `audioCodec`; `encode`; build `TapeRecordHeader{seq, Audio, codecId, conceptualTs=lmcTs, lmcTs=Rational(framesWritten, sr)}`; `encodeRecord` into a reusable worker buffer; write to stream; `framesWritten += n`; `++nextSeq`. Flush at the CV-`wait_for(flushIntervalMs)` cadence. `closeTape`/dtor flush+close (valid complete file). Construct an internal `TapeCodecRegistry` with both audio codecs.
- [ ] Run → PASS. Commit: `feat: T0a — TapeRecordWriter (worker-framed append + tier flush)`.

## Task 5 — TapeRecordReader (scan, index, random-access decode)

**Files:** Create `audio/include/ida/TapeRecordReader.h`, `audio/src/TapeRecordReader.cpp`; modify `audio/CMakeLists.txt`; tests append.

- [ ] Write failing tests: (a) write N PCM records via the writer, `open(recover=true)` → `recordCount()==N`, `index()` seqs are 0..N-1 ascending with strictly increasing offsets; (b) `readAudioRecord(K)` for an out-of-order K decodes that record's payload **without** decoding 0..K-1 (PCM bit-exact; assert returned `hOut.seq==K`); (c) FLAC-encoded round-trip within tolerance; (d) `readAudioRecord(position>=count)` returns false.
- [ ] Run → FAIL.
- [ ] Implement scan: read+validate the 12-byte file header; loop from offset 12 — read `u32 bodyLen`; if fewer than `bodyLen+4` bytes remain → stop (recovery handled in Task 6); read body+crc; if `crc32(body)!=storedCrc` → stop; else push `RecordIndexEntry` (parse header fields via `decodeRecordBody`) and advance. `readAudioRecord` seeks to `entry.fileOffset`, reads the body, dispatches `registry_->codecFor(entry.codec)->decode(...)`.
- [ ] Run → PASS. Commit: `feat: T0a — TapeRecordReader scan/index/random-access decode`.

## Task 6 — Crash recovery (scan-and-truncate + honest report)

**Files:** Modify `audio/src/TapeRecordReader.cpp`; tests append.

- [ ] Write failing tests: (a) write N records, append a deliberately partial trailing record (write a `bodyLen` prefix claiming more bytes than follow), `open(recover=true)` → `recordCount()==N`, `report.truncated==true`, `report.recordsKept==N`, `report.truncatedAtOffset==` the partial record's offset, file on disk is physically truncated to that offset; (b) flip a byte inside the **last** record's body → CRC mismatch → truncated to that record, report names it; (c) corrupt a **middle** record's CRC → recovery truncates from that record onward (append-only ⇒ everything after a bad record is unrecoverable), report says so; (d) clean file → `report.truncated==false`, `reason` empty.
- [ ] Run → FAIL.
- [ ] Implement: in `open` with `recover=true`, when the scan hits an incomplete/CRC-bad record, record `truncatedAtOffset = recordStartOffset`, open a `juce::FileOutputStream`, `setPosition(recordStartOffset)`, `truncate()`, fill the report. With `recover=false`, stop without writing and leave `truncated=false` (partial tail is just "not yet flushed").
- [ ] Run → PASS. Commit: `feat: T0a — tape crash recovery scan-and-truncate with honest report`.

## Task 7 — Concurrent read-during-write

**Files:** Modify `audio/src/TapeRecordReader.cpp` (`refresh`); tests append.

- [ ] Write failing tests: (a) start a writer, deliver M blocks, wait one flush window, `open(recover=false)` → sees ≥0 and ≤M complete records, never a partial one (every returned record has valid CRC); deliver M more, wait a flush window, `refresh()` → count grows, all CRCs valid, no truncation; (b) a `recover=false` open on a file with an in-progress partial trailing record returns only the complete prefix and `truncated==false` (does **not** modify the file — assert file size unchanged).
- [ ] Run → FAIL.
- [ ] Implement `refresh`: resume scanning from `scannedTo_` (last fully-validated offset), append new complete records to `index_`, stop at the first incomplete record, never write. `open(recover=false)` initializes `scannedTo_` and shares the scan core with Task 5/6 (extract a private `scanFrom(offset, recover, report)`).
- [ ] Run → PASS. Commit: `feat: T0a — concurrent read-during-write via reader refresh`.

## Task 8 — Tier-driven flush cadence

**Files:** Modify `app/CapabilityTier.h` (+ its .cpp `policyFor`), `audio/include/ida/TapeRecordWriter.h`/`.cpp` (clamp); tests append.

- [ ] Write failing tests: (a) `policyFor(tier).flushIntervalMs` is 1 / 50 / 200 / 1000 for Lavish / Comfortable / Tight / Survival; (b) a block delivered to a writer with `flushIntervalMs=50` becomes visible to a `recover=false` reader's `refresh()` within a bounded wait (e.g. ≤500 ms, generous for CI); (c) the writer clamps a constructor `flushIntervalMs` to `[1,5000]` (assert `flushIntervalMs()` getter reflects the clamp for inputs 0 and 99999).
- [ ] Run → FAIL.
- [ ] Implement: add `int flushIntervalMs;` to `TierPolicy` and set it in `policyFor` (find the existing `policyFor` definition — likely `app/CapabilityTier.cpp` or wherever `selectTier`/`policyFor` live; grep). Clamp in the writer ctor.
- [ ] Run → PASS. Commit: `feat: T0a — tier-driven flush cadence (1/50/200/1000 ms, clamped)`.

## Task 9 — Live swap + retire FlacTapeSink

**Files:** Modify `app/MainComponent.cpp` (`:4244` construction, `:6078`/`:6085` calls), `app/MainComponent.h` (`:15` include, `:371` member); delete `audio/include/ida/FlacTapeSink.h`, `audio/src/FlacTapeSink.cpp`, `tests/FlacTapeSinkTests.cpp`; modify `audio/CMakeLists.txt` (drop `src/FlacTapeSink.cpp`), `tests/CMakeLists.txt` (drop `FlacTapeSinkTests.cpp`).

- [ ] Replace the `flacTapeSink_` member type with `std::unique_ptr<ida::TapeRecordWriter>` (rename to `tapeRecordWriter_`), include `ida/TapeRecordWriter.h`. At `:4244` construct `TapeRecordWriter(tapesDir, rate, 256, codecForTier, policyFor(tier).flushIntervalMs)` where `codecForTier = (tier==Lavish ? TapeCodecId::AudioPcm : TapeCodecId::AudioFlac)`. Keep `setSampleRate`/`droppedBlockCount`/`closeTape` call sites (same names). Verify `tapeColoringSink_` still wraps it (inner `ITapeSink*` = the writer).
- [ ] Delete the three FlacTapeSink files; drop them from the two CMakeLists. Update the stale `FlacTapeSink`-naming comments in `engine/include/ida/TapeColoringSink.h:22/51`, `engine/CMakeLists.txt:28` to name `TapeRecordWriter`.
- [ ] `grep -rnE "TODO|FIXME|XXX|stub|placeholder" <changed files>` → zero (or accounted for in `todo.md`).
- [ ] Clean build + full suite: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests && ctest --test-dir build` → green (baseline minus retired FlacTapeSink cases, plus the new `[tape-record]` suite).
- [ ] Commit: `feat: T0a — swap live capture to TapeRecordWriter; retire FlacTapeSink`.

## Verification (whole plan)

- All `[tape-record]` cases pass; `ctest` green after the swap.
- RT-safety: `TapeRecordWriter::deliverTapeBlock` is `noexcept`, stack POD + single ring push, no alloc/lock/IO (matches `docs/RT_SAFETY_CONTRACT.md`).
- Clean build before any operator handoff.
- `git grep -n FlacTapeSink` returns zero matches outside historical docs.

## Self-review notes

- Spec coverage: record container (T1), media-agnostic codec registry (T2), audio codec only (T3), durable worker writer (T4), random-access reader (T5), crash recovery + honest report §17.9 (T6), concurrent read (T7), tier flush §17.8 (T8), RAM-ring-stays-volatile is preserved (untouched; writer is the disk path). T0b explicitly out of scope.
- Timestamp honesty: conceptual=lmc at capture is a documented v1 limit (real mapping is T0b render-time).
- No new mono path; audio is stereo throughout (HARD INVARIANT).
