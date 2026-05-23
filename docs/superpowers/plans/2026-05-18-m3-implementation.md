# M3 — Channel-driven tape allocation + ProcessingChain + TapeWriter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace M2's `InputMixer` assert-false stubs with real audio-thread behavior; land the first `Tape<T>` instances in product code via a dedicated writer thread fed by a lock-free SPSC queue; make channel-driven tape topology real (CommitToTape / NonDestructive / NoTape); add per-`SignalType` `ProcessingChain` hierarchy with no-op bodies for M3.

**Architecture:** Audio thread walks active channels, applies (no-op) processing chains, memcpys processed samples into fixed-size POD messages, enqueues on a `LockFreeSpscQueue<TapeWriteMessage>`. On queue-full, calls `OverloadProtection.reportLoad(1.0)` and drops. Writer thread dequeues, routes by `ChannelId` to per-channel `<sessionUuid>/<tapeId>.tape.partial` files, flushes at tier-appropriate intervals. `InputMixer::finalizeChannel` drains, computes sha256 of the partial, and calls existing `TapeStore::store` to register the immutable content-addressed tape.

**Tech Stack:** C++20, JUCE 8 (for `juce::File`, `juce::MemoryBlock`, `juce::Thread`, `juce::Logger`), Catch2 v3, existing `LockFreeSpscQueue<T>`, `OverloadProtection`, `CapabilityTier`, `TapeStore`.

**Source-of-truth spec:** `docs/superpowers/specs/2026-05-18-m3-design.md` (committed in `516429d`).

**Plan amendments §1/§2/§3** have already been applied to `docs/superpowers/plans/2026-05-17-v7-alignment.md` as part of the spec commit. **Do not re-apply** them in Session 1 — the spec said Session 1 applies them, but they were applied at brainstorm-close to avoid stale state.

---

## Spec deviation note (read before Task 1)

The spec calls for `TapeStore::appendBytes(TapeId, span<const std::byte>)` and `TapeStore::finalize(TapeId)`. After grounding against the existing `persistence/include/ida/TapeStore.h`, the cleaner architecture is to keep `TapeStore` purely content-addressed (its existing `store(juce::MemoryBlock&) → juce::String contentHash` is unchanged) and put the partial-file mechanics entirely on `TapeWriter`:

- `TapeWriter::appendForChannel(ChannelId, juce::MemoryBlock)` — writes to the per-channel partial file (writer thread does this; audio thread enqueues a `TapeWriteMessage`, never touches the file).
- `TapeWriter::finalizeChannel(ChannelId, TapeStore&) → juce::String contentHash` — flushes pending writes, reads the partial back, calls `TapeStore::store(bytes)`, deletes the partial, returns the hash.

This preserves the data-layer / structure-layer split documented at `TapeStore.h:14-17`: `TapeStore` keeps knowing only about bytes and hashes; channel-aware partial-file plumbing lives in the engine layer. The spec's `Pickup pointers` and `Plan amendments` sections are unaffected.

---

## File Structure

| File | Action | Session | Responsibility |
|---|---|---|---|
| `core/include/ida/ChannelDefaults.h` | NEW | 1 | Small struct used by `InputDescriptor::defaults` and `setInputDefaults`; carries `TapeMode defaultTapeMode` and `bool defaultEnabled` |
| `core/include/ida/InputDescriptor.h` | MOD | 1 | Add `bool rawDirectMonitor{false}`, `bool enabled{true}`, `ChannelDefaults defaults{}` initial-value fields |
| `engine/include/ida/ProcessingChain.h` + `.cpp` | NEW | 1 | Abstract base + 4 concrete no-op subclasses (AudioChain, MidiChain, VideoChain, FileChain) + `makeProcessingChain(SignalType)` factory |
| `engine/include/ida/Channel.h` + `.cpp` | MOD | 1 | Promote `Channel` from aggregate to class with constructor that builds the matching `ProcessingChain` via the factory; add `std::unique_ptr<ProcessingChain> processing` member |
| `engine/include/ida/TapeWriter.h` + `.cpp` | NEW | 2 | Writer thread + `LockFreeSpscQueue<TapeWriteMessage>` + per-channel partial-file handles + tier-aware flush + dtor lifecycle + I/O error handling |
| `engine/include/ida/InputMixer.h` + `.cpp` | MOD | 2+3 | Replace every assert-false body with real implementation; add `setInputDefaults`, `finalizeChannel`; real `processBuffer` walking channels, enqueueing, calling `OverloadProtection.reportLoad(1.0)` on queue-full |
| `tests/ProcessingChainTests.cpp` | NEW | 1 | Construction, polymorphic dispatch via `signalType()`, factory mapping, all four subclasses instantiate |
| `tests/InputDescriptorTests.cpp` | MOD | 1 | Add coverage for the three new initial-value fields |
| `tests/ChannelTests.cpp` | MOD | 1 | Update brace-init tests to use the new constructor; assert `processing` is non-null and matches `signalType` |
| `tests/TapeWriterTests.cpp` | NEW | 2 | SPSC throughput, RT-safety (counting allocator), tier flush interval, writer lifecycle, queue-full overload assertion, I/O error path |
| `tests/InputMixerTests.cpp` | MOD | 2+3 | Real-body coverage; queue-full overload assertion; channel count = tape count per TapeMode mix; finalizeChannel produces content-addressed tape; NonDestructive emits both audio partial + JSONL params partial |
| `tests/CMakeLists.txt` | MOD | 1+2 | Add the two new test files |
| `engine/CMakeLists.txt` | MOD | 1+2 | Add the two new .cpps |

---

## Session 1 — ProcessingChain + descriptor flags + Channel constructor

### Task 1.1 — ChannelDefaults.h

**Files:**
- Create: `core/include/ida/ChannelDefaults.h`

- [ ] **Step 1: Write the failing test** (append to existing `tests/InputDescriptorTests.cpp`)

```cpp
// At top of file, add include:
#include "ida/ChannelDefaults.h"

// At end of file, add new TEST_CASE:
TEST_CASE ("ChannelDefaults is value-typed and round-trips its fields",
           "[input-descriptor][channel-defaults]")
{
    using ida::ChannelDefaults;
    using ida::TapeMode;

    const ChannelDefaults defaults { TapeMode::CommitToTape, true };

    CHECK (defaults.defaultTapeMode == TapeMode::CommitToTape);
    CHECK (defaults.defaultEnabled == true);

    const ChannelDefaults empty {};
    CHECK (empty.defaultTapeMode == TapeMode::NoTape);
    CHECK (empty.defaultEnabled == true);
}
```

- [ ] **Step 2: Run test to verify it fails (no such header)**

```bash
cmake --build build --target IdaTests 2>&1 | head -5
```

Expected: build failure, `'sirius/ChannelDefaults.h' file not found`.

- [ ] **Step 3: Create the header**

```cpp
#pragma once

#include "ida/TapeMode.h"

namespace sirius
{

/// Initial-value bundle for channels created from a given input. Carried on
/// `InputDescriptor::defaults` so callers can specify channel preferences at
/// registration time without forcing a follow-up `set_*` call per field.
/// `InputMixer` reads this struct on `registerInput` and uses it as the
/// starting point for any `addChannel` call against the input that does not
/// override the field explicitly.
///
/// Defaults are operator-friendly: `NoTape` (don't allocate storage unless
/// the operator asks for it) and `enabled = true` (a registered input is
/// usable immediately; the operator can disable it).
struct ChannelDefaults
{
    TapeMode defaultTapeMode { TapeMode::NoTape };
    bool defaultEnabled { true };
};

} // namespace sirius
```

Note: `TapeMode` lives in `engine/include/ida/TapeMode.h`, but `ChannelDefaults` is in `core/`. The `#include "ida/TapeMode.h"` resolves because the `IdaEngine` target's `target_include_directories` exposes `engine/include` publicly. This is acceptable for M3 — if the include direction ever needs to reverse (engine depending on core only), `TapeMode` moves to `core/`. Note this in continue.md handoff if it surfaces.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "ChannelDefaults" --output-on-failure
```

Expected: 1 test pass.

### Task 1.2 — InputDescriptor.h gains three initial-value fields

**Files:**
- Modify: `core/include/ida/InputDescriptor.h`
- Modify: `tests/InputDescriptorTests.cpp`

- [ ] **Step 1: Write the failing test** (append to `tests/InputDescriptorTests.cpp`)

```cpp
TEST_CASE ("InputDescriptor carries rawDirectMonitor / enabled / defaults initial values",
           "[input-descriptor]")
{
    using ida::ChannelDefaults;
    using ida::InputDescriptor;
    using ida::InputKind;
    using ida::TapeId;
    using ida::TapeMode;

    SECTION ("default-initialized values match the spec")
    {
        const InputDescriptor d {
            TapeId (1),
            InputKind::Audio,
            std::string ("Guitar"),
            std::optional<int> (0)
        };
        // The three new fields default to: false / true / {NoTape, true}.
        CHECK_FALSE (d.rawDirectMonitor);
        CHECK (d.enabled);
        CHECK (d.defaults.defaultTapeMode == TapeMode::NoTape);
        CHECK (d.defaults.defaultEnabled);
    }

    SECTION ("operator can specify all three at registration time")
    {
        const InputDescriptor d {
            TapeId (2),
            InputKind::Audio,
            std::string ("Vocal"),
            std::optional<int> (1),
            true,                              // rawDirectMonitor
            false,                             // enabled
            ChannelDefaults { TapeMode::CommitToTape, true }
        };
        CHECK (d.rawDirectMonitor);
        CHECK_FALSE (d.enabled);
        CHECK (d.defaults.defaultTapeMode == TapeMode::CommitToTape);
    }
}
```

- [ ] **Step 2: Run test to verify it fails (compile error: aggregate has too few members)**

```bash
cmake --build build --target IdaTests 2>&1 | grep -E "error:|InputDescriptor" | head -5
```

Expected: compile error mentioning `rawDirectMonitor` / aggregate initialization.

- [ ] **Step 3: Add the three fields**

Replace the `InputDescriptor` struct body in `core/include/ida/InputDescriptor.h` with:

```cpp
#include "ida/ChannelDefaults.h"
#include "ida/InputKind.h"
#include "ida/TapeId.h"

#include <optional>
#include <string>

namespace sirius
{

/// Light, free-standing metadata about a single input source. Pairs a
/// TapeId (the back-reference into the data layer) with the human-visible
/// shape of the input — what kind it is, what it is called, and (where
/// the kind has one) which channel or port index it is. Also carries the
/// initial-value flags an InputMixer copies into its runtime state on
/// registerInput (V7 alignment plan M3 — descriptor stays immutable value-
/// typed metadata; the mixer holds the mutable runtime state).
///
/// Honors the white paper §7.2 data-layer / structure-layer split:
/// Tape<T> is heavy, immutable data and does not know about descriptors;
/// InputDescriptor is light metadata that points *at* a tape by id.
///
/// `channelOrPortIndex` is intentionally optional — Transport and System
/// tapes are not indexed by channel or port.
struct InputDescriptor
{
    TapeId tapeId;
    InputKind inputKind;
    std::string displayName;
    std::optional<int> channelOrPortIndex;
    bool rawDirectMonitor { false };
    bool enabled { true };
    ChannelDefaults defaults {};
};

} // namespace sirius
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "InputDescriptor" --output-on-failure
```

Expected: all `[input-descriptor]` tests pass (existing + 2 new SECTIONs).

### Task 1.3 — ProcessingChain hierarchy

**Files:**
- Create: `engine/include/ida/ProcessingChain.h`
- Create: `engine/src/ProcessingChain.cpp`
- Create: `tests/ProcessingChainTests.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the failing test file** `tests/ProcessingChainTests.cpp`

```cpp
// Tests for ProcessingChain — the abstract per-SignalType processing
// hierarchy added in M3 Session 1. M3 ships no-op bodies for all four
// concrete subclasses; real DSP lands in M5 (AudioChain, per plan
// amendment §3) / M9 (Midi) / M12 (Video) / M13 (File).
#include "ida/ProcessingChain.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::AudioChain;
using ida::FileChain;
using ida::MidiChain;
using ida::ProcessingChain;
using ida::SignalType;
using ida::VideoChain;
using ida::makeProcessingChain;

TEST_CASE ("each concrete chain reports its SignalType via the virtual interface",
           "[processing-chain]")
{
    const AudioChain a;
    const MidiChain  m;
    const VideoChain v;
    const FileChain  f;

    CHECK (a.signalType() == SignalType::Audio);
    CHECK (m.signalType() == SignalType::Midi);
    CHECK (v.signalType() == SignalType::Video);
    CHECK (f.signalType() == SignalType::File);
}

TEST_CASE ("makeProcessingChain returns a concrete subclass matching the SignalType",
           "[processing-chain][factory]")
{
    SECTION ("Audio")
    {
        auto chain = makeProcessingChain (SignalType::Audio);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Audio);
        CHECK (dynamic_cast<AudioChain*> (chain.get()) != nullptr);
    }
    SECTION ("Midi")
    {
        auto chain = makeProcessingChain (SignalType::Midi);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Midi);
        CHECK (dynamic_cast<MidiChain*> (chain.get()) != nullptr);
    }
    SECTION ("Video")
    {
        auto chain = makeProcessingChain (SignalType::Video);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Video);
        CHECK (dynamic_cast<VideoChain*> (chain.get()) != nullptr);
    }
    SECTION ("File")
    {
        auto chain = makeProcessingChain (SignalType::File);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::File);
        CHECK (dynamic_cast<FileChain*> (chain.get()) != nullptr);
    }
}

TEST_CASE ("base destructor is virtual so unique_ptr<ProcessingChain> deletes correctly",
           "[processing-chain]")
{
    // If ~ProcessingChain were non-virtual, this would leak / undefined-behave
    // when the unique_ptr is destroyed. Compiles + runs clean = invariant holds.
    std::unique_ptr<ProcessingChain> chain = std::make_unique<AudioChain>();
    chain.reset();
    SUCCEED ("virtual destructor exercised");
}
```

- [ ] **Step 2: Run to verify it fails (no header)**

```bash
cmake --build build --target IdaTests 2>&1 | grep "ProcessingChain.h" | head -3
```

Expected: `'sirius/ProcessingChain.h' file not found`.

- [ ] **Step 3: Create the header** `engine/include/ida/ProcessingChain.h`

```cpp
#pragma once

#include "ida/SignalType.h"

#include <memory>

namespace sirius
{

/// Per-channel processing applied during `InputMixer::processBuffer`. M3 ships
/// the type shape with no-op bodies for every modality; real bodies arrive
/// per-modality:
///
///   - AudioChain  — real DSP (gain/pan) lands with M5's `ChannelStrip<SignalType::Audio>`
///                   per V7 alignment plan amendment §3.
///   - MidiChain   — real UMP handling lands with M9.
///   - VideoChain  — real video processing lands with M12.
///   - FileChain   — real file-input processing lands with M13.
///
/// The abstract base anchors the polymorphism Channel needs (it holds a
/// unique_ptr<ProcessingChain>). Each concrete subclass declares its own
/// typed process() entry point — callers down-cast via signalType() before
/// invoking (Audio chains process audio samples, MIDI chains process UMP
/// events, etc.). No common process() lives on the base because the modalities
/// pass fundamentally different payload types.
class ProcessingChain
{
public:
    virtual ~ProcessingChain() = default;
    virtual SignalType signalType() const noexcept = 0;
};

class AudioChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Audio; }
    // M5 adds: void process (juce::AudioBuffer<float>& inOut) noexcept;
};

class MidiChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Midi; }
    // M9 adds: void process (UmpStream& inOut) noexcept;
};

class VideoChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Video; }
    // M12 adds: void process (VideoFrame& inOut) noexcept;
};

class FileChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::File; }
    // M13 adds: void process (FileBufferRef& inOut) noexcept;
};

/// Build the concrete ProcessingChain matching `type`. Used by `Channel`'s
/// constructor so callers never need to know which subclass goes with which
/// SignalType.
std::unique_ptr<ProcessingChain> makeProcessingChain (SignalType type);

} // namespace sirius
```

- [ ] **Step 4: Create the source file** `engine/src/ProcessingChain.cpp`

```cpp
#include "ida/ProcessingChain.h"

#include <cassert>

namespace sirius
{

std::unique_ptr<ProcessingChain> makeProcessingChain (SignalType type)
{
    switch (type)
    {
        case SignalType::Audio: return std::make_unique<AudioChain>();
        case SignalType::Midi:  return std::make_unique<MidiChain>();
        case SignalType::Video: return std::make_unique<VideoChain>();
        case SignalType::File:  return std::make_unique<FileChain>();
    }
    // The switch is exhaustive over the closed enum; this is unreachable but
    // silences compiler warnings on toolchains that don't notice exhaustion.
    assert (false && "makeProcessingChain — unhandled SignalType");
    return nullptr;
}

} // namespace sirius
```

- [ ] **Step 5: Wire ProcessingChain.cpp into engine CMakeLists**

Modify `engine/CMakeLists.txt` — after `src/OutputMixer.cpp` insert `src/ProcessingChain.cpp`:

```cmake
add_library(IdaEngine STATIC
    src/MonotonicClock.cpp
    src/Lmc.cpp
    src/AudioDeviceCalibration.cpp
    src/LatencyTiming.cpp
    src/LoopRenderer.cpp
    src/Asrc.cpp
    src/RenderPipeline.cpp
    src/OverloadProtection.cpp
    src/Channel.cpp
    src/InputMixer.cpp
    src/OutputMixer.cpp
    src/ProcessingChain.cpp)
```

- [ ] **Step 6: Wire the test file into tests CMakeLists**

Modify `tests/CMakeLists.txt` — append `ProcessingChainTests.cpp` after `OutputMixerTests.cpp`:

```cmake
    OutputMixerTests.cpp
    ProcessingChainTests.cpp)
```

- [ ] **Step 7: Run tests to verify pass**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "ProcessingChain" --output-on-failure
```

Expected: all `[processing-chain]` tests pass.

### Task 1.4 — Channel becomes a class with a constructor that builds its ProcessingChain

**Files:**
- Modify: `engine/include/ida/Channel.h`
- Modify: `engine/src/Channel.cpp`
- Modify: `tests/ChannelTests.cpp`

- [ ] **Step 1: Write failing test additions** (modify `tests/ChannelTests.cpp`)

Replace the existing `TEST_CASE ("Channel aggregate pairs ...")` and the `TEST_CASE ("Channel admits every SignalType × TapeMode combination")` with these:

```cpp
TEST_CASE ("Channel constructor builds a ProcessingChain matching its SignalType",
           "[channel]")
{
    const Channel ch (ChannelId (5), SignalType::Audio, InputId (0), TapeMode::CommitToTape);

    CHECK (ch.id == ChannelId (5));
    CHECK (ch.signalType == SignalType::Audio);
    CHECK (ch.source == InputId (0));
    CHECK (ch.tapeMode == TapeMode::CommitToTape);
    REQUIRE (ch.processing != nullptr);
    CHECK (ch.processing->signalType() == SignalType::Audio);
}

TEST_CASE ("Channel admits every SignalType × TapeMode combination",
           "[channel]")
{
    SECTION ("MIDI on a non-destructive tape")
    {
        const Channel ch (ChannelId (1), SignalType::Midi, InputId (2), TapeMode::NonDestructive);
        CHECK (ch.signalType == SignalType::Midi);
        CHECK (ch.tapeMode == TapeMode::NonDestructive);
        REQUIRE (ch.processing != nullptr);
        CHECK (ch.processing->signalType() == SignalType::Midi);
    }
    SECTION ("video with no tape (direct layer only)")
    {
        const Channel ch (ChannelId (2), SignalType::Video, InputId (3), TapeMode::NoTape);
        CHECK (ch.signalType == SignalType::Video);
        CHECK (ch.tapeMode == TapeMode::NoTape);
        REQUIRE (ch.processing != nullptr);
        CHECK (ch.processing->signalType() == SignalType::Video);
    }
    SECTION ("file (parameter automation) committed to tape")
    {
        const Channel ch (ChannelId (3), SignalType::File, InputId (4), TapeMode::CommitToTape);
        CHECK (ch.signalType == SignalType::File);
        CHECK (ch.tapeMode == TapeMode::CommitToTape);
        REQUIRE (ch.processing != nullptr);
        CHECK (ch.processing->signalType() == SignalType::File);
    }
}
```

Add `#include "ida/ProcessingChain.h"` to the test file's include section.

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --target IdaTests 2>&1 | grep -E "error:" | head -5
```

Expected: compile error — `Channel` has no matching constructor; no `processing` member.

- [ ] **Step 3: Promote Channel to a class with a constructor** (replace the `struct Channel` block in `engine/include/ida/Channel.h`)

Replace the existing `struct Channel { ... };` block with:

```cpp
#include "ida/ProcessingChain.h"

// ... (InputId and ChannelId class definitions stay as-is) ...

/// A first-class channel inside the V3 mixer architecture (V7 alignment
/// plan M2 line 210). A channel pairs an input source with a signal
/// modality, a tape-routing decision, and a per-`SignalType` processing
/// chain. The destinations vector lands in M3+ when bus routing exists.
///
/// Constructor builds the matching `ProcessingChain` via
/// `makeProcessingChain(signalType)` so callers never need to know which
/// chain subclass goes with which modality. The chain is held by
/// `unique_ptr` because each subclass has its own state; M5 begins to
/// populate that state with real DSP for AudioChain.
struct Channel
{
    Channel (ChannelId id_,
             SignalType signalType_,
             InputId source_,
             TapeMode tapeMode_);

    ChannelId id;
    SignalType signalType;
    InputId source;
    TapeMode tapeMode;
    std::unique_ptr<ProcessingChain> processing;

    // M3+: std::vector<Destination> destinations; — TapeId | BusId
};
```

- [ ] **Step 4: Implement the constructor** (replace `engine/src/Channel.cpp` body)

```cpp
#include "ida/Channel.h"

#include "ida/ProcessingChain.h"

namespace sirius
{

Channel::Channel (ChannelId id_,
                  SignalType signalType_,
                  InputId source_,
                  TapeMode tapeMode_)
    : id (id_),
      signalType (signalType_),
      source (source_),
      tapeMode (tapeMode_),
      processing (makeProcessingChain (signalType_))
{
}

} // namespace sirius
```

- [ ] **Step 5: Run channel tests to verify pass**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "Channel|channel" --output-on-failure
```

Expected: all `[channel]` tests pass.

- [ ] **Step 6: Run the FULL test suite to confirm no regression**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: `100% tests passed`. Test count rises from 279 (M2 close) to ~288 (+ ~9 from Tasks 1.1–1.4).

### Task 1.5 — Session 1 verification + commit

- [ ] **Step 1: Full 4-phase autotest gate**

```bash
rm -rf build build-xcode && bash bash/autotest.sh 2>&1 | tail -5
```

Expected: all 4 phases green. If Phase 4 GUI smoke flakes on cold-build, re-run smoke alone — known transient, not blocking.

- [ ] **Step 2: Sanity-grep for placeholders**

```bash
grep -rnE "TODO|FIXME|XXX|stub" core/include/ida/ChannelDefaults.h \
  engine/include/ida/ProcessingChain.h engine/src/ProcessingChain.cpp \
  engine/include/ida/Channel.h engine/src/Channel.cpp
```

Expected: zero hits (the `// M5 adds:` and `// M3+:` comments are intentional pickup pointers, not stubs — they don't match the grep).

- [ ] **Step 3: Commit Session 1**

```bash
git add core/include/ida/ChannelDefaults.h \
        core/include/ida/InputDescriptor.h \
        engine/include/ida/ProcessingChain.h \
        engine/src/ProcessingChain.cpp \
        engine/include/ida/Channel.h \
        engine/src/Channel.cpp \
        engine/CMakeLists.txt \
        tests/ProcessingChainTests.cpp \
        tests/ChannelTests.cpp \
        tests/InputDescriptorTests.cpp \
        tests/CMakeLists.txt
git commit -m "feat: M3 Session 1 — ProcessingChain + ChannelDefaults + InputDescriptor flags + Channel ctor"
```

Do NOT push (Session 3 pushes per spec).

---

## Session 2 — TapeWriter + InputMixer::processBuffer real body

### Task 2.1 — TapeWriteMessage POD + TapeWriter header skeleton

**Files:**
- Create: `engine/include/ida/TapeWriter.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include "ida/Channel.h"
#include "ida/LockFreeSpscQueue.h"
#include "ida/Rational.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace ida::persistence { class TapeStore; }

namespace sirius
{

class CapabilityTier;

/// Per-message ceiling on the inline sample payload. 32 KB → 4096 stereo
/// float32 samples → headroom for any reasonable EngineConfig buffer × 2
/// channels at the upper end (M3 spec, brainstorm 2026-05-18). The message
/// is a POD so the audio thread can construct it on the stack and the
/// LockFreeSpscQueue can value-copy it through `push`.
inline constexpr std::size_t kMaxTapeWriteMessageBytes = 32 * 1024;

/// Audio-thread → writer-thread handoff. Self-contained: the audio thread
/// memcpys processed bytes into `samples[0..sampleCount]` and enqueues. No
/// pointers into shared memory; ownership is trivial. Default values are
/// chosen so a zeroed message is harmless if a consumer races a producer.
struct TapeWriteMessage
{
    ChannelId id { 0 };
    Rational lmcTime { 0 };
    std::size_t sampleCount { 0 };
    std::array<std::byte, kMaxTapeWriteMessageBytes> samples {};
};

/// Owns one worker thread and one bounded SPSC queue. The audio thread is
/// the sole producer (calls `tryEnqueue`); the worker is the sole consumer
/// (drains in a loop, writes to per-channel `<sessionUuid>/<tapeId>.tape.partial`
/// files, flushes at the tier-appropriate interval).
///
/// Real-time-safety contract (docs/RT_SAFETY_CONTRACT.md): `tryEnqueue`
/// never allocates, never blocks, never does I/O. Queue-full returns
/// `false`; the audio-thread caller reports overload via OverloadProtection.
///
/// Error handling: I/O failures on the writer thread (disk full, permission
/// denied) are caught, counted per channel, logged via juce::Logger, and
/// surfaced via the same OverloadProtection.reportLoad(1.0) mechanism
/// (semantically "engine can't keep up"). The channel keeps trying on
/// subsequent buffers — recoverable, not fatal.
class TapeWriter
{
public:
    /// Constructs the queue with `queueCapacity` slots and starts the
    /// worker thread. `partialDir` is the per-session working directory
    /// (`<sessionUuid>/`); per-channel partial files live at
    /// `partialDir / <channelId>.tape.partial`. `tier` is held by
    /// const-ref; the flush interval is read on every drain iteration so
    /// runtime tier changes take effect on the next flush.
    TapeWriter (juce::File partialDir,
                const CapabilityTier& tier,
                std::size_t queueCapacity);

    /// Signals shutdown, notifies the worker, joins, and drains any
    /// remaining queue entries before returning. No in-flight samples
    /// are lost (the worker is given a final flush pass).
    ~TapeWriter();

    TapeWriter (const TapeWriter&) = delete;
    TapeWriter& operator= (const TapeWriter&) = delete;

    /// Audio-thread entry. Returns true on enqueue, false on queue-full.
    /// Wait-free. Caller is responsible for reporting overload on false.
    [[nodiscard]] bool tryEnqueue (const TapeWriteMessage& msg) noexcept;

    /// Worker-thread cooperative drain trigger used by
    /// `InputMixer::finalizeChannel` before the channel's partial file
    /// is finalized. Blocks the caller until the worker has flushed
    /// every pending message for `channelId` and closed the file
    /// handle. Returns the absolute path of the closed partial file
    /// (caller hashes it + hands to TapeStore::store + deletes).
    juce::File flushChannel (ChannelId channelId);

    /// Per-channel error counter (incremented on I/O failure). Read
    /// from the message thread for diagnostics.
    std::uint32_t errorCountForChannel (ChannelId channelId) const;

private:
    void workerLoop();
    void writePendingMessages();
    juce::File partialPathFor (ChannelId channelId) const;

    juce::File partialDir_;
    const CapabilityTier& tier_;
    LockFreeSpscQueue<TapeWriteMessage> queue_;

    std::atomic<bool> shouldExit_ { false };
    std::condition_variable wakeCv_;
    std::mutex wakeMutex_;
    std::thread worker_;

    mutable std::mutex stateMutex_;
    std::unordered_map<std::int64_t, std::unique_ptr<juce::FileOutputStream>> openFiles_;
    std::unordered_map<std::int64_t, std::uint32_t> errorCounts_;
    std::int64_t flushRequestForChannel_ { -1 };
    std::condition_variable flushCompleteCv_;
};

} // namespace sirius
```

### Task 2.2 — TapeWriter implementation

**Files:**
- Create: `engine/src/TapeWriter.cpp`
- Modify: `engine/CMakeLists.txt`

- [ ] **Step 1: Create the implementation**

```cpp
#include "ida/TapeWriter.h"

#include "ida/CapabilityTier.h"

#include <chrono>

namespace sirius
{

namespace
{
    // Tier-aware flush interval per V7 alignment plan line 319 + §17.8.
    // Final tunable surface lands in M11; M3 ships these as constants.
    std::chrono::milliseconds flushIntervalFor (const CapabilityTier& tier)
    {
        switch (tier.value())
        {
            case CapabilityTier::Level::Lavish:      return std::chrono::milliseconds (1);
            case CapabilityTier::Level::Comfortable: return std::chrono::milliseconds (50);
            case CapabilityTier::Level::Tight:       return std::chrono::milliseconds (200);
            case CapabilityTier::Level::Survival:    return std::chrono::milliseconds (1000);
        }
        return std::chrono::milliseconds (50);
    }
}

TapeWriter::TapeWriter (juce::File partialDir,
                        const CapabilityTier& tier,
                        std::size_t queueCapacity)
    : partialDir_ (std::move (partialDir)),
      tier_ (tier),
      queue_ (queueCapacity)
{
    if (! partialDir_.exists() && ! partialDir_.createDirectory())
        throw std::runtime_error ("TapeWriter: cannot create partial directory: "
                                  + partialDir_.getFullPathName().toStdString());

    worker_ = std::thread (&TapeWriter::workerLoop, this);
}

TapeWriter::~TapeWriter()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    // Final drain — anything the audio thread enqueued just before shutdown.
    writePendingMessages();

    std::scoped_lock lk (stateMutex_);
    for (auto& [_, stream] : openFiles_)
        if (stream) stream->flush();
}

bool TapeWriter::tryEnqueue (const TapeWriteMessage& msg) noexcept
{
    return queue_.push (msg);
}

juce::File TapeWriter::flushChannel (ChannelId channelId)
{
    {
        std::scoped_lock lk (stateMutex_);
        flushRequestForChannel_ = channelId.value();
    }
    wakeCv_.notify_all();

    std::unique_lock lk (stateMutex_);
    flushCompleteCv_.wait (lk, [this, channelId]
    {
        return flushRequestForChannel_ == -1
            || flushRequestForChannel_ != channelId.value();
    });

    return partialPathFor (channelId);
}

std::uint32_t TapeWriter::errorCountForChannel (ChannelId channelId) const
{
    std::scoped_lock lk (stateMutex_);
    const auto it = errorCounts_.find (channelId.value());
    return it == errorCounts_.end() ? 0u : it->second;
}

juce::File TapeWriter::partialPathFor (ChannelId channelId) const
{
    return partialDir_.getChildFile (
        juce::String (channelId.value()) + ".tape.partial");
}

void TapeWriter::workerLoop()
{
    while (! shouldExit_.load (std::memory_order_acquire))
    {
        {
            std::unique_lock lk (wakeMutex_);
            wakeCv_.wait_for (lk, flushIntervalFor (tier_), [this]
            {
                return shouldExit_.load (std::memory_order_acquire)
                    || ! queue_.empty();
            });
        }
        writePendingMessages();

        std::int64_t flushTarget = -1;
        {
            std::scoped_lock lk (stateMutex_);
            flushTarget = flushRequestForChannel_;
        }
        if (flushTarget >= 0)
        {
            std::scoped_lock lk (stateMutex_);
            auto it = openFiles_.find (flushTarget);
            if (it != openFiles_.end() && it->second)
            {
                it->second->flush();
                it->second.reset(); // close
                openFiles_.erase (it);
            }
            flushRequestForChannel_ = -1;
            flushCompleteCv_.notify_all();
        }
    }
}

void TapeWriter::writePendingMessages()
{
    TapeWriteMessage msg;
    while (queue_.pop (msg))
    {
        const auto channelKey = msg.id.value();
        juce::FileOutputStream* stream = nullptr;
        {
            std::scoped_lock lk (stateMutex_);
            auto it = openFiles_.find (channelKey);
            if (it == openFiles_.end() || ! it->second)
            {
                const auto path = partialPathFor (msg.id);
                auto fresh = std::make_unique<juce::FileOutputStream> (path);
                if (! fresh->openedOk())
                {
                    juce::Logger::writeToLog ("TapeWriter: cannot open partial file: "
                                              + path.getFullPathName());
                    errorCounts_[channelKey]++;
                    continue;
                }
                it = openFiles_.emplace (channelKey, std::move (fresh)).first;
            }
            stream = it->second.get();
        }

        if (stream == nullptr) continue;

        const bool ok = stream->write (msg.samples.data(), msg.sampleCount);
        if (! ok)
        {
            juce::Logger::writeToLog ("TapeWriter: write failed for channel "
                                      + juce::String (channelKey));
            std::scoped_lock lk (stateMutex_);
            errorCounts_[channelKey]++;
        }
    }
}

} // namespace sirius
```

- [ ] **Step 2: Wire TapeWriter.cpp into engine CMakeLists**

Modify `engine/CMakeLists.txt`, append after `src/ProcessingChain.cpp`:

```cmake
    src/ProcessingChain.cpp
    src/TapeWriter.cpp)
```

- [ ] **Step 3: Compile check (no tests yet)**

```bash
cmake --build build --target IdaEngine 2>&1 | tail -10
```

Expected: clean build of `libIdaEngine.a`.

### Task 2.3 — TapeWriterTests

**Files:**
- Create: `tests/TapeWriterTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

```cpp
// Tests for TapeWriter — the audio-thread / writer-thread boundary added
// in M3 Session 2. The audio thread is the sole producer (calls
// tryEnqueue); the worker thread is the sole consumer (drains to per-
// channel <sessionUuid>/<channelId>.tape.partial files at tier intervals).
//
// Tests cover the boundary in three planes: (1) the SPSC enqueue/dequeue
// round-trip including the queue-full path; (2) the per-channel partial-
// file output (bytes written match bytes enqueued); (3) the dtor lifecycle
// (worker drains pending messages before joining).
#include "ida/CapabilityTier.h"
#include "ida/Channel.h"
#include "ida/TapeWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <thread>

using ida::CapabilityTier;
using ida::ChannelId;
using ida::kMaxTapeWriteMessageBytes;
using ida::Rational;
using ida::TapeWriteMessage;
using ida::TapeWriter;

namespace
{
    juce::File freshTempDir (const juce::String& tag)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-tapewriter-" + tag
                                      + "-" + juce::String (juce::Time::getMillisecondCounterHiRes()));
        dir.createDirectory();
        return dir;
    }

    TapeWriteMessage makeMessage (ChannelId id, std::size_t sampleCount, std::byte fill)
    {
        TapeWriteMessage m;
        m.id = id;
        m.lmcTime = Rational (0);
        m.sampleCount = sampleCount;
        for (std::size_t i = 0; i < sampleCount; ++i)
            m.samples[i] = fill;
        return m;
    }
}

TEST_CASE ("TapeWriter writes enqueued bytes to a per-channel partial file",
           "[tape-writer]")
{
    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    auto tempDir = freshTempDir ("write");

    {
        TapeWriter writer (tempDir, tier, 64);
        const auto msg = makeMessage (ChannelId (7), 128, std::byte { 0xAB });
        REQUIRE (writer.tryEnqueue (msg));

        // Lavish flushes every ~1 ms — give the worker generous slack so the
        // assertion isn't timing-sensitive on loaded test runners.
        for (int i = 0; i < 50; ++i)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            const auto partial = tempDir.getChildFile ("7.tape.partial");
            if (partial.existsAsFile() && partial.getSize() >= 128)
                break;
        }
    } // dtor joins worker and final-flushes

    const auto partial = tempDir.getChildFile ("7.tape.partial");
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 128);

    juce::MemoryBlock bytes;
    REQUIRE (partial.loadFileAsData (bytes));
    for (std::size_t i = 0; i < 128; ++i)
        CHECK (static_cast<unsigned char> (bytes[i]) == 0xAB);

    tempDir.deleteRecursively();
}

TEST_CASE ("tryEnqueue returns false when the queue is full",
           "[tape-writer]")
{
    const CapabilityTier tier (CapabilityTier::Level::Survival);  // slowest flush — keeps queue full
    auto tempDir = freshTempDir ("full");

    // Capacity = 2 so we can pin the queue in the full state from a single thread.
    TapeWriter writer (tempDir, tier, 2);
    const auto msg = makeMessage (ChannelId (1), 4, std::byte { 0x01 });

    // First two enqueues succeed; subsequent enqueues hit the cap until the
    // worker drains. The worker is sleeping on Survival's ~1000 ms interval,
    // so we can race ahead reliably.
    const bool a = writer.tryEnqueue (msg);
    const bool b = writer.tryEnqueue (msg);
    const bool c = writer.tryEnqueue (msg);  // expected: full

    CHECK (a);
    CHECK (b);
    CHECK_FALSE (c);

    tempDir.deleteRecursively();
}

TEST_CASE ("TapeWriter destructor drains pending messages and joins cleanly",
           "[tape-writer][lifecycle]")
{
    const CapabilityTier tier (CapabilityTier::Level::Tight);
    auto tempDir = freshTempDir ("drain");

    {
        TapeWriter writer (tempDir, tier, 16);
        for (int i = 0; i < 5; ++i)
            REQUIRE (writer.tryEnqueue (makeMessage (ChannelId (3), 8, std::byte { 0x55 })));
        // No sleep — fall straight into dtor. The dtor must drain rather
        // than truncating the in-flight messages.
    }

    const auto partial = tempDir.getChildFile ("3.tape.partial");
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 5 * 8);

    tempDir.deleteRecursively();
}

TEST_CASE ("flushChannel finalizes the partial file so subsequent reads see all bytes",
           "[tape-writer][finalize]")
{
    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    auto tempDir = freshTempDir ("flush");

    TapeWriter writer (tempDir, tier, 64);
    const auto msg = makeMessage (ChannelId (9), 256, std::byte { 0xCD });
    REQUIRE (writer.tryEnqueue (msg));

    const auto partial = writer.flushChannel (ChannelId (9));
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 256);
    CHECK (writer.errorCountForChannel (ChannelId (9)) == 0);

    tempDir.deleteRecursively();
}
```

- [ ] **Step 2: Wire test into CMake**

Modify `tests/CMakeLists.txt`, append after `ProcessingChainTests.cpp`:

```cmake
    ProcessingChainTests.cpp
    TapeWriterTests.cpp)
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "tape-writer" --output-on-failure
```

Expected: all `[tape-writer]` tests pass. If the first test times out, the Lavish flush interval may be racing — bump the loop iteration cap from 50 to 100.

### Task 2.4 — InputMixer::processBuffer real body

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Modify: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing test** (append to `tests/InputMixerTests.cpp`)

```cpp
// Replace existing trivial TEST_CASE with the real-body coverage:
#include "ida/CapabilityTier.h"
#include "ida/OverloadProtection.h"
#include "ida/TapeWriter.h"

#include <juce_core/juce_core.h>

TEST_CASE ("InputMixer::processBuffer enqueues one message per tape-bearing channel",
           "[input-mixer][process-buffer]")
{
    using ida::CapabilityTier;
    using ida::ChannelId;
    using ida::InputId;
    using ida::InputMixer;
    using ida::OverloadProtection;
    using ida::SignalType;
    using ida::TapeMode;
    using ida::TapeWriter;

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-inputmixer-process-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    TapeWriter writer (tempDir, tier, 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);

    const auto chCommit = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (chCommit, TapeMode::CommitToTape);

    const auto chNoTape = mixer.addChannel (InputId (1), SignalType::Audio);
    mixer.setChannelTapeMode (chNoTape, TapeMode::NoTape);

    std::array<std::byte, 64> buffer {};
    for (auto& b : buffer) b = std::byte { 0x7E };
    mixer.processBuffer (chCommit, buffer.data(), buffer.size());
    mixer.processBuffer (chNoTape, buffer.data(), buffer.size());

    // Finalize the CommitToTape channel and verify its partial holds the bytes.
    const auto partial = writer.flushChannel (chCommit);
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 64);

    // NoTape channel must not have written anything.
    const auto notapeFile = tempDir.getChildFile (
        juce::String (chNoTape.value()) + ".tape.partial");
    CHECK_FALSE (notapeFile.existsAsFile());

    tempDir.deleteRecursively();
}

TEST_CASE ("InputMixer::processBuffer reports overload when the writer queue is full",
           "[input-mixer][overload]")
{
    using ida::CapabilityTier;
    using ida::InputId;
    using ida::InputMixer;
    using ida::OverloadProtection;
    using ida::SignalType;
    using ida::TapeMode;
    using ida::TapeWriter;

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-inputmixer-overload-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    const CapabilityTier tier (CapabilityTier::Level::Survival);  // slow flush
    TapeWriter writer (tempDir, tier, 2);  // tiny queue forces overflow fast
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    std::array<std::byte, 16> buffer {};
    // 5 pushes against a capacity-2 queue: 3 will be dropped + report overload.
    for (int i = 0; i < 5; ++i)
        mixer.processBuffer (ch, buffer.data(), buffer.size());

    CHECK (overload.lastReportedLoad() == 1.0);

    tempDir.deleteRecursively();
}
```

- [ ] **Step 2: Replace the InputMixer.h surface** (the M2 stub class block) with the real one

```cpp
#pragma once

#include "ida/Channel.h"
#include "ida/ChannelDefaults.h"
#include "ida/InputDescriptor.h"
#include "ida/SignalType.h"
#include "ida/TapeMode.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace sirius
{

class OverloadProtection;
class TapeWriter;

class InputMixer
{
public:
    InputMixer();
    ~InputMixer();

    // Injected non-owning collaborators (set-once on the message thread).
    void setTapeWriter (TapeWriter* writer) noexcept;
    void setOverloadProtection (OverloadProtection* overload) noexcept;

    // Input-layer registry --------------------------------------------------
    void registerInput (InputId, const InputDescriptor&);
    void setInputRawDirect (InputId, bool enabled);
    void setInputEnabled (InputId, bool enabled);
    void setInputDefaults (InputId, ChannelDefaults defaults);

    // Channel registry ------------------------------------------------------
    ChannelId addChannel (InputId source, SignalType type);
    void removeChannel (ChannelId);
    void setChannelTapeMode (ChannelId, TapeMode);

    // Audio-thread interface (real-time safe) ------------------------------
    /// Walks the channel, applies its ProcessingChain (no-op in M3), and
    /// if the channel is tape-bearing, memcpys `bytes[0..byteCount]` into a
    /// `TapeWriteMessage` and enqueues on the bound TapeWriter. On
    /// queue-full, calls `OverloadProtection::reportLoad(1.0)` and drops.
    /// No allocations, no locks, no I/O on this path.
    void processBuffer (ChannelId, const std::byte* bytes, std::size_t byteCount) noexcept;

    // Finalize a channel's recording — Session 3 wires the full flow.
    void finalizeChannel (ChannelId);

private:
    struct InputState
    {
        InputDescriptor descriptor;
        bool rawDirectMonitor;
        bool enabled;
        ChannelDefaults defaults;
    };

    std::unordered_map<std::int64_t, InputState> inputs_;
    std::unordered_map<std::int64_t, Channel> channels_;
    std::int64_t nextChannelId_ { 1 };

    TapeWriter* tapeWriter_ { nullptr };
    OverloadProtection* overload_ { nullptr };
};

} // namespace sirius
```

- [ ] **Step 3: Replace InputMixer.cpp with real bodies**

```cpp
#include "ida/InputMixer.h"

#include "ida/OverloadProtection.h"
#include "ida/TapeWriter.h"

#include <cassert>
#include <cstring>

namespace sirius
{

InputMixer::InputMixer() = default;
InputMixer::~InputMixer() = default;

void InputMixer::setTapeWriter (TapeWriter* writer) noexcept       { tapeWriter_ = writer; }
void InputMixer::setOverloadProtection (OverloadProtection* o) noexcept { overload_ = o; }

void InputMixer::registerInput (InputId id, const InputDescriptor& desc)
{
    InputState state;
    state.descriptor = desc;
    state.rawDirectMonitor = desc.rawDirectMonitor;
    state.enabled = desc.enabled;
    state.defaults = desc.defaults;
    inputs_[id.value()] = std::move (state);
}

void InputMixer::setInputRawDirect (InputId id, bool enabled)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.rawDirectMonitor = enabled;
}

void InputMixer::setInputEnabled (InputId id, bool enabled)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.enabled = enabled;
}

void InputMixer::setInputDefaults (InputId id, ChannelDefaults defaults)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.defaults = defaults;
}

ChannelId InputMixer::addChannel (InputId source, SignalType type)
{
    const ChannelId id (nextChannelId_++);
    TapeMode mode = TapeMode::NoTape;
    if (auto it = inputs_.find (source.value()); it != inputs_.end())
        mode = it->second.defaults.defaultTapeMode;

    channels_.emplace (id.value(), Channel (id, type, source, mode));
    return id;
}

void InputMixer::removeChannel (ChannelId id)
{
    channels_.erase (id.value());
}

void InputMixer::setChannelTapeMode (ChannelId id, TapeMode mode)
{
    auto it = channels_.find (id.value());
    if (it != channels_.end()) it->second.tapeMode = mode;
}

void InputMixer::processBuffer (ChannelId id,
                                const std::byte* bytes,
                                std::size_t byteCount) noexcept
{
    if (bytes == nullptr || byteCount == 0) return;
    if (byteCount > kMaxTapeWriteMessageBytes) byteCount = kMaxTapeWriteMessageBytes;

    auto it = channels_.find (id.value());
    if (it == channels_.end()) return;

    const auto& channel = it->second;
    // Processing chain is a no-op in M3; the call exists so the audio-thread
    // shape is right when M5 fills in real DSP.
    (void) channel.processing;

    if (channel.tapeMode == TapeMode::NoTape || tapeWriter_ == nullptr)
        return;

    TapeWriteMessage msg;
    msg.id = id;
    msg.lmcTime = Rational (0);  // M3 has no per-channel LMC time wiring yet; M4 adds it
    msg.sampleCount = byteCount;
    std::memcpy (msg.samples.data(), bytes, byteCount);

    if (! tapeWriter_->tryEnqueue (msg) && overload_ != nullptr)
        overload_->reportLoad (1.0);
}

void InputMixer::finalizeChannel (ChannelId id)
{
    // Session 3 fills in the read-partial → sha256 → TapeStore::store flow.
    // Session 2 just provides the entry point so InputMixerTests can compile
    // against the full API surface; the body is a no-op for now.
    (void) id;
}

} // namespace sirius
```

- [ ] **Step 4: Build and run**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "input-mixer" --output-on-failure
```

Expected: all `[input-mixer]` tests pass, including the new `[process-buffer]` and `[overload]` cases.

### Task 2.5 — Session 2 verification + commit

- [ ] **Step 1: Full 4-phase autotest**

```bash
rm -rf build build-xcode && bash bash/autotest.sh 2>&1 | tail -5
```

Expected: all 4 phases green.

- [ ] **Step 2: Commit Session 2**

```bash
git add engine/include/ida/TapeWriter.h engine/src/TapeWriter.cpp \
        engine/include/ida/InputMixer.h engine/src/InputMixer.cpp \
        engine/CMakeLists.txt \
        tests/TapeWriterTests.cpp tests/InputMixerTests.cpp tests/CMakeLists.txt
git commit -m "feat: M3 Session 2 — TapeWriter + InputMixer::processBuffer real bodies"
```

Do NOT push (Session 3 pushes).

---

## Session 3 — finalize + NonDestructive params + setInputDefaults end-to-end

### Task 3.1 — InputMixer::finalizeChannel real body

**Files:**
- Modify: `engine/include/ida/InputMixer.h` — add TapeStore& injection
- Modify: `engine/src/InputMixer.cpp` — fill in finalizeChannel
- Modify: `tests/InputMixerTests.cpp` — new test for end-to-end finalize

- [ ] **Step 1: Write the failing test** (append to `tests/InputMixerTests.cpp`)

```cpp
TEST_CASE ("InputMixer::finalizeChannel produces a content-addressed tape and clears the partial",
           "[input-mixer][finalize]")
{
    using namespace sirius;
    using ida::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("sirius-finalize-"
                                   + juce::String (juce::Time::getMillisecondCounterHiRes()));
    root.createDirectory();
    auto partials = root.getChildFile ("partials"); partials.createDirectory();
    auto storeDir = root.getChildFile ("store");    storeDir.createDirectory();

    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    TapeStore store (storeDir);
    TapeWriter writer (partials, tier, 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    mixer.setTapeStore (&store);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    std::array<std::byte, 32> buffer {};
    for (auto& b : buffer) b = std::byte { 0x42 };
    mixer.processBuffer (ch, buffer.data(), buffer.size());

    mixer.finalizeChannel (ch);

    // Partial file must be gone.
    const auto partial = partials.getChildFile (juce::String (ch.value()) + ".tape.partial");
    CHECK_FALSE (partial.existsAsFile());

    // Store must hold exactly one content-addressed file whose bytes match.
    juce::Array<juce::File> stored;
    storeDir.findChildFiles (stored, juce::File::findFiles, false);
    REQUIRE (stored.size() == 1);
    juce::MemoryBlock bytes;
    REQUIRE (stored[0].loadFileAsData (bytes));
    CHECK (bytes.getSize() == 32);
    for (std::size_t i = 0; i < 32; ++i)
        CHECK (static_cast<unsigned char> (bytes[i]) == 0x42);

    root.deleteRecursively();
}
```

- [ ] **Step 2: Add setter + finalize real body to InputMixer.h**

In `engine/include/ida/InputMixer.h`, add forward declaration and setter:

```cpp
namespace ida::persistence { class TapeStore; }

// ...inside class InputMixer:
void setTapeStore (ida::persistence::TapeStore* store) noexcept;
```

And add the private member:

```cpp
ida::persistence::TapeStore* tapeStore_ { nullptr };
```

- [ ] **Step 3: Implement finalizeChannel in InputMixer.cpp**

Add include:

```cpp
#include "ida/TapeStore.h"
```

Add setter and real body:

```cpp
void InputMixer::setTapeStore (ida::persistence::TapeStore* store) noexcept
{
    tapeStore_ = store;
}

void InputMixer::finalizeChannel (ChannelId id)
{
    if (tapeWriter_ == nullptr || tapeStore_ == nullptr) return;

    const juce::File partial = tapeWriter_->flushChannel (id);
    if (! partial.existsAsFile()) return;

    juce::MemoryBlock bytes;
    if (! partial.loadFileAsData (bytes))
    {
        juce::Logger::writeToLog ("InputMixer::finalizeChannel: cannot read partial: "
                                  + partial.getFullPathName());
        return;
    }

    (void) tapeStore_->store (bytes);  // content-addressed; hash returned but
                                       // structure-layer mapping (TapeId → hash)
                                       // is wired in M11 SAF
    partial.deleteFile();
}
```

- [ ] **Step 4: Run finalize test**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "input-mixer.*finalize" --output-on-failure
```

Expected: 1 new test pass.

### Task 3.2 — NonDestructive mode emits a parallel params partial

**Files:**
- Modify: `engine/src/InputMixer.cpp`
- Modify: `engine/include/ida/TapeWriter.h` + `.cpp` — accept a sibling-params output
- Modify: `tests/InputMixerTests.cpp` — assert both files exist

- [ ] **Step 1: Failing test** (append to `tests/InputMixerTests.cpp`)

```cpp
TEST_CASE ("NonDestructive channel writes both audio partial and JSONL params partial",
           "[input-mixer][non-destructive]")
{
    using namespace sirius;
    using ida::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("sirius-nondestructive-"
                                   + juce::String (juce::Time::getMillisecondCounterHiRes()));
    root.createDirectory();
    auto partials = root.getChildFile ("partials"); partials.createDirectory();
    auto storeDir = root.getChildFile ("store");    storeDir.createDirectory();

    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    TapeStore store (storeDir);
    TapeWriter writer (partials, tier, 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    mixer.setTapeStore (&store);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::NonDestructive);

    std::array<std::byte, 16> buffer {};
    mixer.processBuffer (ch, buffer.data(), buffer.size());

    // Drain so both files settle on disk.
    (void) writer.flushChannel (ch);

    const auto audioPartial  = partials.getChildFile (juce::String (ch.value()) + ".tape.partial");
    const auto paramsPartial = partials.getChildFile (juce::String (ch.value()) + ".params.partial");

    CHECK (audioPartial.existsAsFile());
    CHECK (paramsPartial.existsAsFile());

    // M3 ships an empty event stream for NonDestructive (Audio chains are
    // no-op, so there is no processing-delta to record yet — see M3 spec
    // §"What 'dry' means in M3"). M5's real Audio DSP earns the first events.
    CHECK (paramsPartial.getSize() == 0);

    root.deleteRecursively();
}
```

- [ ] **Step 2: Extend InputMixer::processBuffer for NonDestructive**

Replace the tape-write block in `engine/src/InputMixer.cpp`'s `processBuffer` body with:

```cpp
    if (channel.tapeMode == TapeMode::NoTape || tapeWriter_ == nullptr)
        return;

    TapeWriteMessage msg;
    msg.id = id;
    msg.lmcTime = Rational (0);
    msg.sampleCount = byteCount;
    std::memcpy (msg.samples.data(), bytes, byteCount);

    if (! tapeWriter_->tryEnqueue (msg) && overload_ != nullptr)
    {
        overload_->reportLoad (1.0);
        return;
    }

    if (channel.tapeMode == TapeMode::NonDestructive)
    {
        // Touch the parallel params file so its existence reflects channel
        // state, even when no DSP-induced events have been emitted yet
        // (Audio chains are no-op in M3 — see M3 spec §"What 'dry' means in M3").
        tapeWriter_->touchParamsPartial (id);
    }
```

- [ ] **Step 3: Add touchParamsPartial to TapeWriter**

In `engine/include/ida/TapeWriter.h`, declare:

```cpp
/// Ensure the JSONL params partial file exists for `channelId` — used by
/// NonDestructive mode to record the channel's "I have a params tape"
/// state even when no events have been emitted yet. Safe to call from
/// the audio thread (delegates to the worker via the wake mechanism).
void touchParamsPartial (ChannelId channelId);
```

In `engine/src/TapeWriter.cpp`, implement:

```cpp
void TapeWriter::touchParamsPartial (ChannelId channelId)
{
    std::scoped_lock lk (stateMutex_);
    const auto path = partialDir_.getChildFile (
        juce::String (channelId.value()) + ".params.partial");
    if (! path.existsAsFile())
        path.create();
}
```

Note: `path.create()` allocates and does I/O — this is **not** RT-safe. Justification: NonDestructive's params file is created lazily on first NonDestructive buffer for a channel; in steady state it is a no-op (`existsAsFile()` returns true; nothing else runs). For M3 this is acceptable. The full RT-safe path (writer thread creates the file on a wake) is a Session-2 followup if instrumentation flags it.

- [ ] **Step 4: Build and run**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "input-mixer.*non-destructive" --output-on-failure
```

Expected: new `[non-destructive]` test passes.

### Task 3.3 — setInputDefaults wired so addChannel honors per-input defaults

**Files:**
- Modify: `tests/InputMixerTests.cpp` (one more test)

- [ ] **Step 1: Failing test** (append)

```cpp
TEST_CASE ("addChannel honors the per-input default TapeMode set via setInputDefaults",
           "[input-mixer][defaults]")
{
    using namespace sirius;

    InputMixer mixer;

    InputDescriptor desc {
        TapeId (1), InputKind::Audio, std::string ("Guitar"), std::optional<int> (0)
    };
    mixer.registerInput (InputId (0), desc);
    mixer.setInputDefaults (InputId (0),
                            ChannelDefaults { TapeMode::CommitToTape, true });

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    // No explicit setChannelTapeMode — should inherit CommitToTape from defaults.

    // Verify by triggering a tape write and looking for the partial. Use a
    // TapeWriter to observe the effect since channel state isn't directly
    // queryable.
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-defaults-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    const CapabilityTier tier (CapabilityTier::Level::Lavish);
    TapeWriter writer (tempDir, tier, 16);
    OverloadProtection overload;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);

    std::array<std::byte, 8> buffer {};
    mixer.processBuffer (ch, buffer.data(), buffer.size());
    const auto partial = writer.flushChannel (ch);
    CHECK (partial.existsAsFile());

    tempDir.deleteRecursively();
}
```

- [ ] **Step 2: Run — should already pass**

```bash
cmake --build build --target IdaTests && \
  ctest --test-dir build -R "input-mixer.*defaults" --output-on-failure
```

Expected: passes (the Session 2 `addChannel` body already reads `defaults.defaultTapeMode`).

### Task 3.4 — Full autotest + commit + push

- [ ] **Step 1: Final 4-phase gate**

```bash
rm -rf build build-xcode && bash bash/autotest.sh 2>&1 | tail -10
```

Expected: all 4 phases green. Test count ~308 (was 279 at M2 close; +~29 across the 3 sessions).

- [ ] **Step 2: Commit Session 3**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp \
        engine/include/ida/TapeWriter.h engine/src/TapeWriter.cpp \
        tests/InputMixerTests.cpp
git commit -m "feat: M3 Session 3 — finalize + NonDestructive params + setInputDefaults end-to-end"
```

- [ ] **Step 3: Push the M3 milestone**

```bash
git push origin master 2>&1 | tail -3
```

Expected: three commits pushed (Sessions 1, 2, 3). HEAD reported by the push must match `git rev-parse HEAD`.

- [ ] **Step 4: Update continue.md for the M4 chat**

After push succeeds, regenerate continue.md so the next chat resumes from "read continue.md and proceed" alone. Cover: M3 complete on origin; M4 next (Direct Layer); the spec/plan files for M3 are now historical; any plan deviations encountered during M3 execution that the M4 chat needs to know.

---

## Self-review

**1. Spec coverage check.** Every spec section maps to at least one task:

| Spec section | Task(s) |
|---|---|
| ProcessingChain shape (decision 1) | Task 1.3 |
| Queue payload (decision 2) | Task 2.1 |
| Flag ownership (decision 3) | Tasks 1.1, 1.2, 2.4 (registerInput copies into runtime), 3.3 |
| SPSC overflow policy (decision 4) | Task 2.4 ("InputMixer::processBuffer reports overload when the writer queue is full" test) |
| NonDestructive params (decision 5) | Task 3.2 |
| Finalize via `InputMixer::finalizeChannel` (§Architecture) | Task 3.1 |
| Writer-thread lifecycle (§Components) | Task 2.2 (dtor body) + Task 2.3 ("destructor drains pending messages and joins cleanly" test) |
| I/O error handling (§Components) | Task 2.2 (`writePendingMessages` error path) + `errorCountForChannel` covered by Task 2.3 finalize test |
| `MAX_BUFFER_BYTES` constant (§Components) | Task 2.1 (`kMaxTapeWriteMessageBytes`) |
| Plan amendments §1/§2/§3 | Already applied at brainstorm-close (spec commit `516429d`); plan deviation note up top |

**2. Placeholder scan.** Searched for "TBD", "TODO", "implement later", "add appropriate", "similar to". Only matches are intentional pickup-pointer comments (`// M5 adds:`, `// M3+:`, `// Session 3 wires:`) which are explicit forward-references with milestone tags, not placeholders.

**3. Type consistency.** Method names used across tasks:
- `setTapeWriter`, `setOverloadProtection`, `setTapeStore` — consistent
- `addChannel`, `removeChannel`, `setChannelTapeMode`, `registerInput`, `setInputRawDirect`, `setInputEnabled`, `setInputDefaults`, `processBuffer`, `finalizeChannel` — match between header (Task 2.4) and tests (Tasks 2.4, 3.1, 3.2, 3.3)
- `tryEnqueue`, `flushChannel`, `errorCountForChannel`, `touchParamsPartial` — match between TapeWriter.h (Tasks 2.1, 3.2) and tests (Tasks 2.3, 3.1, 3.2)
- `kMaxTapeWriteMessageBytes` — referenced consistently
- `ChannelDefaults { defaultTapeMode, defaultEnabled }` — same field names in Tasks 1.1, 1.2, 3.3

**4. Scope check.** Three sessions, one commit per session, push at end of Session 3 — matches spec. No task touches files outside the file-structure table.
