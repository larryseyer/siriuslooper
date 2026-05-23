# Tape Subsystem Slice 3 — Capture-to-Disk Wiring (FLAC) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make routing an input node to tape X *actually record* — wire the existing `ITapeSink` seam to a real append-only **FLAC** recorder per `TapeId`, and put `renderInputGraph` on the live audio path.

**Architecture:** A new `Ida::Audio`-layer `FlacTapeSink : ida::ITapeSink`. The audio thread's `deliverTapeBlock` only copies the stereo block into a fixed-size POD message and `push`es it onto **one** lock-free SPSC queue (RT-safe: no alloc/lock/I/O). A single worker thread is the sole consumer; it **exclusively owns** the `TapeId → juce::AudioFormatWriter (FLAC)` map, lazily creating a `<tapesDir>/tape-<id>.flac` writer on the first block for a tape, encoding via `writeFromFloatArrays`, and flushing the underlying `FileOutputStream` each drain pass. Tape lifecycle (close) is an ordered control message through the same queue, so the map needs no lock. `MainComponent` constructs the sink, points `InputMixer::setTapeSink` at it, and `AudioCallback` calls `renderInputGraph` in place of the legacy `processBuffer` + `processDeviceInputs` pair.

**Tech Stack:** C++20, JUCE (`juce_audio_formats` — newly linked into `Ida::Audio`), `ida::LockFreeSpscQueue` (header-only), Catch2 (`IdaTests`), CMake/Ninja.

**Format decision (locked — spec `2026-05-21-tape-subsystem-design.md` Slice 3, whitepaper §8.5/§8.3/§17.8):** RAM ring stays uncompressed PCM; the **live disk tape is append-only FLAC per `TapeId`**; the tape is immutable from the instant bytes flow (no "finalize the take" event). **SHA-256 content-addressing + the `TapeId → contentHash` manifest are a session-close archival step and are OUT OF SCOPE here** — live tapes are identified on disk by `TapeId` (filename `tape-<id>.flac`).

**Known fidelity boundary (documented, not solved here):** FLAC is lossless over *integer* PCM; the engine graph output is 32-bit float. This plan writes **24-bit** FLAC (pro-audio capture convention), so the float→int24 quantization is the capture fidelity floor. Bit-exact-float archival (if §15 "archival exact" later demands it) needs a different archival codec — recorded in `todo.md`, deferred.

---

## File Structure

- **Create** `audio/include/ida/FlacTapeSink.h` — the sink class (public API: ctor, `setSampleRate`, `closeTape`, `deliverTapeBlock` override; RT-safe message POD).
- **Create** `audio/src/FlacTapeSink.cpp` — SPSC queue + worker thread + per-tape FLAC writers.
- **Modify** `audio/CMakeLists.txt` — link `juce::juce_audio_formats`.
- **Create** `tests/FlacTapeSinkTests.cpp` — `[flac-tape-sink]` headless TDD.
- **Modify** `tests/CMakeLists.txt` — add `FlacTapeSinkTests.cpp` to `IdaTests` sources.
- **Modify** `audio/src/AudioCallback.cpp` — replace Step 2 (`dispatchInputMixer`) + Step 2b (`processDeviceInputs`) with a single `renderInputGraph` call.
- **Modify** `tests/AudioCallbackTests.cpp` (or the existing audio-callback test file) — prove the callback drives `renderInputGraph` → sink.
- **Modify** `app/MainComponent.h` / `app/MainComponent.cpp` — construct + own `FlacTapeSink`, `setTapeSink`, set sample rate, tapes dir under app-data. (Operator-verified; compile-checked.)
- **Modify** `todo.md`, `continue.md` — fidelity boundary + handoff.

---

## Task 1: Link juce_audio_formats into Ida::Audio

**Files:**
- Modify: `audio/CMakeLists.txt:25-27`

- [ ] **Step 1: Add the module to the link line**

In `audio/CMakeLists.txt`, change:

```cmake
target_link_libraries(SiriusAudio
    PUBLIC  Ida::Engine
            juce::juce_audio_devices)
```
to:
```cmake
target_link_libraries(SiriusAudio
    PUBLIC  Ida::Engine
            juce::juce_audio_devices
            juce::juce_audio_formats)
```

- [ ] **Step 2: Configure to verify the module resolves**

Run: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`
Expected: configures with no error about `juce_audio_formats`.

- [ ] **Step 3: Commit**

```bash
git add audio/CMakeLists.txt
git commit -m "build: link juce_audio_formats into Ida::Audio (tape slice 3)"
```

---

## Task 2: FlacTapeSink — header + RT-safe message + lazy per-tape FLAC write

**Files:**
- Create: `audio/include/ida/FlacTapeSink.h`
- Create: `audio/src/FlacTapeSink.cpp`
- Create: `tests/FlacTapeSinkTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

- [ ] **Step 1: Write the header**

Create `audio/include/ida/FlacTapeSink.h`:

```cpp
#pragma once

#include "sirius/ITapeSink.h"
#include "sirius/LockFreeSpscQueue.h"
#include "sirius/TapeId.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace sirius
{

/// Per-message ceiling on the inline stereo payload. 4096 stereo float32 frames
/// (32 KB) covers any realistic audio block; renderInputGraph already clamps a
/// block to kMaxScratchSamples, which is <= this. A POD message so the audio
/// thread constructs it on the stack and the SPSC queue value-copies it.
inline constexpr int kFlacSinkMaxFramesPerMessage = 4096;

/// Real, RT-safe ITapeSink: writes one append-only FLAC stream per TapeId on a
/// worker thread (tape subsystem slice 3). The audio thread only copies the
/// block into a POD message and pushes it onto a single SPSC queue — no alloc,
/// no lock, no I/O, noexcept (docs/RT_SAFETY_CONTRACT.md). The worker thread is
/// the sole owner of the FLAC writers, so the writer map needs no lock; tape
/// close flows as an ordered control message through the same queue.
///
/// Files are <tapesDir>/tape-<id>.flac. The tape is immutable as written;
/// SHA-256 content-addressing is a session-close archival step handled
/// elsewhere (NOT here). FLAC block emission (~one FLAC block ≈ 85 ms at 48 kHz)
/// bounds worst-case crash loss; finer per-tier flushing needs direct libFLAC
/// (deferred).
class FlacTapeSink : public ITapeSink
{
public:
    /// `tapesDir` is created if absent. `sampleRate` may be 0 at construction
    /// (set later via setSampleRate before audio starts); `queueCapacity` slots
    /// must cover the worst-case touched-tapes-per-block burst.
    FlacTapeSink (juce::File tapesDir, double sampleRate, std::size_t queueCapacity);

    /// Stops the worker, drains remaining messages, finalizes every open FLAC
    /// writer (each produces a valid, complete file).
    ~FlacTapeSink() override;

    FlacTapeSink (const FlacTapeSink&) = delete;
    FlacTapeSink& operator= (const FlacTapeSink&) = delete;

    /// Message-thread, before the audio device starts (or on device change while
    /// the audio callback is detached). The worker reads it when lazily creating
    /// a writer. Changing it after a tape's writer exists does not retro-rewrite
    /// that file's header — set it before capture.
    void setSampleRate (double sampleRate) noexcept;

    /// Message-thread: enqueue an ordered request to finalize and close tape
    /// `id`'s FLAC writer. No-op if the tape has no open writer. Used by tape
    /// removal (slice 4) and explicit teardown.
    void closeTape (TapeId id);

    /// Diagnostics (message thread): blocks dropped because the queue was full.
    std::uint64_t droppedBlockCount() const noexcept;

    // --- audio thread ---
    void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                           int numSamples) noexcept override;

private:
    enum class MessageKind : std::uint8_t { Audio, CloseTape };

    struct Message
    {
        MessageKind kind { MessageKind::Audio };
        std::int64_t tapeId { 0 };
        int numFrames { 0 };
        // Interleaved L,R,L,R… so a single memcpy-free copy fills it.
        std::array<float, static_cast<std::size_t> (kFlacSinkMaxFramesPerMessage) * 2> samples {};
    };

    void workerLoop();
    void drainQueue();
    void writeAudio (const Message& msg);
    void finalizeTape (std::int64_t tapeId);
    juce::AudioFormatWriter* writerFor (std::int64_t tapeId); // worker-thread only

    juce::File tapesDir_;
    std::atomic<double> sampleRate_;
    LockFreeSpscQueue<Message> queue_;
    std::atomic<std::uint64_t> droppedBlocks_ { 0 };

    std::atomic<bool> shouldExit_ { false };
    std::condition_variable wakeCv_;
    std::mutex wakeMutex_;
    std::thread worker_;

    // Worker-thread-only state — never touched by the audio or message thread.
    juce::FlacAudioFormat flacFormat_;
    struct OpenTape
    {
        juce::FileOutputStream* rawStream { nullptr }; // non-owning; writer owns it
        std::unique_ptr<juce::AudioFormatWriter> writer;
    };
    std::unordered_map<std::int64_t, OpenTape> writers_;
};

} // namespace sirius
```

- [ ] **Step 2: Add the test source to CMake**

In `tests/CMakeLists.txt`, add `FlacTapeSinkTests.cpp` to the `IdaTests` source list (alongside the other `*Tests.cpp` entries — match the existing `target_sources`/`add_executable` form used in that file).

- [ ] **Step 3: Write the failing test**

Create `tests/FlacTapeSinkTests.cpp`:

```cpp
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

// Reads a FLAC file back into interleaved-by-channel float vectors.
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
        ida::FlacTapeSink sink (dir, 48000.0, 256);

        // A recognizable ramp so we can verify content survived encode/decode.
        std::vector<float> l (480), r (480);
        for (int i = 0; i < 480; ++i) { l[i] = static_cast<float> (i) / 480.0f * 0.5f;
                                        r[i] = -l[i]; }

        for (int block = 0; block < 4; ++block)
            sink.deliverTapeBlock (ida::TapeId { 1 }, l.data(), r.data(), 480);
        // sink destructor finalizes the writer.
    }

    const auto f = dir.getChildFile ("tape-1.flac");
    REQUIRE (f.existsAsFile());
    const auto decoded = decodeFlac (f);
    CHECK (decoded.numChannels == 2);
    CHECK (decoded.sampleRate == 48000.0);
    CHECK (decoded.numSamples == 480 * 4);
    // First block's ramp survived (24-bit quantization tolerance).
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
```

- [ ] **Step 4: Run the test to verify it fails (no implementation)**

Run: `cmake --build build --target IdaTests 2>&1 | tail -20`
Expected: FAIL — link/compile error (`FlacTapeSink.cpp` missing / undefined symbols).

- [ ] **Step 5: Write the implementation**

Create `audio/src/FlacTapeSink.cpp`:

```cpp
#include "sirius/FlacTapeSink.h"

#include <algorithm>

namespace sirius
{

FlacTapeSink::FlacTapeSink (juce::File tapesDir, double sampleRate, std::size_t queueCapacity)
    : tapesDir_ (std::move (tapesDir)),
      sampleRate_ (sampleRate),
      queue_ (queueCapacity)
{
    if (! tapesDir_.isDirectory())
        tapesDir_.createDirectory();
    worker_ = std::thread (&FlacTapeSink::workerLoop, this);
}

FlacTapeSink::~FlacTapeSink()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    drainQueue();                  // flush anything enqueued just before exit
    for (auto& [id, _] : writers_) // finalize every open FLAC stream
        (void) id;
    writers_.clear();              // unique_ptr<AudioFormatWriter> dtor finalizes
}

void FlacTapeSink::setSampleRate (double sampleRate) noexcept
{
    sampleRate_.store (sampleRate, std::memory_order_release);
}

void FlacTapeSink::closeTape (TapeId id)
{
    Message msg;
    msg.kind = MessageKind::CloseTape;
    msg.tapeId = id.value();
    if (! queue_.push (msg))
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
    wakeCv_.notify_all();
}

std::uint64_t FlacTapeSink::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load (std::memory_order_relaxed);
}

void FlacTapeSink::deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                     int numSamples) noexcept
{
    const int n = std::min (numSamples, kFlacSinkMaxFramesPerMessage);
    if (n <= 0 || left == nullptr || right == nullptr) return;

    Message msg;
    msg.kind = MessageKind::Audio;
    msg.tapeId = tape.value();
    msg.numFrames = n;
    for (int i = 0; i < n; ++i)
    {
        msg.samples[static_cast<std::size_t> (i) * 2]     = left[i];
        msg.samples[static_cast<std::size_t> (i) * 2 + 1] = right[i];
    }
    if (! queue_.push (msg))
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
    // No wakeCv_ notify on the audio thread (no lock on the hot path); the
    // worker's wait_for timeout (below) bounds latency.
}

void FlacTapeSink::workerLoop()
{
    using namespace std::chrono_literals;
    while (! shouldExit_.load (std::memory_order_acquire))
    {
        {
            std::unique_lock lk (wakeMutex_);
            wakeCv_.wait_for (lk, 20ms, [this]
            {
                return shouldExit_.load (std::memory_order_acquire) || ! queue_.empty();
            });
        }
        drainQueue();
        for (auto& [id, ot] : writers_)   // periodic durability flush
            if (ot.rawStream != nullptr) ot.rawStream->flush();
    }
}

void FlacTapeSink::drainQueue()
{
    Message msg;
    while (queue_.pop (msg))
    {
        if (msg.kind == MessageKind::CloseTape) finalizeTape (msg.tapeId);
        else                                    writeAudio (msg);
    }
}

juce::AudioFormatWriter* FlacTapeSink::writerFor (std::int64_t tapeId)
{
    auto it = writers_.find (tapeId);
    if (it != writers_.end()) return it->second.writer.get();

    const double sr = sampleRate_.load (std::memory_order_acquire);
    if (sr <= 0.0) return nullptr;     // no rate yet — drop until set

    const auto file = tapesDir_.getChildFile ("tape-" + juce::String (tapeId) + ".flac");
    file.deleteFile();                 // fresh stream per session run
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk())
    {
        juce::Logger::writeToLog ("FlacTapeSink: cannot open " + file.getFullPathName());
        return nullptr;
    }
    OpenTape ot;
    ot.rawStream = stream.get();
    ot.writer.reset (flacFormat_.createWriterFor (stream.release(), sr, 2, 24, {}, 0));
    if (ot.writer == nullptr)
    {
        juce::Logger::writeToLog ("FlacTapeSink: FLAC writer create failed for tape " + juce::String (tapeId));
        delete ot.rawStream; // reclaim the released stream the writer didn't take
        return nullptr;
    }
    auto* w = ot.writer.get();
    writers_.emplace (tapeId, std::move (ot));
    return w;
}

void FlacTapeSink::writeAudio (const Message& msg)
{
    auto* writer = writerFor (msg.tapeId);
    if (writer == nullptr) return;

    // De-interleave the POD payload into the two channel pointers the writer wants.
    float left[kFlacSinkMaxFramesPerMessage];
    float right[kFlacSinkMaxFramesPerMessage];
    for (int i = 0; i < msg.numFrames; ++i)
    {
        left[i]  = msg.samples[static_cast<std::size_t> (i) * 2];
        right[i] = msg.samples[static_cast<std::size_t> (i) * 2 + 1];
    }
    const float* channels[2] { left, right };
    writer->writeFromFloatArrays (channels, 2, msg.numFrames);
}

void FlacTapeSink::finalizeTape (std::int64_t tapeId)
{
    auto it = writers_.find (tapeId);
    if (it == writers_.end()) return;
    writers_.erase (it); // unique_ptr<AudioFormatWriter> dtor flushes + finalizes
}

} // namespace sirius
```

- [ ] **Step 6: Build and run the test to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[flac-tape-sink]"`
Expected: PASS (2 test cases).

- [ ] **Step 7: Commit**

```bash
git add audio/include/ida/FlacTapeSink.h audio/src/FlacTapeSink.cpp tests/FlacTapeSinkTests.cpp tests/CMakeLists.txt
git commit -m "feat: FlacTapeSink — RT-safe per-tape append-only FLAC recorder (tape slice 3)"
```

---

## Task 3: closeTape finalizes a valid file mid-session; dropped-block accounting

**Files:**
- Modify: `tests/FlacTapeSinkTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/FlacTapeSinkTests.cpp`:

```cpp
TEST_CASE ("FlacTapeSink closeTape finalizes a complete, re-readable file mid-session", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    ida::FlacTapeSink sink (dir, 48000.0, 256);
    std::vector<float> s (480, 0.1f);
    for (int b = 0; b < 3; ++b)
        sink.deliverTapeBlock (ida::TapeId { 2 }, s.data(), s.data(), 480);

    sink.closeTape (ida::TapeId { 2 });

    // Poll briefly for the worker to finalize (no production polling — test only).
    const auto f = dir.getChildFile ("tape-2.flac");
    for (int i = 0; i < 200 && ! (f.existsAsFile() && f.getSize() > 0); ++i)
        juce::Thread::sleep (5);

    REQUIRE (f.existsAsFile());
    CHECK (decodeFlac (f).numSamples == 480 * 3);
    dir.deleteRecursively();
}

TEST_CASE ("FlacTapeSink counts dropped blocks when the queue is full", "[flac-tape-sink]")
{
    auto dir = makeTempTapesDir();
    // Capacity 1 + flooding from this (producer) thread guarantees overflow
    // before the worker can drain.
    ida::FlacTapeSink sink (dir, 48000.0, 1);
    std::vector<float> s (480, 0.2f);
    for (int b = 0; b < 5000; ++b)
        sink.deliverTapeBlock (ida::TapeId { 1 }, s.data(), s.data(), 480);
    CHECK (sink.droppedBlockCount() > 0);
    dir.deleteRecursively();
}
```

- [ ] **Step 2: Run to verify behavior**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[flac-tape-sink]"`
Expected: PASS (4 cases). `closeTape` + `droppedBlockCount` already exist from Task 2, so these tests verify the worker's ordered close path and overflow accounting; if the close test flakes on timing, raise the poll bound — do not weaken the sample-count assert.

- [ ] **Step 3: Commit**

```bash
git add tests/FlacTapeSinkTests.cpp
git commit -m "test: FlacTapeSink mid-session closeTape finalization + dropped-block accounting (tape slice 3)"
```

---

## Task 4: Put renderInputGraph on the live audio path

**Files:**
- Modify: `audio/src/AudioCallback.cpp:163-176` (Step 2 + Step 2b)
- Modify: the audio-callback test file (`tests/AudioCallbackTests.cpp` if present; otherwise add to the file that currently tests `AudioCallback`)

> **Why a replacement, not an addition:** `renderInputGraph` runs `ChannelStrip<Audio>::process` once per channel (publishing the same peak/LUFS meters the UI reads — `InputMixer.cpp:651`) *and* sums per-tape and delivers to the sink. Keeping `processDeviceInputs` as well would double-process strips (corrupting LUFS integration and wasting CPU). The legacy `processBuffer` per-device-channel tape path is the M3 single-tape model the routing graph supersedes. So Step 2 + Step 2b collapse into one `renderInputGraph` call. DirectLayer monitoring (Step 3) and OutputMixer (Step 4) are untouched; `directOut` is passed null/0 (no hardware-output routes are active by default — that path is exercised later).

- [ ] **Step 1: Write the failing test**

Add to the audio-callback test file a case that builds an `InputMixer` with one registered, tape-routed stereo channel, binds a `RecordingTapeSink`-style fake (mirror the one at `tests/InputMixerTests.cpp:399`), drives one `AudioCallback` block, and asserts the sink received a block for the primary tape:

```cpp
TEST_CASE ("AudioCallback drives renderInputGraph: a tape-routed channel reaches the sink", "[audio-callback][render]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);   // stereo from device ch 0/1
    REQUIRE (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Tape); // default → primary tape

    struct Sink : ida::ITapeSink {
        bool got = false; std::int64_t tapeId = 0;
        void deliverTapeBlock (ida::TapeId t, const float*, const float*, int n) noexcept override
        { got = (n > 0); tapeId = t.value(); }
    } sink;
    mixer.setTapeSink (&sink);

    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setInputMixer (&mixer);

    const int n = 64;
    std::vector<float> in0 (n, 0.3f), in1 (n, 0.3f);
    std::vector<float> out0 (n, 0.0f), out1 (n, 0.0f);
    const float* ins[2]  { in0.data(), in1.data() };
    float*       outs[2] { out0.data(), out1.data() };
    cb.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, n, {});

    CHECK (sink.got);
    CHECK (sink.tapeId == 1);   // primary tape
}
```

(Adjust includes / namespace to match the existing audio-callback test file. If `addChannel`'s default main-out is not Tape, set it explicitly with `mixer.setChannelMainOutToTape (ch);` and keep the `REQUIRE` as documentation.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[audio-callback][render]"`
Expected: FAIL — `sink.got` is false (the callback still calls the legacy `processBuffer`/`processDeviceInputs`, which do not deliver to `tapeSink_`).

- [ ] **Step 3: Make the change**

In `audio/src/AudioCallback.cpp`, replace the Step 2 + Step 2b block (currently lines ~163-176):

```cpp
    // Step 2: per-channel InputMixer dispatch. Runs regardless of the
    // monitoring gate — tape recording is independent of monitoring.
    dispatchInputMixer (inputMixer_,
                        inputChannelData,
                        numInputChannels,
                        numSamples);

    // Step 2b: Input Mixer strip processing + metering. ...
    if (inputMixer_ != nullptr)
        inputMixer_->processDeviceInputs (inputChannelData, numInputChannels, numSamples);
```
with:
```cpp
    // Step 2: full input routing graph (tape subsystem slice 3). One pass does
    // strip processing + per-strip peak/LUFS metering + graph routing + per-tape
    // summing + ITapeSink delivery, superseding the M3 processBuffer path and the
    // separate processDeviceInputs metering pass (which would double-process
    // strips). Tape recording is independent of the monitoring gate. directOut is
    // null/0: hardware-output routing is not active by default and DirectLayer
    // (Step 3) owns monitoring.
    if (inputMixer_ != nullptr)
        inputMixer_->renderInputGraph (inputChannelData, numInputChannels,
                                       nullptr, 0, numSamples);
```

Then delete the now-unused `dispatchInputMixer` anonymous-namespace function (lines ~38-60) — it has no other caller. (Verify with `grep -n dispatchInputMixer audio/src/AudioCallback.cpp` → only the definition remains; remove it. Leave `dispatchDirectLayer` / `dispatchOutputMixer` untouched.)

- [ ] **Step 4: Run the new test + the full suite**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[audio-callback]"`
Expected: PASS, including the new `[render]` case.

Run: `ctest --test-dir build`
Expected: the suite's prior pass count holds, minus any test that asserted the *legacy* `processBuffer`/`processDeviceInputs` call path from the callback. If such a test exists, it must be retargeted to `renderInputGraph` (the metering behavior it asserts is preserved by `renderInputGraph`'s `strip->process`), not deleted silently — note the change in the commit body.

- [ ] **Step 5: Commit**

```bash
git add audio/src/AudioCallback.cpp tests/AudioCallbackTests.cpp
git commit -m "feat: renderInputGraph drives the live audio callback (tape slice 3); retire legacy processBuffer/processDeviceInputs path"
```

---

## Task 5: Construct + wire FlacTapeSink in MainComponent (operator-verified)

**Files:**
- Modify: `app/MainComponent.h` (member + tapes-dir helper decl)
- Modify: `app/MainComponent.cpp` (helper def; construct sink; setTapeSink; sample rate)

> GUI/runtime wiring — **operator-verified, not unit-tested** (per the project's MainComponent convention). Compile + a clean rebuild + the operator confirming a routed input produces a growing `tape-1.flac`.

- [ ] **Step 1: Add the member and the tapes-dir helper declaration**

In `app/MainComponent.h`, near the `notificationBus_` members (around line 184), add the include `#include "sirius/FlacTapeSink.h"` at the top with the other engine/audio includes, and the member **after** `notificationBus_` but **before** `audioCallback_`-related teardown ordering matters: declare it so it is destroyed *after* `audioCallback_` is torn down. Place it immediately after `audioCallback_`'s declaration is NOT correct (it must outlive the callback's last use). Declare it right after `notificationBus_`:

```cpp
    // Tape subsystem slice 3 — the live per-tape FLAC recorder behind ITapeSink.
    // Declared after notificationBus_ and before inputMixer_ is given the sink,
    // so it outlives every audio callback that can deliver to it.
    std::unique_ptr<ida::FlacTapeSink> flacTapeSink_;
```

And declare a private helper:

```cpp
    static juce::File tapesDirectory();
```

- [ ] **Step 2: Define the tapes-dir helper**

In `app/MainComponent.cpp`, beside `calibrationSidecarFile()` (around line 48):

```cpp
juce::File MainComponent::tapesDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("IDA")
        .getChildFile ("tapes");
}
```

(If `calibrationSidecarFile()` is a file-local free function rather than a member, mirror that — make `tapesDirectory()` a file-local free function in the same anonymous namespace instead of a static member, and drop the `MainComponent::` qualifier + the header decl. Match the existing pattern exactly.)

- [ ] **Step 3: Construct + wire the sink**

In the `MainComponent` constructor, immediately after `inputMixer_->setNotificationBus (notificationBus_.get());` (line 1084) and before `audioCallback_` is created (line 1086):

```cpp
    // Tape subsystem slice 3 — bind the live FLAC recorder. Sample rate is set
    // from the device setup just below (and re-set on device change); 256 queue
    // slots cover the worst-case touched-tapes-per-block burst with headroom.
    flacTapeSink_ = std::make_unique<ida::FlacTapeSink> (
        tapesDirectory(),
        audioDeviceManager_.getAudioDeviceSetup().sampleRate,
        256);
    inputMixer_->setTapeSink (flacTapeSink_.get());
```

- [ ] **Step 4: Keep the sink's sample rate correct on device (re)start**

Find where `MainComponent` initializes/changes the audio device (the `setAudioChannels` / `initialiseWithDefaultDevices` / device-setup path, or its `changeListenerCallback` for `audioDeviceManager_`). Immediately after the device is (re)configured — and before/around the `addAudioCallback` — set the rate:

```cpp
    if (flacTapeSink_ != nullptr)
        flacTapeSink_->setSampleRate (audioDeviceManager_.getAudioDeviceSetup().sampleRate);
```

If there is no existing device-change hook, add the call once after the initial device setup in the constructor; a sample-rate change mid-session is a documented follow-on (note in `todo.md`) rather than a slice-3 requirement.

- [ ] **Step 5: Verify teardown order**

Confirm in `app/MainComponent.h` that `flacTapeSink_` is declared **before** `audioCallback_` (members destroy in reverse declaration order, so `audioCallback_` — and thus any in-flight `renderInputGraph` → `deliverTapeBlock` — tears down first, then the sink). If `audioCallback_` is declared earlier, the existing destructor ordering already detaches the callback explicitly; verify the destructor calls `audioDeviceManager_.removeAudioCallback(audioCallback_.get())` before members unwind. If it does, declaration order is moot for safety but keep the sink alive across that call.

- [ ] **Step 6: Clean rebuild the app**

Run:
```bash
rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --target IDA && cmake --build build --target IdaTests
```
Expected: both targets build clean.

- [ ] **Step 7: Run the full suite**

Run: `ctest --test-dir build`
Expected: green (baseline + the new `[flac-tape-sink]` and `[audio-callback][render]` cases; the one documented `MainComponentPluginEditorTests_NOT_BUILT` Not-Run remains).

- [ ] **Step 8: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: MainComponent owns + binds FlacTapeSink to the input mixer (tape slice 3)"
```

---

## Task 6: Operator eyes-on + fidelity-boundary todo + handoff

**Files:**
- Modify: `todo.md`
- Modify: `continue.md`

- [ ] **Step 1: Record the fidelity boundary + deferrals in todo.md**

Append a `### 2026-05-21 — tape slice 3` entry:
- Files: `audio/src/FlacTapeSink.cpp`
- Deferred: (1) **float→24-bit FLAC quantization** is the capture fidelity floor; bit-exact-float archival (§15) would need a different codec. (2) **SHA-256 content-addressing + `TapeId→hash` manifest** = session-close archival (pairs with M8 S7–8). (3) **Per-tier sub-FLAC-block flush** (§17.8 ~1–3 ms Lavish) needs direct libFLAC; current granularity ≈ one FLAC block (~85 ms @48 k). (4) **§17.8 scan-and-truncate on reopen** — no tape reader/playback consumer exists yet; add with the read path. (5) **Mid-session sample-rate change** re-prepare of the sink. (6) **`TapePool` ↔ sink lifecycle** (open/closeTape on add/remove) lands with the slice-4 Tapes UI.
- Why: slice 3 scope is "make routing record"; these are read-side / archival / UI concerns.
- Needed to finish each: their own slice as noted.

- [ ] **Step 2: Operator eyes-on (manual — the agent cannot confirm GUI/audio)**

Build + launch (authorized): `cmake --build build --target IDA && open "build/app/IDA_artefacts/Release/IDA.app"`. Operator confirms: with live input on a strip routed to the primary tape, `~/Library/Application Support/IDA/tapes/tape-1.flac` appears and grows, and decodes/plays externally (e.g. `afplay` or import into a DAW). Record the operator's verdict in the commit / continue.md.

- [ ] **Step 3: Update continue.md**

Rewrite the RESUME-HERE block: slice 3 shipped (commit range, ctest count, clean-rebuild result, operator eyes-on verdict). Next = **slice 4 (Tapes UI + Input Mixer destination picker + blank-area creation gesture)**, with its first moves — and the slice-4 wiring of `TapePool` ↔ `FlacTapeSink` (open/closeTape on tape add/remove, bracketed by remove/addAudioCallback) plus surfacing `droppedBlockCount` via the NotificationBus.

- [ ] **Step 4: Commit + push**

```bash
git add todo.md continue.md
git commit -m "docs: tape slice 3 shipped — handoff to slice 4 (Tapes UI + destination picker)"
git push origin master
```

---

## Self-Review

- **Spec coverage:** Slice-3 spec requires (a) route→tape actually records [Tasks 2,4,5], (b) per-tape append-only FLAC under `<Sirius>/tapes` [Task 2 + Task 5 tapes dir], (c) RT-safe `ITapeSink` over a worker [Task 2], (d) SHA-256/manifest deferred [documented Task 6]. Covered.
- **Type consistency:** `FlacTapeSink` ctor `(juce::File, double, std::size_t)`, `setSampleRate(double)`, `closeTape(TapeId)`, `deliverTapeBlock(TapeId,const float*,const float*,int)` — identical across header (Task 2), tests (Tasks 2,3), and MainComponent (Task 5). `TapeId::value()` used for all int64 keys (matches `core/include/ida/TapeId.h`).
- **Placeholder scan:** every code step shows full code; no TBD/TODO in code (the deferrals live in `todo.md`, per project rule).
- **RT-safety:** `deliverTapeBlock` does only stack-POD construction + one wait-free `queue_.push` + a relaxed atomic increment; no alloc/lock/I/O/notify on the audio thread. Worker owns all FLAC state; no shared-state lock. Matches `docs/RT_SAFETY_CONTRACT.md`.
- **Risk flagged:** Task 4 is the operator-visible behavior change (metering + capture now flow through `renderInputGraph`); Task 5/6 are operator-verified. The `juce_audio_formats` raw-stream-flush idiom (Task 2) is the one fragile JUCE-ownership detail — keep the `rawStream` pointer non-owning and never touch it after the writer is erased.
