# Slice 4 — Explicit channel creation + 1:1 auto-tape + recording-iff-assigned + CaptureSession terminology split — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make IDA's Input Mixer start with zero channels and grow by an explicit **Add Channel** gesture that picks an unused physical input pair (capped at the device's input-pair count, per-channel mono/stereo source toggle), auto-mints a hidden 1:1 stereo tape, and assigns the channel to it — at which point that tape begins recording **because** an input is assigned. Removing a channel STOPS its tape but never deletes it (retained as archive, not an orphan). Remove the input strip's tape-destination picker (tape is implicit). Finish unpinning `TapeId{1}` in `InputMixer`/`MixerGraph`. Split `CaptureSession` terminology so `Armed`/`AwaitingOut` are *marking* sub-states, not tape-recording states.

**Architecture:** The LMC always runs; a tape records **iff ≥1 input is assigned** (resource-aware capture, spec §2). This is already the structural behavior of `InputMixer::renderInputGraph` — `tapeTouched_[slot]` is reset every audio block and set only by `accumulateIntoTape`, which runs for a channel only when `channel.tapeMode != NoTape` **and** its main-out targets a tape slot; `ITapeSink::deliverTapeBlock` is called only for touched slots, and `TapeRecordWriter` opens a tape's append-only stream lazily on first delivery and never deletes it. So "remove the last channel feeding a tape ⇒ that slot is never touched ⇒ no further writes, file retained" is automatic and RT-safe (the on/off transition is the message-thread `removeChannel`/`setChannelTapeMode` mutation, bracketed by `removeAudioCallback`/`addAudioCallback`; the audio thread only ever reads). Slice 4's engine work is therefore to **remove the looper-floor enforcement** that currently forbids the last channel from leaving tape (`canDisarmChannelRecording` gate in `setChannelTapeMode`), **unpin `TapeId{1}`** so a removed/empty tape topology is legal, and **split `CaptureSession` terminology**. The UI work is Add Channel + 1:1 auto-tape + picker removal. Default topology is 1:1 (one channel ↔ one hidden stereo tape); the engine must NOT preclude N:1 submix (spec §6) — `setChannelMainOutToTape(ChannelId, TapeId)` already supports it, so Slice 4 only avoids *hardcoding* 1:1 into the engine.

**Tech Stack:** C++/JUCE. `core` (JUCE-free; `CaptureSession`), `engine` (`InputMixer`, `MixerGraph`, `TapePoolMirror`), `audio` (`TapeRecordWriter`), `app` (`MainComponent` + the inline `InputMixerPane`). Catch2 (`IdaTests`, flat `tests/`, registered in `tests/CMakeLists.txt`). CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §5, §2, §3, §6.

**Dependencies:**
- **Slice 1 (assumed done):** `TapePool` empty-allowed, `primary()` → `std::optional<TapeId>`, no `TapeId{1}` pin in the pool; all `primary()` callers guard `nullopt`.
- **Slice 2 (assumed done):** `IdaProject` unit + project-scoped tape paths. **This slice does NOT touch `MainComponent::tapesDirectory()` / `addTape` path naming** — it reuses whatever Slice 2 left in place. (At the time this plan was authored the working tree still had flat `…/IDA/tapes` and a demo boot; the implementer must rebase onto the post-Slice-3 tree, where `tapesDirectory()`/`tapeFile()` resolve under the project folder and boot is blank.)
- **Slice 3 (assumed done):** blank-slate boot (zero channels at start, demo retired). `rebuildInputStrips()` must already be reachable with `inputStripChannelIds_` empty. Slice 4 changes `rebuildInputStrips()` from "one strip per physical pair" to "only user-created channels."
- **RT-safety:** `docs/RT_SAFETY_CONTRACT.md`. The record on/off flip is a message-thread map mutation, bracketed by the existing `removeAudioCallback`/`addAudioCallback` pattern (see `rebuildInputStrips`, `onDestinationChosen`). No allocation/lock on the audio thread.
- **Out of scope (not precluded):** advanced N:1 / reassign UI lives in the Tapes tab (spec §6, Slice 7+). Per-channel auto-tape minting here is 1:1 only; the engine N:1 capability stays intact.

---

## Live-code anchors (verified, do not re-guess)

- `core/include/ida/CaptureSession.h` — `enum class CaptureState { Disarmed, Armed, AwaitingOut }` (lines 25–30); class doc lines 11–24 say "the tape is always running" / "the tape is being captured" (line 21) — both wrong under §3. Accessors `isArmed()`/`isCapturing()` (57–61), `arm`/`disarm`/`markIn`/`markOut`/`cancel` (69–95).
- `tests/CaptureSessionTests.cpp` — drives every transition by `CaptureState::Armed`/`AwaitingOut`/`Disarmed`.
- `engine/include/ida/InputMixer.h:97–101` — `removeTape` doc says "Returns false for an unknown id or the primary tape (`TapeId{1}` — the permanent default)."
- `engine/src/InputMixer.cpp`:
  - ctor line 63: `tapeTerminals_.push_back ({ 1, graph_.terminalNode (MixerTerminal::Tape) });` — pins tape id 1 to the graph's permanent primary terminal.
  - `removeTape` line 787: `if (id == TapeId { 1 }) return false;` — the explicit pin.
  - `setChannelTapeMode` lines 355–378 — floor enforcement lines 364–366 (`canDisarmChannelRecording`).
  - `canDisarmChannelRecording` lines 380–397 — the floor predicate.
  - `renderInputGraph` tape gating: `tapeTouched_` reset line 1151; set in `accumulateIntoTape` line 1132; per-channel guard `if (channel.tapeMode != TapeMode::NoTape)` lines 1209, 1298; delivery `if (tapeTouched_[i])` line 1413.
  - `removeChannel` (≈ line 1419+ in header order; impl earlier) tears the channel's graph node out entirely → no tape contribution after removal.
- `engine/src/MixerGraph.cpp:107–122` — `removeTerminal` line 110: `if (node == terminals_.front().id) return false; // primary is permanent`. The Tape terminal is `terminals_.front()` (seeded in the `MixerGraph` ctor).
- `app/MainComponent.cpp`:
  - `InputMixerPane` class — inline at line 453. `struct StripInfo { juce::String name; bool stereo; }` line 526. Blank-area menu `showBlankAreaMenu` lines 1594–1604 (`onAddBus`/`onAddFxReturn`/`onAddTape`/`onAddFileInput`). Long-press timer `kLongPressMs`/`startTimer` (≈1083–1100), `showBlankAreaMenu` dispatch line 1527. Strip context menu `showToggleMenu` lines 1536–1588 (the "Record to tape" item 1549–1556). Destination picker `showDestinationMenu` 1609–1624; `setDestinations` 873.
  - `rebuildInputStrips()` line 7921 — the `registerStrip` lambda 7936–7958 (looper-invariant comment 7942–7953, `setChannelTapeMode(chId, CommitToTape)` 7953); per-pair loop 7960–7992; the `audioDeviceManager_.removeAudioCallback`/`addAudioCallback` bracket (7927 / 8075).
  - `refreshInputDestinations()` line 7734 — builds the tape/bus picker choices (tapes 7744–7746) and per-strip current dest 7766–7801.
  - capture/tape wiring 4255–4320 (`TapeRecordWriter` ctor 4263, `TapeColoringSink` 4276, `setTapeSink` 4281, pool seed + `mirrorTapePool` 4292–4319).
  - `inputPairs_` populated in the Input-Mixer-tab ctor block 4557–4575 (device input count 4558–4562).
  - `onDestinationChosen` wiring 4720–4739; `onToggleChannelRecording` wiring 4933–4958; `onAddTape` wiring 4764; `addNextTape` 8222; `addTape` (`tapeCount` cap, `tapePool_.add`, `inputMixer_->addTape`, bracket) just below.
- Test fake to reuse: `tests/InputMixerTests.cpp:399` `struct RecordingTapeSink : ida::ITapeSink` (records `deliverTapeBlock` calls; test-only, allocates — fine for tests).

---

## Task 1 — Remove looper-floor enforcement (last channel may leave tape)

The spec replaces `[[project_looper_at_least_one_tape_invariant]]`: tapes record iff assigned; zero channels/zero tapes is the legal empty state. The `canDisarmChannelRecording` gate in `setChannelTapeMode` enforces the old floor and must go. Keep `canDisarmChannelRecording` *itself* only if a caller still needs it — there is none besides the gate and its own test, so remove the method too (zero dead code, per house rule).

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (remove `canDisarmChannelRecording` decl ~237 + its doc ~230–237; fix `setChannelTapeMode` doc ~222–228 which references the floor)
- Modify: `engine/src/InputMixer.cpp` (`setChannelTapeMode` 355–378; delete `canDisarmChannelRecording` 380–397)
- Modify: `tests/InputMixerTests.cpp` (rewrite the floor cases to the new contract)

- [ ] **Step 1: Rewrite the failing tests in `tests/InputMixerTests.cpp`**

Replace the floor case at line 1466 (`"InputMixer::canDisarmChannelRecording — true with multiple armed; false at floor; false on unknown"`) with the new contract: any channel can leave tape, including the last one.

```cpp
TEST_CASE ("setChannelTapeMode: the last tape-bearing channel may leave tape (no looper floor)",
           "[input-mixer][tape-mode]")
{
    ida::InputMixer mixer;
    const auto a = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    const auto b = mixer.addChannel (ida::InputId { 1 }, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));
    REQUIRE (mixer.setChannelTapeMode (b, ida::TapeMode::CommitToTape));

    // Disarm both, including the last one — previously a floor violation.
    CHECK (mixer.setChannelTapeMode (b, ida::TapeMode::NoTape));
    CHECK (mixer.setChannelTapeMode (a, ida::TapeMode::NoTape));   // last one — now allowed
    CHECK (mixer.channelTapeMode (a) == ida::TapeMode::NoTape);
    CHECK (mixer.channelTapeMode (b) == ida::TapeMode::NoTape);

    // Unknown id is still a no-op failure.
    CHECK_FALSE (mixer.setChannelTapeMode (ida::ChannelId { 9999 }, ida::TapeMode::NoTape));
}
```

Also grep `tests/InputMixerTests.cpp` for any other `canDisarmChannelRecording` references (the case above had several CHECKs) and delete them — the symbol is being removed.

- [ ] **Step 2: Run the tests, verify they FAIL**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R input-mixer`
Expected: FAIL — `canDisarmChannelRecording` still exists (compile error in the deleted-symbol references is the point; if the old test case is fully replaced the failure is a link/compile error on the removed symbol, or the new case fails because the floor still blocks the last disarm).

- [ ] **Step 3: Remove the floor from `setChannelTapeMode` (minimal impl)**

In `engine/src/InputMixer.cpp`, delete the floor block (lines 364–366):

```cpp
bool InputMixer::setChannelTapeMode (ChannelId id, TapeMode mode)
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return false;

    it->second.tapeMode = mode;

    // For NonDestructive channels, ensure the params partial file exists as
    // soon as the mode is set. Touching here (message thread, set-once) avoids
    // any RT-safety deviation that would result from doing filesystem I/O on the
    // audio thread inside processBuffer.
    if (mode == TapeMode::NonDestructive && tapeWriter_ != nullptr)
        tapeWriter_->touchParamsPartial (id);

    return true;
}
```

Then delete `canDisarmChannelRecording` (lines 380–397) entirely.

- [ ] **Step 4: Remove the declaration + fix the doc in `InputMixer.h`**

Delete the `canDisarmChannelRecording` declaration (line 237) and its doc block (≈230–237). Rewrite the `setChannelTapeMode` doc (≈222–228) to drop the floor-violation case:

```cpp
    /// Sets a channel's tape-routing mode. Returns `true` on success
    /// (including the idempotent same-mode → same-mode case). Returns
    /// `false` only for an unknown `ChannelId`. There is NO looper floor:
    /// the last tape-bearing channel may leave tape — a tape records iff
    /// at least one input is assigned to it (spec §2), and zero is legal.
    bool setChannelTapeMode (ChannelId, TapeMode);
```

- [ ] **Step 5: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R input-mixer`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: remove looper floor — last tape-bearing channel may leave tape (record iff assigned)"
```

---

## Task 2 — Unpin `TapeId{1}`: removable primary, removable Tape terminal

Today the Tape terminal is the graph's permanent `terminals_.front()` and `TapeId{1}` is welded to it (`InputMixer` ctor line 63; `removeTape` line 787; `MixerGraph::removeTerminal` line 110). For an honest 1:1-per-channel pool, any tape — including the first minted — must be removable, and the empty-pool state (no tapes registered in the mixer) must be legal. The fix: the Tape terminal stops being special; `removeTape`/`removeTerminal` no longer refuse `TapeId{1}`/the front terminal. A node whose tape destination is removed falls back to the graph's primary terminal (already the `removeTerminal` policy) — but with no tape minted, a freshly-added channel must default to `NoTape` (Task 3 handles assignment explicitly), so a fallback target's existence is not relied upon for *recording*.

**Scope guard:** `MixerGraph::removeTerminal` refusing `terminals_.front()` protects the HardwareOutput terminal too (whichever is front). Verify the seed order in the `MixerGraph` ctor before relaxing — if Tape is front, relaxing the front-refusal would also let HardwareOutput be removed, which nothing does but is undesirable. **Preferred minimal change:** keep `MixerGraph::removeTerminal` refusing the front terminal (HardwareOutput safety), and instead have `InputMixer` *not* treat its first tape terminal as the graph front. Concretely: register the first pooled tape via `graph_.addTerminal(MixerTerminal::Tape)` (a fresh, removable terminal) rather than reusing `graph_.terminalNode(MixerTerminal::Tape)` (the permanent front). The permanent front Tape terminal stays as the fallback sink only.

**Files:**
- Modify: `engine/src/InputMixer.cpp` (ctor 50–64; `removeTape` 785–796)
- Modify: `engine/include/ida/InputMixer.h` (`removeTape` doc 97–101; the `tapeTerminals_` comment ≈459 "[0] = primary (TapeId 1); >= 1")
- Modify: `tests/InputMixerTests.cpp` (add removable-tape coverage)
- Verify only (no change expected): `engine/src/MixerGraph.cpp` ctor seed order

- [ ] **Step 1: Write the failing test in `tests/InputMixerTests.cpp`**

```cpp
TEST_CASE ("InputMixer: every pooled tape is removable, including the first (no permanent primary tape)",
           "[input-mixer][tape][unpin]")
{
    ida::InputMixer mixer;
    // A fresh mixer registers no removable tapes by itself; mirror a pool of two.
    REQUIRE (mixer.addTape (ida::TapeId { 1 }));
    REQUIRE (mixer.addTape (ida::TapeId { 2 }));
    REQUIRE (mixer.hasTape (ida::TapeId { 1 }));
    REQUIRE (mixer.hasTape (ida::TapeId { 2 }));

    // The first tape is NOT permanent — it can be removed.
    CHECK (mixer.removeTape (ida::TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (ida::TapeId { 1 }));
    CHECK (mixer.removeTape (ida::TapeId { 2 }));
    CHECK_FALSE (mixer.hasTape (ida::TapeId { 2 }));

    // Removing an unknown id still fails.
    CHECK_FALSE (mixer.removeTape (ida::TapeId { 999 }));
}
```

Also adjust any existing case that asserts `addTape(TapeId{1})` returns false / that `removeTape(TapeId{1})` returns false (the old "primary is permanent" expectation). Grep: `grep -n "TapeId { 1 }\|TapeId{1}\|primary is permanent" tests/InputMixerTests.cpp tests/TapePoolMirrorTests.cpp`.

> **Cross-check `mirrorTapePool` semantics:** `engine/include/ida/TapePoolMirror.h` currently skips the primary tape on the assumption the mixer already pre-registered it (the ctor pin). With the pin removed, `mirrorTapePool` must register **every** pool tape (no skip). Read `TapePoolMirror.h` + `tests/TapePoolMirrorTests.cpp` and, if it special-cases the primary, fold the fix into this task with a matching test update ("mirrorTapePool registers every pool tape including the first").

- [ ] **Step 2: Run the test, verify it FAILS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "input-mixer|mirror"`
Expected: FAIL — `removeTape(TapeId{1})` returns false (the explicit pin); `addTape(TapeId{1})` may also collide with the ctor-pinned terminal.

- [ ] **Step 3: Stop pinning the first tape to the graph front (ctor)**

In `engine/src/InputMixer.cpp` ctor, delete line 63 so the mixer starts with **no** registered tape terminals (the empty-pool state):

```cpp
    buses_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    busNodeIds_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    tapeTerminals_.reserve (static_cast<std::size_t> (kMaxTapes));
    // No tape is pinned: the pool (and mirrorTapePool) registers every tape,
    // and a tape records iff a channel is assigned to it (spec §2). The graph's
    // permanent front Tape terminal remains only as the orphan-fallback sink.
```

Caller impact: `tests/InputMixerTests.cpp:544` (`"InputMixer constructs with Tape+HardwareOutput terminals and zero buses"`) asserts `tapeCount() == 1` for a fresh mixer. Update it to `tapeCount() == 0` (the graph still *has* its permanent terminals; the mixer just registers no pooled tape until one is minted). Grep for other `tapeCount()` assumptions.

- [ ] **Step 4: Relax `removeTape` (minimal impl)**

```cpp
bool InputMixer::removeTape (TapeId id)
{
    const MixerNodeId node = tapeNodeFor (id);
    if (! node.isValid()) return false;
    if (! graph_.removeTerminal (node)) return false; // graph refuses only its permanent front
    tapeTerminals_.erase (std::remove_if (tapeTerminals_.begin(), tapeTerminals_.end(),
                                          [id] (const TapeTerminal& t)
                                          { return t.tapeId == id.value(); }),
                          tapeTerminals_.end());
    return true;
}
```

Because Task 2 Step 3 registers pooled tapes via `addTerminal` (fresh terminals, never the front), `graph_.removeTerminal` will accept them. The permanent front Tape terminal is never in `tapeTerminals_`, so it is never offered to `removeTape` — HardwareOutput/Tape front safety is preserved without a `TapeId{1}` special-case.

- [ ] **Step 5: Fix docs + the `mirrorTapePool` skip (if present)**

`InputMixer.h`: rewrite `removeTape` doc (97–101) to drop "or the primary tape (`TapeId{1}` — the permanent default)"; rewrite the `tapeTerminals_` member comment (≈459) from "[0] = primary (TapeId 1); >= 1" to "pooled tapes registered via addTape; empty until the project mints one." If `TapePoolMirror.h` skipped the primary, remove the skip so it mirrors all tapes.

- [ ] **Step 6: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "input-mixer|mirror|tape"`
Expected: PASS. Then `cmake --build build --target IDA` — links green (the load path at `MainComponent.cpp:8839` snapshots non-primary tapes via `tape.id != tapePool_.primary()`; with `primary()` now `optional` per Slice 1 the comparison must already be guarded — verify, fix if a stray comparison remains).

- [ ] **Step 7: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp engine/include/ida/TapePoolMirror.h tests/InputMixerTests.cpp tests/TapePoolMirrorTests.cpp
git commit -m "feat: unpin TapeId{1} — every pooled tape is removable, empty tape topology is legal"
```

---

## Task 3 — `CaptureSession` terminology split (marking vs tape recording)

Spec §3: `CaptureSession`'s `Armed`/`AwaitingOut` are **marking** sub-states ("ready to mark" / "mark in progress"), not tape-recording states. Tape recording is assignment-gated (Tasks 1–2), not `CaptureSession`-gated. Rename to `ReadyToMark` / `Marking` and fix the misleading doc. This is a doc + rename change, **not** a behavior change to the marking flow — every transition stays identical.

**Decision (apply, override only with reason):** rename the enum values `Armed → ReadyToMark`, `AwaitingOut → Marking`, keep `Disarmed` (it is the genuine "marking stood down" state — the performer's marking gestures are off; the tape still records iff assigned, independently). Keep the accessor names `isArmed()`/`isCapturing()`? They now describe marking, not arming. Rename to `isMarkingReady()` (true in `ReadyToMark` or `Marking`) and `isMarking()` (true in `Marking`) to kill the "arm" collision the spec calls out. Update all call sites.

**Files:**
- Modify: `core/include/ida/CaptureSession.h` (enum 25–30; class doc 11–24; `isArmed`/`isCapturing` 57–61)
- Modify: `tests/CaptureSessionTests.cpp` (all `CaptureState::Armed`/`AwaitingOut` + `isArmed`/`isCapturing`)
- Modify: every other caller — grep first (see Step 1)

- [ ] **Step 1: Inventory the call sites**

Run: `grep -rn "CaptureState::Armed\|CaptureState::AwaitingOut\|\.isArmed()\|->isArmed()\|\.isCapturing()\|->isCapturing()" --include="*.cpp" --include="*.h" core engine audio app ui tests | grep -v build`
Record every hit; each is renamed in Step 4. (`MainComponent.cpp` consumes `CaptureSession` at the Record/Mark wiring — Slice 5 owns that flow, but the rename must still compile here. If a hit is purely Slice-5-owned bottom-bar wiring, rename the symbol but do not change behavior.)

- [ ] **Step 2: Rewrite `tests/CaptureSessionTests.cpp` to the new names**

Mechanical rename in the test file: `CaptureState::Armed → CaptureState::ReadyToMark`, `CaptureState::AwaitingOut → CaptureState::Marking`, `isArmed() → isMarkingReady()`, `isCapturing() → isMarking()`. Update the test-case titles that say "arm"/"Armed" to "ready to mark"/"ReadyToMark" so the intent reads true (e.g. `"arm transitions Disarmed to Armed"` → `"arm transitions Disarmed to ReadyToMark"`). The `arm()`/`disarm()` method names stay (the gesture is still "arm marking"); only the *state* names and the *boolean accessors* change.

Example (the fresh-session case):

```cpp
TEST_CASE ("a fresh session is Disarmed with no pending in-point or tape",
           "[capture-session]")
{
    const CaptureSession s;
    CHECK (s.state() == CaptureState::Disarmed);
    CHECK_FALSE (s.isMarkingReady());
    CHECK_FALSE (s.isMarking());
    CHECK_FALSE (s.pendingIn().has_value());
    CHECK_FALSE (s.pendingTape().has_value());
}
```

- [ ] **Step 3: Run the tests, verify they FAIL**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R capture-session`
Expected: FAIL (compile error — `ReadyToMark`/`Marking`/`isMarkingReady`/`isMarking` do not exist yet).

- [ ] **Step 4: Rename in `CaptureSession.h` + fix the doc (minimal impl)**

```cpp
/// A pure, JUCE-free state machine for the **marking** gestures (arm /
/// mark-in / mark-out) the performer uses to carve structure (a phrase, a
/// loop) out of a tape. It does NOT control whether the tape records:
/// tape recording is *assignment-gated* (a tape records iff ≥1 input is
/// assigned to it — spec §2/§3), entirely independent of this session.
///
/// Three states:
/// - **Disarmed:** the performer's marking gestures are stood down. No
///   in/out marks land. Default on construction — nothing is marked by
///   surprise. (The tape may still be recording, if an input is assigned.)
/// - **ReadyToMark:** marking is live; no in-point is set yet.
/// - **Marking:** an in-point is set; the session awaits the out-point.
enum class CaptureState
{
    Disarmed,
    ReadyToMark,
    Marking
};
```

And the accessors:

```cpp
    /// True in ReadyToMark or Marking. Performer-facing "is marking live?"
    bool isMarkingReady() const noexcept { return state_ != CaptureState::Disarmed; }

    /// True in Marking. Performer-facing "is a mark in progress?"
    bool isMarking() const noexcept { return state_ == CaptureState::Marking; }
```

Update the member-default `state_ { CaptureState::Disarmed }` (unchanged value, name still valid) and every internal transition (`arm`/`markIn`/`markOut`/`cancel`/`disarm` bodies in `CaptureSession.cpp` if a `.cpp` exists — check `core/src/CaptureSession.cpp`; it may be header-only) so they use `ReadyToMark`/`Marking`. Fix the `markIn`/`markOut`/`arm`/`cancel` doc comments that say "Armed"/"AwaitingOut".

- [ ] **Step 5: Rename the remaining call sites from Step 1**

Apply the mechanical rename to every non-test hit (notably `app/MainComponent.cpp`). Build to flush out any miss.

- [ ] **Step 6: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R capture-session`
Expected: PASS.
Run: `cmake --build build --target IDA`
Expected: links green.

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/CaptureSession.h core/src/CaptureSession.cpp tests/CaptureSessionTests.cpp app/MainComponent.cpp
git commit -m "refactor: CaptureSession states are marking sub-states (ReadyToMark/Marking), not tape-recording"
```

---

## Task 4 — Headless proof: assignment ⇒ record-on, last-unassign ⇒ record-off + tape retained, RT-safe

This task adds the **engine-level TDD** that nails the spec's recording-iff-assigned contract end to end through `renderInputGraph` + a recording `ITapeSink`, proving (a) a channel assigned to a tape causes `deliverTapeBlock` for that tape, (b) removing the last channel feeding a tape stops further deliveries while the tape stays registered in the pool/mixer, and (c) the on/off flip is the message-thread mutation (the audio path is `noexcept` and allocation-free — already statically asserted at `tests/InputMixerTests.cpp:38`). No production code changes here if Tasks 1–2 are correct; this is the regression net the spec §13 demands.

**Files:**
- Create: `tests/InputMixerRecordingGatingTests.cpp`
- Modify: `tests/CMakeLists.txt` (register the new file)

- [ ] **Step 1: Write the failing test file `tests/InputMixerRecordingGatingTests.cpp`**

```cpp
// Spec §2/§3 (2026-05-30): a tape records IFF >=1 input is assigned to it.
// These tests drive renderInputGraph against a recording ITapeSink and prove
// the assignment-gated on/off behavior end to end — assignment => deliveries,
// last-unassign => no further deliveries, tape retained. The flip is a
// message-thread mutation; renderInputGraph itself is noexcept/alloc-free
// (asserted in InputMixerTests.cpp).
#include "ida/InputMixer.h"
#include "ida/ITapeSink.h"
#include "ida/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using ida::InputMixer;
using ida::TapeId;
using ida::TapeMode;
using ida::SignalType;
using ida::InputId;

namespace
{
// Test-only sink: counts deliveries per tape. NOT a production sink (it would
// allocate); fine for a message-thread-driven test that never runs a real
// audio device.
struct CountingTapeSink : ida::ITapeSink
{
    std::vector<std::int64_t> deliveredTapes;
    void deliverTapeBlock (TapeId tape, const float*, const float*, int) noexcept override
    {
        deliveredTapes.push_back (tape.value());
    }
    int countFor (std::int64_t id) const
    {
        int n = 0;
        for (auto t : deliveredTapes) if (t == id) ++n;
        return n;
    }
    void reset() { deliveredTapes.clear(); }
};
} // namespace

TEST_CASE ("renderInputGraph delivers to a tape only while a channel is assigned to it",
           "[input-mixer][recording-gating]")
{
    InputMixer mixer;
    CountingTapeSink sink;
    mixer.setTapeSink (&sink);

    REQUIRE (mixer.addTape (TapeId { 1 }));
    const auto ch = mixer.addChannel (InputId { 0 }, SignalType::Audio);
    mixer.setChannelInputSource (ch, /*L*/ 0, /*R*/ 1, /*stereo*/ true);

    // A non-silent device buffer so the strip produces signal.
    constexpr int n = 64;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };

    SECTION ("unassigned (NoTape) => no delivery")
    {
        // Default tape mode for a channel with no per-input default is NoTape.
        REQUIRE (mixer.channelTapeMode (ch) == TapeMode::NoTape);
        mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);
        CHECK (sink.countFor (1) == 0);
    }

    SECTION ("assigned => delivery to that tape")
    {
        REQUIRE (mixer.setChannelTapeMode (ch, TapeMode::CommitToTape));
        REQUIRE (mixer.setChannelMainOutToTape (ch, TapeId { 1 }));
        mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);
        CHECK (sink.countFor (1) == 1);
    }
}

TEST_CASE ("removing the last channel feeding a tape stops delivery; the tape stays registered",
           "[input-mixer][recording-gating][retain]")
{
    InputMixer mixer;
    CountingTapeSink sink;
    mixer.setTapeSink (&sink);

    REQUIRE (mixer.addTape (TapeId { 1 }));
    const auto ch = mixer.addChannel (InputId { 0 }, SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    REQUIRE (mixer.setChannelTapeMode (ch, TapeMode::CommitToTape));
    REQUIRE (mixer.setChannelMainOutToTape (ch, TapeId { 1 }));

    constexpr int n = 64;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };

    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);
    REQUIRE (sink.countFor (1) == 1);

    // Remove the only channel feeding tape 1 (the message-thread "unassign").
    mixer.removeChannel (ch);
    sink.reset();

    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);
    CHECK (sink.countFor (1) == 0);          // recording stopped
    CHECK (mixer.hasTape (TapeId { 1 }));    // tape retained, not deleted/orphaned
}
```

- [ ] **Step 2: Register the test in `tests/CMakeLists.txt`**

Add `InputMixerRecordingGatingTests.cpp` to the `add_executable(IdaTests …)` list (near `InputMixerTests.cpp`, line 61):

```cmake
    InputMixerTests.cpp
    InputMixerRecordingGatingTests.cpp
```

- [ ] **Step 3: Run the tests, verify they FAIL then PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R recording-gating`
Expected: with Tasks 1–2 landed, these should already PASS (the behavior exists). If a section FAILS, the failure is a real Task-1/2 gap — fix the engine, not the test. (If `addChannel`'s default mode is not `NoTape` for `InputId{0}` because a stray per-input default leaks, assert/adjust — `addChannel` line 115 defaults to `NoTape` unless `setInputDefaults` set otherwise, which this test does not.)

- [ ] **Step 4: Commit**

```bash
git add tests/InputMixerRecordingGatingTests.cpp tests/CMakeLists.txt
git commit -m "test: assignment-gated tape recording end-to-end through renderInputGraph (record-on/off + retain)"
```

---

## Task 5 — Add Channel UI + 1:1 auto-tape mint + assign; remove the tape-destination picker

The UI half (operator-verified). `rebuildInputStrips()` builds **only user-created channels**; a new **Add Channel** affordance in the blank-area menu picks an **unused** physical input pair (capped at the device's input-pair count) with a per-channel stereo/mono source choice; on confirm it mints a 1:1 hidden stereo tape (`addTape` path, Slice-2 naming), registers the channel bound to that input source, assigns the channel's main-out to the new tape, sets `CommitToTape` → recording begins. The strip's tape-destination picker is removed (tape is implicit); the existing per-strip "Record to tape" toggle and Split/Collapse stay.

> **GUI = operator-verified, never faked in unit tests** (repo rule). No Catch2 here; the headless contract is Task 4. Clean `rm -rf build` before the operator hand-off.

**Files:**
- Modify: `app/MainComponent.cpp`:
  - `InputMixerPane` (line 453): add `std::function<void()> onAddChannel;` near `onAddTape` (588); add it to `showBlankAreaMenu` (1594–1604); **remove** the per-strip destination picker — delete `onDestinationChosen` decl (584), `showDestinationMenu` (1609–1624), `destButtons_` (1772) and their `setDestinations`/`setBounds`/visibility plumbing (728, 763, 837, 873–882, 1191, 1250, 1289), and the call to `showDestinationMenu`. **Keep** `DestChoice`/`DestKind`/`StripDest` types — bus-row pickers (`onBusDestinationChosen`) and MON pickers still use them. The MON destination picker (`onMonDestinationChosen` 2048, 3492) and bus picker stay.
  - `rebuildInputStrips()` (7921): drop the `inputPairs_` per-pair loop (7960–7992); instead rebuild strips from a new `inputChannels_` model (the user-created channels: input pair + stereo flag + the channel's tape id). The file-input strip loop (7994–8020) stays.
  - The Input-Mixer-tab ctor block (4557–4575): stop pre-seeding `inputPairs_` from the device. Keep computing the device input-pair count for the **cap**.
  - Add `void MainComponent::addInputChannel (int devLeftCh, int devRightCh, bool stereo);` (new) that mints the tape + registers the channel, bracketed by `removeAudioCallback`/`addAudioCallback`.
  - Wire `inputMixerPane_->onAddChannel = [this] { showAddChannelPicker(); };` near 4764.
  - Remove the `onDestinationChosen` wiring (4720–4739).
- Modify: `app/MainComponent.h`: add the `inputChannels_` model + `addInputChannel`/`showAddChannelPicker` decls; the existing `inputPairs_`/`inputStripPair_` usage shrinks (see Step note).

- [ ] **Step 1: Remove the strip tape-destination picker from `InputMixerPane`**

Delete `onDestinationChosen`, `showDestinationMenu`, `destButtons_`, and `setDestinations`'s strip-picker half (the tape/bus/HW choices for *channels*). In `refreshInputDestinations()` (7734), delete the channel-strip `choices`/`perStrip` block (7744–7802) and its `setDestinations` call — **keep** the `setMonitorModes` (7807–7811) and `setChannelTapeModes` (7817–7823) pushes (the "Record to tape" toggle + dim overlay still need TapeMode). Keep the bus-row destination plumbing untouched. Rename the now-channel-free `refreshInputDestinations` to `refreshInputStripStates` if its remaining job is only MON + TapeMode (optional; if renamed, update the ~6 call sites — grep `refreshInputDestinations`).

> Why safe: the spec §5 says "no tape-destination picker on the default input strip — the tape is implicit." Advanced reassign lives in the Tapes tab (§6, out of scope). The strip context menu's "Record to tape" toggle (`onToggleChannelRecording`, 1549) is the on/off arm gesture and stays.

- [ ] **Step 2: Add an `inputChannels_` model + Add Channel affordance**

In `app/MainComponent.h`, add:

```cpp
    /// A user-created input channel: which device pair feeds it, its stereo
    /// flag, and the 1:1 hidden tape minted for it (spec §5). Replaces the
    /// device-driven inputPairs_ auto-seeding — channels are explicit now.
    struct InputChannelSlot { int devLeft; int devRight; bool stereo; ida::TapeId tape; };
    std::vector<InputChannelSlot> inputChannels_;

    void addInputChannel (int devLeft, int devRight, bool stereo);
    void showAddChannelPicker();
    int  deviceInputPairCount() const;        // the Add Channel cap
```

In the `InputMixerPane` (line 453, near `onAddTape` decl 588):

```cpp
    /// Blank-pane-area "Add channel" gesture (right-click / long-press on empty
    /// pane). MainComponent presents the unused-input-pair picker and mints the
    /// channel + its 1:1 tape (spec §5).
    std::function<void()>                              onAddChannel;
```

In `showBlankAreaMenu` (1594–1604), add as the first item:

```cpp
        menu.addItem ("Add channel\xe2\x80\xa6", [this] { if (onAddChannel) onAddChannel(); });
        menu.addSeparator();
```

(Keep "Add bus"/"Add FX return"/"Add tape"/"Add file input" — those advanced affordances remain available, but the *default* gesture the first-run walk uses is Add Channel.)

- [ ] **Step 3: Implement the cap + picker `showAddChannelPicker()`**

```cpp
int MainComponent::deviceInputPairCount() const
{
    int numInputs = 0;
    if (auto* dev = audioDeviceManager_.getCurrentAudioDevice())
        numInputs = dev->getActiveInputChannels().countNumberOfSetBits();
    if (numInputs < 1)
        numInputs = kDefaultStereoChannels;          // no device → one default stereo pair
    return (numInputs + 1) / 2;                       // pairs (odd leftover counts as one)
}

void MainComponent::showAddChannelPicker()
{
    const int pairCount = deviceInputPairCount();

    // Which device pairs are already in use by an existing channel.
    std::vector<bool> used (static_cast<std::size_t> (pairCount), false);
    for (const auto& slot : inputChannels_)
    {
        const int pair = slot.devLeft / 2;
        if (pair >= 0 && pair < pairCount) used[static_cast<std::size_t> (pair)] = true;
    }

    juce::PopupMenu menu;
    bool any = false;
    for (int p = 0; p < pairCount; ++p)
    {
        if (used[static_cast<std::size_t> (p)]) continue;       // cap: only unused pairs
        any = true;
        const int l = p * 2, r = p * 2 + 1;
        // Stereo (one strip) by default; the per-channel mono/stereo toggle is
        // the existing Split/Collapse strip gesture (RME model) post-create.
        menu.addItem ("In " + juce::String (l + 1) + "-" + juce::String (r + 1) + " (stereo)",
                      [this, l, r] { addInputChannel (l, r, /*stereo*/ true); });
        menu.addItem ("In " + juce::String (l + 1) + " (mono)",
                      [this, l] { addInputChannel (l, l, /*stereo*/ false); });
    }
    if (! any)
        menu.addItem ("All inputs in use", /*enabled*/ false, false, [] {});

    menu.showMenuAsync (juce::PopupMenu::Options{}.withMousePosition());
}
```

> Per-channel mono/stereo: the picker offers the initial choice; the existing per-strip **Split to two mono channels / Collapse to stereo** gesture (`onToggleStereoMono`, 1561) remains the post-create RME toggle, honoring `[[project_input_source_mono_stereo_rme]]`. (Note: under the explicit-channel model, the Split/Collapse handler `toggleInputPairStereo` at 8126 — which today flips an `inputPairs_` entry — must be re-pointed at the `inputChannels_` slot; fold that into Step 5.)

- [ ] **Step 4: Implement `addInputChannel` — mint the 1:1 tape + assign (record begins)**

```cpp
void MainComponent::addInputChannel (int devLeft, int devRight, bool stereo)
{
    if (inputMixer_->tapeCount() >= ida::InputMixer::kMaxTapes) return;   // capacity guard

    // 1:1 hidden tape for this channel (spec §5). Tapes are named as TAPES,
    // not after phrases. Slice 2 owns the on-disk path; addTape mirrors the
    // pool into the mixer + TAPECOLOR decorator (bracketed inside addTape).
    const auto tapeId = tapePool_.add ("Tape " + std::to_string (tapePool_.count() + 1));

    audioDeviceManager_.removeAudioCallback (audioCallback_.get());

    const bool tapeOk = inputMixer_->addTape (tapeId);
    jassert (tapeOk); juce::ignoreUnused (tapeOk);
    if (tapeColoringSink_ != nullptr)
    {
        tapeColoringSink_->addTape (tapeId);
        if (const auto* d = tapePool_.find (tapeId))
            tapeColoringSink_->setMode (tapeId, d->tapeColor);
    }

    const auto inputId = ida::InputId (devLeft);
    const auto chId    = inputMixer_->addChannel (inputId, ida::SignalType::Audio);
    inputMixer_->setChannelInputSource (chId, devLeft, stereo ? devRight : -1, stereo);
    // Assignment IS the arm (spec §2): route to this channel's own tape and
    // commit → recording begins the next audio block.
    inputMixer_->setChannelMainOutToTape (chId, tapeId);
    juce::ignoreUnused (inputMixer_->setChannelTapeMode (chId, ida::TapeMode::CommitToTape));

    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    inputChannels_.push_back ({ devLeft, stereo ? devRight : devLeft, stereo, tapeId });
    rebuildInputStrips();
    refreshTapesPane();          // the new tape appears in the Tapes-tab archive (Slice 7)

    // Spec §15.2 — Add Channel is undoable. Push a labeled UndoStack entry.
    // (Undo infra exists — ui/include/ida/UndoStack.h; see Self-review note.)
}
```

> **Undo note (spec §15.2 / §10):** Add/Remove Channel "default YES" for undoability. The `UndoStack` is tree-pointer-based for *Constituent* edits; a channel edit is mixer-graph state, not a Constituent. Honest scoping: wire a labeled "Add channel" entry **only if** `UndoStack` already carries a non-Constituent command lane usable here. If it does not (likely — it restores `CaptureRestorePoint`/tree roots), do **not** fake it: record the gap in `todo.md` (entry: "channel add/remove undo — needs a mixer-state command in UndoStack or a session-snapshot restore point; see spec §15.2") and proceed. Per `[[feedback_no_deferral_get_it_done]]`, if a minimal labeled-snapshot push is bounded, do it now; if it requires new UndoStack machinery, that is genuinely its own slice and belongs in `todo.md`. **Surface this to the operator in the hand-off.**

- [ ] **Step 5: Rewrite `rebuildInputStrips()` to build only user-created channels**

Replace the `inputPairs_` loop (7960–7992) with an `inputChannels_` loop; the `registerStrip` lambda stays but loses its looper-invariant `CommitToTape` default (assignment is now done in `addInputChannel`; on rebuild, re-assert the channel→tape route from the slot). Drop the obsolete looper-invariant comment (7942–7953). Re-point `toggleInputPairStereo` (8126) at `inputChannels_`. Keep the file-input strip loop. `inputStripPair_` can be retired (the per-strip device pair now lives in `inputChannels_`); if other code reads `inputStripPair_`, migrate those reads. Net effect: a fresh boot with empty `inputChannels_` builds zero hardware strips (plus any file-input strips), satisfying spec §4.1.

```cpp
    for (std::size_t i = 0; i < inputChannels_.size(); ++i)
    {
        const auto& slot = inputChannels_[i];
        const bool single = (slot.devLeft == slot.devRight);
        const juce::String name = slot.stereo
            ? (single ? "In " + juce::String (slot.devLeft + 1)
                      : "In " + juce::String (slot.devLeft + 1) + "-" + juce::String (slot.devRight + 1))
            : "In " + juce::String (slot.devLeft + 1);
        const auto inputId = InputId (slot.devLeft);
        const auto chId = inputMixer_->addChannel (inputId, SignalType::Audio);
        inputMixer_->setChannelInputSource (chId, slot.devLeft,
                                            slot.stereo ? slot.devRight : -1, slot.stereo);
        inputMixer_->setChannelMainOutToTape (chId, slot.tape);   // re-assert the 1:1 route
        juce::ignoreUnused (inputMixer_->setChannelTapeMode (chId, TapeMode::CommitToTape));
        inputStripChannelIds_.push_back (chId);
        inputStripInputIds_.push_back  (inputId);
        infos.push_back ({ name, slot.stereo });
    }
```

- [ ] **Step 6: Clean build + operator hand-off**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests && ctest --test-dir build
cmake --build build --target IDA
```

Then build + launch the app and give the operator these numbered steps:

1. Launch IDA → Input Mixer tab. **Expect zero hardware channel strips** (blank slate).
2. Right-click (desktop) / long-press (iOS) the empty Input Mixer area → menu shows **Add channel…** first.
3. Choose **Add channel… → In 1-2 (stereo)**. **Expect** one new strip "In 1-2" to appear, metering live; no tape-destination picker on the strip.
4. Open the **Tapes** tab. **Expect** one tape ("Tape 1") with its recording indicator lit (it is recording because the channel is assigned).
5. Back in Input Mixer, right-click the strip → **Record to tape** is checked. Uncheck it → the strip dims; re-check → un-dims. (Toggling off stops recording without removing the channel.)
6. Right-click the empty area → **Add channel…** again → **Expect** only **unused** pairs offered (In 1-2 absent). On a 2-in device, expect **All inputs in use** (the cap).
7. Right-click the strip → **Split to two mono channels** → **Expect** the stereo strip become two mono strips of the same source (RME toggle), still recording.
8. Remove the channel (via its context-menu remove, if present, or the Tapes tab) → **Expect** the strip gone, but the tape **still listed** in the Tapes tab (retained), recording indicator now **unlit**.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "feat: explicit Add Channel with 1:1 auto-tape; remove implicit per-pair strips + tape picker"
```

---

## Self-review

- **Spec coverage:**
  - §5 (Add Channel, pick unused input, capped, per-channel mono/stereo, 1:1 auto-tape, assign ⇒ record, remove ⇒ stop+retain, no strip tape picker) → Task 5 + Task 1/2 engine.
  - §2 (assignment IS the arm; no assigned-but-paused; record iff ≥1 input; retained not orphaned) → Tasks 1, 4, 5.
  - §3 (two orthogonal layers; `CaptureSession` terminology split; the misleading `:21` comment) → Task 3.
  - §6 (engine must NOT preclude N:1 submix) → Task 2 keeps `setChannelMainOutToTape(ChannelId, TapeId)` and per-tape terminals intact; 1:1 is a UI default, not an engine constraint. ✓
- **RT-safety:** the on/off flip is the message-thread `addChannel`/`removeChannel`/`setChannelTapeMode`/`setChannelMainOutToTape` mutation, bracketed by `removeAudioCallback`/`addAudioCallback` (Task 5). The audio path (`renderInputGraph`) only *reads* `tapeMode`/`tapeTouched_`; it stays `noexcept` and allocation-free (static_assert at `InputMixerTests.cpp:38`). No new audio-thread work added. ✓
- **TDD vs operator-verified:** engine logic is headless TDD (Tasks 1, 2, 4 with failing-test-first); GUI (Task 5) is operator-verified with numbered steps and a clean `rm -rf build`, never faked in unit tests. ✓
- **No dead code:** `canDisarmChannelRecording` removed (Task 1), not left orphaned; the strip tape-picker plumbing fully removed (Task 5) rather than hidden; `DestChoice`/`DestKind` retained because bus + MON pickers still use them. ✓
- **Decomposition / sequencing:** Tasks 1–3 are independent engine/core refactors (any order); Task 4 verifies the post-1/2 contract; Task 5 (UI) depends on 1 (no floor) + 2 (removable tapes). ✓
- **Unpin subtlety:** the real `TapeId{1}` pin is the *graph-front Tape terminal*, not just the `removeTape` guard — Task 2 registers pooled tapes via `addTerminal` (removable) and leaves the permanent front terminal as orphan-fallback only, preserving HardwareOutput-front safety. ✓
- **Cross-slice dependency the roadmap under-specified:** (1) **`mirrorTapePool` primary-skip** — the roadmap's Slice 1 relaxed the pool but the mirror helper likely still skips the primary on the now-removed ctor-pin assumption; Task 2 Step 5 folds in the fix. (2) **Channel add/remove undo (§15.2)** is "default YES," but `UndoStack` is Constituent-tree-shaped and has no obvious mixer-state lane — Task 5 Step 4 flags this as a genuine deferral candidate for `todo.md`/operator sign-off rather than faking an undo entry. (3) Task 5 retires `inputPairs_`/`inputStripPair_`; any reader of those members outside the touched functions must be migrated — grep before deleting. ✓
