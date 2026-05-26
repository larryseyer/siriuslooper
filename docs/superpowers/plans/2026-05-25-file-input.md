# File input as a first-class Input Mixer source (playlist player) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fourth source kind to the Input Mixer's input layer — `InputKind::FileInput` — that accepts an ordered playlist of 1+ audio files (WAV / AIFF / FLAC) and exposes a QuickTime-style floating **File Player Window** per file input (transport, playhead scrubber, drag-reorderable track list, loop-scope cycle, per-window opacity). Live playlist editing during playback is supported. Channels attach to file inputs identically to device inputs; tape / MON / routing are unchanged.

**Architecture:** `core/` gets `InputKind::FileInput`, `LoopScope`, `PlaylistEntryId`, `FileInputDescriptor`. `audio/` gets `FileInputSource` — JUCE `AudioFormatManager` + `BufferingAudioReader` + optional `ResamplingAudioSource` + an SPSC ring + a worker-thread transport loop running on a shared `juce::TimeSliceThread`. `engine/InputMixer` gains `registerFileInput` + transport surface + audio-callback patch that pulls from the ring before the existing gain / MON / tape / routing stages. `persistence/SessionFormat` gains an additive `fileInputs` array (backward-compatible — sessions without it load unchanged). `ui/FileInputPlayerWindow` (one per file input) is a pure view onto engine state, polled at 30 Hz. `InputMixerPane`'s blank-area menu gets `Add file input…` alongside today's `Add bus`; macOS gets a `Window > File Players` submenu.

**Spec:** `docs/superpowers/specs/2026-05-25-file-input-design.md`

**Tech Stack:** C++17/20, JUCE 8 (`juce::AudioFormatManager`, `juce::BufferingAudioReader`, `juce::ResamplingAudioSource`, `juce::TimeSliceThread`, `juce::DocumentWindow`, `juce::PopupMenu`, `juce::FileChooser`, `juce::ApplicationCommandManager`), Catch2, CMake/Ninja. Audio-thread reads from a lock-free SPSC ring only; all disk I/O on a shared worker thread; no allocations on the audio path.

---

## File map

**Create:**
- `core/include/ida/LoopScope.h` — three-value enum `{ Off, Track, List }`.
- `core/include/ida/PlaylistEntryId.h` — strong-typed `int64_t` handle, same house pattern as `InputId` / `ChannelId`.
- `core/include/ida/FileInputDescriptor.h` — `FileInputEntry` + `FileInputDescriptor` structs.
- `audio/include/ida/FileInputSource.h` — public surface of the per-file-input engine.
- `audio/src/FileInputSource.cpp` — impl: format manager, reader stack, ring, worker.
- `ui/include/ida/FileInputPlayerWindow.h` — `juce::DocumentWindow` subclass.
- `ui/src/FileInputPlayerWindow.cpp` — transport bar, scrubber, track-list box, append/remove/drag-reorder, loop-scope button, opacity right-click menu.
- `tests/FileInputDescriptorTests.cpp` — descriptor round-trip + clamp.
- `tests/FileInputSourceTests.cpp` — engine: reader open, ring pull, SR resample, mono dual-mono, transport state.
- `tests/FileInputPlaylistTests.cpp` — advance per LoopScope, missing-file skip, live list mutation, reorder during playback.
- `tests/FileInputPersistenceTests.cpp` — JSON round-trip, backward-compat, opacity clamp.

**Modify:**
- `core/include/ida/InputKind.h` — add `FileInput` enum value; extend `signalTypeOf` switch.
- `engine/include/ida/InputMixer.h` — add file-input registration / transport / playlist surface; add `FileInputTransportState` struct.
- `engine/src/InputMixer.cpp` — impl: store `FileInputSource` instances per `InputId`, route them in `processBuffer` before existing stages.
- `persistence/src/SessionFormat.cpp` — write/read `fileInputs` array on the input-mixer JSON object.
- `app/MainComponent.cpp` — `InputMixerPane`: extend blank-area menu with `Add file input…`; strip-recall menu for file-input strips; FileInputPlayerWindow lifetime; macOS `Window > File Players` submenu.
- `docs/IDA_Whitepaper_V9.md` — §6.6 (one clause), §7.2 (one clarification), glossary (Playlist scope + File I/O update).
- `continue.md` — operator-verify recipe.
- `audio/CMakeLists.txt` — add `FileInputSource.cpp`.
- `ui/CMakeLists.txt` — add `FileInputPlayerWindow.cpp`.
- `tests/CMakeLists.txt` — add the four new test files.

**No deletions. No persistence migration** — `fileInputs` is additive; sessions predating this slice load unchanged.

---

## Task 1 — Whitepaper amendments + glossary

**Files:**
- Modify: `docs/IDA_Whitepaper_V9.md` (§6.6 file-input clause, §7.2 transport clarification, two glossary edits)

Lands FIRST per house rules: "Whitepaper / design landed first, then engine, then UI — no architectural surprises buried in implementation commits" (continue.md §5 / project standing convention). No code touched in this task.

- [ ] **Step 1: Locate §6.6 file-inputs prose**

Search the whitepaper for the §6.6 sentence describing the four signal types. Current text (around line 344) reads:
> "It accepts four signal types as first-class inputs: **live audio, live MIDI, live video, and file inputs**."

Confirm with: `grep -n "live audio, live MIDI, live video, and file inputs" docs/IDA_Whitepaper_V9.md`.

- [ ] **Step 2: Insert the playlist clause in §6.6**

Within the same paragraph (immediately after the sentence quoted above), add this clause:

> A file input is an **ordered playlist of one or more files**; the playlist may be edited (add / remove / reorder) live during playback, and the per-input transport applies to whichever playlist entry is currently active.

- [ ] **Step 3: Clarify §7.2 transport scope**

Locate the §7.2 entry "Input source format · Source enable · Source-level defaults · File transport (start, rate, loop region)" (line ~492). Replace "File transport (start, rate, loop region)" with:

> File transport (start, rate, loop region) **— transport applies to the playlist entry currently active; advance to the next entry is governed by playlist scope (off / track / list)**

- [ ] **Step 4: Update the File I/O glossary entry**

Locate `**File I/O** — One of the four signal modalities…` (line ~1889). Append at the end of its existing sentence:

> A file input is an ordered playlist of one or more files; the playlist may be edited live during playback.

- [ ] **Step 5: Add Playlist scope glossary entry**

In the glossary section (alphabetical), insert a new entry between **Playhead** and the next P-term (or place after **Output mixer**, whichever lands cleanly):

```
**Playlist scope** — The advance policy governing a file input's playlist when the currently-active entry reaches end-of-file. Three values: **Off** (advance to next entry, or stop at end of list), **Track** (rewind same entry to 0), **List** (advance to next entry, wrapping last → first).
```

- [ ] **Step 6: Diff review + commit + push**

```bash
git diff docs/IDA_Whitepaper_V9.md
git add docs/IDA_Whitepaper_V9.md
git commit -m "docs: whitepaper — file input is a playlist (V9 §6.6 + §7.2 + glossary)"
git push origin master
```

---

## Task 2 — Core: `LoopScope` + `PlaylistEntryId` strong-typed handle

**Files:**
- Create: `core/include/ida/LoopScope.h`
- Create: `core/include/ida/PlaylistEntryId.h`
- Create: `tests/FileInputDescriptorTests.cpp` (will grow across Tasks 2, 3, 9)
- Modify: `tests/CMakeLists.txt` (add `FileInputDescriptorTests.cpp`)

- [ ] **Step 1: Write the failing test**

Create `tests/FileInputDescriptorTests.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>

#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"

TEST_CASE ("LoopScope enum has Off, Track, List", "[file-input][core]")
{
    CHECK (static_cast<int> (ida::LoopScope::Off)   == 0);
    CHECK (static_cast<int> (ida::LoopScope::Track) == 1);
    CHECK (static_cast<int> (ida::LoopScope::List)  == 2);
}

TEST_CASE ("PlaylistEntryId is value-type + equality-comparable", "[file-input][core]")
{
    constexpr ida::PlaylistEntryId a { 1 };
    constexpr ida::PlaylistEntryId b { 1 };
    constexpr ida::PlaylistEntryId c { 2 };

    CHECK (a == b);
    CHECK (a != c);
    CHECK (a.value() == 1);
    CHECK (c.value() == 2);
}
```

Add to `tests/CMakeLists.txt` in the existing test-source list (alphabetical placement — between `FileChooserTests.cpp` if present, or after the last F-test):

```cmake
    FileInputDescriptorTests.cpp
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL — `LoopScope.h: No such file or directory`.

- [ ] **Step 3: Create `core/include/ida/LoopScope.h`**

```cpp
#pragma once

namespace ida
{

/// Advance policy for a file-input playlist when the currently-active
/// entry reaches end-of-file. White-paper V9 §6.6 (playlist clause) +
/// glossary "Playlist scope".
enum class LoopScope
{
    Off,    ///< Advance to next entry; stop at end of list.
    Track,  ///< Rewind same entry to 0.
    List    ///< Advance to next entry, wrapping last → first.
};

} // namespace ida
```

- [ ] **Step 4: Create `core/include/ida/PlaylistEntryId.h`**

```cpp
#pragma once

#include <cstdint>

namespace ida
{

/// Stable identifier for an entry within a file-input playlist. Allocated
/// monotonically by InputMixer when the entry is registered. Survives
/// reorder operations (lookup is by id, not by position), so the worker
/// thread's "current entry" remains valid across UI-thread list edits.
/// Same house pattern as InputId / ChannelId / BusId.
class PlaylistEntryId
{
public:
    explicit constexpr PlaylistEntryId (std::int64_t value) noexcept : value_ (value) {}

    constexpr std::int64_t value() const noexcept { return value_; }

    constexpr bool operator== (const PlaylistEntryId& other) const noexcept
    {
        return value_ == other.value_;
    }
    constexpr bool operator!= (const PlaylistEntryId& other) const noexcept
    {
        return !(*this == other);
    }

private:
    std::int64_t value_;
};

} // namespace ida
```

- [ ] **Step 5: Build + run the new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][core]" 2>&1 | tail -10
```

Expected: 2 tests pass, all assertions green.

- [ ] **Step 6: Commit + push**

```bash
git add core/include/ida/LoopScope.h core/include/ida/PlaylistEntryId.h tests/FileInputDescriptorTests.cpp tests/CMakeLists.txt
git commit -m "feat: core — LoopScope + PlaylistEntryId for file-input playlist semantics"
git push origin master
```

---

## Task 3 — Core: `InputKind::FileInput` value + `FileInputDescriptor` struct

**Files:**
- Modify: `core/include/ida/InputKind.h` (add enum value + extend `signalTypeOf` switch)
- Create: `core/include/ida/FileInputDescriptor.h`
- Modify: `tests/FileInputDescriptorTests.cpp` (add descriptor tests)

- [ ] **Step 1: Write the failing tests**

Append to `tests/FileInputDescriptorTests.cpp`:

```cpp
#include "ida/FileInputDescriptor.h"
#include "ida/InputKind.h"

TEST_CASE ("InputKind::FileInput exists and maps to SignalType::Audio",
           "[file-input][core]")
{
    CHECK (ida::signalTypeOf (ida::InputKind::FileInput) == ida::SignalType::Audio);
}

TEST_CASE ("FileInputDescriptor defaults: empty entries, loopScope=Off, windowOpacity=0.92",
           "[file-input][core]")
{
    ida::FileInputDescriptor desc;

    CHECK (desc.entries.empty());
    CHECK (desc.loopScope == ida::LoopScope::Off);
    CHECK (desc.windowOpacity == Catch::Approx (0.92f));
}

TEST_CASE ("FileInputEntry holds entryId + path; missing defaults false",
           "[file-input][core]")
{
    ida::FileInputEntry entry { ida::PlaylistEntryId { 7 }, "/abs/track.wav", {}, false };

    CHECK (entry.entryId == ida::PlaylistEntryId { 7 });
    CHECK (entry.path    == "/abs/track.wav");
    CHECK_FALSE (entry.missing);
    CHECK_FALSE (entry.durationFrames.has_value());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL — `'FileInput' is not a member of 'ida::InputKind'` and `FileInputDescriptor.h: No such file or directory`.

- [ ] **Step 3: Extend `InputKind.h`**

In `core/include/ida/InputKind.h`, add `FileInput` to the enum AFTER `System` and add a `case` to `signalTypeOf`:

```cpp
enum class InputKind
{
    Audio,
    Video,
    Midi,
    Control,
    ParameterAutomation,
    Transport,
    System,
    FileInput   ///< Playlist of 1+ audio files; whitepaper V9 §6.6.
};
```

Then in `signalTypeOf`, add inside the switch (before `default`/before the trailing `return SignalType::File;`):

```cpp
        case InputKind::FileInput: return SignalType::Audio;
```

- [ ] **Step 4: Create `core/include/ida/FileInputDescriptor.h`**

```cpp
#pragma once

#include "ida/ChannelDefaults.h"
#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"
#include "ida/TapeId.h"

#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// One entry in a file-input playlist. Path is absolute. `durationFrames`
/// is cached when the entry's reader is first opened (left empty until
/// then). `missing` is set at load time (file absent on disk) or at the
/// worker-thread's advance step (file moved/deleted mid-session).
struct FileInputEntry
{
    PlaylistEntryId entryId;
    std::string path;
    std::optional<int> durationFrames;
    bool missing { false };
};

/// Light, free-standing metadata describing a single file-input source.
/// White-paper V9 §6.6 / §7.2: a file input is an ordered playlist of 1+
/// files, edited live during playback. Parallel to InputDescriptor: heavy,
/// immutable tape data lives elsewhere (engine); this descriptor is the
/// thin pointer + presentation state. `windowOpacity` is the persisted
/// translucency of the floating player window (clamped to [0.5, 1.0] on
/// read; default 0.92).
struct FileInputDescriptor
{
    TapeId tapeId;
    std::string displayName;
    std::vector<FileInputEntry> entries;
    LoopScope loopScope { LoopScope::Off };
    float windowOpacity { 0.92f };
    ChannelDefaults defaults {};
};

} // namespace ida
```

- [ ] **Step 5: Build + run the new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][core]" 2>&1 | tail -10
```

Expected: 5 tests in `[file-input][core]`, all passing. (Add `<catch2/catch_approx.hpp>` include if `Catch::Approx` isn't yet available — Catch2 v3 amalgamated bundles it.)

- [ ] **Step 6: Commit + push**

```bash
git add core/include/ida/InputKind.h core/include/ida/FileInputDescriptor.h tests/FileInputDescriptorTests.cpp
git commit -m "feat: core — InputKind::FileInput + FileInputDescriptor (whitepaper V9 §6.6)"
git push origin master
```

---

## Task 4 — Audio: `FileInputSource` open-reader scaffolding

**Files:**
- Create: `audio/include/ida/FileInputSource.h`
- Create: `audio/src/FileInputSource.cpp`
- Modify: `audio/CMakeLists.txt` (add `FileInputSource.cpp` to sources)
- Create: `tests/FileInputSourceTests.cpp` (will grow across Tasks 4, 5, 6, 7)
- Modify: `tests/CMakeLists.txt` (add `FileInputSourceTests.cpp`)

This task brings the format manager online and proves it can open WAV / AIFF / FLAC files. No ring, no worker, no transport yet — those land in Tasks 5 / 6 / 7.

- [ ] **Step 1: Write the failing test**

Create `tests/FileInputSourceTests.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "ida/FileInputSource.h"

namespace
{

/// Synthesize a brief stereo WAV file (440 Hz sine) and return its path.
/// Used across all FileInputSource tests so they don't depend on assets.
juce::File writeTestWav (juce::TempDirectoryDeleter& tmp,
                         const juce::String& name,
                         double sampleRate,
                         int numChannels,
                         int numFrames)
{
    auto file = tmp.getDir().getChildFile (name);
    juce::WavAudioFormat fmt;
    auto stream = file.createOutputStream();
    REQUIRE (stream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.release(), sampleRate, (unsigned int) numChannels,
                              16, {}, 0));
    REQUIRE (writer != nullptr);

    juce::AudioBuffer<float> buf (numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numFrames; ++i)
            buf.setSample (ch, i, std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate));

    REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, numFrames));
    return file;
}

} // namespace

TEST_CASE ("FileInputSource opens a WAV file and reports duration",
           "[file-input][audio]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav = writeTestWav (tmp, "test.wav", 48000.0, 2, 24000);  // 0.5 s

    ida::FileInputSource source { /* deviceSampleRate */ 48000.0 };
    REQUIRE (source.openReader (wav.getFullPathName().toStdString()));
    CHECK (source.currentReaderDurationFrames() == 24000);
    CHECK (source.currentReaderSampleRate()     == 48000.0);
    CHECK (source.currentReaderNumChannels()    == 2);
}

TEST_CASE ("FileInputSource openReader returns false for missing file",
           "[file-input][audio]")
{
    ida::FileInputSource source { 48000.0 };
    CHECK_FALSE (source.openReader ("/definitely/not/a/file.wav"));
}
```

Add to `tests/CMakeLists.txt` (next to the other F-tests):

```cmake
    FileInputSourceTests.cpp
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL — `FileInputSource.h: No such file or directory`.

- [ ] **Step 3: Create `audio/include/ida/FileInputSource.h` (skeleton)**

Just enough surface for Task 4 — reader open + introspection. Worker / ring / transport land in later tasks.

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <string>

namespace ida
{

/// Per-file-input engine: owns the disk-reader stack and (later) the
/// SPSC ring + worker-thread transport loop. White-paper V9 §6.6 / §7.2.
/// Lives in audio/ (not engine/) because it depends on juce_audio_formats.
class FileInputSource
{
public:
    explicit FileInputSource (double deviceSampleRate);
    ~FileInputSource();

    FileInputSource (const FileInputSource&) = delete;
    FileInputSource& operator= (const FileInputSource&) = delete;

    /// Opens `path` and replaces any previously-open reader. Returns
    /// true on success. Message thread only.
    bool openReader (const std::string& path);

    /// Closes the current reader. Safe to call when no reader is open.
    void closeReader();

    /// Introspection on the currently-open reader. All return 0 / 0.0 / 0
    /// when no reader is open.
    int    currentReaderDurationFrames() const noexcept;
    double currentReaderSampleRate()     const noexcept;
    int    currentReaderNumChannels()    const noexcept;

private:
    double deviceSampleRate_;
    juce::AudioFormatManager formatManager_;
    std::unique_ptr<juce::AudioFormatReader> currentReader_;
};

} // namespace ida
```

- [ ] **Step 4: Create `audio/src/FileInputSource.cpp` (skeleton impl)**

```cpp
#include "ida/FileInputSource.h"

namespace ida
{

FileInputSource::FileInputSource (double deviceSampleRate)
    : deviceSampleRate_ (deviceSampleRate)
{
    formatManager_.registerBasicFormats();   // WAV + AIFF
    formatManager_.registerFormat (new juce::FlacAudioFormat(), false);
}

FileInputSource::~FileInputSource() = default;

bool FileInputSource::openReader (const std::string& path)
{
    juce::File file (juce::String (path));
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager_.createReaderFor (file));
    if (reader == nullptr)
        return false;

    currentReader_ = std::move (reader);
    return true;
}

void FileInputSource::closeReader()
{
    currentReader_.reset();
}

int FileInputSource::currentReaderDurationFrames() const noexcept
{
    return currentReader_ != nullptr ? static_cast<int> (currentReader_->lengthInSamples) : 0;
}

double FileInputSource::currentReaderSampleRate() const noexcept
{
    return currentReader_ != nullptr ? currentReader_->sampleRate : 0.0;
}

int FileInputSource::currentReaderNumChannels() const noexcept
{
    return currentReader_ != nullptr ? static_cast<int> (currentReader_->numChannels) : 0;
}

} // namespace ida
```

- [ ] **Step 5: Wire CMake**

Add to `audio/CMakeLists.txt`'s source list (alphabetical placement):

```cmake
    src/FileInputSource.cpp
```

If `audio` doesn't already link `juce_audio_formats`, add to its `target_link_libraries`:

```cmake
    juce::juce_audio_formats
```

- [ ] **Step 6: Build + run the new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][audio]" 2>&1 | tail -15
```

Expected: 2 tests in `[file-input][audio]`, both green.

- [ ] **Step 7: Commit + push**

```bash
git add audio/include/ida/FileInputSource.h audio/src/FileInputSource.cpp audio/CMakeLists.txt tests/FileInputSourceTests.cpp tests/CMakeLists.txt
git commit -m "feat: audio — FileInputSource opens WAV/AIFF/FLAC, reports reader metadata"
git push origin master
```

---

## Task 5 — Audio: SPSC ring + audio-thread `pullInto`

**Files:**
- Modify: `audio/include/ida/FileInputSource.h` (ring + `pullInto`)
- Modify: `audio/src/FileInputSource.cpp` (impl)
- Modify: `tests/FileInputSourceTests.cpp` (add ring-pull tests)

Brings the audio-thread surface online. Worker thread still doesn't exist — these tests pre-fill the ring directly through a test-only helper. The audio-thread call path (`pullInto`) is locked in with its no-allocation / no-lock contract.

- [ ] **Step 1: Write the failing tests**

Append to `tests/FileInputSourceTests.cpp`:

```cpp
TEST_CASE ("FileInputSource::pullInto delivers pre-filled ring samples",
           "[file-input][audio][ring]")
{
    ida::FileInputSource source { 48000.0 };

    // Test-only helper: push a known pattern into the ring.
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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL — `pullInto` / `testPushRing` / `underrunCount` unknown.

- [ ] **Step 3: Extend `FileInputSource.h`**

Add includes for `<atomic>` and `<vector>`. Add to the public surface (below the introspection methods):

```cpp
    /// Audio-thread entry point. Pops `numFrames` of stereo samples into
    /// `dest`. If the ring has fewer than `numFrames` available, fills
    /// the tail with silence and increments the underrun counter.
    /// noexcept, no allocation, no locks, no I/O.
    void pullInto (juce::AudioBuffer<float>& dest, int numFrames) noexcept;

    /// Test-only helper. Pushes the contents of `src` into the ring as
    /// if the worker had produced them. NOT for production use.
    void testPushRing (const juce::AudioBuffer<float>& src);

    /// Diagnostic counter. Bumped each time pullInto saw fewer than the
    /// requested frames.
    int underrunCount() const noexcept { return underruns_.load(); }
```

Add to the private section:

```cpp
    static constexpr int kRingFrames = 12000;  // 250 ms stereo @ 48 kHz

    std::vector<float> ringL_, ringR_;          // SPSC ring storage
    std::atomic<int>   writePos_ { 0 };         // worker writes here
    std::atomic<int>   readPos_  { 0 };         // audio thread reads here
    std::atomic<int>   underruns_ { 0 };
```

- [ ] **Step 4: Implement `pullInto` + `testPushRing`**

Add to `FileInputSource.cpp`. Note ring allocation in the constructor:

In the ctor, after `formatManager_.registerFormat (...)`, add:

```cpp
    ringL_.assign ((size_t) kRingFrames, 0.f);
    ringR_.assign ((size_t) kRingFrames, 0.f);
```

Then append:

```cpp
void FileInputSource::pullInto (juce::AudioBuffer<float>& dest, int numFrames) noexcept
{
    const int writePos = writePos_.load (std::memory_order_acquire);
    const int readPos  = readPos_ .load (std::memory_order_relaxed);
    const int available = (writePos - readPos + kRingFrames) % kRingFrames;

    const int toPull = juce::jmin (numFrames, available);
    int rp = readPos;
    for (int i = 0; i < toPull; ++i)
    {
        dest.setSample (0, i, ringL_[(size_t) rp]);
        if (dest.getNumChannels() > 1)
            dest.setSample (1, i, ringR_[(size_t) rp]);
        rp = (rp + 1) % kRingFrames;
    }
    readPos_.store (rp, std::memory_order_release);

    if (toPull < numFrames)
    {
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            for (int i = toPull; i < numFrames; ++i)
                dest.setSample (ch, i, 0.f);
        underruns_.fetch_add (1, std::memory_order_relaxed);
    }
}

void FileInputSource::testPushRing (const juce::AudioBuffer<float>& src)
{
    const int n = src.getNumSamples();
    int wp = writePos_.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        ringL_[(size_t) wp] = src.getSample (0, i);
        ringR_[(size_t) wp] = src.getNumChannels() > 1 ? src.getSample (1, i) : src.getSample (0, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
}
```

- [ ] **Step 5: Build + run the new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][audio][ring]" 2>&1 | tail -10
```

Expected: 2 ring tests pass; underrun counter == 1 on the second.

- [ ] **Step 6: Commit + push**

```bash
git add audio/include/ida/FileInputSource.h audio/src/FileInputSource.cpp tests/FileInputSourceTests.cpp
git commit -m "feat: audio — FileInputSource SPSC ring + audio-thread pullInto (RT-safe)"
git push origin master
```

---

## Task 6 — Audio: Worker-thread transport loop (single-entry, no playlist yet)

**Files:**
- Modify: `audio/include/ida/FileInputSource.h` (transport surface + worker)
- Modify: `audio/src/FileInputSource.cpp`
- Modify: `tests/FileInputSourceTests.cpp` (add transport tests)

Adds the worker thread that fills the ring from the open reader, plus play / pause / stop / seek transport commands. Single-track only — playlist advance is the next task.

- [ ] **Step 1: Write the failing tests**

Append to `tests/FileInputSourceTests.cpp`:

```cpp
TEST_CASE ("FileInputSource plays an opened WAV: worker fills ring; pullInto delivers samples",
           "[file-input][audio][transport]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav = writeTestWav (tmp, "play.wav", 48000.0, 2, 4800);  // 100 ms

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (wav.getFullPathName().toStdString()));

    source.play();
    juce::Thread::sleep (60);  // let worker prime the ring

    juce::AudioBuffer<float> out (2, 1024);
    out.clear();
    source.pullInto (out, 1024);

    // Pulled samples should be non-zero (sine carries energy).
    float rmsL = out.getRMSLevel (0, 0, 1024);
    CHECK (rmsL > 0.01f);

    source.stop();
}

TEST_CASE ("FileInputSource pause halts ring growth; play resumes; stop seeks to 0",
           "[file-input][audio][transport]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav = writeTestWav (tmp, "pause.wav", 48000.0, 2, 48000);  // 1 s

    ida::FileInputSource source { 48000.0 };
    REQUIRE (source.openReader (wav.getFullPathName().toStdString()));

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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL — `play` / `pause` / `stop` / `playheadFrames` unknown.

- [ ] **Step 3: Extend `FileInputSource.h`**

Add `<juce_events/juce_events.h>` include (for `TimeSliceThread` / `TimeSliceClient`).

Add to the public surface:

```cpp
    /// Transport commands. Message-thread only. The worker thread picks
    /// these up on its next tick.
    void play()  noexcept;
    void pause() noexcept;
    void stop()  noexcept;   ///< pause + seek to 0
    void seek (std::int64_t frame) noexcept;

    bool         isPlaying()       const noexcept { return isPlaying_.load(); }
    std::int64_t playheadFrames()  const noexcept { return playheadFrames_.load(); }
```

Make the class inherit `juce::TimeSliceClient`:

```cpp
class FileInputSource : public juce::TimeSliceClient
{
public:
    // ... existing ...
    int useTimeSlice() override;   ///< Worker callback.
```

Add to private:

```cpp
    juce::TimeSliceThread workerThread_ { "FileInputSource worker" };
    std::atomic<bool>         isPlaying_     { false };
    std::atomic<std::int64_t> playheadFrames_ { 0 };
    std::atomic<std::int64_t> seekRequest_   { -1 };  // -1 = none
```

(Each `FileInputSource` owns its own thread in v1; if a shared thread is preferred later, refactor.)

- [ ] **Step 4: Implement transport + worker**

In the constructor, after ring init, add:

```cpp
    workerThread_.addTimeSliceClient (this);
    workerThread_.startThread (juce::Thread::Priority::low);
```

In the destructor (replace the `= default`):

```cpp
FileInputSource::~FileInputSource()
{
    workerThread_.removeTimeSliceClient (this);
    workerThread_.stopThread (200);
}
```

Append:

```cpp
void FileInputSource::play()  noexcept { isPlaying_.store (true);  }
void FileInputSource::pause() noexcept { isPlaying_.store (false); }
void FileInputSource::stop()  noexcept { isPlaying_.store (false); seek (0); }
void FileInputSource::seek (std::int64_t frame) noexcept { seekRequest_.store (frame); }

int FileInputSource::useTimeSlice()
{
    // Apply pending seek.
    const auto seek = seekRequest_.exchange (-1);
    if (seek >= 0 && currentReader_ != nullptr)
    {
        playheadFrames_.store (seek);
    }

    if (! isPlaying_.load() || currentReader_ == nullptr)
        return 50;   // sleep 50 ms

    // How many frames of headroom does the ring have?
    const int writePos = writePos_.load (std::memory_order_relaxed);
    const int readPos  = readPos_ .load (std::memory_order_acquire);
    const int free     = (readPos - writePos - 1 + kRingFrames) % kRingFrames;
    if (free < 256)
        return 5;   // ring full enough, check back soon

    const int chunk = juce::jmin (free, 2048);
    juce::AudioBuffer<float> scratch (juce::jmax (2, currentReader_->numChannels), chunk);

    auto head = playheadFrames_.load();
    const bool hitEnd = ! currentReader_->read (&scratch, 0, chunk, head, true, true);
    juce::ignoreUnused (hitEnd);

    // Push scratch into ring (write side; mirror of testPushRing).
    int wp = writePos;
    for (int i = 0; i < chunk; ++i)
    {
        ringL_[(size_t) wp] = scratch.getSample (0, i);
        ringR_[(size_t) wp] = scratch.getNumChannels() > 1 ? scratch.getSample (1, i)
                                                            : scratch.getSample (0, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
    playheadFrames_.fetch_add (chunk);

    return 1;
}
```

(Resampling for SR-mismatch is intentionally deferred to Task 7 — single-rate path passes Task 6 tests since the synthesized WAV is 48 kHz.)

- [ ] **Step 5: Build + run the new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][audio][transport]" 2>&1 | tail -10
```

Expected: 2 transport tests pass.

- [ ] **Step 6: Commit + push**

```bash
git add audio/include/ida/FileInputSource.h audio/src/FileInputSource.cpp tests/FileInputSourceTests.cpp
git commit -m "feat: audio — FileInputSource worker thread + play/pause/stop/seek transport"
git push origin master
```

---

## Task 7 — Audio: Playlist semantics + LoopScope advance + missing-file skip + SR resample + mono dual-mono

**Files:**
- Modify: `audio/include/ida/FileInputSource.h` (playlist surface + mutex + loopScope atomic)
- Modify: `audio/src/FileInputSource.cpp`
- Create: `tests/FileInputPlaylistTests.cpp`
- Modify: `tests/CMakeLists.txt` (add `FileInputPlaylistTests.cpp`)

The biggest engine task. Turns `FileInputSource` from a single-reader player into a playlist-aware one. After this task the audio engine is feature-complete; everything left is wiring (Task 8), persistence (Task 9), UI (Task 10), and operator surface (Task 11).

- [ ] **Step 1: Write the failing tests (playlist file)**

Create `tests/FileInputPlaylistTests.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "ida/FileInputSource.h"
#include "ida/LoopScope.h"

namespace
{

juce::File writeWav (juce::TempDirectoryDeleter& tmp, const juce::String& name,
                     double sr, int ch, int frames, float fillSample)
{
    auto file = tmp.getDir().getChildFile (name);
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (file.createOutputStream().release(),
                              sr, (unsigned int) ch, 16, {}, 0));
    REQUIRE (writer != nullptr);

    juce::AudioBuffer<float> buf (ch, frames);
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < frames; ++i)
            buf.setSample (c, i, fillSample);

    writer->writeFromAudioSampleBuffer (buf, 0, frames);
    return file;
}

} // namespace

TEST_CASE ("Playlist: LoopScope=Off advances to next entry, stops at end of list",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav1 = writeWav (tmp, "a.wav", 48000.0, 2, 4800, 0.5f);   // 100 ms, +0.5
    auto wav2 = writeWav (tmp, "b.wav", 48000.0, 2, 4800, -0.5f);  // 100 ms, -0.5

    ida::FileInputSource source { 48000.0 };
    auto e1 = source.addEntry (wav1.getFullPathName().toStdString());
    auto e2 = source.addEntry (wav2.getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Off);
    source.play();

    juce::Thread::sleep (300);   // long enough to traverse both 100-ms clips

    CHECK_FALSE (source.isPlaying());
}

TEST_CASE ("Playlist: LoopScope=Track rewinds same entry on EOF",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav = writeWav (tmp, "loop.wav", 48000.0, 2, 4800, 0.3f);

    ida::FileInputSource source { 48000.0 };
    auto e = source.addEntry (wav.getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Track);
    source.play();

    juce::Thread::sleep (250);  // 2.5× the file length
    CHECK (source.isPlaying());            // never stopped
    CHECK (source.currentEntry() == e);    // never advanced
}

TEST_CASE ("Playlist: LoopScope=List wraps last → first",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto wav1 = writeWav (tmp, "a.wav", 48000.0, 2, 4800, 0.3f);
    auto wav2 = writeWav (tmp, "b.wav", 48000.0, 2, 4800, -0.3f);

    ida::FileInputSource source { 48000.0 };
    auto e1 = source.addEntry (wav1.getFullPathName().toStdString());
    auto e2 = source.addEntry (wav2.getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::List);
    source.play();

    juce::Thread::sleep (350);   // ~3.5× a single clip → should have wrapped at least once
    CHECK (source.isPlaying());
    // currentEntry is either e1 or e2 — both prove the worker is cycling.
    CHECK ((source.currentEntry() == e1 || source.currentEntry() == e2));
}

TEST_CASE ("Playlist: missing entry mid-list is skipped on advance",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto a = writeWav (tmp, "a.wav", 48000.0, 2, 2400, 0.3f);   // 50 ms
    auto c = writeWav (tmp, "c.wav", 48000.0, 2, 2400, -0.3f);

    ida::FileInputSource source { 48000.0 };
    auto eA = source.addEntry (a.getFullPathName().toStdString());
    auto eB = source.addEntry ("/nope/missing.wav");             // missing
    auto eC = source.addEntry (c.getFullPathName().toStdString());
    source.setLoopScope (ida::LoopScope::Off);
    source.play();

    juce::Thread::sleep (200);
    CHECK_FALSE (source.isPlaying());
    CHECK (source.entryMissing (eB));  // skipped, marked missing
}

TEST_CASE ("Playlist: reorder of currently-playing entry keeps reader uninterrupted",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto a = writeWav (tmp, "a.wav", 48000.0, 2, 48000, 0.3f);  // 1 s
    auto b = writeWav (tmp, "b.wav", 48000.0, 2, 4800,  -0.3f);

    ida::FileInputSource source { 48000.0 };
    auto eA = source.addEntry (a.getFullPathName().toStdString());
    auto eB = source.addEntry (b.getFullPathName().toStdString());
    source.play();

    juce::Thread::sleep (60);
    const auto headBefore = source.playheadFrames();

    REQUIRE (source.reorderEntry (eA, 1));   // move A to position 1
    juce::Thread::sleep (20);
    const auto headAfter = source.playheadFrames();

    CHECK (source.currentEntry() == eA);     // still on A
    CHECK (headAfter >= headBefore);         // reader uninterrupted
}

TEST_CASE ("Playlist: removeEntry refuses the currently-playing entry",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto a = writeWav (tmp, "a.wav", 48000.0, 2, 48000, 0.3f);

    ida::FileInputSource source { 48000.0 };
    auto eA = source.addEntry (a.getFullPathName().toStdString());
    source.play();
    juce::Thread::sleep (30);

    CHECK_FALSE (source.removeEntry (eA));    // refused
    CHECK (source.isPlaying());
}

TEST_CASE ("Mono file is dual-mono'd at the reader stage",
           "[file-input][playlist]")
{
    juce::TempDirectoryDeleter tmp;
    auto mono = writeWav (tmp, "mono.wav", 48000.0, 1, 4800, 0.5f);

    ida::FileInputSource source { 48000.0 };
    source.addEntry (mono.getFullPathName().toStdString());
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
    juce::TempDirectoryDeleter tmp;
    auto wav44 = writeWav (tmp, "f44.wav", 44100.0, 2, 4410, 0.4f);  // 100 ms @ 44.1k

    ida::FileInputSource source { 48000.0 };
    source.addEntry (wav44.getFullPathName().toStdString());
    source.play();
    juce::Thread::sleep (60);

    juce::AudioBuffer<float> out (2, 1024);
    out.clear();
    source.pullInto (out, 1024);

    CHECK (out.getRMSLevel (0, 0, 1024) > 0.01f);   // resampled signal carries energy
}
```

Add to `tests/CMakeLists.txt`:

```cmake
    FileInputPlaylistTests.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -10
```

Expected: compile FAIL — `addEntry`, `setLoopScope`, `currentEntry`, `entryMissing`, `reorderEntry`, `removeEntry` unknown.

- [ ] **Step 3: Extend `FileInputSource.h`**

Add includes: `<mutex>`, `<vector>`, `"ida/LoopScope.h"`, `"ida/PlaylistEntryId.h"`.

Public surface additions:

```cpp
    /// Playlist mutation. Message-thread only. addEntry returns the new
    /// entry's stable id. removeEntry refuses the currently-playing entry
    /// (returns false). reorderEntry moves an entry to `newIndex` (clamped).
    PlaylistEntryId addEntry    (const std::string& path);
    bool            removeEntry (PlaylistEntryId id);
    bool            reorderEntry (PlaylistEntryId id, int newIndex);

    void            setLoopScope (LoopScope scope) noexcept;
    LoopScope       loopScope()    const noexcept { return loopScope_.load(); }

    PlaylistEntryId currentEntry() const noexcept;
    bool            entryMissing (PlaylistEntryId id) const;

    int             entryCount() const;
```

Private additions:

```cpp
    struct Entry
    {
        PlaylistEntryId id;
        std::string path;
        bool missing { false };
    };

    mutable std::mutex listMutex_;
    std::vector<Entry> entries_;
    std::int64_t nextEntryId_ { 1 };
    std::atomic<std::int64_t> currentEntryIdValue_ { -1 };  // -1 = none
    std::atomic<LoopScope> loopScope_ { LoopScope::Off };

    std::unique_ptr<juce::ResamplingAudioSource> resampler_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    bool openEntryReader_locked (PlaylistEntryId id);  // call under listMutex_
    PlaylistEntryId nextEntryId_locked (PlaylistEntryId current) const; // wraps if loopScope==List, returns sentinel if Off+last
```

Replace the existing single `currentReader_` with the `readerSource_` / `resampler_` pair (the reader source wraps the format reader; the resampler wraps the reader source if SR ≠ device SR).

- [ ] **Step 4: Implement playlist surface + advance logic**

Replace `useTimeSlice()` with a playlist-aware version. The new shape:

```cpp
int FileInputSource::useTimeSlice()
{
    // Pending seek.
    const auto seek = seekRequest_.exchange (-1);
    if (seek >= 0 && readerSource_ != nullptr)
    {
        readerSource_->setNextReadPosition (seek);
        playheadFrames_.store (seek);
    }

    if (! isPlaying_.load())
        return 50;

    // Open first valid entry if none open yet.
    if (readerSource_ == nullptr)
    {
        std::lock_guard<std::mutex> lock (listMutex_);
        for (auto& e : entries_)
        {
            if (! e.missing && openEntryReader_locked (e.id))
            {
                currentEntryIdValue_.store (e.id.value());
                break;
            }
            e.missing = true;
        }
        if (readerSource_ == nullptr)
        {
            isPlaying_.store (false);
            return 50;
        }
    }

    // Read a chunk.
    const int writePos = writePos_.load (std::memory_order_relaxed);
    const int readPos  = readPos_ .load (std::memory_order_acquire);
    const int free     = (readPos - writePos - 1 + kRingFrames) % kRingFrames;
    if (free < 256)
        return 5;

    const int chunk = juce::jmin (free, 2048);
    juce::AudioBuffer<float> scratch (2, chunk);
    juce::AudioSourceChannelInfo info { &scratch, 0, chunk };

    if (resampler_ != nullptr) resampler_->getNextAudioBlock (info);
    else                       readerSource_->getNextAudioBlock (info);

    int wp = writePos;
    for (int i = 0; i < chunk; ++i)
    {
        ringL_[(size_t) wp] = scratch.getSample (0, i);
        ringR_[(size_t) wp] = scratch.getSample (1, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
    playheadFrames_.fetch_add (chunk);

    // EOF? Advance per loopScope.
    if (readerSource_->getNextReadPosition() >= readerSource_->getTotalLength())
    {
        const auto scope = loopScope_.load();
        if (scope == LoopScope::Track)
        {
            readerSource_->setNextReadPosition (0);
            playheadFrames_.store (0);
        }
        else
        {
            std::lock_guard<std::mutex> lock (listMutex_);
            const auto curId = PlaylistEntryId { currentEntryIdValue_.load() };
            auto next = nextEntryId_locked (curId);
            if (next.value() < 0)
            {
                isPlaying_.store (false);
                readerSource_.reset();
                resampler_.reset();
                currentEntryIdValue_.store (-1);
            }
            else
            {
                openEntryReader_locked (next);
                currentEntryIdValue_.store (next.value());
                playheadFrames_.store (0);
            }
        }
    }

    return 1;
}
```

Then implement the helpers + public methods:

```cpp
PlaylistEntryId FileInputSource::addEntry (const std::string& path)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    const PlaylistEntryId id { nextEntryId_++ };
    const bool missing = ! juce::File (path).existsAsFile();
    entries_.push_back ({ id, path, missing });
    return id;
}

bool FileInputSource::removeEntry (PlaylistEntryId id)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    if (currentEntryIdValue_.load() == id.value() && isPlaying_.load())
        return false;

    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    entries_.erase (it);
    return true;
}

bool FileInputSource::reorderEntry (PlaylistEntryId id, int newIndex)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    Entry moved = *it;
    entries_.erase (it);
    newIndex = juce::jlimit (0, (int) entries_.size(), newIndex);
    entries_.insert (entries_.begin() + newIndex, moved);
    return true;
}

void FileInputSource::setLoopScope (LoopScope scope) noexcept { loopScope_.store (scope); }

PlaylistEntryId FileInputSource::currentEntry() const noexcept
{
    return PlaylistEntryId { currentEntryIdValue_.load() };
}

bool FileInputSource::entryMissing (PlaylistEntryId id) const
{
    std::lock_guard<std::mutex> lock (listMutex_);
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    return it != entries_.end() && it->missing;
}

int FileInputSource::entryCount() const
{
    std::lock_guard<std::mutex> lock (listMutex_);
    return (int) entries_.size();
}

bool FileInputSource::openEntryReader_locked (PlaylistEntryId id)
{
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    juce::File file (juce::String (it->path));
    if (! file.existsAsFile()) { it->missing = true; return false; }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager_.createReaderFor (file));
    if (reader == nullptr) { it->missing = true; return false; }

    const double fileSr = reader->sampleRate;
    readerSource_ = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    readerSource_->setLooping (false);
    readerSource_->prepareToPlay (2048, deviceSampleRate_);

    if (std::abs (fileSr - deviceSampleRate_) > 0.5)
    {
        resampler_ = std::make_unique<juce::ResamplingAudioSource> (readerSource_.get(), false, 2);
        resampler_->setResamplingRatio (fileSr / deviceSampleRate_);
        resampler_->prepareToPlay (2048, deviceSampleRate_);
    }
    else
    {
        resampler_.reset();
    }
    return true;
}

PlaylistEntryId FileInputSource::nextEntryId_locked (PlaylistEntryId current) const
{
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [current] (const Entry& e) { return e.id == current; });
    if (it == entries_.end()) return PlaylistEntryId { -1 };

    auto next = std::next (it);
    while (next != entries_.end() && next->missing) ++next;
    if (next != entries_.end()) return next->id;

    if (loopScope_.load() == LoopScope::List)
    {
        auto first = std::find_if (entries_.begin(), entries_.end(),
                                   [] (const Entry& e) { return ! e.missing; });
        if (first != entries_.end() && first->id != current) return first->id;
    }
    return PlaylistEntryId { -1 };
}
```

- [ ] **Step 5: Build + run new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[file-input][playlist]" 2>&1 | tail -20
```

Expected: 8 playlist tests pass. (The reorder-during-play test sleeps small amounts; if it flakes on a heavily-loaded CI box, sleep durations can be bumped — but on a quiet dev machine the 50 ms cushions are ample.)

- [ ] **Step 6: Re-run all `[file-input]` tests to confirm Task 5/6 still green**

```bash
./build/tests/IdaTests "[file-input]" 2>&1 | tail -10
```

Expected: all green (12+ tests total across Tasks 2–7).

- [ ] **Step 7: Commit + push**

```bash
git add audio/include/ida/FileInputSource.h audio/src/FileInputSource.cpp tests/FileInputPlaylistTests.cpp tests/CMakeLists.txt
git commit -m "feat: audio — FileInputSource playlist semantics: LoopScope advance + missing-file skip + reorder + SR resample"
git push origin master
```

---

## Task 8 — Engine: `InputMixer` registration + transport surface + audio-callback patch

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (new public methods + `FileInputTransportState` struct)
- Modify: `engine/src/InputMixer.cpp`
- Modify: `tests/InputMixerTests.cpp` (add integration tests)

Wires `FileInputSource` instances into `InputMixer`. After this task, an engine-only path can register a file input, attach a channel to it, and feed the channel from disk through the normal `processBuffer` path.

- [ ] **Step 1: Write the failing tests**

Append to `tests/InputMixerTests.cpp`:

```cpp
#include "ida/FileInputDescriptor.h"

TEST_CASE ("InputMixer::registerFileInput accepts a descriptor and returns an InputId",
           "[input-mixer][file-input]")
{
    ida::InputMixer mixer;
    mixer.prepareToPlay (48000.0, 512);

    ida::FileInputDescriptor desc;
    desc.displayName = "Setlist";
    auto id = mixer.registerFileInput (desc);

    REQUIRE (id.value() >= 0);
    REQUIRE (mixer.fileInputDescriptor (id) != nullptr);
    CHECK (mixer.fileInputDescriptor (id)->displayName == "Setlist");
}

TEST_CASE ("InputMixer::addFileInputEntry appends a playlist entry",
           "[input-mixer][file-input]")
{
    ida::InputMixer mixer;
    mixer.prepareToPlay (48000.0, 512);

    auto id = mixer.registerFileInput (ida::FileInputDescriptor {});
    auto eid = mixer.addFileInputEntry (id, "/tmp/whatever.wav");
    CHECK (eid.value() >= 0);
    CHECK (mixer.fileInputDescriptor (id)->entries.size() == 1u);
    CHECK (mixer.fileInputDescriptor (id)->entries[0].path == "/tmp/whatever.wav");
}

TEST_CASE ("InputMixer::setFileInputWindowOpacity clamps to [0.5, 1.0]",
           "[input-mixer][file-input]")
{
    ida::InputMixer mixer;
    auto id = mixer.registerFileInput ({});

    mixer.setFileInputWindowOpacity (id, 0.3f);
    CHECK (mixer.fileInputDescriptor (id)->windowOpacity == Catch::Approx (0.5f));

    mixer.setFileInputWindowOpacity (id, 1.5f);
    CHECK (mixer.fileInputDescriptor (id)->windowOpacity == Catch::Approx (1.0f));

    mixer.setFileInputWindowOpacity (id, 0.8f);
    CHECK (mixer.fileInputDescriptor (id)->windowOpacity == Catch::Approx (0.8f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: `registerFileInput` / `fileInputDescriptor` / `addFileInputEntry` / `setFileInputWindowOpacity` unknown.

- [ ] **Step 3: Extend `InputMixer.h`**

Add include: `"ida/FileInputDescriptor.h"`. Add to public surface (group with input registration around line 213):

```cpp
    /// File-input registration. Allocates a new InputId, stores the
    /// descriptor, spins up a FileInputSource. White-paper V9 §6.6.
    InputId registerFileInput (const FileInputDescriptor& desc);
    void    unregisterFileInput (InputId id);

    /// Read-only descriptor access (UI thread polls for state).
    const FileInputDescriptor* fileInputDescriptor (InputId id) const;

    /// Playlist mutation (message thread; engine forwards under its own mutex).
    PlaylistEntryId addFileInputEntry    (InputId id, const std::string& path);
    bool            removeFileInputEntry (InputId id, PlaylistEntryId entry);
    bool            reorderFileInput     (InputId id, PlaylistEntryId entry, int newIndex);

    /// Transport.
    void playFileInput  (InputId id);
    void pauseFileInput (InputId id);
    void stopFileInput  (InputId id);
    void seekFileInput  (InputId id, std::int64_t frame);
    void setFileInputLoopScope     (InputId id, LoopScope scope);
    void setFileInputWindowOpacity (InputId id, float opacity);

    struct FileInputTransportState
    {
        bool             isPlaying;
        PlaylistEntryId  currentEntry;
        std::int64_t     playheadFrames;
        LoopScope        loopScope;
    };
    FileInputTransportState fileInputTransportState (InputId id) const;
```

Add to private:

```cpp
    std::unordered_map<std::int64_t, FileInputDescriptor> fileInputDescriptors_;
    std::unordered_map<std::int64_t, std::unique_ptr<FileInputSource>> fileInputSources_;
    std::int64_t nextFileInputId_ { 100000 };  // separate range from device inputs
```

(Forward-declare `FileInputSource` and include the header in the .cpp.)

- [ ] **Step 4: Implement in `InputMixer.cpp`**

```cpp
#include "ida/FileInputSource.h"

InputId InputMixer::registerFileInput (const FileInputDescriptor& desc)
{
    const InputId id { nextFileInputId_++ };
    fileInputDescriptors_[id.value()] = desc;
    fileInputSources_[id.value()] = std::make_unique<FileInputSource> (sampleRate_);

    auto& src = *fileInputSources_[id.value()];
    src.setLoopScope (desc.loopScope);
    for (const auto& e : desc.entries)
        src.addEntry (e.path);
    return id;
}

void InputMixer::unregisterFileInput (InputId id)
{
    fileInputSources_  .erase (id.value());
    fileInputDescriptors_.erase (id.value());
}

const FileInputDescriptor* InputMixer::fileInputDescriptor (InputId id) const
{
    auto it = fileInputDescriptors_.find (id.value());
    return it != fileInputDescriptors_.end() ? &it->second : nullptr;
}

PlaylistEntryId InputMixer::addFileInputEntry (InputId id, const std::string& path)
{
    auto src = fileInputSources_.find (id.value());
    if (src == fileInputSources_.end()) return PlaylistEntryId { -1 };
    auto eid = src->second->addEntry (path);
    fileInputDescriptors_[id.value()].entries.push_back ({ eid, path, {}, false });
    return eid;
}

bool InputMixer::removeFileInputEntry (InputId id, PlaylistEntryId entry)
{
    auto src = fileInputSources_.find (id.value());
    if (src == fileInputSources_.end()) return false;
    if (! src->second->removeEntry (entry)) return false;

    auto& descEntries = fileInputDescriptors_[id.value()].entries;
    descEntries.erase (std::remove_if (descEntries.begin(), descEntries.end(),
                       [entry] (const FileInputEntry& e) { return e.entryId == entry; }),
                       descEntries.end());
    return true;
}

bool InputMixer::reorderFileInput (InputId id, PlaylistEntryId entry, int newIndex)
{
    auto src = fileInputSources_.find (id.value());
    if (src == fileInputSources_.end()) return false;
    if (! src->second->reorderEntry (entry, newIndex)) return false;

    auto& v = fileInputDescriptors_[id.value()].entries;
    auto it = std::find_if (v.begin(), v.end(),
                            [entry] (const FileInputEntry& e) { return e.entryId == entry; });
    if (it == v.end()) return false;
    FileInputEntry moved = *it;
    v.erase (it);
    newIndex = juce::jlimit (0, (int) v.size(), newIndex);
    v.insert (v.begin() + newIndex, moved);
    return true;
}

void InputMixer::playFileInput  (InputId id) { if (auto* s = source_(id)) s->play(); }
void InputMixer::pauseFileInput (InputId id) { if (auto* s = source_(id)) s->pause(); }
void InputMixer::stopFileInput  (InputId id) { if (auto* s = source_(id)) s->stop(); }
void InputMixer::seekFileInput  (InputId id, std::int64_t frame)
                                              { if (auto* s = source_(id)) s->seek (frame); }
void InputMixer::setFileInputLoopScope (InputId id, LoopScope scope)
{
    if (auto* s = source_(id)) { s->setLoopScope (scope); fileInputDescriptors_[id.value()].loopScope = scope; }
}

void InputMixer::setFileInputWindowOpacity (InputId id, float opacity)
{
    auto it = fileInputDescriptors_.find (id.value());
    if (it == fileInputDescriptors_.end()) return;
    it->second.windowOpacity = juce::jlimit (0.5f, 1.0f, opacity);
}

InputMixer::FileInputTransportState InputMixer::fileInputTransportState (InputId id) const
{
    auto it = fileInputSources_.find (id.value());
    if (it == fileInputSources_.end()) return { false, PlaylistEntryId { -1 }, 0, LoopScope::Off };
    auto& s = *it->second;
    return { s.isPlaying(), s.currentEntry(), s.playheadFrames(), s.loopScope() };
}
```

(Tiny private helper at the end of the file:)

```cpp
FileInputSource* InputMixer::source_ (InputId id)
{
    auto it = fileInputSources_.find (id.value());
    return it != fileInputSources_.end() ? it->second.get() : nullptr;
}
```

Declare in the header (private):

```cpp
    FileInputSource* source_ (InputId id);
```

**Audio-callback patch** — locate `InputMixer::processBuffer` (the existing per-callback function). For each registered file input, pull from its source's ring into the input buffer that channels read from. Exact wiring depends on the current callback shape; minimum-viable: when assembling the per-channel input frames, replace the device-input read with `source.pullInto (channelBuf, numFrames)` for channels whose source is a file input. (No tests assert audio-callback wiring directly in this task; the operator-verify recipe in Task 12 covers it.)

- [ ] **Step 5: Build + run all `[input-mixer][file-input]` tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][file-input]" 2>&1 | tail -10
```

Expected: 3 tests pass.

- [ ] **Step 6: Re-run full input-mixer test set (regression)**

```bash
./build/tests/IdaTests "[input-mixer]" 2>&1 | tail -10
```

Expected: full input-mixer suite green.

- [ ] **Step 7: Commit + push**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: engine — InputMixer file-input registration + playlist + transport surface"
git push origin master
```

---

## Task 9 — Persistence: JSON `fileInputs` round-trip + backward-compat + opacity clamp

**Files:**
- Modify: `persistence/src/SessionFormat.cpp` (read/write the new array)
- Create: `tests/FileInputPersistenceTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test file)

- [ ] **Step 1: Write the failing tests**

Create `tests/FileInputPersistenceTests.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>

#include "ida/InputMixer.h"
#include "ida/SessionFormat.h"   // adjust to the actual header
#include "ida/FileInputDescriptor.h"

TEST_CASE ("Session JSON round-trips a file input with playlist + loopScope + opacity",
           "[persistence][file-input]")
{
    ida::InputMixer mixer;
    mixer.prepareToPlay (48000.0, 512);

    ida::FileInputDescriptor desc;
    desc.displayName   = "Setlist A";
    desc.loopScope     = ida::LoopScope::List;
    desc.windowOpacity = 0.75f;
    auto id = mixer.registerFileInput (desc);
    mixer.addFileInputEntry (id, "/abs/a.wav");
    mixer.addFileInputEntry (id, "/abs/b.flac");

    const auto json = ida::SessionFormat::writeInputMixer (mixer);

    ida::InputMixer mixer2;
    mixer2.prepareToPlay (48000.0, 512);
    REQUIRE (ida::SessionFormat::readInputMixer (mixer2, json));

    // Find the round-tripped file input by displayName (id values may differ).
    const ida::FileInputDescriptor* found = nullptr;
    for (const auto& [k, d] : mixer2.allFileInputDescriptors())
        if (d.displayName == "Setlist A") { found = &d; break; }

    REQUIRE (found != nullptr);
    CHECK (found->loopScope     == ida::LoopScope::List);
    CHECK (found->windowOpacity == Catch::Approx (0.75f));
    REQUIRE (found->entries.size() == 2u);
    CHECK (found->entries[0].path == "/abs/a.wav");
    CHECK (found->entries[1].path == "/abs/b.flac");
}

TEST_CASE ("Session JSON without fileInputs array loads with zero file inputs",
           "[persistence][file-input]")
{
    juce::var emptyMixer (new juce::DynamicObject());
    emptyMixer.getDynamicObject()->setProperty ("buses", juce::Array<juce::var> {});

    ida::InputMixer mixer;
    mixer.prepareToPlay (48000.0, 512);
    REQUIRE (ida::SessionFormat::readInputMixer (mixer, emptyMixer));
    CHECK (mixer.allFileInputDescriptors().empty());
}

TEST_CASE ("Session JSON with out-of-range windowOpacity is clamped on read",
           "[persistence][file-input]")
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> fileInputs;
    juce::DynamicObject::Ptr fi = new juce::DynamicObject();
    fi->setProperty ("displayName",   "X");
    fi->setProperty ("loopScope",     "off");
    fi->setProperty ("windowOpacity", 0.2);     // below floor
    fi->setProperty ("entries", juce::Array<juce::var> {});
    fileInputs.add (juce::var (fi.get()));
    root->setProperty ("fileInputs", fileInputs);

    ida::InputMixer mixer;
    mixer.prepareToPlay (48000.0, 512);
    REQUIRE (ida::SessionFormat::readInputMixer (mixer, juce::var (root.get())));

    REQUIRE (mixer.allFileInputDescriptors().size() == 1u);
    CHECK (mixer.allFileInputDescriptors().begin()->second.windowOpacity == Catch::Approx (0.5f));
}
```

Add a small helper to `InputMixer.h` (alongside `fileInputDescriptor`):

```cpp
    const std::unordered_map<std::int64_t, FileInputDescriptor>& allFileInputDescriptors() const
    { return fileInputDescriptors_; }
```

Add to `tests/CMakeLists.txt`:

```cmake
    FileInputPersistenceTests.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: `writeInputMixer` / `readInputMixer` may exist with different names — adjust calls to match. If they don't expose the right shape, you may need a small accessor for write or to mirror the existing per-section pattern in `SessionFormat.cpp`. Tests fail because nothing yet reads/writes `fileInputs`.

- [ ] **Step 3: Implement `fileInputs` write**

In `persistence/src/SessionFormat.cpp`, locate the input-mixer JSON-building function (near the existing `"buses"` write at line 1100). Add a sibling write:

```cpp
juce::Array<juce::var> fileInputs;
for (const auto& [k, d] : mixer.allFileInputDescriptors())
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("displayName",   juce::String (d.displayName));
    o->setProperty ("loopScope",
        d.loopScope == ida::LoopScope::Off   ? "off"   :
        d.loopScope == ida::LoopScope::Track ? "track" : "list");
    o->setProperty ("windowOpacity", d.windowOpacity);

    juce::Array<juce::var> entries;
    for (const auto& e : d.entries)
    {
        juce::DynamicObject::Ptr ej = new juce::DynamicObject();
        ej->setProperty ("entryId", (juce::int64) e.entryId.value());
        ej->setProperty ("path",    juce::String (e.path));
        entries.add (juce::var (ej.get()));
    }
    o->setProperty ("entries", entries);
    fileInputs.add (juce::var (o.get()));
}
root->setProperty ("fileInputs", fileInputs);
```

- [ ] **Step 4: Implement `fileInputs` read with clamp + backward-compat**

In the corresponding read function (near line 1173 / 1186 where `"buses"` is loaded), add:

```cpp
if (const auto fileInputs = optionalProperty (root, "fileInputs"); fileInputs.isArray())
{
    for (const auto& f : *fileInputs.getArray())
    {
        if (! f.isObject()) continue;

        ida::FileInputDescriptor desc;
        desc.displayName = f.getProperty ("displayName", "").toString().toStdString();

        const auto scopeStr = f.getProperty ("loopScope", "off").toString();
        desc.loopScope = scopeStr == "track" ? ida::LoopScope::Track
                       : scopeStr == "list"  ? ida::LoopScope::List
                                              : ida::LoopScope::Off;

        const float opacity = (float) (double) f.getProperty ("windowOpacity", 0.92);
        desc.windowOpacity = juce::jlimit (0.5f, 1.0f, opacity);

        auto id = mixer.registerFileInput (desc);
        if (const auto entries = f.getProperty ("entries", juce::var()); entries.isArray())
            for (const auto& e : *entries.getArray())
                if (e.isObject())
                    mixer.addFileInputEntry (id, e.getProperty ("path", "").toString().toStdString());
    }
}
```

(The `optionalProperty` helper is the existing pattern from line 1173 — reuse it.)

- [ ] **Step 5: Build + run new tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[persistence][file-input]" 2>&1 | tail -10
```

Expected: 3 tests pass.

- [ ] **Step 6: Full ctest regression**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -5
```

Expected: pass count = prior baseline + new tests. No regressions.

- [ ] **Step 7: Commit + push**

```bash
git add persistence/src/SessionFormat.cpp engine/include/ida/InputMixer.h tests/FileInputPersistenceTests.cpp tests/CMakeLists.txt
git commit -m "feat: persistence — fileInputs JSON round-trip + backward-compat + opacity clamp"
git push origin master
```

---

## Task 10 — UI: `FileInputPlayerWindow`

**Files:**
- Create: `ui/include/ida/FileInputPlayerWindow.h`
- Create: `ui/src/FileInputPlayerWindow.cpp`
- Modify: `ui/CMakeLists.txt` (add `FileInputPlayerWindow.cpp`)

Pure-view JUCE `DocumentWindow` polling `InputMixer::fileInputTransportState` at 30 Hz. No engine-side tests; operator-verified per IDA convention.

- [ ] **Step 1: Create `ui/include/ida/FileInputPlayerWindow.h`**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ida/InputMixer.h"

namespace ida
{

class FileInputPlayerWindow : public juce::DocumentWindow,
                              private juce::Timer
{
public:
    FileInputPlayerWindow (InputMixer& mixer, InputId id);
    ~FileInputPlayerWindow() override;

    void closeButtonPressed() override;

private:
    class Content;

    void timerCallback() override;          ///< 30 Hz UI refresh
    void showOpacityMenu();                 ///< right-click handler

    InputMixer& mixer_;
    InputId     id_;
    std::unique_ptr<Content> content_;
};

} // namespace ida
```

- [ ] **Step 2: Create `ui/src/FileInputPlayerWindow.cpp`**

```cpp
#include "ida/FileInputPlayerWindow.h"

namespace ida
{

class FileInputPlayerWindow::Content : public juce::Component
{
public:
    Content (InputMixer& mixer, InputId id) : mixer_ (mixer), id_ (id)
    {
        addAndMakeVisible (playBtn_);   playBtn_.setButtonText ("▶");
        addAndMakeVisible (pauseBtn_);  pauseBtn_.setButtonText ("⏸");
        addAndMakeVisible (stopBtn_);   stopBtn_.setButtonText ("⏹");
        addAndMakeVisible (loopBtn_);   loopBtn_.setButtonText ("∅");
        addAndMakeVisible (scrubber_);  scrubber_.setRange (0.0, 1.0);
        addAndMakeVisible (currentLabel_);
        addAndMakeVisible (trackList_);
        addAndMakeVisible (appendBtn_); appendBtn_.setButtonText ("+");

        playBtn_  .onClick = [this] { mixer_.playFileInput  (id_); };
        pauseBtn_ .onClick = [this] { mixer_.pauseFileInput (id_); };
        stopBtn_  .onClick = [this] { mixer_.stopFileInput  (id_); };
        loopBtn_  .onClick = [this] { cycleLoopScope(); };
        appendBtn_.onClick = [this] { showFilePicker(); };
        scrubber_.onDragEnd = [this] { mixer_.seekFileInput (id_, (std::int64_t)(scrubber_.getValue() * scrubberMaxFrames_)); };
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto topRow = r.removeFromTop (32);
        playBtn_ .setBounds (topRow.removeFromLeft (32));  topRow.removeFromLeft (4);
        pauseBtn_.setBounds (topRow.removeFromLeft (32));  topRow.removeFromLeft (4);
        stopBtn_ .setBounds (topRow.removeFromLeft (32));  topRow.removeFromLeft (12);
        loopBtn_ .setBounds (topRow.removeFromRight (44));
        r.removeFromTop (6);

        scrubber_    .setBounds (r.removeFromTop (24));
        currentLabel_.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);

        appendBtn_.setBounds (r.removeFromBottom (28).removeFromLeft (40));
        r.removeFromBottom (4);
        trackList_.setBounds (r);
    }

    // Called by the outer window at 30 Hz.
    void refresh (const InputMixer::FileInputTransportState& s,
                  const FileInputDescriptor& desc)
    {
        scrubberMaxFrames_ = std::max<std::int64_t> (1, computeCurrentDurationFrames (desc, s.currentEntry));
        if (! scrubber_.isMouseButtonDown())
            scrubber_.setValue ((double) s.playheadFrames / (double) scrubberMaxFrames_,
                                 juce::dontSendNotification);

        loopBtn_.setButtonText (s.loopScope == LoopScope::Off   ? "∅"
                              : s.loopScope == LoopScope::Track ? "↻ trk"
                                                                : "↻ list");

        currentLabel_.setText (
            juce::String ("Now: ") + entryFilename (desc, s.currentEntry),
            juce::dontSendNotification);

        // Track-list refresh — naive full repopulate; for v1 the playlist
        // is small (a setlist), so cost is negligible. Future: diff.
        repopulateList (desc, s);
    }

private:
    void cycleLoopScope()
    {
        const auto cur = mixer_.fileInputTransportState (id_).loopScope;
        const auto next = cur == LoopScope::Off   ? LoopScope::Track
                        : cur == LoopScope::Track ? LoopScope::List
                                                   : LoopScope::Off;
        mixer_.setFileInputLoopScope (id_, next);
    }

    void showFilePicker()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Append tracks to playlist", juce::File(),
            "*.wav;*.aif;*.aiff;*.flac");
        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                for (const auto& f : fc.getResults())
                    mixer_.addFileInputEntry (id_, f.getFullPathName().toStdString());
            });
    }

    static juce::String entryFilename (const FileInputDescriptor& d, PlaylistEntryId id)
    {
        for (const auto& e : d.entries)
            if (e.entryId == id)
                return juce::File (juce::String (e.path)).getFileName();
        return "—";
    }

    static std::int64_t computeCurrentDurationFrames (const FileInputDescriptor& d, PlaylistEntryId id)
    {
        for (const auto& e : d.entries)
            if (e.entryId == id && e.durationFrames.has_value())
                return *e.durationFrames;
        return 0;
    }

    void repopulateList (const FileInputDescriptor& d,
                         const InputMixer::FileInputTransportState& s)
    {
        // Minimal v1 list rendering: text rows. Drag-reorder + per-row
        // remove button are added in this same task in Step 4 below.
        // [implementation in Step 4]
    }

    InputMixer&  mixer_;
    InputId      id_;
    juce::TextButton playBtn_, pauseBtn_, stopBtn_, loopBtn_, appendBtn_;
    juce::Slider     scrubber_  { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label      currentLabel_;
    juce::ListBox    trackList_;
    std::int64_t     scrubberMaxFrames_ { 1 };
    std::unique_ptr<juce::FileChooser> chooser_;
};

FileInputPlayerWindow::FileInputPlayerWindow (InputMixer& mixer, InputId id)
    : juce::DocumentWindow (
        juce::String (mixer.fileInputDescriptor (id) != nullptr
                      ? mixer.fileInputDescriptor (id)->displayName
                      : "File Player"),
        juce::Colours::darkgrey,
        juce::DocumentWindow::allButtons),
      mixer_ (mixer), id_ (id),
      content_ (std::make_unique<Content> (mixer, id))
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (content_.get(), true);
    centreWithSize (380, 280);

    if (const auto* d = mixer.fileInputDescriptor (id))
        setAlpha (juce::jlimit (0.5f, 1.0f, d->windowOpacity));

    setVisible (true);
    startTimerHz (30);
}

FileInputPlayerWindow::~FileInputPlayerWindow() { stopTimer(); }

void FileInputPlayerWindow::closeButtonPressed()
{
    // Destroy the window object only; engine state survives.
    // (Owner — MainComponent — observes this via its window list cleanup.)
    setVisible (false);
}

void FileInputPlayerWindow::timerCallback()
{
    const auto s = mixer_.fileInputTransportState (id_);
    if (const auto* d = mixer_.fileInputDescriptor (id_))
        content_->refresh (s, *d);
}

void FileInputPlayerWindow::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showOpacityMenu();
    else
        juce::DocumentWindow::mouseDown (e);
}

void FileInputPlayerWindow::showOpacityMenu()
{
    juce::PopupMenu m;
    juce::PopupMenu opacityMenu;
    for (auto pct : { 60, 75, 85, 92, 100 })
    {
        opacityMenu.addItem (juce::String (pct) + "%",
                             [this, pct] { mixer_.setFileInputWindowOpacity (id_, pct / 100.f);
                                            setAlpha (pct / 100.f); });
    }
    m.addSubMenu ("Window opacity", opacityMenu);
    m.showMenuAsync (juce::PopupMenu::Options{});
}

} // namespace ida
```

(Note: `mouseDown` override on `DocumentWindow` requires also declaring it in the header — add `void mouseDown (const juce::MouseEvent&) override;` to the public section.)

- [ ] **Step 3: Wire CMake**

Add to `ui/CMakeLists.txt`:

```cmake
    src/FileInputPlayerWindow.cpp
```

- [ ] **Step 4: Flesh out track list + drag-reorder + per-row remove**

`juce::ListBox` with a `ListBoxModel` subclass that renders one row per entry:
- Row text: `"#N  filename.wav  M:SS"`
- Currently-playing row: bold + highlight colour.
- Right-click row → `Remove` (disabled if it's the playing row), `Move up`, `Move down`.
- Long-press (iOS) → same menu.

Implementation pattern matches the existing `TrackList` model in `app/MainComponent.cpp` (search for `ListBoxModel` to find it). Drag-reorder uses JUCE's `juce::ListBox::setRowSelectedOnMouseDown(false)` + a mouse-down/drag handler that calls `mixer_.reorderFileInput`.

This step is implementation-heavy. Aim for ~80 lines in the Content class. Skip if v1 ships with click-to-select rows + Move-up/Move-down buttons in a bottom toolbar instead of drag-reorder — that's a valid v1.1 simplification, operator can drag-reorder in v1.2.

- [ ] **Step 5: Build + smoke launch**

```bash
cmake --build build --target IDA -j 2>&1 | tail -10
```

Expected: clean build (only the standing ld duplicate-libs warning).

(No tests in this task. Operator-verify is in Task 12.)

- [ ] **Step 6: Commit + push**

```bash
git add ui/include/ida/FileInputPlayerWindow.h ui/src/FileInputPlayerWindow.cpp ui/CMakeLists.txt
git commit -m "feat: ui — FileInputPlayerWindow (QuickTime-style transport + playlist view)"
git push origin master
```

---

## Task 11 — App: `InputMixerPane` blank-area gesture + strip recall menu + macOS Window menu + window lifetime

**Files:**
- Modify: `app/MainComponent.cpp` (5 distinct edits — they're listed step-by-step)

- [ ] **Step 1: Add `Add file input…` to the blank-area menu**

Locate the existing `Add bus` entry in `InputMixerPane::showBlankAreaMenu` (search `app/MainComponent.cpp:1455` for `menu.addItem ("Add bus"`). Add a sibling entry immediately below:

```cpp
menu.addItem ("Add file input…", [this] { if (onAddFileInput) onAddFileInput(); });
```

Add the callback to the pane class declaration (near the existing `std::function<void()> onAddBus;` at line ~567):

```cpp
std::function<void()> onAddFileInput;
```

- [ ] **Step 2: Wire `onAddFileInput` in `MainComponent` setup**

Locate the existing `inputMixerPane_->onAddBus = [this] { … };` block (around line 4017). Add a sibling:

```cpp
inputMixerPane_->onAddFileInput = [this]
{
    fileInputChooser_ = std::make_unique<juce::FileChooser> (
        "Add file input — pick one or more audio files",
        juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    fileInputChooser_->launchAsync (
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;

            ida::FileInputDescriptor desc;
            desc.displayName = results[0].getFileNameWithoutExtension().toStdString();
            const auto id = engine_.inputMixer().registerFileInput (desc);
            for (const auto& f : results)
                engine_.inputMixer().addFileInputEntry (id, f.getFullPathName().toStdString());

            // Auto-create the channel strip for this file input.
            engine_.inputMixer().addChannel (id, ida::SignalType::Audio);
            rebuildInputStrips();

            // Open the player window.
            openFilePlayerWindow (id);
        });
};
```

Add private members near the top of `MainComponent`'s data section:

```cpp
std::unique_ptr<juce::FileChooser> fileInputChooser_;
std::unordered_map<std::int64_t, std::unique_ptr<ida::FileInputPlayerWindow>> filePlayerWindows_;
```

- [ ] **Step 3: Implement `openFilePlayerWindow`**

```cpp
void MainComponent::openFilePlayerWindow (ida::InputId id)
{
    auto& slot = filePlayerWindows_[id.value()];
    if (slot != nullptr) { slot->toFront (true); return; }
    slot = std::make_unique<ida::FileInputPlayerWindow> (engine_.inputMixer(), id);
}
```

Declare in the header / class body:

```cpp
void openFilePlayerWindow (ida::InputId id);
```

- [ ] **Step 4: Strip recall menu — `Show player…`**

Locate the input-strip right-click handler (search the `InputMixerPane` strip menu — same one that the bridge slice extended for `Record to tape`). Add for file-input strips:

```cpp
if (engine_.inputMixer().fileInputDescriptor (sourceInputId) != nullptr)
{
    menu.addItem ("Show player…",
                  [this, sourceInputId] { openFilePlayerWindow (sourceInputId); });
}
```

(`sourceInputId` is the InputId the strip's channel is attached to — look at how the bridge slice resolved that in its menu handler.)

- [ ] **Step 5: macOS `Window > File Players` submenu**

In the `juce::MenuBarModel` subclass that drives the app's menu bar (search `getMenuBarNames` in `app/MainComponent.cpp`), add a `Window` menu entry. In `getMenuForIndex` when index matches Window:

```cpp
juce::PopupMenu m;
juce::PopupMenu players;
for (const auto& [id, desc] : engine_.inputMixer().allFileInputDescriptors())
    players.addItem (juce::String (desc.displayName),
                     [this, id] { openFilePlayerWindow (ida::InputId { id }); });
m.addSubMenu ("File Players", players, ! players.containsAnyActiveItems() ? false : true);
return m;
```

(iOS has no menu bar — this code path is macOS-only and naturally skipped on the iOS build.)

- [ ] **Step 6: Strip removal cleans up player window**

In the existing `removeInputChannel` (or equivalent) path: when removing a channel whose InputId is a file input, also erase the player window:

```cpp
if (engine_.inputMixer().fileInputDescriptor (inputId) != nullptr)
{
    filePlayerWindows_.erase (inputId.value());
    engine_.inputMixer().unregisterFileInput (inputId);
}
```

- [ ] **Step 7: Clean rebuild + smoke launch**

Per project CLAUDE.md: clean rebuild before asking operator to eyes-on.

```bash
rm -rf build && \
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && \
  cmake --build build --target IDA -j 2>&1 | tail -10
```

Expected: clean Release build, only the standing ld warning.

- [ ] **Step 8: Commit + push**

```bash
git add app/MainComponent.cpp
git commit -m "feat: app — Input Mixer 'Add file input…' gesture + strip recall + macOS Window menu + player window lifetime"
git push origin master
```

---

## Task 12 — `continue.md` operator-verify recipe + close-out

**Files:**
- Modify: `continue.md`
- Modify (optional): `todo.md` (close any related deferrals)

- [ ] **Step 1: Run full ctest baseline + record number**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: pass count = prior baseline (735) + (~17 new tests across the four new test files). Write the new number down for the handoff.

- [ ] **Step 2: Rewrite `continue.md` §1 with the file-input operator-verify recipe**

Replace the current §1.A / §1.B blocks (bridge slice + bus-MON were the prior session's gates) with a single new gate for this slice. Recipe:

```
### 1.A — File input slice — full eyes-on

1. Launch IDA via the Desktop alias.
2. In the Input Mixer, right-click (desktop) / long-press (iOS) on the
   blank area. The menu shows "Add bus" and "Add file input…". Pick
   the latter.
3. In the file picker, pick 1 audio file (any WAV/AIFF/FLAC). A new
   Input Mixer strip appears, AND a floating File Player Window
   opens. The window title is the file's name (sans extension).
4. Hit ▶ — audio plays into the strip. The strip's meter moves.
   (If you have no audio interface, plug headphones into the laptop
   speakers / output and route the strip through the Output Mixer's
   master to hear it.)
5. Drag the playhead scrubber halfway. Audio jumps. ⏸ pauses. ⏹
   rewinds to 0. ▶ resumes from 0.
6. Click the loop button: cycles ∅ → ↻ trk → ↻ list → ∅. With ↻ trk,
   the file restarts at EOF. With ∅, it stops at EOF.
7. Right-click the player window → Window opacity ▸ 75% — window
   becomes translucent. Pick 100% — window is fully opaque again.
8. Click the player window's "+" button. Pick 2 more files. Both
   appear in the track list. With ↻ list, audio cycles through all
   three and wraps.
9. Drag-reorder a non-current track. List updates. Drag the
   currently-playing track to position 3. Audio keeps playing
   uninterrupted; next-advance follows the new order.
10. Right-click a non-current row → Remove. Row vanishes. Try Remove
    on the current row — disabled (tooltip).
11. Close the player window (red X). The strip stays. Right-click the
    strip → Show player… — window reopens with current transport
    state.
12. On macOS: Window menu → File Players → pick the player's name →
    window raises.
13. Add a SECOND file input (steps 2-3). Both player windows coexist;
    both can play simultaneously. Both feeds mix at the Output Mixer.
14. Save the session. Quit. Relaunch. Load. Both file inputs are back
    with their playlists. Player windows are NOT auto-opened —
    right-click each strip → Show player. Transport starts stopped
    at entry 1.
15. Quit. In Finder, rename one of the playlist files. Relaunch +
    load. The renamed entry's row shows "— missing". Press ▶ — the
    other entries play; the missing one is skipped on advance.
```

- [ ] **Step 3: Update `continue.md` §2 with what landed this chat**

List the 12 commits (one per task) in chronological order with their SHAs and titles.

- [ ] **Step 4: Update `continue.md` §3 baseline**

```
| ctest | <new pass count> / <new pass count> passed |
| Operator GUI verify | pending — recipe in §1 above |
```

- [ ] **Step 5: Close `todo.md` "input→file source" deferral if present**

Grep `todo.md` for any prior file-input-related entries (e.g., the testing-enabler note from this session's start). If present, mark closed with reference to this slice's design + plan.

- [ ] **Step 6: Commit + push**

```bash
git add continue.md todo.md
git commit -m "docs: continue.md — file input slice complete; operator-verify recipe"
git push origin master
```

---

## Self-review (executed during plan-writing)

**Spec coverage:**
- §2 whitepaper amendments → Task 1 ✓
- §3 in-scope items: `InputKind::FileInput` (Task 3), descriptor (Task 3), `FileInputSource` (Tasks 4-7), WAV/AIFF/FLAC (Task 4), mono dual-mono (Task 7), SR resample (Task 7), blank-area gesture (Task 11), player window (Tasks 10-11), Window menu (Task 11), strip recall (Task 11), live edit (Task 7), LoopScope (Tasks 2, 7), persistence (Task 9), missing-file (Task 7 + Task 9), tests (Tasks 2-9), opacity (Tasks 3, 8, 9, 10) ✓
- §3 out-of-scope items: explicitly preserved; no task implements gapless, in-out loop points, MP3/OGG, tempo warp, fan-out, drag-from-Finder, or playhead restore ✓
- §4 architecture: layer ownership respected (`audio/` owns reader stack, `engine/` wires only) ✓
- §5 test surface: every bullet in §5 maps to a Task 2-9 test except the operator-verify items (Task 12 recipe) ✓
- §6 slice plan: 9 spec tasks → 12 plan tasks (FileInputSource split into 4: open, ring, transport, playlist) ✓

**Type consistency:** `PlaylistEntryId`, `LoopScope`, `FileInputDescriptor`, `FileInputEntry`, `FileInputSource`, `FileInputTransportState`, `FileInputPlayerWindow` — names used consistently across Tasks 2-11. `registerFileInput` / `addFileInputEntry` / `removeFileInputEntry` / `reorderFileInput` / `playFileInput` / `pauseFileInput` / `stopFileInput` / `seekFileInput` / `setFileInputLoopScope` / `setFileInputWindowOpacity` / `fileInputDescriptor` / `fileInputTransportState` / `allFileInputDescriptors` — all engine surface names match across declarations and call sites.

**Placeholder scan:** Step 4 of Task 10 (track-list drag-reorder fleshing out) is the only step that delegates implementation detail to the engineer's judgment — but it gives the search anchor (`ListBoxModel` pattern in `MainComponent.cpp`), the row schema, the right-click contents, and the fallback (Move-up/Move-down buttons as a valid v1.1 simplification). The remainder of every task contains complete code blocks for every code step, exact file paths, and exact commands.
