# File-Input Audio Routing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `FileInputRegistry` into `InputMixer::renderInputGraph` via a JUCE-free `IFileInputSourceRegistry` interface in `core/`, so a strip whose source is a file input renders that file's audio through the strip → graph → OutputMixer pipeline and reaches the speakers.

**Architecture:** Engine forward-declares a single-method JUCE-free interface; `FileInputRegistry` (audio/) implements it; `FileInputSource` (audio/) gains a static thunk + raw-pointer `pullInto` overload so the resolved callable is RT-safe and JUCE-free. Engine caches the resolved callable in per-channel state at bind time (message thread); the audio thread invokes it directly with zero map lookups, zero locks, zero allocations. No change to the existing `renderInputGraph` signature; no change to `AudioCallback`.

**Tech Stack:** C++17, JUCE 8, Catch2 v3 (test macros + `Approx`), Ninja generator, CMake. Engine tests are headless; the final operator-verify step is the only GUI surface.

**Reference docs:**
- Spec: `docs/superpowers/specs/2026-05-26-file-input-audio-routing-design.md`
- Predecessor: `docs/superpowers/specs/2026-05-25-file-input-design.md`
- RT-safety: `docs/RT_SAFETY_CONTRACT.md`
- Session handoff: `continue.md` §2 (the deferred slice this plan closes)

**Subagent pre-corrections** (from `continue.md` §4 Deviation D — apply to every test file):
- Catch2 includes: `<catch2/catch_test_macros.hpp>` + `<catch2/catch_approx.hpp>`. **NOT** `<catch2/catch_amalgamated.hpp>`.
- Wrap any float compare in `Catch::Approx` (`-Wfloat-equal` is on).
- For any test that needs to write audio files, use `juce::AudioFormatWriterOptions{}.withSampleRate(...).withNumChannels(...).withBitsPerSample(...)` (NOT the deprecated `fmt.createWriterFor(stream*, double, uint, int, ...)`).
- Use `juce::TemporaryFile`, NOT the non-existent `juce::TempDirectoryDeleter`.

**House rules** (apply to every task):
- Work on `master`. Single-line commit message in the form `<type>: <short title>` per `~/.claude/CLAUDE.md`.
- After each task: commit AND push to `origin/master` per `feedback_subagents_push_to_master`. Never `--amend` a pushed commit.
- Each task must build cleanly under `-Werror` and the relevant test slice must pass before commit.

---

## File Structure

**New files (2):**
- `core/include/ida/IFileInputSourceRegistry.h` — JUCE-free 1-method interface + `FileInputPullCallable` POD.
- `tests/InputMixerFileInputTests.cpp` — 6 Catch2 cases tagged `[file-input][input-mixer]`.

**Edited files (8):**
- `core/include/ida/Channel.h` — add `kFileInputIdBase` constant.
- `audio/include/ida/FileInputSource.h` — declare raw-pointer `pullInto` overload + `pullIntoStatic` thunk.
- `audio/src/FileInputSource.cpp` — implement both.
- `audio/include/ida/FileInputRegistry.h` — inherit `IFileInputSourceRegistry`; declare override.
- `audio/src/FileInputRegistry.cpp` — implement `resolveFileInputPull`; initialize `nextFileInputId_` from constant.
- `engine/include/ida/InputMixer.h` — forward-declare interface/struct; add two setters; add `FileInputPullCallable` to channel state.
- `engine/src/InputMixer.cpp` — implement setters; add branch in `renderInputGraph`.
- `app/MainComponent.cpp` — wire registry pointer once; call `setChannelFileInputSource` for file-input strips; audit register/unregister bracket.
- `tests/FileInputRegistryTests.cpp` — one new case for `resolveFileInputPull`.

**No changes to:** spec docs other than the spec already committed; whitepaper; OTTO submodule; `persistence/`, `host/`, `ui/`; any UI surface in `app/`.

---

## Task 1: Core scaffolding — `kFileInputIdBase` constant + `IFileInputSourceRegistry` interface

**Files:**
- Modify: `core/include/ida/Channel.h` (add one constant near `InputId`)
- Create: `core/include/ida/IFileInputSourceRegistry.h`

Scaffolding only — no behaviour change. Verified by compile.

- [ ] **Step 1: Locate `InputId` in `core/include/ida/Channel.h`**

Run: `grep -n "InputId\b" /Users/larryseyer/IDA/core/include/ida/Channel.h | head`
Expected: at least one line declaring `InputId` (a strong typedef around `std::int64_t`).

- [ ] **Step 2: Add the `kFileInputIdBase` constant**

In `core/include/ida/Channel.h`, immediately after the `InputId` declaration (or in the same `namespace ida {}` block), add:

```cpp
/// File-input InputIds start at this value. Below = device-input ids.
/// FileInputRegistry initializes its allocator from this constant, and
/// engine code can use it for debug assertions on file-input bindings.
/// (Actual dispatch goes through the cached FileInputPullCallable —
/// engine does NOT branch on this constant in the hot path.)
static constexpr InputId kFileInputIdBase { 100000 };
```

Match the existing brace-init style for `InputId`. If `InputId` is a strong typedef, use `InputId { 100000 }`; if it's a raw `std::int64_t` alias, use `static_cast<InputId>(100000)`.

- [ ] **Step 3: Create the `IFileInputSourceRegistry` header**

Create `core/include/ida/IFileInputSourceRegistry.h`:

```cpp
#pragma once

#include "ida/Channel.h"   // ida::InputId

namespace ida
{

/// A resolved pull function for one file-input source. The pull function
/// is RT-safe (noexcept, no allocation, no locks, no I/O); returns true
/// on success or false when the source has no data to provide (caller
/// fills the destination with silence). The userdata pointer remains
/// valid until the source is unregistered.
///
/// Engine resolves these on the message thread and caches them in channel
/// state; the audio thread invokes the cached pair directly with zero
/// map lookups.
struct FileInputPullCallable
{
    using Fn = bool (*) (void* userdata, float* L, float* R, int numFrames) noexcept;

    Fn    fn       { nullptr };
    void* userdata { nullptr };

    bool valid() const noexcept { return fn != nullptr; }
};

/// JUCE-free seam: engine consumes this; audio/FileInputRegistry implements it.
/// One method by design — keeps the engine layer free of juce_audio_formats.
class IFileInputSourceRegistry
{
public:
    virtual ~IFileInputSourceRegistry() = default;

    /// Resolves a pull callable for the source registered under `id`.
    /// Returns an invalid callable (fn == nullptr) when `id` is unknown.
    /// Message thread only. The returned callable's userdata is owned by
    /// the registry; unregistering the source must bracket with audio-
    /// callback removal (the engine never invalidates the cached pair on
    /// its own).
    virtual FileInputPullCallable resolveFileInputPull (InputId id) noexcept = 0;
};

} // namespace ida
```

- [ ] **Step 4: Build everything to confirm compile**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaCore IdaAudio IdaEngine IdaTests 2>&1 | tail -10`
Expected: builds clean. The new constant + header are not yet consumed; the build verifies the header is well-formed.

- [ ] **Step 5: Commit + push**

```bash
cd /Users/larryseyer/IDA
git add core/include/ida/Channel.h core/include/ida/IFileInputSourceRegistry.h
git commit -m "feat: core — IFileInputSourceRegistry interface + kFileInputIdBase constant (file-input audio routing scaffolding)"
git push origin master
```

---

## Task 2: `FileInputSource` raw-pointer `pullInto` overload + static thunk (TDD)

**Files:**
- Modify: `audio/include/ida/FileInputSource.h` (declare new overload + static thunk)
- Modify: `audio/src/FileInputSource.cpp` (implement both)
- Modify: `tests/FileInputSourceTests.cpp` (add 1 case)

Mirrors the existing `pullInto(juce::AudioBuffer<float>&, int)` consumer of the SPSC ring, but takes raw L/R pointers — matching the JUCE-free `FileInputPullCallable::Fn` signature.

- [ ] **Step 1: Read the existing `pullInto` to mirror its semantics**

Run: `grep -n "pullInto\|underruns_" /Users/larryseyer/IDA/audio/src/FileInputSource.cpp | head -30`
Then read the existing `pullInto(juce::AudioBuffer<float>&, int)` body. The new overload must match: pop up to `numFrames` of stereo samples from the SPSC ring into L/R; on underrun fill the tail with silence and bump `underruns_`. Returns `true` (the existing pattern always "succeeds" because it silences internally; the `false` return path in `FileInputPullCallable` is reserved for future scenarios — Task 2 implementation always returns `true`).

- [ ] **Step 2: Write the failing test**

In `tests/FileInputSourceTests.cpp`, find the existing `[file-input]`-tagged cases (`grep -n "TEST_CASE" /Users/larryseyer/IDA/tests/FileInputSourceTests.cpp`) and add a new case at the end of the file:

```cpp
TEST_CASE ("FileInputSource raw-pointer pullInto consumes the ring exactly like the AudioBuffer overload",
           "[file-input][pull]")
{
    ida::FileInputSource src (48000.0);

    // Seed the ring with a known stereo pattern via the test-only helper.
    constexpr int frames = 256;
    juce::AudioBuffer<float> seed (2, frames);
    for (int n = 0; n < frames; ++n)
    {
        seed.getWritePointer (0)[n] = 0.10f + 0.001f * static_cast<float> (n);
        seed.getWritePointer (1)[n] = 0.50f - 0.001f * static_cast<float> (n);
    }
    src.testPushRing (seed);

    // Consume via the new raw-pointer overload.
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

    constexpr int seeded   = 100;
    constexpr int requested = 200;
    juce::AudioBuffer<float> seed (2, seeded);
    for (int n = 0; n < seeded; ++n)
    {
        seed.getWritePointer (0)[n] = 0.25f;
        seed.getWritePointer (1)[n] = 0.75f;
    }
    src.testPushRing (seed);

    std::vector<float> L (requested, -1.0f);   // sentinel to confirm overwrite
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
```

If `tests/FileInputSourceTests.cpp` does not yet `#include <vector>`, add it at the top.

- [ ] **Step 3: Run the tests to confirm they fail**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Expected: compile error — `'pullInto' is not a member function ...` or `'pullIntoStatic' is not a member` (whichever the compiler hits first). This confirms the tests are wired against the new API.

- [ ] **Step 4: Declare the new overload + thunk in the header**

In `audio/include/ida/FileInputSource.h`, in the public `Audio-thread (Task 5 surface — unchanged)` section, just under the existing `pullInto(juce::AudioBuffer<float>&, int) noexcept` declaration, add:

```cpp
    /// Raw-pointer overload of `pullInto`. Same SPSC-ring consumer contract
    /// as the AudioBuffer overload (noexcept, no alloc, no locks, no I/O);
    /// silences the tail on underrun and bumps `underruns_`. Returns true
    /// always (matches IFileInputSourceRegistry's contract — the `false`
    /// return path is reserved for future "source destroyed" scenarios
    /// which cannot occur given the audio-callback-bracket discipline).
    bool pullInto (float* L, float* R, int numFrames) noexcept;

    /// Static thunk matching `FileInputPullCallable::Fn`. Casts `userdata`
    /// back to `FileInputSource*` and forwards to `pullInto`. The registry
    /// returns `{&pullIntoStatic, sourcePtr}` from `resolveFileInputPull`.
    static bool pullIntoStatic (void* userdata, float* L, float* R, int numFrames) noexcept;
```

- [ ] **Step 5: Implement the overload + thunk in the .cpp**

In `audio/src/FileInputSource.cpp`, near the existing `pullInto(juce::AudioBuffer<float>&, int)` body, add:

```cpp
bool FileInputSource::pullInto (float* L, float* R, int numFrames) noexcept
{
    // Mirror the AudioBuffer overload exactly: same SPSC ring, same
    // underrun semantics. Caller-provided pointers must be non-null and
    // each must hold at least numFrames floats.
    const int writePos = writePos_.load (std::memory_order_acquire);
    int       readPos  = readPos_.load  (std::memory_order_relaxed);

    const int ringSize = static_cast<int> (ringL_.size());
    const int available = (writePos - readPos + ringSize) % ringSize;
    const int toCopy    = juce::jmin (numFrames, available);

    for (int n = 0; n < toCopy; ++n)
    {
        L[n] = ringL_[static_cast<std::size_t> (readPos)];
        R[n] = ringR_[static_cast<std::size_t> (readPos)];
        readPos = (readPos + 1) % ringSize;
    }
    readPos_.store (readPos, std::memory_order_release);

    if (toCopy < numFrames)
    {
        for (int n = toCopy; n < numFrames; ++n)
        {
            L[n] = 0.0f;
            R[n] = 0.0f;
        }
        underruns_.fetch_add (1, std::memory_order_relaxed);
    }

    return true;
}

bool FileInputSource::pullIntoStatic (void* userdata, float* L, float* R, int numFrames) noexcept
{
    return static_cast<FileInputSource*> (userdata)->pullInto (L, R, numFrames);
}
```

Adjust the ring-pop logic to match the existing `pullInto(juce::AudioBuffer<float>&, int)` body byte-for-byte if it differs in detail (it's the same ring, same atomics — easiest is to refactor the AudioBuffer overload to call the new raw-pointer overload, eliminating duplication). If you refactor:

```cpp
void FileInputSource::pullInto (juce::AudioBuffer<float>& dest, int numFrames) noexcept
{
    pullInto (dest.getWritePointer (0), dest.getWritePointer (1), numFrames);
}
```

DRY: prefer the refactor if the existing pullInto's body is non-trivial. If it's a 5-line memcpy, just duplicate.

- [ ] **Step 6: Run the new tests + the full `[file-input]` slice**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Then: `/Users/larryseyer/IDA/build/tests/IdaTests "[file-input]" 2>&1 | tail -20`
Expected: all `[file-input]` cases pass (predecessor slice = 27 cases / 1493 assertions; this task adds 3 cases). No new flakes; no regressions.

- [ ] **Step 7: Commit + push**

```bash
cd /Users/larryseyer/IDA
git add audio/include/ida/FileInputSource.h audio/src/FileInputSource.cpp tests/FileInputSourceTests.cpp
git commit -m "feat: audio — FileInputSource raw-pointer pullInto overload + pullIntoStatic thunk"
git push origin master
```

---

## Task 3: `FileInputRegistry` implements `IFileInputSourceRegistry` (TDD)

**Files:**
- Modify: `audio/include/ida/FileInputRegistry.h` (inherit + declare override)
- Modify: `audio/src/FileInputRegistry.cpp` (implement override; rebase `nextFileInputId_` on `kFileInputIdBase`)
- Modify: `tests/FileInputRegistryTests.cpp` (add 1 case)

- [ ] **Step 1: Write the failing test**

Append to `tests/FileInputRegistryTests.cpp`:

```cpp
TEST_CASE ("FileInputRegistry::resolveFileInputPull returns a valid callable that consumes the source's ring",
           "[file-input][registry][resolve]")
{
    ida::FileInputRegistry registry (48000.0);

    // Register a file input (no entries needed — the resolve path doesn't
    // depend on playlist state). Use a synthetic descriptor.
    ida::FileInputDescriptor desc {};
    desc.displayName = "TestFile";
    const auto id = registry.registerFileInput (desc);

    // Resolve the callable; it must be valid.
    const auto callable = registry.resolveFileInputPull (id);
    REQUIRE (callable.valid());
    REQUIRE (callable.fn != nullptr);
    REQUIRE (callable.userdata != nullptr);

    // Unknown ids resolve to an invalid callable.
    const auto bogus = registry.resolveFileInputPull (ida::InputId { 999999999 });
    REQUIRE_FALSE (bogus.valid());
    REQUIRE (bogus.fn == nullptr);
}
```

The "resolve consumes the ring" round-trip is already exercised by Task 2's tests against `FileInputSource` directly; the registry-level test only needs to confirm the callable resolves correctly.

- [ ] **Step 2: Run the test to confirm it fails**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Expected: compile error — `'resolveFileInputPull' is not a member of FileInputRegistry`.

- [ ] **Step 3: Make `FileInputRegistry` implement the interface (header)**

In `audio/include/ida/FileInputRegistry.h`:

Add `#include "ida/IFileInputSourceRegistry.h"` near the existing includes.

Change the class declaration from:
```cpp
class FileInputRegistry
```
to:
```cpp
class FileInputRegistry : public ida::IFileInputSourceRegistry
```

Inside the public section, add the override declaration (place it near the existing `fileInputTransportState`):

```cpp
    /// IFileInputSourceRegistry — returns a callable that forwards to
    /// the registered FileInputSource's raw-pointer pullInto. The
    /// callable's userdata is the FileInputSource pointer; lifetime is
    /// the source's lifetime (unregister must bracket the audio
    /// callback). Returns an invalid callable for unknown ids.
    FileInputPullCallable resolveFileInputPull (InputId id) noexcept override;
```

- [ ] **Step 4: Implement the override (cpp)**

In `audio/src/FileInputRegistry.cpp`, add:

```cpp
FileInputPullCallable FileInputRegistry::resolveFileInputPull (InputId id) noexcept
{
    if (auto* src = source_ (id))
        return FileInputPullCallable { &FileInputSource::pullIntoStatic, src };
    return FileInputPullCallable {};
}
```

- [ ] **Step 5: Rebase `nextFileInputId_` on the shared constant**

Still in `audio/src/FileInputRegistry.cpp` (or the header, wherever `nextFileInputId_` is initialized), change the initializer to use `ida::kFileInputIdBase`. Currently in the header at `FileInputRegistry.h:101`:

```cpp
    std::int64_t nextFileInputId_ { 100000 };
```

Change to:
```cpp
    InputId nextFileInputId_ { ida::kFileInputIdBase };
```

(If `nextFileInputId_` is `std::int64_t` rather than `InputId` for the existing arithmetic, leave the type and just initialize from `kFileInputIdBase` cast appropriately — but match the existing arithmetic in `registerFileInput` so nothing breaks.)

If `Channel.h` isn't already transitively included by `FileInputRegistry.h`, it is (via `#include "ida/Channel.h"` already at the top per the predecessor slice). Verify before editing.

- [ ] **Step 6: Run the tests**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Then: `/Users/larryseyer/IDA/build/tests/IdaTests "[file-input]" 2>&1 | tail -20`
Expected: all `[file-input]` cases pass, including the new one. The predecessor slice's existing `FileInputRegistryTests` cases (3 originally; now 4) all pass.

- [ ] **Step 7: Commit + push**

```bash
cd /Users/larryseyer/IDA
git add audio/include/ida/FileInputRegistry.h audio/src/FileInputRegistry.cpp tests/FileInputRegistryTests.cpp
git commit -m "feat: audio — FileInputRegistry implements IFileInputSourceRegistry"
git push origin master
```

---

## Task 4: `InputMixer` — registry setter + per-channel callable + `renderInputGraph` branch (TDD primary case)

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (forward-declare; add two setters; add `FileInputPullCallable` to channel state)
- Modify: `engine/src/InputMixer.cpp` (implement setters; branch in `renderInputGraph`)
- Create: `tests/InputMixerFileInputTests.cpp` (case 1 only — additional cases land in Task 5)
- Modify: `tests/CMakeLists.txt` (add the new test source)

- [ ] **Step 1: Find the channel-state struct + the per-channel gather block in `renderInputGraph`**

Run:
```bash
grep -n "struct.*[Cc]hannel\b\|renderInputGraph\|setChannelInputSource\|deviceIn\[" /Users/larryseyer/IDA/engine/include/ida/InputMixer.h /Users/larryseyer/IDA/engine/src/InputMixer.cpp | head -40
```
Then read the per-channel gather inside `renderInputGraph`'s implementation — typically a loop that resolves the channel's left/right device-channel indices, copies `deviceIn[idx]` into a stereo scratch, runs the strip. The new branch sits where the current device-channel gather happens.

- [ ] **Step 2: Add the test scaffolding — create `tests/InputMixerFileInputTests.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ida/InputMixer.h"
#include "ida/IFileInputSourceRegistry.h"
#include "ida/Channel.h"

#include <array>
#include <cstring>
#include <vector>

namespace
{

/// Minimal stub registry: returns a callable that fills L/R with a known
/// constant per InputId. Used to verify InputMixer correctly dispatches
/// file-input channels through the cached callable without ever touching
/// the registry on the audio thread.
class StubFileInputRegistry : public ida::IFileInputSourceRegistry
{
public:
    struct Pattern
    {
        float left  { 0.0f };
        float right { 0.0f };
    };

    void seed (ida::InputId id, Pattern p) { patterns_[id.value()] = p; }

    ida::FileInputPullCallable resolveFileInputPull (ida::InputId id) noexcept override
    {
        auto it = patterns_.find (id.value());
        if (it == patterns_.end()) return {};
        // Store a stable pointer to the per-id pattern so the static thunk
        // can recover it from userdata.
        return ida::FileInputPullCallable { &StubFileInputRegistry::pullStatic,
                                            &it->second };
    }

private:
    static bool pullStatic (void* userdata, float* L, float* R, int n) noexcept
    {
        const auto* p = static_cast<const Pattern*> (userdata);
        for (int i = 0; i < n; ++i) { L[i] = p->left; R[i] = p->right; }
        return true;
    }

    std::unordered_map<std::int64_t, Pattern> patterns_;
};

} // namespace

TEST_CASE ("InputMixer renders a file-input channel through its cached pull callable",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };  // 100000
    stub.seed (fileId, { 0.25f, -0.75f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    // Register the file input as a known InputId (the real registry does
    // this; here we mirror it manually via registerInput so addChannel
    // succeeds).
    ida::InputDescriptor desc {};   // default-construct; fields per existing
                                    // InputMixer contract (the engine doesn't
                                    // care about descriptor fields for file
                                    // inputs).
    mixer.registerInput (fileId, desc);

    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 128;
    std::array<float, numFrames> dummyDeviceL {};   // empty device input
    std::array<float, numFrames> dummyDeviceR {};
    const float* deviceIn[2] = { dummyDeviceL.data(), dummyDeviceR.data() };

    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);

    // Post-strip should reflect the stub's pattern (channel defaults to
    // unity gain; any per-strip processing introduced later that breaks
    // this assumption needs explicit configuration in the test).
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.25f));
        REQUIRE (postR[n] == Catch::Approx (-0.75f));
    }
}
```

If the `InputDescriptor` default constructor or `addChannel` signature has required fields not shown here, mirror the existing `tests/InputMixerTests.cpp` setup pattern for a registered input. Read `tests/InputMixerTests.cpp` to confirm the boilerplate.

- [ ] **Step 3: Register the new test file with CMake**

In `tests/CMakeLists.txt`, find the test-sources list and add `InputMixerFileInputTests.cpp` alongside the existing `FileInput*Tests.cpp` entries.

Run: `grep -n "FileInputRegistryTests\|FileInputSourceTests" /Users/larryseyer/IDA/tests/CMakeLists.txt`
Add `InputMixerFileInputTests.cpp` to the same list.

- [ ] **Step 4: Build to confirm test fails**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Expected: compile error — `'setFileInputSourceRegistry' is not a member of InputMixer` or similar.

- [ ] **Step 5: Add the two setters to `InputMixer.h`**

In `engine/include/ida/InputMixer.h`, near the top forward-declare:

```cpp
namespace ida
{
    class IFileInputSourceRegistry;
    struct FileInputPullCallable;
}
```

(Or include `"ida/IFileInputSourceRegistry.h"` since it's a JUCE-free core header — including is fine and simplifies the channel-state field below.)

In the public section, near `setChannelInputSource`, add:

```cpp
    /// Message-thread setter. The pointer is stored and used to resolve
    /// per-channel file-input callables via setChannelFileInputSource.
    /// Pass nullptr to disable file-input routing entirely (per-channel
    /// branch then short-circuits on filePull.valid()).
    void setFileInputSourceRegistry (IFileInputSourceRegistry*) noexcept;

    /// Message-thread binding. Resolves the file-input pull callable for
    /// the given InputId via the registry set by setFileInputSourceRegistry
    /// and caches it on the channel's state. Calling on a channel that was
    /// NOT created with addChannel(InputId == this id, …) is a programming
    /// error (asserted in debug; ignored in release).
    void setChannelFileInputSource (ChannelId, InputId) noexcept;
```

- [ ] **Step 6: Add the registry pointer + per-channel callable to the state declarations**

Still in `engine/include/ida/InputMixer.h`, in the private section, add:

```cpp
    IFileInputSourceRegistry* fileInputRegistry_ { nullptr };
```

For per-channel state: find the existing channel-state struct (probably named `ChannelState`, `InputState`, or similar — Step 1 located it). Add:

```cpp
    FileInputPullCallable filePull;   ///< Resolved file-input pull; invalid for device-input channels.
```

If you forward-declared rather than included `IFileInputSourceRegistry.h`, switch to including it now (the per-channel struct definition needs the full type for `FileInputPullCallable`).

- [ ] **Step 7: Implement the setters in `InputMixer.cpp`**

```cpp
void InputMixer::setFileInputSourceRegistry (IFileInputSourceRegistry* reg) noexcept
{
    fileInputRegistry_ = reg;
}

void InputMixer::setChannelFileInputSource (ChannelId channelId, InputId inputId) noexcept
{
    // Find the channel state. Match the existing lookup pattern used by
    // setChannelInputSource (e.g. iterating channels_ or indexing into a
    // map keyed by ChannelId).
    auto* state = channelStateFor (channelId);   // use whatever the existing accessor is named
    if (state == nullptr) return;

    if (fileInputRegistry_ == nullptr)
    {
        state->filePull = {};   // explicit clear
        return;
    }

    state->filePull = fileInputRegistry_->resolveFileInputPull (inputId);
}
```

If there's no existing `channelStateFor` accessor, add a private helper that mirrors the channel-lookup logic used elsewhere in the file. Match the existing style.

- [ ] **Step 8: Add the branch inside `renderInputGraph`**

Locate the per-channel source-gather block (Step 1 found it). The current shape is approximately:

```cpp
for (auto& channel : channels_)
{
    // Gather L/R from deviceIn into pre-strip scratch (mono dual-mono / stereo).
    const int li = channel.leftDeviceChannel;
    const int ri = channel.rightDeviceChannel;
    if (li < 0 || li >= numDeviceChannels) continue;

    for (int n = 0; n < numSamples; ++n) preStripL[n] = deviceIn[li][n];
    if (channel.stereo && ri >= 0 && ri < numDeviceChannels)
        for (int n = 0; n < numSamples; ++n) preStripR[n] = deviceIn[ri][n];
    else
        std::memcpy (preStripR, preStripL, sizeof (float) * numSamples);

    // …strip processing follows…
}
```

Replace with the file-input branch:

```cpp
for (auto& channel : channels_)
{
    if (channel.filePull.valid())
    {
        // File-input source: invoke the cached callable directly. On
        // false (reserved future case), silence.
        if (! channel.filePull.fn (channel.filePull.userdata, preStripL, preStripR, numSamples))
        {
            std::memset (preStripL, 0, sizeof (float) * numSamples);
            std::memset (preStripR, 0, sizeof (float) * numSamples);
        }
    }
    else
    {
        // Existing device-channel gather (unchanged).
        const int li = channel.leftDeviceChannel;
        const int ri = channel.rightDeviceChannel;
        if (li < 0 || li >= numDeviceChannels) continue;

        for (int n = 0; n < numSamples; ++n) preStripL[n] = deviceIn[li][n];
        if (channel.stereo && ri >= 0 && ri < numDeviceChannels)
            for (int n = 0; n < numSamples; ++n) preStripR[n] = deviceIn[ri][n];
        else
            std::memcpy (preStripR, preStripL, sizeof (float) * numSamples);
    }

    // …strip processing follows (unchanged)…
}
```

Match the existing variable names exactly. The above is the conceptual shape; preserve every existing line outside the new branch.

- [ ] **Step 9: Build and run the test**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Then: `/Users/larryseyer/IDA/build/tests/IdaTests "InputMixer renders a file-input channel" -v 2>&1 | tail -20`
Expected: PASS.

Then run the full `[input-mixer]` slice to confirm no regression:
`/Users/larryseyer/IDA/build/tests/IdaTests "[input-mixer]" 2>&1 | tail -20`
Expected: all `[input-mixer]` cases pass.

- [ ] **Step 10: Commit + push**

```bash
cd /Users/larryseyer/IDA
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerFileInputTests.cpp tests/CMakeLists.txt
git commit -m "feat: engine — InputMixer file-input dispatch via cached FileInputPullCallable"
git push origin master
```

---

## Task 5: `InputMixer` file-input robustness cases (TDD — round out the surface)

**Files:**
- Modify: `tests/InputMixerFileInputTests.cpp` (add cases 2–6)

Each case targets a specific edge: the implementation from Task 4 should handle most of them already — the test additions lock the behaviour in. Add tests one at a time; any case that fails means a real implementation gap to fix.

- [ ] **Step 1: Add case 2 — no registry**

Append to `tests/InputMixerFileInputTests.cpp`:

```cpp
TEST_CASE ("InputMixer file-input channel renders silence when no registry is set",
           "[file-input][input-mixer]")
{
    ida::InputMixer mixer;
    // Deliberately skip setFileInputSourceRegistry.

    const ida::InputId fileId { ida::kFileInputIdBase };
    ida::InputDescriptor desc {};
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);   // no-op (registry null)

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.0f));
        REQUIRE (postR[n] == Catch::Approx (0.0f));
    }
}
```

- [ ] **Step 2: Add case 3 — unknown InputId returns invalid callable**

```cpp
TEST_CASE ("InputMixer file-input channel renders silence when registry doesn't know the id",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    // Do NOT seed any patterns.

    const ida::InputId fileId { ida::kFileInputIdBase + 5 };
    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {};
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);   // resolves invalid

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.0f));
        REQUIRE (postR[n] == Catch::Approx (0.0f));
    }
}
```

- [ ] **Step 3: Add case 4 — device + file mixed**

```cpp
TEST_CASE ("InputMixer renders device and file channels correctly in one render call",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };
    stub.seed (fileId, { 0.10f, 0.20f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    // Device channel — InputId 0 on device channel index 0.
    const ida::InputId deviceId { 0 };
    ida::InputDescriptor devDesc {};
    mixer.registerInput (deviceId, devDesc);
    const auto devChannel = mixer.addChannel (deviceId, ida::SignalType::Audio);
    mixer.setChannelInputSource (devChannel, 0, 0, /*stereo=*/false);

    // File channel.
    ida::InputDescriptor fileDesc {};
    mixer.registerInput (fileId, fileDesc);
    const auto fileChannel = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (fileChannel, fileId);

    constexpr int numFrames = 32;
    std::array<float, numFrames> devL;
    devL.fill (0.40f);
    const float* deviceIn[1] = { devL.data() };

    mixer.renderInputGraph (deviceIn, 1, nullptr, 0, numFrames);

    const float* devPostL = mixer.postStripPointer (devChannel,  0);
    const float* filePostL = mixer.postStripPointer (fileChannel, 0);
    const float* filePostR = mixer.postStripPointer (fileChannel, 1);

    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (devPostL[n]  == Catch::Approx (0.40f));   // device-driven
        REQUIRE (filePostL[n] == Catch::Approx (0.10f));   // file-driven
        REQUIRE (filePostR[n] == Catch::Approx (0.20f));
    }
}
```

- [ ] **Step 4: Add case 5 — multi-call stability**

```cpp
TEST_CASE ("InputMixer file-input channel renders consistently across consecutive blocks",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };
    stub.seed (fileId, { 0.33f, 0.66f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {};
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };

    for (int block = 0; block < 3; ++block)
    {
        mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);
        const float* postL = mixer.postStripPointer (channelId, 0);
        const float* postR = mixer.postStripPointer (channelId, 1);
        for (int n = 0; n < numFrames; ++n)
        {
            REQUIRE (postL[n] == Catch::Approx (0.33f));
            REQUIRE (postR[n] == Catch::Approx (0.66f));
        }
    }
}
```

- [ ] **Step 5: Add case 6 — pull returns false → engine silences**

Add a second stub variant that lets the test toggle the return value:

```cpp
namespace
{
class StubReturnsFalseRegistry : public ida::IFileInputSourceRegistry
{
public:
    ida::FileInputPullCallable resolveFileInputPull (ida::InputId /*id*/) noexcept override
    {
        return ida::FileInputPullCallable { &pullStatic, nullptr };
    }
private:
    static bool pullStatic (void* /*userdata*/, float* L, float* R, int n) noexcept
    {
        // Deliberately leave L/R untouched and return false — engine must silence.
        for (int i = 0; i < n; ++i) { L[i] = 9999.0f; R[i] = 9999.0f; }
        return false;
    }
};
} // namespace

TEST_CASE ("InputMixer silences a file-input channel when the pull callable returns false",
           "[file-input][input-mixer]")
{
    StubReturnsFalseRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);
    ida::InputDescriptor desc {};
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 32;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.0f));
        REQUIRE (postR[n] == Catch::Approx (0.0f));
    }
}
```

The second stub class can live in the same anonymous namespace as the first, declared above all test cases in the file. If you'd rather keep one stub class with a toggle parameter, refactor — same outcome.

- [ ] **Step 6: Build + run the full slice**

Run: `cmake --build /Users/larryseyer/IDA/build --target IdaTests 2>&1 | tail -10`
Then: `/Users/larryseyer/IDA/build/tests/IdaTests "[file-input]" 2>&1 | tail -30`
Expected: all `[file-input]` cases pass (now 6 new `[input-mixer]` + the earlier additions).

Then a final regression sweep:
`ctest --test-dir /Users/larryseyer/IDA/build -E "(PluginEditor|MainComponentPlug)" -j 2>&1 | tail -10`
Expected: baseline 762/762 plus 6 new (= 768/768). Allow up to 3 transient flakes (`#279`/`#645`/`#756` family per `continue.md`) that pass on `--rerun-failed`.

- [ ] **Step 7: Commit + push**

```bash
cd /Users/larryseyer/IDA
git add tests/InputMixerFileInputTests.cpp
git commit -m "test: engine — InputMixer file-input dispatch edge cases (no-registry, unknown id, mixed, multi-call, false-return)"
git push origin master
```

---

## Task 6: App wiring + audio-callback bracket audit (final integration)

**Files:**
- Modify: `app/MainComponent.cpp` (wire registry pointer; bind file-input strips; audit register/unregister bracket)

This is the operator-visible payoff: pressing ▶ on a file-input player window makes audio reach the speakers.

- [ ] **Step 1: Wire the registry pointer once**

Find where `inputMixer_` is constructed/wired in `MainComponent.cpp`. The `fileInputRegistry_` member already exists. Add a one-liner immediately after `inputMixer_` is set up, AND immediately before the first `register*` call on `fileInputRegistry_`:

```cpp
inputMixer_->setFileInputSourceRegistry (&fileInputRegistry_);
```

Run: `grep -n "fileInputRegistry_\|inputMixer_.*set\|engine_->inputMixer" /Users/larryseyer/IDA/app/MainComponent.cpp | head -30` to find the right spot.

- [ ] **Step 2: Bind each file-input strip via the new setter**

Find the strip-rebuild path that currently calls `setChannelInputSource` for input channels. Grep:

```bash
grep -n "setChannelInputSource\|isFileFlags\|fileInputRegistry_\." /Users/larryseyer/IDA/app/MainComponent.cpp | head -40
```

The strip rebuild has access to whether each strip is a file input (the `isFileFlags` vector at `:6758` per the existing code). For file-input strips, replace the `setChannelInputSource` call with:

```cpp
inputMixer_->setChannelFileInputSource (channelId, inputId);
```

Device-input strips keep using `setChannelInputSource` unchanged.

Concretely the pattern looks like:
```cpp
if (isFile)
    inputMixer_->setChannelFileInputSource (channelId, inputId);
else
    inputMixer_->setChannelInputSource (channelId, leftDev, rightDev, stereo);
```

Match the existing braces / naming exactly.

- [ ] **Step 3: Audit `registerFileInput` and `unregisterFileInput` call sites for the audio-callback bracket**

Find every call:
```bash
grep -n "registerFileInput\|unregisterFileInput" /Users/larryseyer/IDA/app/MainComponent.cpp
```

For each call site, verify it is bracketed by `removeAudioCallback` ... `addAudioCallback` (or that the call site runs at a time when the audio callback is not yet attached — e.g. during initial session load before `audioDeviceAboutToStart`). Use `rebuildInputStrips()`'s bracket as the reference pattern:

```bash
grep -n "removeAudioCallback\|addAudioCallback\|rebuildInputStrips" /Users/larryseyer/IDA/app/MainComponent.cpp | head -20
```

If any `registerFileInput`/`unregisterFileInput` call is outside both contexts (i.e. could run while the audio callback is live), wrap it with the same bracket. The simplest pattern (mirroring `rebuildInputStrips`):

```cpp
deviceManager_.removeAudioCallback (audioCallback_.get());
const auto id = fileInputRegistry_.registerFileInput (desc);
inputMixer_->setChannelFileInputSource (channelId, id);   // if needed at this site
deviceManager_.addAudioCallback (audioCallback_.get());
```

Or — if the existing `rebuildInputStrips()` already runs after `registerFileInput` (which itself brackets), no extra wrapping is needed because `rebuildInputStrips` is the bracket. Confirm by reading the surrounding code.

- [ ] **Step 4: Build the app**

Run: `cmake --build /Users/larryseyer/IDA/build --target IDA 2>&1 | tail -10`
Expected: clean build.

- [ ] **Step 5: Re-run full test sweep**

```bash
ctest --test-dir /Users/larryseyer/IDA/build -E "(PluginEditor|MainComponentPlug)" -j 2>&1 | tail -10
```
Expected: 768/768 (baseline + 6 new from Task 5 + 3 from Task 2 + 1 from Task 3 = baseline 762 + 10 new = 772). Allow transient flakes on `--rerun-failed`.

- [ ] **Step 6: Launch IDA + ask operator for eyes-on verification**

```bash
open /Users/larryseyer/IDA/build/app/IDA_artefacts/Release/IDA.app
```

Then hand back to the operator with this verification recipe (from the spec's "Verification" section):

```
1. Add a file input… → pick any WAV/AIFF/FLAC. Player window opens.
2. Press ▶.
3. CONFIRM: audio is audible. The input strip's meter moves.
4. Add a second file input. Both play simultaneously; both meters move.
5. Save the session, quit, relaunch, load. Press ▶ on each. Both still play.
```

If any step fails, debug before committing — the slice is not done until the operator confirms audio reaches the speakers.

- [ ] **Step 7: Commit + push (only after operator confirms eyes-on)**

```bash
cd /Users/larryseyer/IDA
git add app/MainComponent.cpp
git commit -m "feat: app — wire FileInputRegistry into InputMixer; file-input audio now reaches speakers"
git push origin master
```

- [ ] **Step 8: Refresh `continue.md`**

The session-end discipline (`feedback_update_continue_md_every_session`): rewrite `continue.md` to reflect "file-input audio-routing slice closed; engine wiring shipped." Remove the §2 "deferred audio-routing follow-on" block; update §1.A step 4 from "audio is honestly deferred" to "audio is verifiable, meter moves." Update the baseline counts. Commit + push.

```bash
git add continue.md
git commit -m "docs: continue.md — file-input audio-routing slice closed"
git push origin master
```

---

## Self-Review Notes

- **Spec coverage:** Every spec section maps to a task. Components 1–7 in the spec map to Tasks 1–6. The two cross-cutting concerns (RT-safety contract + cross-platform) are satisfied by the implementation shape: cached function pointer (RT-safe by construction), pure C++ (cross-platform by construction).
- **Placeholder scan:** No "TBD" / "TODO" / "fill in later". Every code block is complete. The two places that say "Match the existing pattern" or "If there's no existing X, add Y" name the precise grep + the precedent (e.g. `rebuildInputStrips`).
- **Type consistency:** `FileInputPullCallable` field is named `filePull` in both the channel state addition (Task 4 Step 6) and the dispatch branch (Task 4 Step 8) and all stub test cases (Tasks 4–5). `setFileInputSourceRegistry` / `setChannelFileInputSource` names match across InputMixer header, implementation, MainComponent wiring, and tests. `kFileInputIdBase` is referenced consistently across Channel.h definition, FileInputRegistry initializer, and tests.
- **Commit count:** 7 commits total across 6 tasks (Task 6 ends with a docs commit). Each commit is independently buildable + testable.
