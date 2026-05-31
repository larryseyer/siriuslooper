# Tape-UI Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the operator a Tapes management UI and per-input-channel tape routing, wired across the one `TapePool` ↔ `InputMixer` ↔ `FlacTapeSink`, with the multi-tape route persisted.

**Architecture:** `TapePool` (core) is the single source of truth for which tapes exist; `MainComponent` constructs it, mirrors it into `InputMixer`'s tape terminals, and keys `FlacTapeSink` writers by `TapeId`. A new "Tapes" tab manages the pool; a per-strip picker routes each input channel to a chosen tape. Engine/wiring tasks (T1–T4) are headless TDD; GUI tasks (T5–T7) are operator-verified (this repo does not unit-test MainComponent GUI).

**Tech Stack:** C++20, JUCE, Catch2, CMake/Ninja. Canonical model: `docs/superpowers/specs/2026-05-22-io-ownership-and-direct-layer-design.md`. The picker offers **tape destinations only** — no "direct"/hardware-output option (that path is the deferred input→output bridge slice).

**⛔ Invariants (do not violate):**
- Audio-thread rules: `docs/RT_SAFETY_CONTRACT.md`. Any `InputMixer` tape-terminal mutation or `FlacTapeSink::closeTape` MUST be bracketed by `audioDeviceManager_.removeAudioCallback(audioCallback_.get())` / `addAudioCallback(...)` — the audio thread reads the graph and is the SPSC single producer.
- `TapePool::remove` already refuses to drop below 1 (the ≥1 pool floor). The active ≥1-channel→≥1-tape enforcement is NOT in this slice (no no-tape destination exists yet).

---

## File Structure

- `core/include/ida/MixerGraphState.h` — add `tapeId` to `MixerMainOut` (T1).
- `engine/src/InputMixer.cpp` — `mainOutSnapshot` records the tape id; `applyChannelMainOut`/`applyBusMainOut` route to the specific tape (T1).
- `persistence/src/SessionFormat.cpp` — serialize/parse the new `tapeId` (T1).
- `tests/InputMixerTests.cpp`, `tests/MixerGraphPersistenceTests.cpp` — T1 coverage.
- `tests/TapePoolMirrorTests.cpp` (new) + `core/include/ida/TapePoolMirror.h` (new) — T2/T3 testable logic.
- `tests/CMakeLists.txt` — register the new test file.
- `app/MainComponent.h` / `app/MainComponent.cpp` — TapePool ownership, ops, persistence, Tapes tab, picker, gesture (T2–T7).

---

## Task 1: `MixerMainOut` carries the tape id (engine, headless TDD)

Today `InputMixer::mainOutSnapshot` only recognizes the **primary** tape terminal (`graph_.terminalNode(MixerTerminal::Tape)`); a channel routed to a NON-primary tape falls through to the bus loop and trips `jassertfalse` (`engine/src/InputMixer.cpp:185`). The multi-tape picker (T6) creates exactly such routes, and persistence (T4) calls `exportGraphState()`. Fix the snapshot to use `tapeSlotForNode` (which already covers all tapes, primary at slot 0 — ctor seeds it at `InputMixer.cpp:40`) and record the `TapeId`.

**Files:**
- Modify: `core/include/ida/MixerGraphState.h:26-36` (the `MixerMainOut` struct)
- Modify: `engine/src/InputMixer.cpp:162-187` (`mainOutSnapshot`), `:316-336` (`applyChannelMainOut`/`applyBusMainOut`)
- Modify: `persistence/src/SessionFormat.cpp` (the `MixerMainOut` ↔ var helpers — search for where `mainOut.terminal`/`mainOut.busId` are written/read)
- Test: `tests/InputMixerTests.cpp`, `tests/MixerGraphPersistenceTests.cpp`

- [ ] **Step 1: Write the failing test** — append to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("exportGraphState round-trips a non-primary tape route", "[input-mixer][tape]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.addTape (ida::TapeId { 2 }));
    REQUIRE (mixer.setChannelMainOutToTape (ch, ida::TapeId { 2 }));

    // Must NOT trip the mainOutSnapshot jassertfalse, and must record tape 2.
    const auto state = mixer.exportGraphState();
    REQUIRE (state.channels.size() == 1);
    const auto& mo = state.channels[0].mainOut;
    CHECK (mo.kind     == ida::MixerMainOut::Kind::Terminal);
    CHECK (mo.terminal == ida::MixerTerminalKind::Tape);
    CHECK (mo.tapeId   == 2);

    // Re-import into a fresh mixer (with tape 2 present) restores the route.
    ida::InputMixer restored;
    REQUIRE (restored.addTape (ida::TapeId { 2 }));
    restored.importGraphState (state);
    CHECK (restored.channelMainOutIsTape (ida::ChannelId (state.channels[0].channelId),
                                          ida::TapeId { 2 }));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R InputMixer --output-on-failure`
Expected: FAIL — either the `jassertfalse` at `InputMixer.cpp:185` fires (debug) or `mo.tapeId` does not compile (field absent).

- [ ] **Step 3: Add the `tapeId` field** to `MixerMainOut` in `core/include/ida/MixerGraphState.h`:

```cpp
struct MixerMainOut
{
    enum class Kind { Terminal, Bus };
    Kind              kind     { Kind::Terminal };
    MixerTerminalKind terminal { MixerTerminalKind::Tape }; // valid when kind == Terminal
    std::int64_t      tapeId   { 1 };                       // valid when terminal == Tape (1 = primary)
    std::int64_t      busId    { 0 };                       // valid when kind == Bus

    bool operator== (const MixerMainOut& o) const noexcept
    { return kind == o.kind && terminal == o.terminal && tapeId == o.tapeId && busId == o.busId; }
    bool operator!= (const MixerMainOut& o) const noexcept { return ! (*this == o); }
};
```

- [ ] **Step 4: Fix `mainOutSnapshot`** (`engine/src/InputMixer.cpp:162`) to detect any tape terminal and record its id:

```cpp
MixerMainOut InputMixer::mainOutSnapshot (MixerNodeId node) const noexcept
{
    const auto dest = graph_.mainOutOf (node);
    MixerMainOut out;
    const int tapeSlot = tapeSlotForNode (dest);
    if (tapeSlot >= 0)
    {
        out.kind     = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::Tape;
        out.tapeId   = tapeTerminals_[static_cast<std::size_t> (tapeSlot)].tapeId;
        return out;
    }
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput))
    {
        out.kind = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::HardwareOutput;
        return out;
    }
    out.kind = MixerMainOut::Kind::Bus;
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
        if (busNodeIds_[i] == dest)
        {
            out.busId = buses_[i].id().value();
            return out;
        }
    jassertfalse; // graph invariant: dest is a Bus node absent from busNodeIds_
    return out;
}
```

- [ ] **Step 5: Fix the apply side** (`engine/src/InputMixer.cpp:316`) to route to the specific tape:

```cpp
void InputMixer::applyChannelMainOut (ChannelId id, const MixerMainOut& m)
{
    const bool ok = (m.kind == MixerMainOut::Kind::Bus)
                        ? setChannelMainOutToBus (id, BusId (m.busId))
                        : (m.terminal == MixerTerminalKind::HardwareOutput)
                              ? setChannelMainOutToHardwareOutput (id)
                              : setChannelMainOutToTape (id, TapeId (m.tapeId));
    jassert (ok);
    juce::ignoreUnused (ok);
}
```

Apply the identical `setBusMainOutToTape (id, TapeId (m.tapeId))` change to `applyBusMainOut` (`:327`).

- [ ] **Step 6: Serialize the `tapeId`** in `persistence/src/SessionFormat.cpp`. Find the anonymous-namespace helper that turns a `MixerMainOut` into a `juce::var`/`DynamicObject` (it writes `terminal` and `busId`) and the inverse that reads them. Add `tapeId`:
  - On write: when `terminal == Tape`, set property `"tapeId"` to `static_cast<juce::int64>(m.tapeId)`.
  - On read: `m.tapeId = (std::int64_t) optionalProperty(obj, "tapeId", (juce::int64) 1);` — back-compat default 1 (primary) for pre-tapeId docs. Mirror the existing `optionalProperty` pattern used elsewhere in this file.

- [ ] **Step 7: Add a persistence round-trip test** — append to `tests/MixerGraphPersistenceTests.cpp`:

```cpp
TEST_CASE ("input mixer graph serializes a non-primary tape route", "[sessionformat][mixer][tape]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.addTape (ida::TapeId { 3 }));
    REQUIRE (mixer.setChannelMainOutToTape (ch, ida::TapeId { 3 }));

    const auto json    = ida::serializeMixerGraphState (mixer.exportGraphState());
    const auto decoded = ida::deserializeInputMixerGraphState (json);
    CHECK (decoded.channels.size() == 1);
    CHECK (decoded.channels[0].mainOut.terminal == ida::MixerTerminalKind::Tape);
    CHECK (decoded.channels[0].mainOut.tapeId   == 3);
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "InputMixer|MixerGraphPersistence" --output-on-failure`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add core/include/ida/MixerGraphState.h engine/src/InputMixer.cpp persistence/src/SessionFormat.cpp tests/InputMixerTests.cpp tests/MixerGraphPersistenceTests.cpp
git commit -m "fix: MixerMainOut carries TapeId — non-primary tape routes export/persist (tape-UI T1)"
```

---

## Task 2: Construct `TapePool` and mirror it into `InputMixer` (wiring, headless TDD + thin hookup)

Extract the mirror as a free function so it is testable without `MainComponent`, then own a `TapePool` in `MainComponent` and run the mirror at init.

**Files:**
- Create: `core/include/ida/TapePoolMirror.h`
- Create: `tests/TapePoolMirrorTests.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test source)
- Modify: `app/MainComponent.h` (add `#include "ida/TapePool.h"` + a `ida::TapePool tapePool_;` member), `app/MainComponent.cpp` (init block ~1096, after `inputMixer_` exists / `flacTapeSink_` is constructed)

- [ ] **Step 1: Write the failing test** — `tests/TapePoolMirrorTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ida/TapePool.h"
#include "ida/TapePoolMirror.h"
#include "ida/InputMixer.h"

TEST_CASE ("mirrorTapePool registers every non-primary pool tape in the mixer", "[tape-pool][mirror]")
{
    ida::TapePool pool;            // seeds TapeId{1} "Tape 1"
    const auto drums = pool.add ("Drums");
    const auto vox   = pool.add ("Vox");

    ida::InputMixer mixer;         // ctor seeds the primary tape terminal only
    REQUIRE (mixer.tapeCount() == 1);

    ida::mirrorTapePool (pool, mixer);

    CHECK (mixer.tapeCount() == 3);
    CHECK (mixer.hasTape (ida::TapeId { 1 }));
    CHECK (mixer.hasTape (drums));
    CHECK (mixer.hasTape (vox));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `TapePoolMirror.h` not found.

- [ ] **Step 3: Write the mirror helper** — `core/include/ida/TapePoolMirror.h`:

```cpp
#pragma once

#include "ida/TapePool.h"
#include "ida/InputMixer.h"

namespace ida
{

/// Registers every pool tape that is not already a mixer tape terminal. The pool
/// is the source of truth; the mixer holds routing terminals. Idempotent. The
/// primary tape (TapeId{1}) is seeded by both ctors, so it is skipped here.
/// Message-thread only (mutates the mixer's tape-terminal registry).
inline void mirrorTapePool (const TapePool& pool, InputMixer& mixer)
{
    for (const auto& tape : pool.tapes())
        if (! mixer.hasTape (tape.id))
            mixer.addTape (tape.id);
}

} // namespace ida
```

- [ ] **Step 4: Register the test** — in `tests/CMakeLists.txt`, add `TapePoolMirrorTests.cpp` to the test sources list (mirror the existing entries, e.g. next to `TapePoolTests.cpp`).

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "mirror|TapePool" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Own the pool in MainComponent.** In `app/MainComponent.h`, add near the other engine members: `#include "ida/TapePool.h"` and `ida::TapePool tapePool_;`. In `app/MainComponent.cpp`, immediately after the `flacTapeSink_` construction + `inputMixer_->setTapeSink(...)` block (~1103), add:

```cpp
    // Tape-UI slice — TapePool is the single source of truth for which tapes exist.
    // Mirror it into the input mixer's routing terminals at startup.
    ida::mirrorTapePool (tapePool_, *inputMixer_);
```

Add `#include "ida/TapePoolMirror.h"` to `MainComponent.cpp`.

- [ ] **Step 7: Build the app to verify it compiles**

Run: `cmake --build build --target IDA`
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add core/include/ida/TapePoolMirror.h tests/TapePoolMirrorTests.cpp tests/CMakeLists.txt app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: own TapePool in MainComponent + mirror into InputMixer (tape-UI T2)"
```

---

## Task 3: Tape add / rename / remove operations (wiring, headless TDD for invariants)

Three `MainComponent` methods keep `TapePool` ↔ `InputMixer` ↔ `FlacTapeSink` consistent. The consistency rules are headless-testable via the mirror helper + public APIs; the audio-callback bracketing and auto-disarm GUI effects are operator-verified.

**Files:**
- Modify: `app/MainComponent.h` (declare `void addTape(juce::String); void renameTape(ida::TapeId, juce::String); void removeTape(ida::TapeId);`), `app/MainComponent.cpp` (implement)
- Test: `tests/TapePoolMirrorTests.cpp` (extend with a remove-consistency case over pool+mixer)

- [ ] **Step 1: Write the failing test** (the remove-consistency invariant, headless) — append to `tests/TapePoolMirrorTests.cpp`:

```cpp
TEST_CASE ("removing a pooled tape re-mirrors to a consistent mixer", "[tape-pool][mirror]")
{
    ida::TapePool pool;
    const auto drums = pool.add ("Drums");
    ida::InputMixer mixer;
    ida::mirrorTapePool (pool, mixer);
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.setChannelMainOutToTape (ch, drums));

    // The MainComponent remove sequence, modelled headlessly: route dependents to
    // primary, drop the mixer terminal, drop the pool entry.
    if (mixer.channelMainOutIsTape (ch, drums))
        mixer.setChannelMainOutToTape (ch);          // fall back to primary
    REQUIRE (mixer.removeTape (drums));
    REQUIRE (pool.remove (drums));

    CHECK (pool.count() == 1);
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.channelMainOutIsTape (ch, ida::TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (drums));
}

TEST_CASE ("the pool floor and primary tape are protected", "[tape-pool][mirror]")
{
    ida::TapePool pool;                 // one tape
    CHECK_FALSE (pool.remove (ida::TapeId { 1 })); // ≥1 floor refuses
    ida::InputMixer mixer;
    CHECK_FALSE (mixer.removeTape (ida::TapeId { 1 })); // primary is permanent
}
```

- [ ] **Step 2: Run to verify it fails/passes appropriately**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R mirror --output-on-failure`
Expected: these PASS using existing APIs (they encode the contract the MainComponent methods must follow). If any assertion fails, the engine/pool contract is wrong — stop and investigate before writing the MainComponent methods.

- [ ] **Step 3: Implement the three methods** in `app/MainComponent.cpp`. `removeTape` MUST bracket with `removeAudioCallback`/`addAudioCallback` (the pattern at `MainComponent.cpp:1595/1676`), and `closeTape` MUST be inside the bracket:

```cpp
void MainComponent::addTape (juce::String name)
{
    const auto id = tapePool_.add (name.toStdString());
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    inputMixer_->addTape (id);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());
    refreshTapesPane();
}

void MainComponent::renameTape (ida::TapeId id, juce::String name)
{
    tapePool_.rename (id, name.toStdString());   // pool-only; no engine/sink effect
    refreshTapesPane();
}

void MainComponent::removeTape (ida::TapeId id)
{
    if (tapePool_.count() <= 1) return;          // ≥1 pool floor (TapePool also refuses)

    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    // Route any channel that targeted this tape back to the primary tape.
    for (const auto& chId : inputStripChannelIds_)
        if (inputMixer_->channelMainOutIsTape (chId, id))
            inputMixer_->setChannelMainOutToTape (chId);   // primary
    flacTapeSink_->closeTape (id);               // SPSC: inside the bracket only
    inputMixer_->removeTape (id);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    tapePool_.remove (id);

    // Auto-disarm if it was armed (mirror toggleArm's disarm path ~1735).
    armedTapeIds_.erase (id.value());
    if (armedTapeIds_.empty() && captureSession_.isArmed())
        captureSession_.disarm();

    refreshTapesPane();
    refreshTimeline();
}
```

(`refreshTapesPane()` is defined in T5; for now declare it and leave the body to T5, or land T3 and T5 together. `inputStripChannelIds_` is the existing member iterated in `rebuildInputStrips`.)

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build --target IDA`
Expected: compiles (with a forward-declared `refreshTapesPane()`).

- [ ] **Step 5: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp tests/TapePoolMirrorTests.cpp
git commit -m "feat: tape add/rename/remove ops with audio-callback bracketing + closeTape (tape-UI T3)"
```

---

## Task 4: Persist the `TapePool` in the session (wiring, headless TDD)

Embed the pool in session save/load. Depends on T1 (saving a session whose channels route to non-primary tapes calls `exportGraphState()`).

**Files:**
- Modify: `app/MainComponent.cpp` (`chooseFileAndSave` ~2089, `chooseFileAndLoad` ~2124)
- Test: `tests/SessionFormatTests.cpp` (pool round-trip; the functions already exist per `SessionFormat.h:44/52`)

- [ ] **Step 1: Write the failing test** — append to `tests/SessionFormatTests.cpp`:

```cpp
TEST_CASE ("TapePool round-trips through serialize/deserialize", "[sessionformat][tape-pool]")
{
    ida::TapePool pool;
    const auto drums = pool.add ("Drums");
    pool.add ("Vox");
    pool.rename (drums, "Kit");

    const auto restored = ida::deserializeTapePool (ida::serializeTapePool (pool));
    REQUIRE (restored.count() == pool.count());
    CHECK (restored.tapes() == pool.tapes());
    CHECK (restored.primary() == pool.primary());
}

TEST_CASE ("a pre-tape-pool session yields a default TapePool", "[sessionformat][tape-pool]")
{
    const auto restored = ida::deserializeTapePool ("{}");
    CHECK (restored.count() == 1);
    CHECK (restored.primary() == ida::TapeId { 1 });
}
```

- [ ] **Step 2: Run to verify**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R SessionFormat --output-on-failure`
Expected: PASS if `serializeTapePool`/`deserializeTapePool` honor the back-compat contract; if the empty-doc case throws instead of defaulting, fix `deserializeTapePool` per `SessionFormat.h:49` (missing → default `TapePool()`), then re-run.

- [ ] **Step 3: Wire save.** In `chooseFileAndSave` (~2089), after the existing session JSON is produced, write the pool alongside it. Read the function first to match how it composes/writes the document; embed `serializeTapePool(tapePool_)` as a sibling section or sidecar key (the pool is independent of the Constituent per `SessionFormat.h:42`). Keep the existing `serializeSession` output byte-compatible.

- [ ] **Step 4: Wire load.** In `chooseFileAndLoad` (~2124), after the session loads, call `deserializeTapePool(...)` (default `TapePool()` if the section is absent), assign `tapePool_`, then re-mirror inside the audio-callback bracket:

```cpp
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    // Drop non-primary terminals, then re-mirror the loaded pool.
    for (const auto& tape : tapePoolBeforeLoad.tapes())
        if (tape.id != ida::TapeId { 1 })
            inputMixer_->removeTape (tape.id);
    tapePool_ = loadedPool;
    ida::mirrorTapePool (tapePool_, *inputMixer_);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());
    refreshTapesPane();
```

- [ ] **Step 5: Build + test**

Run: `cmake --build build --target IDA IdaTests && ctest --test-dir build -R SessionFormat --output-on-failure`
Expected: app builds; tests PASS.

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.cpp tests/SessionFormatTests.cpp
git commit -m "feat: persist TapePool in session save/load (tape-UI T4)"
```

---

## Task 5: Tapes tab — list, create/rename/remove, diagnostics (GUI, operator-verified)

GUI — no unit test (this repo verifies MainComponent GUI by operator eyes-on). Build to compile; the operator confirms behavior.

**Files:**
- Modify: `app/MainComponent.cpp` (new `TapesPane` nested component near `InputMixerPane` ~425; tab insertion in the ctor ~1299; `refreshTapesPane()` definition; diagnostics on the 30 Hz `timerCallback` ~1469), `app/MainComponent.h` (the pane member + `refreshTapesPane` decl)

- [ ] **Step 1: Add a `TapesPane`** (a `juce::Component`) with: a list/rows built from `tapePool_.tapes()` showing each name; a "New tape" button → `addTape("Tape N")`; per-row rename (a `TextEditor` or rename button → `renameTape(id, ...)`) and Remove button. **Disable Remove when `tapePool_.count() == 1`** (≥1 floor). Expose callbacks (`onCreate`, `onRename(id,name)`, `onRemove(id)`) wired in the ctor to the T3 methods, following the `InputMixerPane` callback pattern (`onGain` etc. at ~1265).

- [ ] **Step 2: Add the tab** after "Input Mixer" in the ctor (~1299): `tabs_.addTab ("Tapes", juce::Colours::black, tapesPane_.get(), false);`

- [ ] **Step 3: Implement `refreshTapesPane()`** to push `tapePool_.tapes()` into the pane (rebuild rows), and call it after each T3 op and on load.

- [ ] **Step 4: Diagnostics.** In `timerCallback` (~1469, near the existing `flacTapeSink_` rate refresh), push `flacTapeSink_->droppedBlockCount()` to a label in `TapesPane` (e.g. `tapesPane_->setDroppedBlocks(...)`). Non-zero = capture overflow.

- [ ] **Step 5: Build**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA`
Expected: clean build (clean rebuild required before operator eyes-on per CLAUDE.md).

- [ ] **Step 6: Operator verification** — launch `build/app/IDA_artefacts/Release/IDA.app`; operator confirms: Tapes tab lists "Tape 1"; New tape adds a row; rename persists in the list; Remove is **disabled at one tape**, enabled with ≥2 and removes; dropped-block counter shows 0 at idle.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "feat: Tapes tab — create/rename/remove with >=1 floor + dropped-block diagnostics (tape-UI T5)"
```

---

## Task 6: Per-channel tape destination picker (GUI, operator-verified)

A picker beneath each Input-Mixer strip choosing which tape the channel records to. Render it in `InputMixerPane` itself — do NOT re-enable `CompactFaderStrip`'s output combo (`setOutputComboVisible(false)` stays, `MainComponent.cpp:496`; it is hardwired to OTTO's hardware-output-pairs model).

**Files:**
- Modify: `app/MainComponent.cpp` (`InputMixerPane`: a per-strip destination button in the space below the strip row — `setStrips` ~483, `resized` ~552; a new `onDestinationChosen(stripIdx, tapeId)` callback wired in the ctor ~1265; a setter to push the current tape list + per-strip current-tape labels), `app/MainComponent.h`

- [ ] **Step 1:** Add a per-strip destination button to `InputMixerPane` (own control, laid out in `resized` below each strip). Its label shows the channel's current tape name (resolved by querying `inputMixer_->channelMainOutIsTape(chId, id)` against `tapePool_.tapes()` from MainComponent, pushed into the pane via a setter on rebuild/refresh).

- [ ] **Step 2:** On click, open a `juce::PopupMenu` listing **the pooled tapes only** (no "Direct"/hardware-output entry). Selecting a tape fires `onDestinationChosen(stripIdx, tapeId)`.

- [ ] **Step 3:** Wire the callback in the ctor: `inputMixerPane_->onDestinationChosen = [this](int idx, ida::TapeId t){ if (auto chId = inputStripChannelIdAt(idx)) inputMixer_->setChannelMainOutToTape(*chId, t); refreshInputMixer(); };` (use the existing `inputStripChannelIds_` indexing; bracketing is NOT needed for a pure `setMainOut` graph edge change — it is a single atomic graph mutation on the message thread that the audio thread reads safely, same as existing send/gain edits; confirm against how `onGain` is handled).

- [ ] **Step 4:** Refresh the picker labels in `refreshInputMixer` (~1569) or on rebuild so they track route changes and pool renames.

- [ ] **Step 5: Build**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA`
Expected: clean build.

- [ ] **Step 6: Operator verification** — with ≥2 tapes, each strip shows its destination tape; choosing a different tape re-routes; **make live input and confirm audio lands in the chosen `~/Library/IDA/tapes/tape-<id>.flac`** (quit to finalize, measure as in the slice-3 procedure); the picker list updates when tapes are added/removed/renamed.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "feat: per-channel tape destination picker (tape-UI T6)"
```

---

## Task 7: Blank-area creation gesture (GUI, operator-verified)

Extend `InputMixerPane::mouseDown` (~510) so a right-click / 500 ms long-press on **blank pane area** (`stripIndexOf(e.eventComponent) == -1`, which currently early-returns at ~513) opens an "Add tape / Use existing" menu.

**Files:**
- Modify: `app/MainComponent.cpp` (`InputMixerPane::mouseDown` ~510, `timerCallback` long-press path ~613, a new `showBlankAreaMenu`; an `onAddTape` callback to MainComponent → `addTape(...)`)

- [ ] **Step 1:** In `mouseDown`, when `idx < 0`: on `e.mods.isPopupMenu()` call `showBlankAreaMenu(e.getScreenPosition())` immediately; otherwise arm the long-press timer with a blank-area sentinel (distinct from `longPressIdx_`, e.g. set `longPressIdx_ = -1` AND a `longPressBlank_ = true` flag) so the timer fires the blank menu rather than the per-strip toggle.

- [ ] **Step 2:** `showBlankAreaMenu` builds a `juce::PopupMenu`: item "Add tape" → `onAddTape()`; submenu "Use existing" listing `tapePool_.tapes()` names (informational/select — fires `onSelectExistingTape(id)` if a select behavior is wanted; otherwise omit the submenu and ship just "Add tape" — the spec only requires the creation surface).

- [ ] **Step 3:** In the long-press `timerCallback` (~613), branch on the blank-area sentinel to call `showBlankAreaMenu` instead of `showToggleMenu`.

- [ ] **Step 4:** Wire `onAddTape` in the ctor → `addTape("Tape " + juce::String(tapePool_.count() + 1))`.

- [ ] **Step 5: Build**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA`
Expected: clean build.

- [ ] **Step 6: Operator verification** — right-click and long-press on empty Input-Mixer pane area both open the menu; "Add tape" creates a tape (appears in the Tapes tab list and in each strip's picker).

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: blank-area Add-tape creation gesture in Input Mixer (tape-UI T7)"
```

---

## Final verification (after all tasks)

- `cmake --build build --target IdaTests && ctest --test-dir build` — green; new `[tape]`/`[mirror]`/`[tape-pool]` cases added over the slice-3 baseline (559/560; the one Not-Run is the documented `MainComponentPluginEditorTests_NOT_BUILT`).
- Clean `rm -rf build` rebuild; operator eyes-on the full flow: create tapes, route channels per-tape, record live input to distinct `tape-<id>.flac` files, save+reload a session and confirm tapes + routes survive.
- Refresh `continue.md` (mark the tape-UI slice shipped, commits + ctest count; next = P6 Input Mixer UI / the bridge slice with P8).

## Self-review notes
- Spec coverage: Tapes tab (T5), per-channel tape picker (T6), creation gesture (T7), TapePool wiring (T2/T3), persistence (T4), the latent non-primary-tape snapshot bug (T1). The active ≥1-channel→≥1-tape enforcement + direct-out opt-out are intentionally OUT (deferred to the bridge slice — no no-tape destination exists here).
- Type consistency: `mirrorTapePool`, `addTape`/`renameTape`/`removeTape`, `refreshTapesPane`, `onDestinationChosen` are used consistently across tasks. `TapeId` is `explicit` (construct as `ida::TapeId{n}`), `.value()` for the int64.
- GUI tasks (T5–T7) carry no unit tests by design (MainComponent GUI is operator-verified in this repo); their "test" steps are clean-build + an operator checklist.
