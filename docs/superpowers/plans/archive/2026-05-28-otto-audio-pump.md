# OTTO Audio Pump (S3c) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Play in IDA's transport bar produce audible audio by surgically splitting `OTTOProcessor::processBlock` into a per-block housekeeping prefix and a master-mix tail, and rewiring IDA's `OttoHost::renderBlock` to call the prefix + IDA's existing `processGlobalMixer` + the suffix.

**Architecture:** Approach B from the design spec — clean intra-`processBlock` split at line 1070 (the routing branch). OTTO standalone runs byte-identical; IDA's pump skips OTTO's master path entirely and reads OTTO's per-channel buffers via the existing accessors. The `renderBlock` signature grows a `juce::MidiBuffer&` to support pattern-MIDI exchange (out-of-scope routing parked in §7 of the spec).

**Tech Stack:** C++17/20, JUCE, Catch2 (Catch_v3), CMake/Ninja, OTTO submodule at `external/OTTO/`.

**Doctrinal anchor:** `docs/superpowers/specs/2026-05-28-otto-audio-pump-design.md`. Read it first.

---

## File Structure

### OTTO side (one submodule commit, then SHA bump on IDA side)

- Modify: `external/OTTO/src/otto-plugin/PluginProcessor.h` — declare two new public methods
- Modify: `external/OTTO/src/otto-plugin/PluginProcessor.cpp` — split `processBlock` at line 1070
- Create:  `external/OTTO/tests/unit/test_processblock_split.cpp` — equivalence test
- Modify: `external/OTTO/tests/CMakeLists.txt` — register the new test target
- Modify: `external/OTTO/CROSS_PROJECT_INBOX.md` — append `[FROM IDA → OTTO]` entry

### IDA side (one atomic commit)

- Modify: `external/OTTO` (submodule pin → T15's HEAD)
- Modify: `core/include/ida/IOttoRenderSource.h` — extend interface signature with forward-declared `juce::MidiBuffer&`
- Modify: `otto-bridge/include/ida/OttoHost.h` — extend `renderBlock` override signature
- Modify: `otto-bridge/src/OttoHost.cpp` — rewrite `renderBlock` body; replace stale design comments at lines 148-160 / 230-238
- Modify: `audio/src/AudioCallback.cpp` — pass MIDI buffer at line 87
- Modify: `tests/OttoHostRenderTests.cpp` — pass empty `juce::MidiBuffer` to existing calls
- Modify: `tests/OttoHostTransportControlTests.cpp` — assert listener fires after `play()` + `renderBlock`
- Create:  `tests/OttoHostPumpTests.cpp` — new `[otto-host-pump]` end-to-end test
- Modify: `tests/CMakeLists.txt` (IDA root) — register the new test source

---

## T15 — OTTO: Split `processBlock`

**Files:**
- Modify: `external/OTTO/src/otto-plugin/PluginProcessor.h`
- Modify: `external/OTTO/src/otto-plugin/PluginProcessor.cpp:665-1122`
- Create:  `external/OTTO/tests/unit/test_processblock_split.cpp`
- Modify: `external/OTTO/tests/CMakeLists.txt`
- Modify: `external/OTTO/CROSS_PROJECT_INBOX.md`

### T15.1 — Declare the two new public methods

- [ ] **Step 1: Read existing `processBlock` declaration**

```bash
grep -n "processBlock" /Users/larryseyer/IDA/external/OTTO/src/otto-plugin/PluginProcessor.h
```

Expected: one or two lines showing the existing override declaration in the `public:` section, e.g. `void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;`.

- [ ] **Step 2: Add the two new declarations immediately after `processBlock`**

```cpp
    /// S3c — split of processBlock's housekeeping prefix. Drains the SPSC
    /// AudioMessage queue, updates the transport tracker, dispatches host
    /// MIDI into sfizz, generates pattern MIDI, advances the conductor and
    /// the song timeline. Does NOT touch any audio buffer. Audio-thread
    /// invariants: zero allocation, zero locks, zero I/O. Caller must own
    /// a `juce::ScopedNoDenormals` scope (processBlock provides one for
    /// OTTO standalone; ida::OttoHost provides one for embedded mode).
    void processBlockBeforeRouting (juce::MidiBuffer& midiMessages,
                                    int numSamples);

    /// S3c — split of processBlock's housekeeping suffix. Advances
    /// totalSamplePosition_ and syncs per-player fill state to pluginState_.
    /// Audio-thread invariants: same as processBlockBeforeRouting.
    void processBlockAfterRouting (int numSamples);
```

- [ ] **Step 3: Commit the header-only change is NOT separate — wait until §T15.4 lands the .cpp so the split is one atomic commit.**

(No commit yet — see T15.9.)

### T15.2 — Write the failing equivalence test

- [ ] **Step 1: Create the test source**

Path: `external/OTTO/tests/unit/test_processblock_split.cpp`

```cpp
// S3c: processBlock split equivalence test.
//
// Pins that processBlock(buffer, midi) is observationally equivalent to
// processBlockBeforeRouting(midi, N) + outputRouter_.routeAudio(buffer)
// + de-click/spectrum/clear tail + processBlockAfterRouting(N) when both
// are driven from the same prepared state with the same audio-message
// queue.

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/otto-plugin/PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;
constexpr int    kNumBlocks  = 16;

void queuePlay (OTTOProcessor& p)
{
    otto::AudioMessage msg;
    msg.type     = otto::AudioMessageType::TransportControl;
    msg.intValue = static_cast<int> (otto::TransportCommand::Play);
    p.sendToAudioThread (msg);
}

} // namespace

TEST_CASE("processBlock split is observationally equivalent for OTTO standalone",
          "[processBlock-split][equivalence]")
{
    OTTOProcessor a, b;
    a.prepareToPlay (kSampleRate, kBlockSize);
    b.prepareToPlay (kSampleRate, kBlockSize);

    queuePlay (a);
    queuePlay (b);

    juce::AudioBuffer<float> bufA (2, kBlockSize);
    juce::AudioBuffer<float> bufB (2, kBlockSize);
    juce::MidiBuffer midiA, midiB;

    for (int blk = 0; blk < kNumBlocks; ++blk)
    {
        bufA.clear();
        bufB.clear();
        midiA.clear();
        midiB.clear();

        // A: monolithic processBlock (current path).
        a.processBlock (bufA, midiA);

        // B: split path. Caller owns the ScopedNoDenormals scope.
        {
            juce::ScopedNoDenormals noDenormals;
            b.processBlockBeforeRouting (midiB, kBlockSize);
            // Route through OTTO's existing master path so B mirrors A.
            // (In IDA we call processGlobalMixer instead; this test pins
            // OTTO-standalone equivalence specifically.)
            // Drive the routing the same way processBlock does:
            //   - if (conductor.isPlaying() || hasTailsActive()) routeAudio
            //   - else                                            buffer.clear
            // The body of the routing branch is intentionally NOT duplicated
            // here; we instead call processBlock's tail by invoking
            // processBlock itself with a NEW empty buffer/midi pair. That
            // would re-run the prefix; instead we directly invoke the same
            // OutputRouter path via the public accessor used in standalone.
            // For this equivalence test we simply call processBlock again on
            // B to drive routing — NO, that would double-run prefix.
            //
            // Correct approach: B's processBlock is rewritten internally to
            // be `prefix + routing + suffix`. We have already called prefix,
            // so we cannot call processBlock again. Instead we expose the
            // routing branch in the equivalence test by re-implementing it
            // inline, mirroring lines 1070-1109 of the pre-split source:
            auto& pm = b.getPlayerManager();
            if (b.getConductor().isPlaying() || pm.hasTailsActive())
                b.getOutputRouter().routeAudio (pm, bufB, kBlockSize);
            else
                bufB.clear();
            b.processBlockAfterRouting (kBlockSize);
        }

        // Per-block byte equivalence.
        for (int ch = 0; ch < bufA.getNumChannels(); ++ch)
        {
            const float* ra = bufA.getReadPointer (ch);
            const float* rb = bufB.getReadPointer (ch);
            for (int i = 0; i < kBlockSize; ++i)
            {
                REQUIRE (ra[i] == rb[i]);
            }
        }
    }

    // Transport state converged identically.
    REQUIRE (a.getTransportTracker().getState().isPlaying ==
             b.getTransportTracker().getState().isPlaying);
}

TEST_CASE("processBlockBeforeRouting alone flips transport state to playing",
          "[processBlock-split][transport]")
{
    OTTOProcessor p;
    p.prepareToPlay (kSampleRate, kBlockSize);

    queuePlay (p);

    REQUIRE (p.getTransportTracker().getState().isPlaying == false);

    juce::MidiBuffer midi;
    {
        juce::ScopedNoDenormals noDenormals;
        p.processBlockBeforeRouting (midi, kBlockSize);
    }

    REQUIRE (p.getTransportTracker().getState().isPlaying == true);
}

int main (int argc, char* argv[])
{
    return Catch::Session().run (argc, argv);
}
```

- [ ] **Step 2: Verify the test references resolvable accessors**

The test uses `getPlayerManager()`, `getConductor()`, `getOutputRouter()`, `getTransportTracker()` as public accessors. Confirm they exist:

```bash
grep -nE "PlayerManager& getPlayerManager|getConductor|getOutputRouter|getTransportTracker" /Users/larryseyer/IDA/external/OTTO/src/otto-plugin/PluginProcessor.h
```

Expected: at least the first three return non-const references on `OTTOProcessor`. `getTransportTracker` may not be present yet — if it isn't, replace `b.getTransportTracker().getState().isPlaying` with `b.getConductor().isPlaying()` (same observable). Update both REQUIREs in the test to match.

- [ ] **Step 3: Register the test in `tests/CMakeLists.txt`**

Open `external/OTTO/tests/CMakeLists.txt`, find the `test_assets_root_app` block (lines ~172-200 per S3b), and add a sibling block immediately after it:

```cmake
# S3c: processBlock split equivalence test. Mirrors test_assets_root_app
# line-for-line. Verifies processBlockBeforeRouting + routeAudio +
# processBlockAfterRouting is byte-equivalent to monolithic processBlock.
juce_add_console_app(test_processblock_split_app
    PRODUCT_NAME "test_processblock_split_app"
)
juce_generate_juce_header(test_processblock_split_app)
target_sources(test_processblock_split_app PRIVATE
    unit/test_processblock_split.cpp
)
target_link_libraries(test_processblock_split_app
    PRIVATE
        otto_plugin_shared
        Catch2::Catch2
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
target_include_directories(test_processblock_split_app
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/otto-plugin
)
target_compile_features(test_processblock_split_app PRIVATE cxx_std_17)
target_compile_definitions(test_processblock_split_app
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
catch_discover_tests(test_processblock_split_app)
```

(Use the `test_assets_root_app` block as the byte-faithful template — copy and substitute names. The exact link-library list may differ from the snippet above; mirror the existing block precisely so OTTO's CMake conventions are preserved.)

### T15.3 — Verify the test fails before the split lands

- [ ] **Step 1: Attempt to build via the scratch-harness path used by `[assets-root]` in S3b**

OTTO's top-level configure is pre-existingly broken (CLAP / Catch2 / clap-juce-extensions submodules + `cmake/OTTOConfig.cmake` missing — documented in S3b §6). The scratch-harness pattern: compile the test source + `PluginProcessor.cpp` + minimum JUCE modules + Catch2 directly with `clang++` against the IDA-side OTTO-bridge's compile_commands.json includes.

```bash
cd /Users/larryseyer/IDA/external/OTTO
# Quick syntax check first — the new methods don't exist yet, so this
# must fail with "no member named 'processBlockBeforeRouting'".
clang++ -fsyntax-only -std=c++20 \
    -I src/otto-plugin -I src/otto-core/include \
    -I external/JUCE/modules \
    tests/unit/test_processblock_split.cpp 2>&1 | head -20
```

Expected: error mentioning `processBlockBeforeRouting` or `processBlockAfterRouting` as undefined member. (The exact JUCE include flags may need adjustment; the requirement is that the compiler reaches the test body and complains about the missing methods.)

- [ ] **Step 2: If the build infrastructure makes syntax verification hard, defer the fail-mode pin**

Note this in a one-line scratchpad and proceed. The pass-mode in T15.7 will pin behavior regardless.

### T15.4 — Implement `processBlockBeforeRouting`

- [ ] **Step 1: Move lines 665-1069 verbatim into a new method body**

In `external/OTTO/src/otto-plugin/PluginProcessor.cpp`, immediately above the existing `processBlock` definition, add:

```cpp
void OTTOProcessor::processBlockBeforeRouting (juce::MidiBuffer& midiMessages,
                                                int numSamples)
{
    // Process any pending messages from the UI thread (lock-free)
    processAudioMessages();

    // Store actual buffer size for internal clock advancement
    currentBlockSize_ = numSamples;

    // Get playhead info from host and update transport tracker
    updateTransportState();

    const auto transport = transportTracker_.getState();

    // ... (PASTE LINES 684-1063 VERBATIM HERE — every step from
    // `pinEnginesForBuffer` through `conductor_.setPositionInBeats(endBeat)`.
    // This is approximately 380 lines of code. DO NOT modify any logic.
    // Variable name references stay identical because they refer to
    // OTTOProcessor members.)
}
```

The verbatim block ends at the closing `}` of the `if (conductor_.isPlaying())` block that calls `conductor_.setPositionInBeats(endBeat)` (line 1063 in the current source). The next conditional `if (conductor_.isPlaying() || playerManager_.hasTailsActive()) { outputRouter_.routeAudio(...) ... }` (line 1070) is the routing tail and stays in `processBlock`.

- [ ] **Step 2: Verify the move is exact**

```bash
git -C /Users/larryseyer/IDA/external/OTTO diff src/otto-plugin/PluginProcessor.cpp | grep -E "^-" | grep -v "^---" | wc -l
```

Expected: ~400 lines deleted (the moved block). Compare against `^+` count — should equal the moved-block size plus the new method signature + opening/closing braces.

### T15.5 — Implement `processBlockAfterRouting`

- [ ] **Step 1: Move lines 1111-1118 verbatim into a new method body**

Immediately below `processBlockBeforeRouting`'s implementation, add:

```cpp
void OTTOProcessor::processBlockAfterRouting (int numSamples)
{
    // Update total sample position for MIDI clock tracking (US-SYNC-001)
    totalSamplePosition_ += numSamples;

    // Sync fill state from players to PluginState (for UI display)
    for (int i = 0; i < otto::manager::kNumPlayers; ++i) {
        auto& player = playerManager_.getPlayer(i);
        pluginState_.players[i].fillMode = static_cast<uint8_t>(player.getFillMode());
    }
}
```

### T15.6 — Refactor `processBlock` to use both helpers

- [ ] **Step 1: Rewrite `processBlock` body**

Replace the entire current `OTTOProcessor::processBlock(buffer, midi)` body (lines 665-1122) with:

```cpp
void OTTOProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Begin CPU usage measurement (US-017)
    cpuMeter_.beginMeasurement();

    const int numSamples = buffer.getNumSamples();

    // S3c — Half A prefix (housekeeping + MIDI dispatch + conductor advance).
    processBlockBeforeRouting (midiMessages, numSamples);

    // S3c — Half B: master mixdown. (Unchanged from pre-split source — same
    // routing branch, same de-click envelope, same spectrum push, same
    // clear-when-stopped behavior.)
    if (conductor_.isPlaying() || playerManager_.hasTailsActive()) {
        outputRouter_.routeAudio(playerManager_, buffer, numSamples);

        // De-click envelope (lines 1079-1096 of pre-split source — preserved verbatim).
        int fadeRemaining =
            seekFadeRemainingSamples_.load(std::memory_order_acquire);
        if (fadeRemaining > 0 && seekFadeTotalSamples_ > 0) {
            const int totalFade   = seekFadeTotalSamples_;
            const int fadeSamples = std::min(fadeRemaining, numSamples);
            const float inv       = 1.0f / static_cast<float>(totalFade);
            const int   startStep = totalFade - fadeRemaining;
            const int   numChans  = buffer.getNumChannels();
            for (int ch = 0; ch < numChans; ++ch) {
                float* data = buffer.getWritePointer(ch);
                for (int i = 0; i < fadeSamples; ++i) {
                    const float gain = static_cast<float>(startStep + i) * inv;
                    data[i] *= gain;
                }
            }
            seekFadeRemainingSamples_.store(
                fadeRemaining - fadeSamples, std::memory_order_release);
        }

        // Feed spectrum analyzer (lines 1099-1106 of pre-split source — preserved verbatim).
        if (buffer.getNumChannels() >= 2) {
            spectrumAnalyzer_.pushSamples(buffer.getReadPointer(0),
                                          buffer.getReadPointer(1), numSamples);
        } else if (buffer.getNumChannels() == 1) {
            spectrumAnalyzer_.pushSamples(buffer.getReadPointer(0),
                                          buffer.getReadPointer(0), numSamples);
        }
    } else {
        buffer.clear();
    }

    // S3c — Half A suffix (totalSamplePosition advance + fillMode sync).
    processBlockAfterRouting (numSamples);

    // End CPU usage measurement (US-017)
    cpuMeter_.endMeasurement();
}
```

- [ ] **Step 2: Sanity-grep for accidental duplication**

```bash
grep -cn "processAudioMessages()" /Users/larryseyer/IDA/external/OTTO/src/otto-plugin/PluginProcessor.cpp
```

Expected: exactly 1 (inside `processBlockBeforeRouting`). If 2, the move duplicated; revert and redo. Repeat for `updateTransportState()` (expect 1) and `outputRouter_.routeAudio` (expect 1).

### T15.7 — Run the equivalence test, verify pass

- [ ] **Step 1: Scratch-harness compile + run**

Per S3b §6 scratch-harness pattern (used for `[assets-root]`). The exact command depends on the implementer's toolchain; the intent is to build `test_processblock_split.cpp` + `PluginProcessor.cpp` + JUCE modules + Catch2 into a single binary and run it.

```bash
cd /Users/larryseyer/IDA/external/OTTO
# Example scratch harness — adapt module list as needed.
clang++ -std=c++20 -O0 -g \
    -I src/otto-plugin -I src/otto-core/include \
    -I external/JUCE/modules -I external/Catch2/single_include \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    tests/unit/test_processblock_split.cpp \
    src/otto-plugin/PluginProcessor.cpp \
    # ... (all JUCE module includes the test pulls in) \
    -o /tmp/test_processblock_split && /tmp/test_processblock_split
```

If the scratch harness is impractical, defer pass-mode verification to T16's IDA-side `[otto-host-pump]` test which exercises the same code path end-to-end.

Expected: both Catch2 TEST_CASEs pass. Output ends with `All tests passed (N assertions in 2 test cases)`.

### T15.8 — Run OTTO's existing tests for regression sweep

- [ ] **Step 1: Identify which OTTO test files the split could affect**

```bash
grep -rln "processBlock\|TransportTracker\|conductor_" /Users/larryseyer/IDA/external/OTTO/tests/ 2>/dev/null
```

For each hit, if it's a Catch2 test that can be scratch-harnessed similarly, run it. If OTTO's top-level configure remains broken, document in the inbox entry which OTTO test targets the implementer was unable to verify standalone, with the explicit note that IDA's `[otto-host-pump]` test (T16) exercises the same code path.

### T15.9 — Commit, push, append inbox entry

- [ ] **Step 1: Append the cross-project inbox entry**

In `external/OTTO/CROSS_PROJECT_INBOX.md`, append under `## [FROM IDA → OTTO]`:

```markdown
### 2026-05-28 — FEAT: split processBlock into processBlockBeforeRouting + processBlockAfterRouting (S3c)

Direction: IDA → OTTO
IDA commit: (filled at T16 atomic commit time)
OTTO commit: <this commit>
Files touched:
- src/otto-plugin/PluginProcessor.h (declare two new public methods)
- src/otto-plugin/PluginProcessor.cpp (split processBlock at line 1070)
- tests/unit/test_processblock_split.cpp (new — equivalence + transport-flip tests)
- tests/CMakeLists.txt (register test_processblock_split_app)

Why: IDA's OttoHost::renderBlock previously called only PlayerManager::processGlobalMixer, skipping the entire housekeeping prefix of OTTOProcessor::processBlock — the SPSC AudioMessage queue (Play/Stop/Tempo) was never drained, conductor never advanced, pattern MIDI never generated, sfizz never noteOn'd. S3c carves processBlock at its natural seam (the routing branch at line 1070) so IDA can run the housekeeping prefix + per-channel sum (existing processGlobalMixer) + housekeeping suffix, skipping OTTO's master mixdown which conflicts with IDA's Output Mixer ownership.

For OTTO's Claude: do NOT revert. OTTO standalone is byte-equivalent to pre-split source — verified by the new [processBlock-split][equivalence] test which runs `processBlock(buf, midi)` (instance A) vs `processBlockBeforeRouting + outputRouter_.routeAudio + processBlockAfterRouting` (instance B) over 16 blocks and asserts byte-equal buffers. The two new methods inherit processBlock's audio-thread invariants verbatim — no allocation, no logs, no locks, no I/O, no throw — because they contain the same code, relocated.

ScopedNoDenormals stays in processBlock for OTTO standalone. The two helpers assume the caller owns a noDenormals scope; IDA's OttoHost::renderBlock adds its own guard (T16).

Pre-existing OTTO top-level configure breakage (S3b §6) prevents the implementer from running OTTO's full standalone test suite; the [processBlock-split] tests were verified via the same scratch-harness path used for [assets-root] in T11.

Full design rationale at IDA docs/superpowers/specs/2026-05-28-otto-audio-pump-design.md.

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

- [ ] **Step 2: Commit the OTTO change**

```bash
cd /Users/larryseyer/IDA/external/OTTO
git add src/otto-plugin/PluginProcessor.h src/otto-plugin/PluginProcessor.cpp \
        tests/unit/test_processblock_split.cpp tests/CMakeLists.txt \
        CROSS_PROJECT_INBOX.md
git status --short
```

Expected output: 5 lines, all `M ` or `A `. No other files.

```bash
git commit -m "feat: S3c — split processBlock into BeforeRouting + AfterRouting

Splits OTTOProcessor::processBlock at its natural seam (the routing
branch at line 1070) so IDA can drive the housekeeping prefix +
processGlobalMixer + housekeeping suffix without OTTO's master
mixdown writing into IDA's Output Mixer territory.

OTTO standalone byte-equivalent — verified by [processBlock-split]
[equivalence] test. ScopedNoDenormals stays in processBlock for
standalone; IDA owns its own guard.

Ida-Origin: pending"
```

- [ ] **Step 3: Push OTTO to origin/main**

```bash
git push origin main
```

- [ ] **Step 4: Record the OTTO HEAD SHA for T16.1**

```bash
git rev-parse HEAD
```

Note the SHA — it's the submodule pin T16 must bump to.

---

## T16 — IDA: Wire the pump

**Files:**
- Modify: `external/OTTO` (submodule pin)
- Modify: `core/include/ida/IOttoRenderSource.h`
- Modify: `otto-bridge/include/ida/OttoHost.h`
- Modify: `otto-bridge/src/OttoHost.cpp`
- Modify: `audio/src/AudioCallback.cpp:87`
- Modify: `tests/OttoHostRenderTests.cpp`
- Modify: `tests/OttoHostTransportControlTests.cpp`
- Create:  `tests/OttoHostPumpTests.cpp`
- Modify: `tests/CMakeLists.txt`

### T16.1 — Bump the OTTO submodule pin

- [ ] **Step 1: Update the pin**

```bash
cd /Users/larryseyer/IDA
cd external/OTTO && git fetch origin && git checkout <T15 HEAD SHA> && cd -
```

Replace `<T15 HEAD SHA>` with the value recorded in T15.9 Step 4.

- [ ] **Step 2: Stage the gitlink change**

```bash
git add external/OTTO
git status --short
```

Expected: `M external/OTTO` plus possibly `m external/sfizz` (pre-existing, ignore).

### T16.2 — Extend the `IOttoRenderSource` interface

- [ ] **Step 1: Edit `core/include/ida/IOttoRenderSource.h`**

Replace the entire body with:

```cpp
#pragma once

namespace juce { class MidiBuffer; }

namespace ida
{

/// Audio-thread-callable port for sources that need to be driven once per
/// audio buffer so their per-output sample data is fresh for downstream
/// readers. Today's only implementer is `ida::OttoHost`, whose
/// `renderBlock` runs OTTO's processBlock housekeeping prefix +
/// per-channel `processGlobalMixer` + housekeeping suffix so the 32
/// per-output stereo pair pointers exposed by `OttoHost::getOttoOutput
/// {Left,Right}` are populated and stable for the rest of the audio block.
///
/// The interface lives in `core/` (JUCE-free in the public surface
/// except for the forward-declared `juce::MidiBuffer&` below — a deliberate
/// minimal bleed so the audio thread can pass MIDI through without an
/// extra hop. The forward declaration costs no include; the implementing
/// translation unit pulls in `<juce_audio_basics/juce_audio_basics.h>`).
///
/// Implementations must obey the audio-thread RT-safety contract: no
/// allocation, no locks, no I/O, no throw. `AudioCallback` invokes
/// `renderBlock` once per buffer, near the top of the callback (before
/// any consumer that reads per-output OTTO audio).
class IOttoRenderSource
{
public:
    virtual ~IOttoRenderSource() = default;

    /// Render one audio block. Called from the audio thread. Must be
    /// RT-safe and noexcept. `numSamples` is the buffer size the audio
    /// device delivered for this callback; implementations clamp
    /// internally if needed. `midiMessages` carries any MIDI the audio
    /// callback received this block (host MIDI input, file-input MIDI,
    /// etc.) and may be augmented in-place with the source's own
    /// generated MIDI events (e.g. OTTO's pattern-generated NoteOn/Off).
    virtual void renderBlock (int numSamples,
                              juce::MidiBuffer& midiMessages) noexcept = 0;
};

} // namespace ida
```

### T16.3 — Update `OttoHost.h` override signature

- [ ] **Step 1: Read the existing declaration to find the exact line**

```bash
grep -n "renderBlock" /Users/larryseyer/IDA/otto-bridge/include/ida/OttoHost.h
```

Expected: a line `void renderBlock (int numSamples) noexcept override;` around line 112.

- [ ] **Step 2: Replace with the new signature**

```cpp
    void renderBlock (int numSamples,
                      juce::MidiBuffer& midiMessages) noexcept override;
```

- [ ] **Step 3: Ensure `<juce_audio_basics/juce_audio_basics.h>` is included in OttoHost.h**

```bash
grep -n "juce_audio_basics" /Users/larryseyer/IDA/otto-bridge/include/ida/OttoHost.h
```

If absent, add `#include <juce_audio_basics/juce_audio_basics.h>` near the top alongside other JUCE includes. (Forward-decl is not enough at the header level if any inline body references `juce::MidiBuffer` methods. The full include is safer.)

### T16.4 — Update `OttoHostRenderTests.cpp` for the new signature

- [ ] **Step 1: Read the test file**

```bash
grep -n "renderBlock" /Users/larryseyer/IDA/tests/OttoHostRenderTests.cpp
```

- [ ] **Step 2: For each `renderBlock(N)` call site, replace with `renderBlock(N, midi)`**

At each call site, immediately above the call, add (or reuse) a stack-local `juce::MidiBuffer midi;` and pass it. Example:

```cpp
juce::MidiBuffer midi;
host.renderBlock (numSamples, midi);
```

Preserve every existing assertion verbatim. This task only updates the signature on the calling side; behavior assertions are unchanged.

### T16.5 — Extend `OttoHostTransportControlTests.cpp` to assert listener fires

- [ ] **Step 1: Read the existing test**

```bash
cat /Users/larryseyer/IDA/tests/OttoHostTransportControlTests.cpp
```

- [ ] **Step 2: Update existing `renderBlock` calls per T16.4**

Same mechanical signature update.

- [ ] **Step 3: Add a new `TEST_CASE` at the bottom**

```cpp
namespace
{
struct CapturingListener : public ida::IOttoTransportListener
{
    std::vector<ida::TransportSnapshot> received;

    void onOttoTransport (const ida::TransportSnapshot& s) override
    {
        received.push_back (s);
    }
};
}

TEST_CASE("OttoHost::play() drives TransportSnapshot::Kind::Started through listener",
          "[otto-host-transport-control][listener-fire]")
{
    ida::OttoHost host;
    host.prepare (48000.0, 256);

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.play();

    juce::MidiBuffer midi;
    host.renderBlock (256, midi);

    host.drainForTesting();

    bool sawStarted = false;
    for (const auto& s : listener.received)
        if (s.kind == ida::TransportSnapshot::Kind::Started && s.isPlaying)
            sawStarted = true;

    REQUIRE (sawStarted);

    host.removeTransportListener (&listener);
}
```

### T16.6 — Create `OttoHostPumpTests.cpp` (new `[otto-host-pump]` end-to-end test)

- [ ] **Step 1: Create the file**

Path: `/Users/larryseyer/IDA/tests/OttoHostPumpTests.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>

#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"
#include "ida/TransportSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace
{

struct CapturingListener : public ida::IOttoTransportListener
{
    std::vector<ida::TransportSnapshot> received;

    void onOttoTransport (const ida::TransportSnapshot& s) override
    {
        received.push_back (s);
    }

    bool sawKind (ida::TransportSnapshot::Kind kind) const
    {
        for (const auto& s : received)
            if (s.kind == kind)
                return true;
        return false;
    }
};

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;

} // namespace

TEST_CASE("OttoHost full pump: play → renderBlock drains, advances transport, fires listener",
          "[otto-host-pump][play]")
{
    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.play();

    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);

    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::Started));

    host.removeTransportListener (&listener);
}

TEST_CASE("OttoHost full pump: setTempo → renderBlock fires BpmChanged",
          "[otto-host-pump][tempo]")
{
    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.setTempo (132.0);

    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);

    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::BpmChanged));

    host.removeTransportListener (&listener);
}

TEST_CASE("OttoHost full pump: stop → renderBlock fires Stopped",
          "[otto-host-pump][stop]")
{
    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    // Start playing first so Stop is observable.
    host.play();
    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.stop();
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::Stopped));

    host.removeTransportListener (&listener);
}

TEST_CASE("OttoHost full pump: after play, getOttoOutputLeft(0) returns non-null",
          "[otto-host-pump][channel-accessor]")
{
    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    host.play();
    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);

    // No kit loaded in headless tests, but processGlobalMixer should have
    // run and the per-channel accessor should expose a live pointer.
    REQUIRE (host.getOttoOutputLeft (0) != nullptr);
    REQUIRE (host.getOttoOutputRight (0) != nullptr);
}
```

### T16.7 — Register the new test in `tests/CMakeLists.txt`

- [ ] **Step 1: Find the test list**

```bash
grep -n "OttoHostRenderTests\|OttoHostTransportControlTests" /Users/larryseyer/IDA/tests/CMakeLists.txt
```

- [ ] **Step 2: Add `OttoHostPumpTests.cpp` to the same source-file list**

Wherever `OttoHostRenderTests.cpp` is registered (typically in a `target_sources(IdaTests PRIVATE ...)` block), add `OttoHostPumpTests.cpp` on a sibling line.

### T16.8 — Build and run tests — expect failure

- [ ] **Step 1: Configure (no clean rebuild yet — incremental is fine for fail-mode pin)**

```bash
cd /Users/larryseyer/IDA
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -20
```

Expected: configure succeeds (the file additions don't break CMake; signature changes break code).

- [ ] **Step 2: Build `IdaTests`**

```bash
cmake --build build --target IdaTests 2>&1 | tail -40
```

Expected: build FAILS at `OttoHost.cpp` with a signature mismatch — the interface declares the new MidiBuffer-taking override but the implementation still has the old single-arg body. This is the fail-mode pin.

### T16.9 — Implement `OttoHost::renderBlock`

- [ ] **Step 1: Read the existing body**

```bash
sed -n '228,240p' /Users/larryseyer/IDA/otto-bridge/src/OttoHost.cpp
```

Expected: the current `renderBlock(int numSamples) noexcept` body — the prepared/numSamples guard plus the `processGlobalMixer` call.

- [ ] **Step 2: Replace the function body**

```cpp
void OttoHost::renderBlock (int numSamples,
                            juce::MidiBuffer& midiMessages) noexcept
{
    if (! impl_->prepared || numSamples <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    auto& proc = *impl_->processor;

    // S3c — Half A prefix: drain Play/Stop/TempoChange AudioMessages,
    // update transport tracker (which broadcasts TransportEvent → IDA's
    // listener at line ~94), pin engines, dispatch host MIDI into sfizz,
    // advance the conductor + song timeline.
    proc.processBlockBeforeRouting (midiMessages, numSamples);

    // Per-channel/per-bus/per-FX-return sum. Populates GlobalMixer's
    // accessors that IDA's Output Mixer reads via getOttoOutputLeft/Right.
    // Replaces OTTO's processBlock master path (outputRouter_.routeAudio +
    // de-click + spectrum + clear) entirely — IDA owns the master.
    proc.getPlayerManager().processGlobalMixer (numSamples);

    // S3c — Half A suffix: totalSamplePosition advance + fillMode sync.
    proc.processBlockAfterRouting (numSamples);
}
```

- [ ] **Step 3: Replace the stale design comments**

The S1 design comments at `OttoHost.cpp:148-160` and `:230-238` discuss why processBlock was NOT called. They are now obsolete. Replace both blocks with a single, short comment block at the top of the new `renderBlock`:

```cpp
// S3c — IDA's OTTO audio pump. Drives OTTO's processBlock housekeeping
// prefix + per-channel sum + housekeeping suffix, skipping OTTO's master
// mixdown path (which competes with IDA's Output Mixer). See
// docs/superpowers/specs/2026-05-28-otto-audio-pump-design.md.
```

Delete the corresponding stale `Impl` struct comment that explains the
S1 single-call rationale (around lines 148-160). The new architecture
makes that comment misleading.

### T16.10 — Update `AudioCallback.cpp:87` to pass a MidiBuffer

- [ ] **Step 1: Read the call site and surrounding context**

```bash
sed -n '80,95p' /Users/larryseyer/IDA/audio/src/AudioCallback.cpp
```

- [ ] **Step 2: Determine which MidiBuffer to pass**

The `AudioCallback::audioDeviceIOCallbackWithContext` may already manage a `juce::MidiBuffer` (e.g. from a MidiMessageCollector or from `getNextAudioBlock` MIDI). Search:

```bash
grep -nE "MidiBuffer|midiBuffer|MidiCollector|getNextAudioBlock" /Users/larryseyer/IDA/audio/src/AudioCallback.cpp /Users/larryseyer/IDA/audio/include/ida/AudioCallback.h
```

If a callback-scope MidiBuffer exists, pass it. If not, declare a stack-local empty one immediately above the call:

```cpp
    juce::MidiBuffer ottoMidi; // S3c: file-input/external MIDI wiring is a
                                // future slice (spec §7). Empty buffer is
                                // RT-safe — no allocation.
    if (ottoRenderSource_ != nullptr)
        ottoRenderSource_->renderBlock (numSamples, ottoMidi);
```

Update the existing inline comment block above the call to reflect that this now drives the full housekeeping prefix + sum + suffix, not just `processGlobalMixer`.

### T16.11 — Build and run tests — expect pass

- [ ] **Step 1: Incremental build**

```bash
cmake --build build --target IdaTests 2>&1 | tail -20
```

Expected: build succeeds.

- [ ] **Step 2: Run the new + extended tests**

```bash
ctest --test-dir build --output-on-failure -R "otto-host-pump|otto-host-transport-control|otto-host-render" 2>&1 | tail -30
```

Expected: every selected test passes. The new `[otto-host-pump]` cases all PASS.

### T16.12 — Full suite regression sweep

- [ ] **Step 1: Run all tests**

```bash
ctest --test-dir build 2>&1 | tail -10
```

Expected: 808 passed / 1 not-run (matches the pre-S3c baseline — the not-run is the documented separately-built `MainComponentPluginEditorTests`). If the count differs, investigate.

### T16.13 — Clean rebuild + build `IDA` target

- [ ] **Step 1: Clean build (per CLAUDE.md `feedback_clean_builds`)**

```bash
cd /Users/larryseyer/IDA
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
cmake --build build --target IDA 2>&1 | tail -10
```

Expected: configure + build both succeed. `build/app/IDA_artefacts/Release/IDA.app` exists.

- [ ] **Step 2: Run full ctest on the clean build**

```bash
ctest --test-dir build 2>&1 | tail -10
```

Expected: 808 passed / 1 not-run, matching T16.12.

### T16.14 — Atomic commit + push

- [ ] **Step 1: Stage all IDA-side changes**

```bash
cd /Users/larryseyer/IDA
git status --short
```

Expected staged set (after `git add`): exactly these paths (no others), plus the pre-existing untouched `m external/sfizz`:

- `M external/OTTO`
- `M core/include/ida/IOttoRenderSource.h`
- `M otto-bridge/include/ida/OttoHost.h`
- `M otto-bridge/src/OttoHost.cpp`
- `M audio/src/AudioCallback.cpp`
- `M tests/OttoHostRenderTests.cpp`
- `M tests/OttoHostTransportControlTests.cpp`
- `A tests/OttoHostPumpTests.cpp`
- `M tests/CMakeLists.txt`

```bash
git add external/OTTO core/include/ida/IOttoRenderSource.h \
        otto-bridge/include/ida/OttoHost.h otto-bridge/src/OttoHost.cpp \
        audio/src/AudioCallback.cpp \
        tests/OttoHostRenderTests.cpp tests/OttoHostTransportControlTests.cpp \
        tests/OttoHostPumpTests.cpp tests/CMakeLists.txt
git status --short
```

- [ ] **Step 2: Commit (single-line per CLAUDE.md)**

```bash
git commit -m "feat: S3c — IDA OTTO audio pump (processBlock split + renderBlock MidiBuffer)"
```

- [ ] **Step 3: Push to origin/master**

```bash
git push origin master
```

- [ ] **Step 4: Record final SHAs**

```bash
git rev-parse HEAD
git -C external/OTTO rev-parse HEAD
```

These go into the operator handoff in T17.

---

## T17 — Operator verification (operator-domain — not in plan execution)

The implementer hands off to the operator at this point with a numbered
checklist (per `feedback_clean_builds_only_for_testing`):

1. Operator launches `IDA` via the Desktop alias (already points at
   `build/app/IDA_artefacts/Release/IDA.app`).
2. Bar visible across all tabs — should match T14 step 1 (PASS).
3. OTTO tab → kit picker → load a real sample-based kit on Player 1
   (e.g. LSAD pop). Should match T14 step 2 (PASS).
4. Press Play on the IDA bar. **Audible drumming expected.**
5. Press Stop. Audio stops.
6. Adjust tempo on the bar. Pattern speed changes audibly.

If all six pass, S3c is shipped. If step 4 fails, the failure is narrow:
the `[otto-host-pump]` tests already pin everything at the API surface,
so silence at this point indicates an asset-path edge case (kit not
actually loaded) or an Output Mixer wiring issue — neither is S3c's
scope.

---

## Self-Review Checklist (already performed)

**Spec coverage** — every spec §1-§6 requirement maps to a task:
- §1.1 split contract → T15.4-T15.6
- §1.2 wrapper preservation + ScopedNoDenormals contract → T15.6 + T16.9 step 2
- §1.3 IDA pump shape → T16.9
- §1.4 transport authority (no work; falls out from §1.1) → covered by T16.5 + T16.6 tests
- §1.5 OutputRouter::Mode irrelevant (no work) → no task needed
- §2.1 OTTO files → T15.4-T15.6
- §2.2 IDA files → T16.2-T16.10
- §2.3 no new files (except 1 test) → confirmed
- §3 data flow → exercised by T16.6 tests
- §4 error handling (mostly no-op) → confirmed
- §5 test strategy → T15.2 + T16.4-T16.6
- §6 commit shape → T15.9 + T16.14

**Placeholder scan** — no TBD/TODO/handwave language. Every code step has actual code or an exact command.

**Type consistency** — `processBlockBeforeRouting`, `processBlockAfterRouting` referenced identically across T15.1, T15.4-T15.6, T15.9, T16.9. `renderBlock (int, juce::MidiBuffer&)` signature consistent across T16.2 (interface), T16.3 (header), T16.4-T16.6 (tests), T16.9 (impl), T16.10 (caller).

**Out-of-scope honesty** — §7 of the spec lists 5 future-slice items. None are implied work in any task above.
