# Slice 5 — Per-Phrase Phrase/Loop State Machine + Source-Agnostic Command Layer + OTTO Transport Coupling — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. Each step is one bite: write a failing test, run it, see it FAIL, write the minimal implementation, run it, see it PASS, then commit. Do not batch steps. Do not write source ahead of its test.

**Goal:** Formalize IDA's phrase/loop creation as a **pure, JUCE-free, per-phrase combined-state machine** (spec §8 states 0–6) driven by a **source-agnostic command layer** (`Record` / `Stop` dispatched identically from GUI, MIDI, or footswitch), built directly on the existing `CaptureSession` (mark-in/out) and `promotion::promote()` (phrase-mint-vs-loop-add). Couple "Record while transport stopped" to OTTO via an injected transport-control port (`OttoHost::play()` through `TransportBarHost`). Wire each creation gesture to push exactly one labeled `UndoStack` entry. Re-point `MainComponent`'s bottom-bar Record path through the new command layer (optionally consolidating Arm / Mark-In / Mark-Out into one Record toggle).

**Architecture:** A tape records iff ≥1 input is assigned (Slice 4 — assumed done). The clock (LMC) always runs; OTTO is the only transport source — IDA invents no engine-side transport (`[[project_otto_is_the_transport_source]]`). The new layer is **structure-marking only** (spec §3): it carves phrases/loops out of the always-recording-while-assigned substrate; it never starts or stops a tape. The state machine is a thin orchestration over two existing pure pieces:

- `ida::CaptureSession` (`core/include/ida/CaptureSession.h`) — the mark-in → mark-out window. Its `Armed`/`AwaitingOut` are the *marking* sub-states (spec §3 note).
- `ida::promotion::promote()` (`core/include/ida/Promotion.h`) — given a closed `CaptureRegion` and `lmcAtMarkIn`, it already (a) mints a phrase wrapper + loop when the playhead is **outside** any phrase (`mintedPhraseId.has_value()`), or (b) adds a loop to the enclosing phrase when **inside** (`mintedPhraseId == nullopt`). The state machine reads `mintedPhraseId` to know which combined-state edge it just traversed.

The per-phrase requirement is satisfied **by construction**: the arrangement is one immutable Constituent tree holding unlimited phrases (each with unlimited loop children); `promote()` only ever touches the *active target* phrase (the one under the playhead) and leaves every other phrase untouched. There is therefore **no global state** — the "combined state" the machine reports is the state of *the phrase the current gesture acts on*, derived from the tree + the in-flight `CaptureSession`. The machine never stores a per-phrase enum array; it computes the active target's combined state from the live data, which is the only honest source (CLAUDE.md rule 11).

Source-agnosticism is a **design-for-isolation** requirement (spec §8.4): the machine accepts an abstract `CaptureCommand` and has no field, parameter, or branch that names the input source. A test drives it from a fake "GUI" source and a fake "MIDI" source and asserts byte-identical resulting trees.

The transport coupling is injected as a **port** (`ITransportControl`) so `core` stays JUCE-free and OTTO-free: the machine calls `transport.play()` on Record-while-stopped; `MainComponent` supplies an adapter that forwards to `OttoHost::play()` (via the existing `TransportBarHost` path, spec §15.1 — capability already exists).

**Tech Stack:** C++17/JUCE. New code lands in the **`core`** library (JUCE-free, pure C++) so it is fully headless-TDD'd. Catch2 (`IdaTests`), tests flat under `tests/`, registered in `tests/CMakeLists.txt`. CMake + Ninja. The only JUCE-touching change is the `MainComponent` re-wiring (operator-verified, not unit-tested, per repo convention).

**Dependencies:** Slices 1–4 (blank slate; channels create tapes that record while assigned). This slice consumes `CaptureSession`, `Promotion`, `Constituent`/`Arrangement`, `TempoMap`, `Rational`, `ConstituentId`, `TapeId`, and (in `MainComponent` only) `OttoHost`/`TransportBarHost` and `UndoStack`. No source file from Slices 1–4 is modified here except `MainComponent.cpp` re-wiring and `tests/CMakeLists.txt`.

---

## Cross-cutting rules (apply to every task)

- **Engine logic is TDD'd headless** in `IdaTests`. The state machine + command layer are pure `core` — every transition is unit-tested. **GUI wiring is operator-verified** (`rm -rf build` before the operator hand-off, per `[[feedback_clean_builds_only_for_testing]]`). The agent cannot keep the GUI alive from CLI.
- **RT-safety:** nothing in this slice runs on the audio callback. The state machine is message-thread orchestration over message-thread `CaptureSession` + pure `promote()`. No `noexcept`/no-alloc constraints apply here (the contract is for the hot path; this is not on it). Do **not** call into the audio thread from the machine.
- **Pure-`core` discipline:** the new files live in `core/include/ida/` + `core/src/` and must **not** `#include` any `juce_*` header. The transport port is an abstract interface; OTTO is injected from `app`.
- **No `Rational::fromDouble`** — it does not exist (memory `[[project_tape_records_are_variable_size]]`). Build `Rational` from integer LMC ticks (`playheadValueToLmc` already does: `Rational(ticks, ticksPerSecond)`), or accept `Rational` directly. The state machine takes `Rational` LMC times; the *caller* converts the playhead/transport-position double → ticks → `Rational` at the boundary.
- **Undo:** every create gesture (coinit phrase+loop, new loop) pushes exactly one labeled `UndoStack` entry with a `CaptureRestorePoint`, matching the existing `onMarkOut` contract (`app/MainComponent.cpp:8423`). The machine returns the data the caller needs to push; the machine itself does **not** depend on `UndoStack` (that lives in `ui`, not `core`).
- **Commits:** each step that ends in PASS commits. Single-line message, `feat:`/`refactor:` type. Trailer on every commit:
  ```
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
  ```
  The implementing subagent pushes its task commits to `origin/master` (`[[feedback_subagents_push_to_master]]`); never `--amend` a pushed commit.

## File-structure map (this slice)

- **New:** `core/include/ida/CaptureCommand.h` — the source-agnostic command enum + the `ITransportControl` port + a `CaptureCommandSource` tag for diagnostics-only (never branched on).
- **New:** `core/include/ida/PhraseLoopMachine.h` + `core/src/PhraseLoopMachine.cpp` — the per-phrase combined-state machine. Owns a `CaptureSession`, computes the combined state (0–6), dispatches `Record`/`Stop` through `CaptureSession` + `promote()`, and reports a `CaptureOutcome` the caller turns into an `UndoStack` push.
- **New:** `core/include/ida/CombinedCaptureState.h` — the `combinedState(phrase, loop)` lookup (the spec §8 table) + the `PhraseCreation`/`LoopCreation` lifecycle enums. Pure, header-only.
- **Modify:** `tests/CMakeLists.txt` — register `CombinedCaptureStateTests.cpp`, `PhraseLoopMachineTests.cpp`, `CaptureCommandTests.cpp`.
- **New tests:** `tests/CombinedCaptureStateTests.cpp`, `tests/PhraseLoopMachineTests.cpp`, `tests/CaptureCommandTests.cpp`.
- **Modify (GUI wiring, operator-verified):** `app/MainComponent.cpp` — replace the `onArmToggle`/`onMarkIn`/`onMarkOut` bottom-bar trio's *internals* with dispatch into a `PhraseLoopMachine` member; add a `recordToggle()`/`stop()` command entry; supply an `OttoTransportControl` adapter forwarding to `ottoHost_->play()`; keep the `UndoStack` push + `announceCapture` at the call site.
- **Modify (GUI wiring):** `app/MainComponent.h` — add the `PhraseLoopMachine` member + the transport adapter member.

---

## Task 1 — The combined-state table (spec §8), pure header

**Files:**
- Create: `core/include/ida/CombinedCaptureState.h`
- Test: `tests/CombinedCaptureStateTests.cpp`
- Modify: `tests/CMakeLists.txt` (add `CombinedCaptureStateTests.cpp` after `PromotionTests.cpp`)

This is the spec §8 product table encoded once, JUCE-free, so the machine and its tests share one source of truth. `nil` (a loop without a phrase) is represented as `std::nullopt`.

- [ ] **Step 1: Failing test — the §8 combined-state table**

Create `tests/CombinedCaptureStateTests.cpp`:

```cpp
// Tests for ida::combinedCaptureState — the spec §8 product of the phrase
// creation lifecycle and the loop creation lifecycle. Pure; no JUCE.
//
// Spec: docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md §8
#include "ida/CombinedCaptureState.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

using ida::CombinedCaptureState;
using ida::LoopCreation;
using ida::PhraseCreation;
using ida::combinedCaptureState;

TEST_CASE ("combined-state table matches the spec §8 product exactly",
           "[capture-state]")
{
    // phrase: NotCreated row — only NotCreated loop is legal; the other two
    // are nil (a loop cannot exist without a phrase to contain it).
    CHECK (combinedCaptureState (PhraseCreation::NotCreated, LoopCreation::NotCreated)
           == CombinedCaptureState::IdleEmpty);                 // id 0
    CHECK_FALSE (combinedCaptureState (PhraseCreation::NotCreated, LoopCreation::Exists)
                 .has_value());                                  // nil
    CHECK_FALSE (combinedCaptureState (PhraseCreation::NotCreated, LoopCreation::BeingCreated)
                 .has_value());                                  // nil

    // phrase: Exists row.
    CHECK (combinedCaptureState (PhraseCreation::Exists, LoopCreation::NotCreated)
           == CombinedCaptureState::PhraseReadyNoLoop);          // id 1
    CHECK (combinedCaptureState (PhraseCreation::Exists, LoopCreation::Exists)
           == CombinedCaptureState::PhraseWithFinishedLoop);     // id 2
    CHECK (combinedCaptureState (PhraseCreation::Exists, LoopCreation::BeingCreated)
           == CombinedCaptureState::PhraseReadyLoopRecording);   // id 3

    // phrase: BeingCreated row.
    CHECK (combinedCaptureState (PhraseCreation::BeingCreated, LoopCreation::NotCreated)
           == CombinedCaptureState::PhraseRecordingNoLoop);      // id 4
    CHECK (combinedCaptureState (PhraseCreation::BeingCreated, LoopCreation::Exists)
           == CombinedCaptureState::PhraseRecordingLoopDone);    // id 5
    CHECK (combinedCaptureState (PhraseCreation::BeingCreated, LoopCreation::BeingCreated)
           == CombinedCaptureState::PhraseLoopCoinit);           // id 6
}

TEST_CASE ("combined-state ids are the canonical 0..6 the spec table fixes",
           "[capture-state]")
{
    CHECK (static_cast<int> (CombinedCaptureState::IdleEmpty)                == 0);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseReadyNoLoop)        == 1);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseWithFinishedLoop)   == 2);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseReadyLoopRecording) == 3);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseRecordingNoLoop)    == 4);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseRecordingLoopDone)  == 5);
    CHECK (static_cast<int> (CombinedCaptureState::PhraseLoopCoinit)         == 6);
}
```

- [ ] **Step 2: Register the test, run it, verify it FAILS (compile error — header missing)**

Add to `tests/CMakeLists.txt` right after `PromotionTests.cpp`:

```cmake
    CombinedCaptureStateTests.cpp
```

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `ida/CombinedCaptureState.h` does not exist.

- [ ] **Step 3: Minimal implementation — `CombinedCaptureState.h`**

Create `core/include/ida/CombinedCaptureState.h`:

```cpp
#pragma once

#include <optional>

namespace ida
{

/// The creation lifecycle of a phrase (spec §8). `NotCreated` = no phrase yet;
/// `Exists` = a finished phrase at rest; `BeingCreated` = the active target
/// phrase whose end has not been marked (the marking state of §3).
enum class PhraseCreation
{
    NotCreated,
    Exists,
    BeingCreated
};

/// The creation lifecycle of a loop within a phrase (spec §8). Mirrors
/// PhraseCreation; a loop cannot exist without a phrase, so the NotCreated
/// phrase row pairs only with a NotCreated loop (every other pair is nil).
enum class LoopCreation
{
    NotCreated,
    Exists,
    BeingCreated
};

/// The combined (phrase, loop) state — the spec §8 product table, ids fixed
/// 0..6. These are *per-phrase*: each phrase instance carries its own combined
/// state. The active-target phrase (the one the current Record gesture acts on)
/// is the only one that leaves `Exists`.
enum class CombinedCaptureState
{
    IdleEmpty                = 0,  ///< idle / empty
    PhraseReadyNoLoop        = 1,  ///< phrase ready, no loop yet
    PhraseWithFinishedLoop   = 2,  ///< phrase with a finished loop
    PhraseReadyLoopRecording = 3,  ///< phrase ready, loop recording
    PhraseRecordingNoLoop    = 4,  ///< phrase recording, no loop
    PhraseRecordingLoopDone  = 5,  ///< phrase recording, loop already done
    PhraseLoopCoinit         = 6   ///< phrase + first loop recording together (coinit)
};

/// The spec §8 product. Returns nullopt for the two nil pairs (a loop that
/// Exists or is BeingCreated under a phrase that was NotCreated — impossible).
inline std::optional<CombinedCaptureState> combinedCaptureState (PhraseCreation phrase,
                                                                 LoopCreation   loop) noexcept
{
    switch (phrase)
    {
        case PhraseCreation::NotCreated:
            return loop == LoopCreation::NotCreated
                       ? std::optional<CombinedCaptureState> (CombinedCaptureState::IdleEmpty)
                       : std::nullopt;

        case PhraseCreation::Exists:
            switch (loop)
            {
                case LoopCreation::NotCreated:   return CombinedCaptureState::PhraseReadyNoLoop;
                case LoopCreation::Exists:       return CombinedCaptureState::PhraseWithFinishedLoop;
                case LoopCreation::BeingCreated: return CombinedCaptureState::PhraseReadyLoopRecording;
            }
            break;

        case PhraseCreation::BeingCreated:
            switch (loop)
            {
                case LoopCreation::NotCreated:   return CombinedCaptureState::PhraseRecordingNoLoop;
                case LoopCreation::Exists:       return CombinedCaptureState::PhraseRecordingLoopDone;
                case LoopCreation::BeingCreated: return CombinedCaptureState::PhraseLoopCoinit;
            }
            break;
    }
    return std::nullopt;
}

} // namespace ida
```

- [ ] **Step 4: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R capture-state`
Expected: PASS (both cases).

- [ ] **Step 5: Commit**

```bash
git add core/include/ida/CombinedCaptureState.h tests/CombinedCaptureStateTests.cpp tests/CMakeLists.txt
git commit -m "feat: combined phrase/loop capture-state table (spec §8 states 0-6)"
```

---

## Task 2 — The source-agnostic command layer (`CaptureCommand` + `ITransportControl`)

**Files:**
- Create: `core/include/ida/CaptureCommand.h`
- Test: `tests/CaptureCommandTests.cpp`
- Modify: `tests/CMakeLists.txt` (add `CaptureCommandTests.cpp`)

This defines the abstract command vocabulary every input source (GUI button, MIDI note, footswitch) feeds, plus the transport port the machine calls on Record-while-stopped. The source-agnosticism contract is encoded structurally: `CaptureCommand` carries **no source field that is ever branched on** — only an optional diagnostics tag (`CaptureCommandSource`) the machine ignores. The proof that GUI and MIDI produce identical behavior lives in Task 4 (the machine test); this task pins the *shape* of the contract and the transport port's default-stopped semantics.

- [ ] **Step 1: Failing test — command vocabulary + transport port double**

Create `tests/CaptureCommandTests.cpp`:

```cpp
// Tests for ida::CaptureCommand + ida::ITransportControl — the source-agnostic
// command layer (spec §8.4). Pure; no JUCE.
#include "ida/CaptureCommand.h"

#include <catch2/catch_test_macros.hpp>

using ida::CaptureCommand;
using ida::CaptureCommandSource;
using ida::ITransportControl;

namespace
{
    /// A test transport port — records calls so the machine's transport
    /// coupling can be asserted without a real OttoHost.
    struct FakeTransport : ida::ITransportControl
    {
        bool playing { false };
        int  playCalls { 0 };
        int  stopCalls { 0 };

        bool isPlaying() const noexcept override { return playing; }
        void play() override            { ++playCalls; playing = true; }
        void stop() override            { ++stopCalls; playing = false; }
    };
}

TEST_CASE ("a CaptureCommand carries a verb and an optional diagnostics source",
           "[capture-command]")
{
    // The verb is the only field the machine acts on. The source is
    // diagnostics-only (spec §8.4 design-for-isolation: the machine must not
    // know whether the command came from a finger or a foot).
    CaptureCommand fromGui { CaptureCommand::Verb::Record, CaptureCommandSource::Gui };
    CaptureCommand fromMidi { CaptureCommand::Verb::Record, CaptureCommandSource::Midi };

    CHECK (fromGui.verb == CaptureCommand::Verb::Record);
    CHECK (fromMidi.verb == CaptureCommand::Verb::Record);

    // Two commands with the same verb are equal *ignoring* source — the source
    // does not participate in command identity, by design.
    CHECK (fromGui.verb == fromMidi.verb);

    // The Stop verb exists.
    CaptureCommand stop { CaptureCommand::Verb::Stop, CaptureCommandSource::Footswitch };
    CHECK (stop.verb == CaptureCommand::Verb::Stop);

    // Default source is Unknown when omitted (a command can be constructed
    // verb-only).
    CaptureCommand bare { CaptureCommand::Verb::Record };
    CHECK (bare.source == CaptureCommandSource::Unknown);
}

TEST_CASE ("ITransportControl default-stopped semantics via a fake port",
           "[capture-command][transport]")
{
    FakeTransport t;
    CHECK_FALSE (t.isPlaying());

    t.play();
    CHECK (t.isPlaying());
    CHECK (t.playCalls == 1);

    t.stop();
    CHECK_FALSE (t.isPlaying());
    CHECK (t.stopCalls == 1);
}
```

- [ ] **Step 2: Register, run, verify FAIL (header missing)**

Add to `tests/CMakeLists.txt` after `CombinedCaptureStateTests.cpp`:

```cmake
    CaptureCommandTests.cpp
```

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `ida/CaptureCommand.h` missing.

- [ ] **Step 3: Minimal implementation — `CaptureCommand.h`**

Create `core/include/ida/CaptureCommand.h`:

```cpp
#pragma once

namespace ida
{

/// Where a CaptureCommand originated. **Diagnostics only.** The state machine
/// (spec §8.4 design-for-isolation) must never branch on this — it exists so a
/// log/telemetry line can say "Record from MIDI" without the *behavior*
/// depending on the source. GUI button, MIDI note, and footswitch all dispatch
/// the same verb; the machine cannot tell them apart, and that is the point.
enum class CaptureCommandSource
{
    Unknown,
    Gui,
    Midi,
    Footswitch
};

/// The single performer action vocabulary (spec §8.1, §8.4). One pedal, one
/// button, one MIDI note all produce one of these. The verb's *effect* is
/// resolved against the current per-phrase combined state by PhraseLoopMachine
/// — exactly the Quantiloop-Pro single-pedal model where one press does a
/// different thing depending on state.
struct CaptureCommand
{
    enum class Verb
    {
        Record,  ///< context-dependent: start transport / coinit phrase+loop / add loop
        Stop     ///< close the active phrase and/or loop (spec §8.2 stop semantics)
    };

    Verb                 verb;
    CaptureCommandSource source { CaptureCommandSource::Unknown };
};

/// Transport-control port (spec §9). IDA has no engine-side transport — OTTO is
/// the source. The state machine depends only on this abstract port so `core`
/// stays JUCE-free and OTTO-free; `app` injects an adapter that forwards
/// `play()`/`stop()` to `ida::OttoHost` (via the existing TransportBarHost path,
/// spec §15.1). Message-thread only; no RT constraint (not on the audio path).
class ITransportControl
{
public:
    virtual ~ITransportControl() = default;

    /// True iff the transport is currently running. The machine reads this to
    /// decide the "transport stopped + Record → start transport first" edge.
    virtual bool isPlaying() const noexcept = 0;

    /// Command the transport to play. On Record-while-stopped the machine calls
    /// this, then proceeds as "transport running".
    virtual void play() = 0;

    /// Command the transport to stop. (Reserved for a future Stop-also-stops-
    /// transport policy; the machine does not call this in this slice — Stop
    /// closes the phrase/loop without stopping OTTO, per spec §8.2 which only
    /// speaks to structure, not transport.)
    virtual void stop() = 0;
};

} // namespace ida
```

- [ ] **Step 4: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R capture-command`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/ida/CaptureCommand.h tests/CaptureCommandTests.cpp tests/CMakeLists.txt
git commit -m "feat: source-agnostic CaptureCommand + ITransportControl transport port"
```

---

## Task 3 — `PhraseLoopMachine`: coinit (state 0 → 6 → 2) over `promote()`

**Files:**
- Create: `core/include/ida/PhraseLoopMachine.h`
- Create: `core/src/PhraseLoopMachine.cpp`
- Modify: `core/CMakeLists.txt` (add `src/PhraseLoopMachine.cpp` to the `core` target sources)
- Test: `tests/PhraseLoopMachineTests.cpp`
- Modify: `tests/CMakeLists.txt` (add `PhraseLoopMachineTests.cpp`)

The machine owns a `CaptureSession`, a current root `Constituent`, a `TempoMap` (session→LMC), an id allocator, and a reference to an `ITransportControl`. `dispatch(command, lmcNow)` resolves the verb against the live state:

- **Record, transport stopped:** call `transport.play()`, then proceed as running.
- **Record, transport running:** `markIn(lmcNow, focusedTape)` then `markOut(lmcNow)` is **not** the model — instead Record **opens** a capture (markIn), and **Stop closes** it (markOut → promote). So `Record` = markIn (begin defining phrase + loop 0 — coinit, the phrase end not yet set ⇒ state 6 if outside a phrase). `Stop` = markOut at `lmcNow` → `promote()` → push result.

Per spec §8.1 + §8.2: while a phrase is being defined (in-point set, out-point not), **loop 0 is being recorded into it and shares the phrase's bounds exactly**; coinit *is* this condition. `promote()` produces the phrase wrapper + the loop-0 child with the loop's tape-region spanning [markIn, markOut] = the phrase span — exactly the "loop 0 start = phrase start, loop 0 end follows phrase end" rule. This task implements **coinit only** (playhead outside any phrase). Task 5 adds the inside-phrase (new-loop) branch; Task 6 the transport-stopped edge; Task 7 stop-semantics assertions.

This task introduces the machine and proves: a fresh machine reports `IdleEmpty` (0); `Record` (transport already running, empty root) opens coinit and reports `PhraseLoopCoinit` (6); `Stop` closes it, mints a phrase containing loop 0 with identical bounds, and the resting state is `PhraseWithFinishedLoop` (2) for that phrase.

- [ ] **Step 1: Failing test — fresh machine is idle; Record→coinit(6); Stop→phrase+loop0**

Create `tests/PhraseLoopMachineTests.cpp`:

```cpp
// Tests for ida::PhraseLoopMachine — the per-phrase phrase/loop state machine
// (spec §8) over CaptureSession + promotion::promote(). Pure; no JUCE.
//
// Spec: docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md
#include "ida/PhraseLoopMachine.h"

#include "ida/CaptureCommand.h"
#include "ida/CombinedCaptureState.h"
#include "ida/Constituent.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::CaptureCommand;
using ida::CaptureCommandSource;
using ida::CombinedCaptureState;
using ida::Constituent;
using ida::ConstituentId;
using ida::ITransportControl;
using ida::PhraseLoopMachine;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TempoMap;

namespace
{
    TempoMap identityMap() { return TempoMap::constant (Rational (1)); }

    /// A 60-second empty song root, the same shape PromotionTests uses.
    std::shared_ptr<const Constituent> emptyRoot()
    {
        return std::make_shared<const Constituent> (
            Constituent (ConstituentId (1), Position(), Position (Rational (60))));
    }

    struct FakeTransport : ITransportControl
    {
        bool playing { true };   // default running for coinit tests
        int  playCalls { 0 };
        bool isPlaying() const noexcept override { return playing; }
        void play() override { ++playCalls; playing = true; }
        void stop() override { playing = false; }
    };

    /// Build a machine on an empty root with a monotonic id allocator and the
    /// given transport. `focusedTape` is the tape the markIn pins (Slice 4's
    /// 1:1 channel→tape; here a fixed test tape).
    PhraseLoopMachine makeMachine (FakeTransport& transport,
                                   TapeId focusedTape = TapeId (1))
    {
        static std::int64_t nextId = 1000;
        return PhraseLoopMachine (emptyRoot(),
                                  identityMap(),
                                  focusedTape,
                                  [] { return ConstituentId (nextId++); },
                                  transport);
    }
}

TEST_CASE ("a fresh PhraseLoopMachine is in IdleEmpty (state 0)",
           "[phrase-loop-machine]")
{
    FakeTransport t;
    auto machine = makeMachine (t);
    CHECK (machine.activeState() == CombinedCaptureState::IdleEmpty);
    CHECK (machine.root()->children().empty());
}

TEST_CASE ("Record with transport running on an empty root enters coinit (state 6)",
           "[phrase-loop-machine][coinit]")
{
    FakeTransport t;             // already playing
    auto machine = makeMachine (t);

    const auto outcome = machine.dispatch (
        CaptureCommand { CaptureCommand::Verb::Record, CaptureCommandSource::Gui },
        /*lmcNow*/ Rational (4));

    // Record opened the coinit: phrase + loop 0 being defined together. No
    // promotion has happened yet (the phrase end is not marked), so nothing was
    // committed to the tree and there is no undo entry to push.
    CHECK (machine.activeState() == CombinedCaptureState::PhraseLoopCoinit);  // 6
    CHECK_FALSE (outcome.committed);
    CHECK (machine.root()->children().empty());      // tree unchanged until Stop
    CHECK (t.playCalls == 0);                         // already running, no start
}

TEST_CASE ("Stop after coinit mints a phrase containing loop 0 with identical bounds",
           "[phrase-loop-machine][coinit][stop]")
{
    FakeTransport t;
    auto machine = makeMachine (t, TapeId (7));

    machine.dispatch ({ CaptureCommand::Verb::Record }, /*lmcNow*/ Rational (4));
    const auto outcome = machine.dispatch ({ CaptureCommand::Verb::Stop },
                                           /*lmcNow*/ Rational (10));

    // The phrase was committed; the caller receives a new root + a label to push.
    REQUIRE (outcome.committed);
    REQUIRE (outcome.newRoot != nullptr);
    CHECK (outcome.undoLabel == "capture phrase");
    REQUIRE (outcome.restorePoint.has_value());
    CHECK (outcome.restorePoint->pendingIn  == Rational (4));
    CHECK (outcome.restorePoint->pendingTape == TapeId (7));

    // One phrase at the root, containing exactly one loop (loop 0).
    REQUIRE (outcome.newRoot->children().size() == 1);
    const auto& phrase = *outcome.newRoot->children()[0];
    REQUIRE (phrase.isPhrase());

    REQUIRE (phrase.children().size() == 1);
    const auto& loop0 = *phrase.children()[0];
    REQUIRE (loop0.isLoop());

    // Loop 0 bounds = phrase bounds exactly (spec §8: loop 0 start = phrase
    // start; loop 0 end follows phrase end). The phrase spans the captured
    // region; loop 0's tape slice is the same [4,10) region.
    CHECK (loop0.tapeReference()->tape    == TapeId (7));
    CHECK (loop0.tapeReference()->tapeIn  == Rational (4));
    CHECK (loop0.tapeReference()->tapeOut == Rational (10));
    // Loop 0's conceptual span (phrase-local) equals the phrase's full span.
    CHECK (loop0.conceptualIn()  == Position());                  // = phrase start
    CHECK (loop0.conceptualOut() == Position (Rational (6)));     // = phrase length (10-4)

    // After committing, the machine has adopted the new root and the active
    // target phrase is now at rest with a finished loop (state 2).
    CHECK (machine.root().get() == outcome.newRoot.get());
    CHECK (machine.activeState() == CombinedCaptureState::PhraseWithFinishedLoop);  // 2
}
```

- [ ] **Step 2: Register, run, verify FAIL (header missing)**

Add to `tests/CMakeLists.txt` after `CaptureCommandTests.cpp`:

```cmake
    PhraseLoopMachineTests.cpp
```

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `ida/PhraseLoopMachine.h` missing.

- [ ] **Step 3: Minimal implementation — `PhraseLoopMachine.h`**

Create `core/include/ida/PhraseLoopMachine.h`:

```cpp
#pragma once

#include "ida/CaptureCommand.h"
#include "ida/CaptureSession.h"
#include "ida/CombinedCaptureState.h"
#include "ida/Constituent.h"
#include "ida/ConstituentId.h"
#include "ida/Promotion.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"
#include "ida/TempoMap.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ida
{

/// The result of dispatching one CaptureCommand. When `committed` is true a
/// promotion happened: `newRoot` is the post-edit tree the caller must adopt
/// into its UndoStack with `undoLabel` + `restorePoint` (matching the existing
/// MainComponent::onMarkOut push contract). When `committed` is false the
/// command only changed the in-flight marking state (e.g. Record opened a
/// capture) and the tree is untouched.
struct CaptureOutcome
{
    bool committed { false };
    std::shared_ptr<const Constituent> newRoot;     // present iff committed
    std::string undoLabel;                           // present iff committed
    std::optional<CaptureRestorePoint> restorePoint; // present iff committed

    /// Mirror of promote()'s result for callers that drive UI banners (the
    /// "New phrase captured" / "Added to verse" messaging). Present iff
    /// committed.
    std::optional<promotion::PromotionResult> promotionResult;
};

/// The per-phrase phrase/loop creation state machine (spec §8). It owns a
/// CaptureSession (the mark window) and the current arrangement root, and
/// resolves a source-agnostic CaptureCommand against the *active target*
/// phrase's combined state:
///
///   - Record, transport stopped  → start transport, then proceed as running.
///   - Record, transport running, no open capture → markIn (begin defining a
///     phrase + loop 0 together; if the playhead is outside any phrase this is
///     coinit, state 6; inside a phrase this is a new loop, state 3).
///   - Stop → markOut → promote() → adopt the new root, return CaptureOutcome.
///
/// "Per-phrase" is satisfied by construction: promote() touches only the phrase
/// under the playhead; every other phrase stays untouched. activeState() reports
/// the combined state of the phrase the *current* gesture acts on, computed from
/// the live tree + CaptureSession — never a stored per-phrase enum.
///
/// JUCE-free, message-thread only. The transport port is injected so `core`
/// never sees OTTO or JUCE.
class PhraseLoopMachine
{
public:
    using RootPtr      = std::shared_ptr<const Constituent>;
    using IdAllocator  = promotion::IdAllocator;

    /// Constructs a machine over `initialRoot`. `sessionToLmc` maps conceptual
    /// time to LMC (identity in the M3 simplification). `focusedTape` is the
    /// tape the next markIn pins (Slice 4's 1:1 channel→tape). `allocateId`
    /// mints fresh ConstituentIds. `transport` must outlive the machine.
    /// Throws std::invalid_argument if `initialRoot` is null.
    PhraseLoopMachine (RootPtr initialRoot,
                       TempoMap sessionToLmc,
                       TapeId focusedTape,
                       IdAllocator allocateId,
                       ITransportControl& transport);

    /// The current arrangement root.
    const RootPtr& root() const noexcept { return root_; }

    /// The combined state (spec §8) of the active target phrase — the phrase the
    /// current gesture acts on. With no capture open and an empty/at-rest tree
    /// this reports IdleEmpty (0) or, when a capture is open, the BeingCreated
    /// row resolved against whether the playhead landed inside an existing
    /// phrase (loop branch) or outside (coinit).
    CombinedCaptureState activeState() const noexcept;

    /// True while a capture is open (markIn set, markOut not). Mirrors
    /// CaptureSession::isCapturing.
    bool isMarking() const noexcept { return session_.isCapturing(); }

    /// The tape the next markIn will pin. Updated by the caller when the focused
    /// channel changes (Slice 4).
    void setFocusedTape (TapeId tape) noexcept { focusedTape_ = tape; }

    /// Adopt a root the caller authoritatively changed (e.g. after undo/redo or
    /// a New Song). Keeps the machine's view of the tree honest.
    void adoptRoot (RootPtr root);

    /// Dispatch one source-agnostic command. `lmcNow` is the current LMC time
    /// (the caller converts the transport position / playhead to Rational ticks
    /// at the boundary — there is no Rational::fromDouble). Returns the outcome
    /// the caller turns into an UndoStack push (when committed).
    CaptureOutcome dispatch (const CaptureCommand& command, Rational lmcNow);

private:
    /// Open a capture at `lmcNow` (markIn). Used by the Record verb.
    void beginMarking (Rational lmcNow);

    /// Close the open capture at `lmcNow` (markOut → promote). Used by Stop.
    CaptureOutcome commitMarking (Rational lmcNow);

    /// Whether `lmcAtMarkIn` sits inside an existing (non-wrapper, non-hybrid)
    /// phrase in the current tree — the coinit-vs-new-loop discriminator. Pure
    /// read of the live tree; mirrors promote()'s host search.
    bool playheadInsidePhrase (Rational lmcAtMarkIn) const;

    RootPtr            root_;
    TempoMap           sessionToLmc_;
    TapeId             focusedTape_;
    IdAllocator        allocateId_;
    ITransportControl& transport_;
    CaptureSession     session_;

    /// Snapshot of where the open capture started, so activeState() can report
    /// the coinit-vs-new-loop branch and the resting state after commit reflects
    /// the active target.
    std::optional<Rational> openMarkInLmc_;
    bool lastCommitAddedLoopOnly_ { false };  // last Stop added a loop to a phrase
    bool haveCommittedAnything_   { false };  // ≥1 phrase committed this session
};

} // namespace ida
```

- [ ] **Step 4: Minimal implementation — `PhraseLoopMachine.cpp` (coinit + Stop only)**

Create `core/src/PhraseLoopMachine.cpp`. This step implements only what Task 3's test needs: ctor, `activeState()` for IdleEmpty/coinit/state-2, Record→markIn, Stop→markOut→promote→adopt. The transport-stopped edge is a stub that assumes running (Task 6 fleshes it out); the inside-phrase branch returns coinit-style for now (Task 5 splits it). Keep it minimal — do not pre-build Task 5/6/7 behavior.

```cpp
#include "ida/PhraseLoopMachine.h"

#include "ida/Position.h"

#include <stdexcept>
#include <utility>

namespace ida
{

PhraseLoopMachine::PhraseLoopMachine (RootPtr initialRoot,
                                      TempoMap sessionToLmc,
                                      TapeId focusedTape,
                                      IdAllocator allocateId,
                                      ITransportControl& transport)
    : root_ (std::move (initialRoot)),
      sessionToLmc_ (std::move (sessionToLmc)),
      focusedTape_ (focusedTape),
      allocateId_ (std::move (allocateId)),
      transport_ (transport)
{
    if (root_ == nullptr)
        throw std::invalid_argument ("ida::PhraseLoopMachine: initialRoot must not be null");

    // The marking layer starts armed: a tape records while assigned (Slice 4),
    // so the machine is always ready to *mark* structure over it. (Disarm is a
    // separate performer gesture not modeled in this slice.)
    session_.arm();
}

void PhraseLoopMachine::adoptRoot (RootPtr root)
{
    if (root == nullptr)
        throw std::invalid_argument ("ida::PhraseLoopMachine: adoptRoot root must not be null");
    root_ = std::move (root);
}

bool PhraseLoopMachine::playheadInsidePhrase (Rational lmcAtMarkIn) const
{
    // A capture region of trivial positive duration just to probe promote()'s
    // host decision would be wasteful; instead we replicate the host predicate:
    // is there a non-wrapper, non-hybrid Phrase whose LMC span contains the
    // point? promote() will make the authoritative call at commit time — this
    // read is only for activeState() reporting and the Task-5 branch. We walk
    // top-level conceptual spans mapped to LMC (identity map in M3); deeper
    // nesting is handled by promote() itself at commit.
    for (const auto& child : root_->children())
    {
        if (! child->isPhrase())                 continue;
        if (child->tapeReference().has_value())  continue;  // hybrid: not a host
        if (isPlacementWrapper (*child))         continue;  // wrapper: descends, not host
        const Rational startLmc = sessionToLmc_.apply (child->conceptualIn().wholeNotes());
        const Rational endLmc   = sessionToLmc_.apply (child->conceptualOut().wholeNotes());
        if (lmcAtMarkIn >= startLmc && lmcAtMarkIn < endLmc)
            return true;
    }
    return false;
}

CombinedCaptureState PhraseLoopMachine::activeState() const noexcept
{
    if (session_.isCapturing() && openMarkInLmc_.has_value())
    {
        // A capture is open → the active phrase's loop 0 (or a new loop) is
        // being recorded. Outside any phrase ⇒ coinit (6); inside ⇒ phrase
        // ready + loop recording (3).
        return playheadInsidePhrase (*openMarkInLmc_)
                   ? CombinedCaptureState::PhraseReadyLoopRecording   // 3
                   : CombinedCaptureState::PhraseLoopCoinit;          // 6
    }

    // No capture open. If we have committed at least one phrase this session the
    // active target is at rest with a finished loop (state 2); otherwise idle.
    if (haveCommittedAnything_)
        return CombinedCaptureState::PhraseWithFinishedLoop;          // 2

    return CombinedCaptureState::IdleEmpty;                           // 0
}

void PhraseLoopMachine::beginMarking (Rational lmcNow)
{
    session_.markIn (lmcNow, focusedTape_);
    openMarkInLmc_ = lmcNow;
}

CaptureOutcome PhraseLoopMachine::commitMarking (Rational lmcNow)
{
    CaptureOutcome outcome;

    const auto region = session_.markOut (lmcNow);
    if (! region.has_value())
        return outcome;   // markOut rejected (out <= in); stays open, nothing committed

    const CaptureRestorePoint restorePoint { region->inLmcSeconds, region->tape };

    auto result = promotion::promote (
        *root_,
        sessionToLmc_,
        *region,
        region->inLmcSeconds,
        promotion::AttachmentMode::Shared,
        allocateId_);

    lastCommitAddedLoopOnly_ = ! result.mintedPhraseId.has_value();
    haveCommittedAnything_   = true;

    outcome.committed       = true;
    outcome.undoLabel       = result.undoLabel;
    outcome.restorePoint    = restorePoint;
    outcome.newRoot         = std::make_shared<const Constituent> (result.newRoot);
    outcome.promotionResult = std::move (result);

    root_          = outcome.newRoot;
    openMarkInLmc_ = std::nullopt;

    return outcome;
}

CaptureOutcome PhraseLoopMachine::dispatch (const CaptureCommand& command, Rational lmcNow)
{
    // Source-agnostic: command.source is NEVER read here. Only the verb and the
    // live state drive behavior.
    switch (command.verb)
    {
        case CaptureCommand::Verb::Record:
        {
            if (! transport_.isPlaying())
                transport_.play();      // Task 6 hardens the stopped→start edge

            if (! session_.isCapturing())
                beginMarking (lmcNow);
            return {};                  // opening a capture commits nothing
        }

        case CaptureCommand::Verb::Stop:
        {
            if (session_.isCapturing())
                return commitMarking (lmcNow);
            return {};
        }
    }

    return {};
}

} // namespace ida
```

- [ ] **Step 5: Add the source file to the `core` target**

In `core/CMakeLists.txt`, add `src/PhraseLoopMachine.cpp` to the `core` library's source list (alongside `src/Promotion.cpp`, `src/CaptureSession.cpp` if present — match the existing list style). Run `grep -n "Promotion.cpp\|CaptureSession" core/CMakeLists.txt` first to find the exact list and mirror its formatting.

- [ ] **Step 6: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R phrase-loop-machine`
Expected: PASS (all three cases).

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/PhraseLoopMachine.h core/src/PhraseLoopMachine.cpp core/CMakeLists.txt tests/PhraseLoopMachineTests.cpp tests/CMakeLists.txt
git commit -m "feat: PhraseLoopMachine coinit — Record opens, Stop mints phrase+loop 0"
```

---

## Task 4 — Prove the command layer is source-agnostic (the §8.4 isolation contract)

**Files:**
- Modify: `tests/PhraseLoopMachineTests.cpp` (append)

This is the test the spec explicitly demands: a fake "GUI" source and a fake "MIDI" source must drive the machine to **identical** state. We run the same Record→Stop sequence twice — once tagging every command `Gui`, once tagging `Midi` — and assert the resulting trees are structurally identical and the resting states match. No production code changes; if the machine ever grows a source-dependent branch, this test fails.

- [ ] **Step 1: Failing-then-passing test — identical trees from GUI vs MIDI sources**

Append to `tests/PhraseLoopMachineTests.cpp`:

```cpp
namespace
{
    /// Run one full Record→Stop coinit cycle, tagging every command with
    /// `source`, and return the committed root. Same lmc times both runs so the
    /// only difference is the (ignored) source tag.
    std::shared_ptr<const Constituent> runCoinitCycle (CaptureCommandSource source)
    {
        FakeTransport t;
        // Fresh deterministic id allocator per run so ids match across runs.
        std::int64_t nextId = 5000;
        PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (3),
                                   [&nextId] { return ConstituentId (nextId++); },
                                   t);

        machine.dispatch ({ CaptureCommand::Verb::Record, source }, Rational (2));
        auto outcome = machine.dispatch ({ CaptureCommand::Verb::Stop, source },
                                         Rational (8));
        return outcome.newRoot;
    }

    /// Structural compare of two Constituent subtrees for the fields this slice
    /// produces: id, phrase-ness, loop tape-reference, conceptual bounds, and
    /// children (recursively). Enough to prove "identical state", JUCE-free.
    bool sameShape (const Constituent& a, const Constituent& b)
    {
        if (a.id() != b.id())                                   return false;
        if (a.isPhrase() != b.isPhrase())                      return false;
        if (a.isLoop()   != b.isLoop())                        return false;
        if (a.conceptualIn()  != b.conceptualIn())             return false;
        if (a.conceptualOut() != b.conceptualOut())            return false;
        if (a.isLoop())
        {
            const auto& ra = *a.tapeReference();
            const auto& rb = *b.tapeReference();
            if (ra.tape != rb.tape || ra.tapeIn != rb.tapeIn || ra.tapeOut != rb.tapeOut)
                return false;
        }
        if (a.children().size() != b.children().size())        return false;
        for (std::size_t i = 0; i < a.children().size(); ++i)
            if (! sameShape (*a.children()[i], *b.children()[i]))
                return false;
        return true;
    }
}

TEST_CASE ("the command layer is source-agnostic: GUI and MIDI yield identical state",
           "[phrase-loop-machine][source-agnostic]")
{
    const auto fromGui  = runCoinitCycle (CaptureCommandSource::Gui);
    const auto fromMidi = runCoinitCycle (CaptureCommandSource::Midi);
    const auto fromFoot = runCoinitCycle (CaptureCommandSource::Footswitch);

    REQUIRE (fromGui  != nullptr);
    REQUIRE (fromMidi != nullptr);
    REQUIRE (fromFoot != nullptr);

    // Same gesture, three different "input sources" → byte-identical structure.
    // This is the design-for-isolation guarantee of spec §8.4: the state machine
    // does not know whether the command came from a finger, a MIDI note, or a
    // footswitch.
    CHECK (sameShape (*fromGui,  *fromMidi));
    CHECK (sameShape (*fromGui,  *fromFoot));
}
```

- [ ] **Step 2: Run, verify PASS (the machine already ignores source)**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R source-agnostic`
Expected: PASS — because Task 3's `dispatch` never reads `command.source`. (If it fails, the machine has a source-dependent branch — that is the bug this test exists to catch; fix the machine, not the test.)

> Note: this is a "characterization test passes immediately" case (the contract was satisfied by construction in Task 3). That is correct TDD here — the test encodes *why* the no-source-branch design matters (spec §8.4) and will fail the instant someone violates it. Per CLAUDE.md rule 9, the test verifies intent (isolation), not just current behavior.

- [ ] **Step 3: Commit**

```bash
git add tests/PhraseLoopMachineTests.cpp
git commit -m "test: prove CaptureCommand layer is source-agnostic (spec §8.4 isolation)"
```

---

## Task 5 — Inside-an-existing-phrase Record → new loop (state 3); non-destructive layering

**Files:**
- Modify: `tests/PhraseLoopMachineTests.cpp` (append)
- Modify: `core/src/PhraseLoopMachine.cpp` (only if the test exposes a gap — `promote()` already routes inside-vs-outside, so the machine likely needs no change beyond reporting)

Spec §8.1: **transport running + Record, playhead INSIDE an existing phrase → new loop in that phrase (state 3)**. `promote()` already does this — when `lmcAtMarkIn` lands inside a phrase, it adds a loop child and returns `mintedPhraseId == nullopt`. The new loop is a **distinct Constituent** layered non-destructively (spec §8.5 overdub = a new loop, not summed audio). This task verifies: (a) a second Record→Stop with the playhead inside the first phrase adds a *second* loop to that phrase (now 2 children), mints no new phrase, and the two loops are distinct Constituents; (b) `activeState()` reports `PhraseReadyLoopRecording` (3) while that capture is open; (c) the first phrase is untouched in identity (per-phrase isolation).

- [ ] **Step 1: Failing test — Record inside an existing phrase adds a layered loop**

Append to `tests/PhraseLoopMachineTests.cpp`:

```cpp
TEST_CASE ("Record with playhead inside an existing phrase adds a new layered loop (state 3)",
           "[phrase-loop-machine][overdub][layer]")
{
    FakeTransport t;
    std::int64_t nextId = 2000;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (1),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    // First cycle: coinit a phrase spanning [0,20) with loop 0.
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (0));
    auto first = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (20));
    REQUIRE (first.committed);
    REQUIRE (first.newRoot->children().size() == 1);
    const auto phraseIdAfterFirst = first.newRoot->children()[0]->id();
    REQUIRE (first.newRoot->children()[0]->children().size() == 1);  // loop 0 only

    // Second cycle: playhead INSIDE the phrase ([0,20)). Record opens a capture
    // whose markIn is at LMC 5 → activeState must report state 3 (loop recording
    // inside an existing phrase), NOT coinit.
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (5));
    CHECK (machine.activeState() == CombinedCaptureState::PhraseReadyLoopRecording);  // 3

    auto second = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (15));
    REQUIRE (second.committed);
    CHECK (second.undoLabel == "capture loop into capture");   // promote's loop-into-host label

    // No new phrase minted; the SAME phrase now owns TWO loops (loop 0 + the
    // overdub), layered non-destructively as distinct Constituents.
    REQUIRE (second.newRoot->children().size() == 1);
    const auto& phrase = *second.newRoot->children()[0];
    CHECK (phrase.id() == phraseIdAfterFirst);                 // same phrase identity
    REQUIRE (phrase.children().size() == 2);                   // two layered loops

    const auto& loop0 = *phrase.children()[0];
    const auto& loop1 = *phrase.children()[1];
    CHECK (loop0.id() != loop1.id());                          // distinct Constituents
    CHECK (loop0.isLoop());
    CHECK (loop1.isLoop());
    // The overdub loop references the [5,15) tape region (its own slice), proving
    // it is a separate layer, not audio summed onto loop 0.
    CHECK (loop1.tapeReference()->tapeIn  == Rational (5));
    CHECK (loop1.tapeReference()->tapeOut == Rational (15));
}
```

> The `undoLabel` literal `"capture loop into capture"` comes from `promote()`'s host-loop label (`"capture loop into " + hostName`); the minted phrase's `role`/name is `"capture"` (see `PromotionTests.cpp` mint case → `phraseMetadata()->role == "capture"`, and the host case label `"capture loop into verse"`). Confirm the actual host name the minted phrase carries by reading `core/src/Promotion.cpp`'s mint branch; if the minted phrase's `name()` is empty (only `role` is set), the label will be `"capture loop into "` — adjust the expected string to whatever `promote()` actually produces. **Read the mint branch before asserting** rather than guessing.

- [ ] **Step 2: Run, verify (likely PASS; FAIL only if activeState mis-reports)**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "overdub|layer"`
Expected: PASS for the layering/identity assertions (driven entirely by `promote()`), and PASS for the state-3 assertion (Task 3's `activeState()` + `playheadInsidePhrase` already distinguish inside vs outside). If `activeState()` reports 6 instead of 3, fix `playheadInsidePhrase` (it must see the committed phrase in `root_`).

- [ ] **Step 3: If the label assertion mismatched, correct the expected string from `promote()`'s real output**

If Step 2 failed only on `undoLabel`, read `core/src/Promotion.cpp` mint/host branches, set the test's expected label to the real value, re-run, verify PASS. Do not change `promote()`.

- [ ] **Step 4: Commit**

```bash
git add tests/PhraseLoopMachineTests.cpp core/src/PhraseLoopMachine.cpp
git commit -m "feat: Record inside an existing phrase adds a non-destructive layered loop (state 3)"
```

---

## Task 6 — Transport-stopped + Record → start transport, then proceed (spec §9 / §15.1)

**Files:**
- Modify: `tests/PhraseLoopMachineTests.cpp` (append)
- Modify: `core/src/PhraseLoopMachine.cpp` (only if Task 3's stub needs hardening)

Spec §8.1 + §9 + §15.1: **transport stopped + Record → command the transport to play, then proceed as "transport running."** The machine calls `transport.play()` exactly once on the stopped→running edge, then opens the capture in the same dispatch. It must **not** call `play()` when already running. We assert call counts on the fake transport and that a single Record both starts transport and opens the coinit.

- [ ] **Step 1: Failing-then-passing test — stopped Record starts transport once then marks**

Append to `tests/PhraseLoopMachineTests.cpp`:

```cpp
TEST_CASE ("Record while transport is stopped starts the transport, then opens the capture",
           "[phrase-loop-machine][transport]")
{
    FakeTransport t;
    t.playing = false;          // transport stopped
    std::int64_t nextId = 3000;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (1),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (4));

    // The stopped→running edge fired exactly once, and the capture opened in the
    // same gesture (state 6 coinit on an empty root).
    CHECK (t.playCalls == 1);
    CHECK (t.isPlaying());
    CHECK (machine.activeState() == CombinedCaptureState::PhraseLoopCoinit);  // 6
    CHECK (machine.isMarking());
}

TEST_CASE ("Record while transport already running does NOT re-start the transport",
           "[phrase-loop-machine][transport]")
{
    FakeTransport t;
    t.playing = true;           // already running
    std::int64_t nextId = 3100;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (1),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (4));

    CHECK (t.playCalls == 0);   // no redundant start
    CHECK (machine.isMarking());
}
```

- [ ] **Step 2: Run, verify PASS (Task 3's `if (! transport_.isPlaying()) transport_.play();` already satisfies this)**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "phrase-loop-machine.*transport|transport.*phrase"`
Expected: PASS. (Task 3 implemented the guard; this task pins the contract with explicit call-count assertions. If `playCalls != 1`/`!= 0` as expected, fix the guard in `dispatch`.)

- [ ] **Step 3: Commit**

```bash
git add tests/PhraseLoopMachineTests.cpp core/src/PhraseLoopMachine.cpp
git commit -m "feat: Record-while-stopped starts transport once then opens capture (spec §9)"
```

---

## Task 7 — Stop semantics: phrase end = Stop point; loop end = parent phrase end (spec §8.2)

**Files:**
- Modify: `tests/PhraseLoopMachineTests.cpp` (append)

Spec §8.2: when Stop is issued —
- **phrase being created:** phrase end = where Stop was issued;
- **loop being created:** loop end = its **parent phrase's** end (the loop inherits the phrase end, not the raw Stop point);
- so in **coinit (6)**: Stop sets phrase end = Stop point AND loop 0 end = phrase end = same point ⇒ loop-0 length == phrase length;
- for a **loop added to an already-finished phrase (3)**: Stop clamps the loop end to the phrase's existing end (layered loops align to the phrase boundary).

`promote()` already encodes the clamp (`PromotionTests.cpp` "clamps Loop bounds to the host Phrase when region extends past" — the loop's *conceptual* bounds are clipped to the host while the tape-reference keeps the raw region). This task asserts both halves through the machine:

(a) **coinit**: loop 0 conceptual end == phrase conceptual end (= Stop − markIn);
(b) **layered loop past the phrase boundary**: a loop whose Stop point is *after* the parent phrase's end has its **conceptual** end clamped to the phrase end, while its tape-reference retains the raw Stop point.

- [ ] **Step 1: Failing-then-passing test — coinit loop bounds == phrase bounds; layered loop clamps to phrase end**

Append to `tests/PhraseLoopMachineTests.cpp`:

```cpp
TEST_CASE ("Stop in coinit makes loop 0 length equal the phrase length (spec §8.2)",
           "[phrase-loop-machine][stop-semantics][coinit]")
{
    FakeTransport t;
    std::int64_t nextId = 4000;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (2),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (3));   // markIn = 3
    auto out = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (11)); // Stop = 11
    REQUIRE (out.committed);

    const auto& phrase = *out.newRoot->children()[0];
    const auto& loop0  = *phrase.children()[0];

    // Phrase end = Stop point: phrase spans [3,11) LMC → conceptual [0,8).
    CHECK (phrase.conceptualIn()  == Position());
    CHECK (phrase.conceptualOut() == Position (Rational (8)));    // 11 - 3

    // Loop 0 end follows the phrase end exactly → loop-0 length == phrase length.
    CHECK (loop0.conceptualIn()  == Position());
    CHECK (loop0.conceptualOut() == Position (Rational (8)));     // = phrase span
    CHECK (loop0.tapeReference()->tapeOut == Rational (11));      // raw Stop point on tape
}

TEST_CASE ("A layered loop whose Stop is past the phrase end clamps its end to the phrase (spec §8.2)",
           "[phrase-loop-machine][stop-semantics][layer]")
{
    FakeTransport t;
    std::int64_t nextId = 4100;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (1),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    // Phrase [0,10).
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (0));
    machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (10));

    // Overdub: markIn inside the phrase at LMC 6, Stop at LMC 14 (PAST the
    // phrase end of 10). The loop's CONCEPTUAL bounds clamp to the phrase
    // boundary [6,10) → phrase-local [6,10); its tape-reference keeps the raw
    // [6,14) region (the audio beyond the boundary still exists on the tape).
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (6));
    auto out = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (14));
    REQUIRE (out.committed);

    const auto& phrase = *out.newRoot->children()[0];
    REQUIRE (phrase.children().size() == 2);
    const auto& layered = *phrase.children()[1];

    // Conceptual end clamped to the phrase boundary (phrase-local 10, since the
    // phrase starts at conceptual 0; the loop sits at [6,10)).
    CHECK (layered.conceptualOut() == Position (Rational (10)));  // clamped to phrase end
    // Tape-reference retains the unclamped Stop point.
    CHECK (layered.tapeReference()->tapeOut == Rational (14));
}
```

> The exact phrase-local conceptual numbers depend on how `promote()` maps host-relative bounds (`PromotionTests.cpp` straddle case shows conceptual bounds are *host-local*: `loop.conceptualIn() == (markIn - hostStart)`). Since here the host phrase starts at conceptual/LMC 0, host-local == LMC. **Verify against the straddle test's arithmetic before locking the numbers** — if `promote()` produces host-local `[6,10)` vs absolute `[6,10)` they coincide here only because the phrase starts at 0. Read `core/src/Promotion.cpp`'s host-clamp branch to confirm the convention, then set the expected values to match.

- [ ] **Step 2: Run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R stop-semantics`
Expected: PASS. These are driven by `promote()`'s existing clamp; the machine only plumbs the region. If a number is off, correct the *expected value* to `promote()`'s real output (read the source — do not change `promote()`).

- [ ] **Step 3: Commit**

```bash
git add tests/PhraseLoopMachineTests.cpp
git commit -m "test: Stop semantics — phrase end = stop point, loop end clamps to phrase (spec §8.2)"
```

---

## Task 8 — `adoptRoot` keeps the machine honest across undo/redo

**Files:**
- Modify: `tests/PhraseLoopMachineTests.cpp` (append)

The caller (`MainComponent`) owns the `UndoStack`; on undo/redo the authoritative root changes outside the machine. The machine must adopt it so a subsequent Record acts on the correct tree (otherwise an undo followed by a Record would promote onto a stale root and re-introduce undone phrases). This task verifies `adoptRoot` replaces the machine's view and that `activeState()` recomputes against the adopted tree.

- [ ] **Step 1: Failing-then-passing test — adoptRoot replaces the tree; subsequent Stop promotes onto it**

Append to `tests/PhraseLoopMachineTests.cpp`:

```cpp
TEST_CASE ("adoptRoot replaces the machine's tree (undo/redo coherence)",
           "[phrase-loop-machine][adopt]")
{
    FakeTransport t;
    std::int64_t nextId = 6000;
    PhraseLoopMachine machine (emptyRoot(), identityMap(), TapeId (1),
                               [&nextId] { return ConstituentId (nextId++); },
                               t);

    // Commit one phrase, then simulate the caller undoing back to empty by
    // adopting the original empty root again.
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (0));
    auto committed = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (8));
    REQUIRE (committed.newRoot->children().size() == 1);

    machine.adoptRoot (emptyRoot());                 // caller's undo result
    CHECK (machine.root()->children().empty());

    // A fresh Record→Stop now promotes onto the adopted (empty) root, producing
    // exactly one phrase — not two (which would happen if the machine had kept
    // the stale post-commit root).
    machine.dispatch ({ CaptureCommand::Verb::Record }, Rational (0));
    auto again = machine.dispatch ({ CaptureCommand::Verb::Stop }, Rational (8));
    REQUIRE (again.newRoot->children().size() == 1);
}

TEST_CASE ("adoptRoot rejects a null root", "[phrase-loop-machine][adopt]")
{
    FakeTransport t;
    auto machine = makeMachine (t);
    CHECK_THROWS_AS (machine.adoptRoot (nullptr), std::invalid_argument);
}
```

- [ ] **Step 2: Run, verify PASS (Task 3 implemented `adoptRoot`)**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R adopt`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/PhraseLoopMachineTests.cpp
git commit -m "test: PhraseLoopMachine adoptRoot keeps tree honest across undo/redo"
```

---

## Task 9 — Wire `MainComponent`'s bottom-bar Record path through the machine (GUI, operator-verified)

**Files:**
- Modify: `app/MainComponent.h` (add the `PhraseLoopMachine` member + an `OttoTransportControl` adapter member; anchors: the existing `captureSession_`, `undoStack_`, `ottoHost_`, `transportBarHost_` members)
- Modify: `app/MainComponent.cpp` — anchors: `onArmToggle` (~8375), `onMarkIn` (~8385), `onMarkOut` (~8402), the bottom-bar button wiring (~5983–5985: `armButton_`/`markInButton_`/`markOutButton_`), the ctor member init (~4176), and `onUndo`/`onRedo` (~8534–8585) to call `machine.adoptRoot(undoStack_.current())`.

This is the **only GUI-touching task** and is **operator-verified, not unit-tested** (repo convention — the agent cannot keep the GUI alive from CLI). The goal: route the existing bottom-bar Record gesture through `PhraseLoopMachine` so GUI, MIDI (future), and footswitch (future) share one code path, and so Record-while-stopped starts OTTO via the existing `ottoHost_->play()`.

Two integration shapes are acceptable; pick the **minimal** one:

- **(Preferred) Consolidate Arm/Mark-In/Mark-Out into a single Record toggle** (spec §8.3 "implementation may consolidate"). Replace the three bottom-bar buttons' behavior with one Record button: first press = `dispatch(Record)`, second press = `dispatch(Stop)`. This matches the single-pedal model the state machine *is* (spec §8.4) and is the most professional/elegant path (`[[feedback_default_to_professional_elegant]]`). Keep the buttons' Component objects if removing them ripples too far; just re-point `onClick`.
- **(Fallback) Keep the three buttons** but make `onMarkIn` call `dispatch(Record)` and `onMarkOut` call `dispatch(Stop)`, leaving `onArmToggle` as the marking-arm gesture. Lower-risk if the bottom-bar layout/test (`MainComponentPluginEditorTests`) asserts the three buttons exist.

> Decision rule: grep `MainComponentPluginEditorTests` and `bash/test-s7.sh` for `markInButton`/`markOutButton`/`armButton` references **before** choosing. If a test or operator script names those buttons, take the Fallback (re-point internals, keep buttons) to avoid breaking the lifecycle harness; otherwise take the Preferred consolidation. Record the choice in the commit message.

- [ ] **Step 1: Add the transport adapter + machine members to `MainComponent.h`**

Add, near the other engine members (after `ottoHost_` / `transportBarHost_` so the adapter can hold a reference to `*ottoHost_`):

```cpp
    /// Adapter exposing OTTO's transport to the JUCE-free PhraseLoopMachine
    /// (spec §9). Forwards play/stop/isPlaying to the embedded OttoHost — the
    /// only transport source (IDA invents none). Lives here so `core` never
    /// sees OTTO. Constructed after ottoHost_ is created.
    struct OttoTransportControl : ida::ITransportControl
    {
        explicit OttoTransportControl (ida::OttoHost& host) : host_ (host) {}
        bool isPlaying() const noexcept override { return host_.snapshotPlayhead().isPlaying; }
        void play() override { host_.play(); }
        void stop() override { host_.stop(); }
        ida::OttoHost& host_;
    };

    std::unique_ptr<OttoTransportControl> ottoTransport_;
    std::unique_ptr<ida::PhraseLoopMachine> machine_;
```

> `PhraseLoopMachine` is heap-held (`unique_ptr`) because it must be constructed *after* `ottoHost_` and the initial root exist — it cannot go in the member-initializer list cleanly given the construction order. Include `ida/PhraseLoopMachine.h` and `ida/CaptureCommand.h` in `MainComponent.h` (or forward-declare + include in the .cpp if header bloat matters; the adapter struct needs the full `ITransportControl` definition, so include `CaptureCommand.h`).

- [ ] **Step 2: Construct the adapter + machine after `ottoHost_` is prepared**

In the ctor body, after `ottoHost_ = std::make_unique<ida::OttoHost>();` and after the initial root is available (the blank-slate root from Slice 3, or `undoStack_.current()`), add:

```cpp
    ottoTransport_ = std::make_unique<OttoTransportControl> (*ottoHost_);
    machine_ = std::make_unique<ida::PhraseLoopMachine> (
        undoStack_.current(),
        demo_.sessionToLmc,                 // identity map (Slice-3 blank session supplies this)
        focusedTape_,
        [this] { return ConstituentId (nextConstituentId_++); },
        *ottoTransport_);
```

> Use the **same** `sessionToLmc` the existing `onMarkOut`/`promote` call uses (`demo_.sessionToLmc`) so behavior is identical to today. After Slice 3 retires the demo, this becomes the blank session's identity map — keep the field name the blank-session builder exposes.

- [ ] **Step 3: Re-point the Record/Stop entry points through the machine**

Replace the *bodies* of the bottom-bar capture handlers so they dispatch into `machine_` and push the outcome. Keep `playheadValueToLmc` as the LMC source (until a real Lmc clock lands), keep `announceCapture`, keep the `UndoStack` push at the call site:

```cpp
void MainComponent::dispatchCapture (ida::CaptureCommand::Verb verb)
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    const auto outcome = machine_->dispatch (
        ida::CaptureCommand { verb, ida::CaptureCommandSource::Gui }, t);

    if (outcome.committed)
    {
        undoStack_.push (outcome.newRoot, outcome.undoLabel, *outcome.restorePoint);
        if (outcome.promotionResult.has_value())
            announceCapture (
                CaptureRegion { outcome.restorePoint->pendingTape,
                                outcome.restorePoint->pendingIn, t },
                *outcome.promotionResult);
        refreshPerformance();
        refreshPreparation();
    }

    refreshCaptureControls();
    refreshDiagnostics();
}
```

Then, for the **Preferred** consolidation, wire a single Record toggle (`onClick` → `dispatchCapture(machine_->isMarking() ? Verb::Stop : Verb::Record)`); for the **Fallback**, set `onMarkIn` → `dispatchCapture(Verb::Record)` and `onMarkOut` → `dispatchCapture(Verb::Stop)`. Add `dispatchCapture` to `MainComponent.h`.

- [ ] **Step 4: Keep undo/redo coherent — adopt the stack's root after undo/redo**

In `onUndo` and `onRedo`, after `undoStack_.undo()` / `undoStack_.redo()`, add:

```cpp
    machine_->adoptRoot (undoStack_.current());
```

so the machine acts on the authoritative tree on the next Record (prevents promoting onto a stale root). Leave the existing `captureSession_` restore logic in place **only if** the old `captureSession_` member is still used elsewhere; if the machine fully owns the capture session now, the old `onUndo`/`onRedo` `captureSession_.*` restore calls become dead and must be removed (no dead code — CLAUDE.md). Grep `captureSession_` across `MainComponent.cpp`; if the machine subsumes it, delete the member and its now-dead call sites in the same task. **Surface this decision in the commit message.**

- [ ] **Step 5: Clean build, then launch for operator verification**

The agent cannot eyeball the GUI; build clean and launch, then hand the operator a terse numbered protocol (`[[feedback_clean_builds_only_for_testing]]`, `[[feedback_can_launch_app]]`):

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
open "build/app/IDA_artefacts/Release/IDA.app"
```

Operator protocol (numbered, point-by-point):
1. App boots to a blank slate (Slice 3) — no demo phrases.
2. Add a channel + pick an input (Slice 4) — it records.
3. Press Record while transport is **stopped** → transport starts (OTTO plays) and capture opens.
4. Perform, then press Record again (or Stop) → a phrase appears on the Output Mixer and plays back.
5. With the playhead **inside** that phrase, Record→Stop again → a second layered loop is added to the same phrase (both play).
6. Undo → the last creation reverses; Redo → it returns.

- [ ] **Step 6: Commit (after the operator confirms, or commit the wiring and note "awaiting operator eyes-on")**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: route bottom-bar Record/Stop through PhraseLoopMachine + OTTO transport"
```

> Per `[[feedback_update_continue_md_every_session]]`, if the operator has not yet eyeballed it when the session ends, note the pending verification in `continue.md`. The headless engine (Tasks 1–8) is fully verified regardless; only the GUI re-wiring needs operator eyes.

---

## Self-review

- **Spec coverage.** §8 combined states 0–6 → Task 1 (table) + Task 3 (coinit 6, rest 2) + Task 5 (state 3). §8.1 transitions: stopped→start (Task 6), running+outside⇒coinit (Task 3), running+inside⇒new loop (Task 5). §8.2 stop semantics (phrase end = stop, loop 0 length = phrase length, layered loop clamps to phrase end) → Task 7. §8.3 maps onto `promote()` — the machine is a thin orchestration; every commit goes through `promotion::promote()` (Tasks 3/5/7). §8.4 source-agnostic command layer → Task 2 (shape) + Task 4 (identical-state proof). §9 + §15.1 OTTO transport via injected port, `MainComponent` adapter forwards to `ottoHost_->play()` → Task 6 + Task 9. §10 undo: every committed gesture returns `undoLabel` + `CaptureRestorePoint`; the caller pushes one labeled entry (Task 9), and `adoptRoot` keeps the machine honest across undo/redo (Task 8). ✓
- **Per-phrase, not global.** No per-phrase enum array is stored; `activeState()` derives the active target's state from the live tree + `CaptureSession`; `promote()` only ever touches the phrase under the playhead, leaving all others untouched (Task 5 asserts the first phrase's identity survives). Unlimited phrases/loops follow from the immutable Constituent tree. ✓
- **Source-agnosticism is structural, not incidental.** `dispatch` never reads `command.source`; Task 4 proves GUI/MIDI/footswitch yield byte-identical trees and will fail the instant a source-branch is added (CLAUDE.md rule 9 — tests verify intent). ✓
- **Pure-`core` discipline / RT-safety.** All new files are JUCE-free `core`; the transport coupling is an abstract port injected from `app`. Nothing runs on the audio thread — the machine orchestrates message-thread `CaptureSession` + pure `promote()`. ✓
- **No `Rational::fromDouble`.** The machine takes `Rational` LMC times; the caller converts via the existing `playheadValueToLmc` (ticks → `Rational`). ✓
- **No dead code, no deferral.** Task 9 explicitly removes the old `captureSession_` call sites if the machine subsumes them, rather than leaving shims. No `TODO`/stub left. The one genuinely-staged item (live MIDI/footswitch *binding*) is spec §14 out-of-scope and is honored *now* by the source-agnostic layer existing — no code is deferred, only a future input source bound to the existing commands. ✓
- **Builds green + ordering.** Tasks 1–2 are header-only (compile fast). Task 3 adds the `.cpp` to `core/CMakeLists.txt`. Tasks 4–8 are test-only appends (no new build wiring). Task 9 is the GUI re-wire (clean build + operator launch). Every step ends in PASS + commit; the subagent pushes to `origin/master`. ✓
- **Risk the roadmap under-specified.** The roadmap's Slice 5 entry says "wire Record-while-stopped → `OttoHost` play via the existing `TransportBarHost::playPauseClicked()` path." `playPauseClicked()` *toggles* (it stops if playing). The machine must NOT toggle — Record-while-stopped must unconditionally **play**, never stop a running transport. This plan therefore calls `OttoHost::play()` directly (via the `OttoTransportControl` adapter), not `TransportBarHost::playPauseClicked()`. Flagged so the implementer does not wire the toggle by mistake.
