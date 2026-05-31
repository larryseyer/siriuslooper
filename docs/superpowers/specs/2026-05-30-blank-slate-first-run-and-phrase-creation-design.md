# Blank-Slate First-Run + Phrase/Loop Creation — Design

Status: design complete (defaults applied to §15; awaiting operator sign-off → implementation plan)
Date: 2026-05-30
Canonical "why": `docs/IDA_Whitepaper_V10.md`
Supersedes the boot path established by `app/DemoSession.cpp`.

---

## 1. Motivation

IDA cannot be honestly tested today. It boots into a **pre-authored demo song**
(`buildDemoSession()` — a fixed intro/verse×3/outro arrangement with phrases
already wired to tapes 1–4). A tester therefore never walks the path a real
operator must walk: **first launch → set up a source → record → turn the
recording into a phrase → play it back.** That path does not exist in the UI.

This design retires the demo as the boot state and builds the real first-run
flow. The terminal success criterion is a single human-walkable sequence with
**no fixture standing in**:

> New Song → create a channel and pick its input → record a performance →
> a phrase appears on the Output Mixer and plays back → undo/redo works.

## 2. Core principle (resource-aware capture)

IDA's aim is unchanged: **capture every inspirational moment a performer has.**
This rule does not retreat from that — it only ensures the capture happens
**without wasting the user's resources.** The whitepaper's "the tape is the
always-running source of truth" is *scoped*, not weakened: the tape runs and
captures every moment **wherever a performer has actually routed a source**; it
does not burn disk where no source exists to be inspired by.

> **The clock (LMC) always runs — it is the honest timebase and never stops.
> A tape records if and only if at least one input is assigned to it. Period.**
> A tape with no assigned input does not run, and therefore consumes no disk.

Consequences:

- **Assignment is the arm.** There is **no** "assigned-but-paused" state. The
  performer's arm/disarm gesture *is* assigning/unassigning an input (under the
  1:1 default of §5, that is creating/removing a channel). This unifies the
  mandatory performer arm/disarm gesture with routing.
- **Retroactive capture is bounded by assignment time.** An assigned tape keeps
  the retroactive ring (RAM) and the on-disk lossless stream from the moment of
  assignment, so a phrase can still be marked a beat *after* it was played — but
  only back to when the input was assigned. Before that there is deliberately no
  data, because spending disk on un-assigned sources is the waste being
  eliminated.
- **"Complete archive" means complete relative to capture, not to wall-clock.**
  The Tapes tab (§7) shows each input's *complete underlying take* while it was
  assigned; phrases are non-destructive trimmed views into that take. The
  archive is complete for the assigned period(s), not for time the input did not
  exist.

This does not weaken the architecture: the LMC is still the only honest
timebase, the tape is still the source of truth for what was captured, and
phrases/loops are still sourced from tape. Only the *unconditional* "always
writing to disk" commitment is scoped to "writing while assigned."

### 2.1 PARAMOUNT invariant — tapes are irreplaceable; deletion requires deliberate consent

**This is the highest-priority safety rule in the entire design.**

- **Phrases are recreatable; tapes are not.** A phrase is a derived view and can
  be re-marked after the fact. A tape is the source recording — once deleted, the
  captured performance is **gone forever.** Tape deletion is the single most
  destructive action in IDA.
- **Never delete a tape without explicit, deliberate user consent.** The only
  path that destroys tapes is a **deliberate erase of an IDA project's
  recordings**, and it **must first present a clear, unambiguous warning** that
  the user is about to **DELETE an IDA project**, that the recordings **cannot be
  recovered**, and that phrases can be recreated but the underlying tapes cannot.
  On **cancel**, nothing changes. On **deliberate confirm**, delete that
  project's tapes (leaving the user in an empty project).
- This is the in-application expression of the global "never take an
  unauthorized destructive action" rule: the *app* must ask the *user* before
  erasing audio.
- **New Song never deletes.** It opens a *new* project (§2.2); the prior
  project's tapes remain assigned to, and stored with, that project. Erasing is
  always the separate, deliberate, warned act above.

### 2.2 Tapes are owned by an IDA project

**Tapes are assigned to an IDA project** — they are not global, free-floating
files. The IDA *project* is the top-level persistence unit that owns its
arrangement (song/set, phrases, channels) **and all of its tapes.** (This spec
uses "project" and "session" interchangeably for this unit.)

- **Tape storage is project-scoped on disk, and the project folder is the
  grouper.** The folder is named `yyyymmddhhmmss-<ida_project_name>` — the
  project's creation timestamp plus its name (sanitized to a filesystem-safe
  form) — and holds that project's tapes directly. Replaces the flat
  `…/IDA/tapes/` of the current build (commit 942ba5b's store path gains the
  project folder): `…/Application Support/IDA/yyyymmddhhmmss-<ida_project_name>/`.
- **Tape files inside are simply `tape_<x>`** (1-based index; FLAC container per
  `[[project_tape_disk_format_flac]]`). The grouper carries the identity, so the
  files stay minimal. The fully-qualified name is therefore folder + file —
  `yyyymmddhhmmss-<ida_project_name>.tape_<x>` — i.e. the timestamp/name live
  **once** on the folder rather than being repeated on every file.
- **Why timestamp the folder:** it disambiguates duplicate project names (two
  `Untitled` projects never collide), sorts by creation time, and is
  human-findable in the OS file browser.
- **The folder name is a stable, creation-stamped id; the display name is a
  separate field.** The folder is set once at creation
  (`yyyymmddhhmmss-<name-at-creation>`); the user-facing project name lives in
  project metadata. Renaming a project changes the display name, **not** the
  folder — avoiding folder churn and broken tape paths. (Rename UI is part of the
  staged project-management breadth; this decoupling is the rule when it lands.)
- **New Song = a new, empty project.** It does **not** delete the prior
  project's tapes — those stay assigned to and stored with that project.
  Switching projects switches which set of tapes is live.
- **Erasing recordings is a separate, deliberate, warned act** (§2.1) — deleting
  a project / clearing its tapes. It is the *only* path that destroys tapes.
- **No orphan tapes — ever.** Every tape on disk belongs to exactly one project
  and is visible in that project's Tapes tab; there is never a tape file with no
  owning project (the flat, orphaned `~/Library/IDA/tapes` of an earlier build is
  exactly the anti-pattern). Deleting a project deletes *all* of its tapes — none
  are left behind. A tape whose channel was removed is **not** an orphan: it stays
  owned by the project and visible as archive (§5), stops recording, and is
  removed only by the deliberate erase of §2.1 — never silently.
- Full project-management UI breadth (browse / open / rename / delete / save-as)
  is **staged**, but the **project-as-tape-owner relationship and project-scoped
  storage are foundational and in scope** — a tape cannot exist without a project
  to belong to. This is the structural reason §2.1 holds: an irreplaceable tape
  always lives inside a named project that persists until the user deliberately
  erases it.

## 3. Two orthogonal layers (terminology discipline)

The word "arm" has collided throughout this codebase. This spec fixes two
distinct layers and names them:

| Layer | What it is | Gated by | User-facing surface |
|---|---|---|---|
| **Tape recording** | Whether a tape is writing audio to disk | **Assignment** (≥1 input ⇒ recording) — automatic | Tapes tab recording indicator (§7) |
| **Phrase/loop capture** | Marking structure (a phrase, a loop) over a recording tape | The **Record** gesture + state machine (§8) | Bottom-bar Record/Mark, Output Mixer |

These are independent. A tape records continuously while assigned (building the
archive) **whether or not** the performer is currently marking a phrase. The
Record gesture carves structure from that always-recording-while-assigned
substrate; it does not start or stop the tape.

> Implementation note: `CaptureSession`'s `Armed` state is documented as "the
> tape is being captured" (`core/include/ida/CaptureSession.h:21`). Under this
> model that is wrong — tape capture is assignment-gated, not
> `CaptureSession`-gated. `Armed`/`AwaitingOut` are **marking** sub-states
> ("ready to mark" / "mark in progress"). Rename/redocument during
> implementation to remove the collision (candidate: `ReadyToMark` /
> `Marking`). This is a doc+semantics change, not a behavior change to the
> marking flow.

## 4. The first-run walk (the test path, normative)

1. **Launch / New Song → blank slate.** No demo, no phrases, no song, no tapes,
   no channels. Minimal default mixers only (no seeded RVB/DLY/FX returns/buses,
   per `[[project_minimal_default_mixers]]`). New Song opens a **new IDA project**
   (§2.2, §2.1) and never deletes the prior project's tapes.
2. **Create a channel.** The user invokes *Add Channel* in the Input Mixer and
   **picks which physical input feeds it.** Channels are **not** auto-created;
   they are only *capped by* the number of physical inputs. Zero channels at
   start.
3. **The channel records immediately.** Creating the channel auto-provisions a
   hidden tape (§5) and, because an input is now assigned, that tape begins
   recording at once. The user never sees or names a tape to make this happen.
4. **Perform and Record.** With a channel live, the user performs and uses the
   Record gesture (§8). Transport-stopped + Record starts the transport (§9) and
   co-initialises a new phrase and its first loop over the captured region.
   Stopping closes the phrase and loop.
5. **The phrase plays back.** The new phrase appears as an **Output Mixer phrase
   strip** and plays (the existing `refreshOutputMixerPhraseChannels` +
   `TapePrefetcher` path already does this once a phrase references a recorded
   tape region).
6. **Undo/redo.** Phrase and loop creation are undoable/redoable (§10).
7. **Find the archive.** The user can open the **Tapes tab** to find each
   input's complete take and reveal its file on disk (§7).

## 5. Channel creation + auto-tape provisioning (1:1 default)

- **Default topology is 1:1:** one channel ↔ one hidden tape. This is forced by
  the architecture — tapes are stereo (the stereo-only hard invariant) and each
  independent source needs its own stream so per-source loops can reference it.
- **Add Channel** flow:
  - Presents the physical input pairs not already in use (a channel owns its
    physical input source; `[[project_input_source_mono_stereo_rme]]` —
    stereo-internal strips with a per-channel mono/stereo source toggle).
  - On confirm: create the input strip bound to that source, mint a fresh
    `TapeId` (tape index `x`) whose on-disk file follows the §2.2 naming
    convention, assign the channel to it → recording begins. (The Tapes tab
    displays each tape alongside the input that feeds it.)
  - **Cap:** Add Channel offers nothing when all physical inputs are in use.
- **Removing a channel** unassigns its input. Under 1:1 that leaves its tape with
  zero inputs → the tape **stops recording**, but it is **retained**: still owned
  by the project and visible in the Tapes tab as archive — it is *not* an orphan
  (§2.2) and is *never* auto-deleted (§2.1). Space is reclaimed only by a
  deliberate, warned erase.
- **No tape-destination picker on the default input strip.** The tape is
  implicit. (This changes today's `refreshInputDestinations`, which puts the
  tape list in the strip's picker.) Advanced re-routing lives in the Tapes tab
  (§6).

## 6. Advanced routing (opt-in, Tapes tab) — preserved, not required

The operator's earlier "assign any channel to Tape 2 / submix several channels
onto one tape" model is **retained as an opt-in advanced surface**, not part of
the default flow:

- A channel may be **reassigned** to a different tape, or **multiple channels**
  may be assigned to one tape (an N:1 submix tape).
- The universal recording rule still governs: a tape records ⟺ ≥1 channel
  assigned; reassigning the last channel away from tape X stops X.
- This surface is **out of scope for the first-run test build** (§12) but the
  engine rule and data model must not preclude it.

## 7. Tapes tab = per-input session archive

- The Tapes tab is the user's window onto the **complete archive of the session
  from each input** — each tape's full underlying take, of which phrases are
  trimmed views. The user is never *required* to open it to record; it is where
  they go to *find* a complete capture. (This is what "eliminate the requirement
  to deal with tapes" means: never *forced*, always *available*.)
- Each tape line shows: the tape (its `tape_x` index and the input that feeds
  it), a **recording indicator** (lit ⟺ the tape has ≥1 assigned input), and a
  **Reveal-in-storage button** that opens the tape's file (named per §2.2) in the
  OS file browser.
- **Reveal:** `juce::File::revealToUser()` (cross-platform). Caveats to handle in
  implementation: iOS file reveal is limited (Files-app integration only); guard
  any `NSWorkspace`/`UIApplication` path for AUv3 extension-safety
  (`APPLICATION_EXTENSION_API_ONLY=YES`) — the standalone app is fine, the
  extension must no-op or route safely.

## 8. Phrase + Loop creation state machine (operator-authored, normative)

This is a **per-phrase state, not a global one.** An IDA arrangement holds an
**unlimited number of phrases**, and each phrase holds an **unlimited number of
loops** — so *each phrase instance* carries its own (phrase, loop) state below.
At rest most phrases sit in `exists`; the Record gesture drives the *active
target* phrase (and its current loop) through `being_created`, while every other
phrase stays in `exists` untouched.

A phrase and a loop each have a creation lifecycle; their product is the combined
state. `nil` marks an impossible pair (a loop cannot exist without a phrase to
contain it).

```
phrase: not_created (0) | exists (1) | being_created (2)
loop:   not_created (0) | exists (1) | being_created (2)

combined[phrase][loop]:
  not_created   -> { not_created: 0, exists: nil,         being_created: nil       }
  exists        -> { not_created: 1, exists: 2,           being_created: 3         }
  being_created -> { not_created: 4, exists: 5,           being_created: 6         }
```

| id | name |
|----|------|
| 0 | idle / empty |
| 1 | phrase ready, no loop yet |
| 2 | phrase with a finished loop |
| 3 | phrase ready, loop recording |
| 4 | phrase recording, no loop |
| 5 | phrase recording, loop already done |
| 6 | **phrase + first loop recording together (coinit)** |

`phrase_loop_coinit = 6` is the canonical "press Record on an empty looper":
a brand-new phrase and its first loop are born in the same gesture.

**Loop 0 tracks the phrase during definition.** While a phrase is being defined
(start point set, end point **not yet** set — the marking state of §3), **loop 0
— the phrase's first, implicit loop — is being recorded into it**, and loop 0
shares the phrase's bounds *exactly*: its start **is** the phrase start, and its
end follows the phrase end when that end is marked. Coinit (state 6) is precisely
this condition. Loops are **zero-indexed**; loop 0 is always created together
with its phrase, never as a separate gesture. Additional loops (loop 1, 2, …)
are the §8.1 "transport running, playhead inside an existing phrase" case — they
may start mid-phrase, but per §8.2 their end is still clamped to the phrase
boundary.

### 8.1 Record gesture transitions

The **Record** gesture is the single performer action. Its effect depends on
transport state and on whether the playhead sits inside an existing phrase —
which is exactly how the existing `promotion::promote()` already decides whether
to mint a new phrase or extend the current one.

- **Transport stopped + Record:** start the transport (§9), then proceed as
  "transport running."
- **Transport running + Record, playhead outside any phrase:** **coinit** →
  state 6 (new phrase + first loop recording together).
- **Transport running + Record, playhead inside an existing phrase:** create a
  **new loop** in that phrase → state 3 (phrase ready, loop recording). This is
  the overdub/layer case.

### 8.2 Stop semantics

When Stop is issued while a phrase and/or loop is being created:

- **Phrase being created:** the phrase end is marked **where Stop was issued.**
- **Loop being created:** the loop end is marked **where its parent phrase
  is** (the loop inherits the phrase's end, not the raw Stop point).

So in coinit (state 6), Stop sets phrase end = Stop point, and loop end = phrase
end = same point ⇒ the first loop length equals the phrase length. For a loop
added to an already-finished phrase (state 3), Stop clamps the loop end to the
phrase's existing end, so layered loops align to the phrase boundary.

### 8.3 Mapping onto existing engine

This state machine is a **formalisation of code that already exists**, not
greenfield:

- `core/include/ida/CaptureSession.h` — `markIn(t, tape)` / `markOut(t)` define
  the in/out window (its `Armed`/`AwaitingOut` are the marking sub-states, §3).
- `core/include/ida/Promotion.h` — `promote()` already mints a phrase wrapper
  when the playhead is outside any phrase, and a loop from the captured region;
  it is invoked at `MainComponent::onMarkOut()` (`app/MainComponent.cpp:8411`).
- The Record gesture wraps mark-in → mark-out → promote with the transport and
  playhead-position logic above. Implementation may consolidate the bottom-bar
  Arm / Mark-In / Mark-Out into a single Record toggle (UI detail, decided in the
  plan).

### 8.4 Future control surface: MIDI / single-pedal (Quantiloop Pro model)

IDA will eventually map **MIDI notes/CC** to control actions, adopting the
**Quantiloop Pro** model: a **single pedal** can create phrases, overdub (create
loops), start/stop the transport, and more — the classic one-switch looper
paradigm where one press does a *different* thing depending on the current state.
**MIDI binding itself is out of scope for this build (§14)**, but it imposes one
constraint honored *now*:

- **The §8 state machine *is* the single-pedal abstraction.** A one-pedal looper
  is a context-dependent Record/Stop command resolved against the combined state
  — exactly §8.1. The state machine built for the GUI is the same one a pedal
  will drive.
- **Expose Record / Stop / overdub / undo / … as abstract, source-agnostic
  commands**, not GUI-button handlers. The bottom-bar button, a MIDI note, a CC,
  and a footswitch must all dispatch the *same* command into the *same* state
  machine. Build that command layer now, so adding MIDI later is purely a new
  input source bound to existing commands — no rework of capture logic.
- The capture state machine must not know whether its input came from a finger or
  a foot (a design-for-isolation requirement). The precise
  press / long-press / double-tap → action vocabulary is defined when MIDI binding
  lands, modeled on Quantiloop Pro.

### 8.5 Phrase-trigger button bank (UI)

A persistent bank of **phrase-trigger buttons** gives every phrase a fixed,
MIDI-mappable launcher — the on-screen counterpart of §8.4's per-phrase pedal.

- **Layout & placement:** a single **horizontal row directly beneath the top-bar
  transport area** — `< 1 2 3 4 5 6 7 8 >`, eight numbered buttons flanked by
  left/right chevrons. The chevrons page through **banks of 8** (bank 1 = phrases
  1–8, bank 2 = phrases 9–16, …), so the unbounded phrase count (§8) is reachable
  eight at a time.
- **Permanent positional assignment.** Button position *p* in bank *b* is
  permanently the phrase at index `(b-1)*8 + p`. Creating phrase N (via the §8
  Record gesture) lights its button; the mapping is positional and stable, never
  hand-assigned.
- **Default numeric labels, renameable.** A button defaults to showing its phrase
  number (1, 2, 3, …); the context menu's **Rename** sets a custom phrase name
  shown on the button instead. The label is cosmetic — it does not change the
  positional phrase-mapping above (same decoupling as project name vs folder, §2.2).
- **Empty slots are inert.** If no phrase exists at a slot, the button renders
  **empty and does nothing** — these buttons *trigger* phrases, they do not
  *create* them (creation is the Record gesture of §8.1).
- **Press = drive that phrase's multi-mode state.** Pressing an active button (or
  firing its mapped MIDI note) advances *that phrase's* per-phrase state machine
  (§8) — play / stop / overdub as the phrase's current state dictates.
- **Traditional looper state colors.** A button is colored by its phrase's current
  state in the universal looper language — **empty** = unlit/dim, **recording** =
  red, **playing** = green, **overdub** = amber, **stopped (loaded)** = dim/off —
  making the per-phrase state (§8) glanceable. The colors resolve through IDA's
  canonical palette (`ui/include/ida/IdaPalette.h` /
  `docs/design/ida-colour-method.md`), never hardcoded; how state color reconciles
  with any per-phrase *identity* color from the colour method is settled with the
  palette during implementation.
- **What the looper verbs mean in IDA** (the colors map to these operations):
  - **rec** (red) = *define* a phrase and its loop — the coinit of §8 / the
    initial loop.
  - **play** (green) = play this phrase **with all of its loops** layered together
    (a phrase is a stack of simultaneous loops). **Implementation note:** this
    lifts a current limit — today the Output Mixer phrase strip prefetches only the
    phrase's *first* leaf-loop tape (a T0b known limit); "play all loops" requires
    the phrase channel to mix every loop the phrase owns.
  - **overdub** (amber) = **create a new loop for this phrase** — a distinct,
    layered Constituent, **not** audio summed onto an existing loop. IDA layers
    loops *non-destructively*; this is the §8.1 "playhead inside an existing
    phrase → new loop" case.
- **Right-click (desktop) / long-press (iOS) → one context menu.** A single
  context gesture opens the phrase button's menu — **Clear, Copy, Paste, Rename**
  (and similar phrase ops), plus **Assign MIDI…** (note + channel + port). Right-click
  and long-press are the *same* gesture: IDA ships on iOS, which has only
  long-press, so MIDI-assign and clear/copy/paste must share **one** menu rather
  than sit on two separate gestures
  (`[[feedback_ios_long_press_pairs_right_click]]`). These ops act on the phrase
  (a view) and are undoable (§10); **Clear** removes the phrase, **never its tape**
  (§2.1).
- Per §8.4, a button press dispatches the same source-agnostic command a finger,
  MIDI note, or footswitch would — the bank is one more input source, not a
  separate code path. The broader single-pedal control *vocabulary*
  (press / long-press / double-tap → action, CC mapping for transport) remains the
  future §8.4 work; **per-button MIDI assignment is part of this design.**

## 9. Transport coupling (OTTO)

IDA has **no engine-side transport** — OTTO is the transport source
(`[[project_otto_is_the_transport_source]]`; `app/TransportBarHost.h` mirrors
OTTO's play/stop via `IOttoTransportListener`). Therefore:

- "**Transport stopped + Record → start transport**" means **commanding OTTO to
  play**, then capturing.
- If OTTO is not yet wired/imported in a given build, the start-transport edge
  of the Record gesture is blocked on that integration. The spec records this
  coupling explicitly; the plan must either depend on OTTO transport control or
  define an interim IDA-local play state **only** if the operator approves
  inventing one (today, inventing an IDA transport is explicitly disallowed).

## 10. Undo/redo

**Undo/redo infrastructure already exists and is complete** — `UndoStack`
(`ui/include/ida/UndoStack.h`, `ui/src/UndoStack.cpp`): multi-level, labeled,
O(1) push via immutable-tree pointer swap, with `CaptureRestorePoint` restoration
for promotion entries. `MainComponent` already pushes capture promotions
(`app/MainComponent.cpp:8423`) and wires `onUndo`/`onRedo` (~8534–8585).

The operator's "MUST enable undo/redo for phrase and loop creation" is therefore
largely **already satisfied**. Work required:

- Ensure **every** new creation gesture (coinit, new-loop, and channel creation
  if we choose to make it undoable) pushes a labeled `UndoStack` entry.
- Confirm the combined-state transitions of §8 each map to exactly one undoable
  step with a human-readable label (e.g. "Create phrase", "Add loop").

## 11. Data model + invariant changes

- **`TapePool` ≥1 floor is relaxed.** Today it seeds exactly one tape and blocks
  removal that would empty the pool (`core/include/ida/TapePool.h:20,34,46`). The
  blank slate requires a **legally empty pool**. `primary()` must become
  optional (e.g. `std::optional<TapeId>`), and every caller (notably
  `refreshInputDestinations`, pool seeding in the `MainComponent` ctor) must
  guard the empty case. **Ripple risk — audit all `primary()` / `tapes()`
  callers.**
- **Looper invariant overturned.** `[[project_looper_at_least_one_tape_invariant]]`
  ("≥1 channel must record to ≥1 tape at all times, else it's a mixer") is
  replaced by: *tapes are armed/recording iff assigned; zero channels / zero
  tapes is the legal empty (New Song / first-run) state.* Update that memory and
  any floor-enforcement code.
- **Boot path.** `MainComponent` ctor `demo_(buildDemoSession())`
  (`app/MainComponent.cpp:4176`) and the `UndoStack` seed from `demo_.root`
  (4177) are replaced by a **blank session builder** (empty root Constituent,
  empty `TapePool`, zero channels). `buildDemoSession()` is removed from the boot
  path; if `IdaTests` fixtures depend on demo-like trees, they construct their
  own — verify before deleting (`app/DemoSession.{h,cpp}`).
- **Input strips.** `rebuildInputStrips()` (`app/MainComponent.cpp:7921`) changes
  from "build one strip per physical pair" to "build only the channels the user
  has created." Refines `[[project_minimal_default_mixers]]`.

## 12. Whitepaper clarifications (specify now, edit during implementation)

These edits **clarify scope; they do not retreat from the capture-every-moment
commitment** (§2). "Always-running" becomes "running wherever a source is
assigned" at these anchors in `docs/IDA_Whitepaper_V10.md` (edit as the first
implementation step, after this spec is approved):

- **Lines 27, 334, 428, 443, 592, 1977, 1999** — "always-running source of
  truth" → add the scoping clause: a tape runs while ≥1 input is assigned; the
  LMC, not the tape, is the unconditional always-on element.
- **Line 129** (comparison table) — "tape is always running" → "tape runs while
  its input is assigned."
- **Line 842** — retroactive grab ("the moment is already on the tape") → bound
  to assignment time.
- **Line 856** — the "gigabyte per hour per source" passage already motivates
  this; add that the primary disk-waste control is *not recording unassigned
  sources at all*, with lossless-on-disk as the secondary control.
- **Line 588** ("Tape topology is mixer topology") is reinforced, not changed.
- Add a short subsection (near §7.2 / Part VIII) stating the assignment-gated
  recording rule and the two-layer model of §3.

## 13. Testing strategy

- **Headless / TDD (engine):** the §8 state machine is pure logic and **must**
  be unit-tested in `IdaTests` (Catch2) — every combined-state transition,
  coinit, the two stop-semantics rules, and the playhead-inside-vs-outside
  branch. The assignment⇒recording rule and the empty-`TapePool` behavior are
  also headless-testable. Use TDD per the engine-is-testable rule.
- **Operator-verified (GUI):** the first-run walk of §4 (Add Channel, Record,
  phrase appears + plays, Reveal-in-storage) is GUI wiring — operator-verified,
  not unit-tested, per repo convention. Clean build (`rm -rf build`) before each
  operator hand-off.
- The §4 walk **is** the operator test protocol; it stays in lockstep with this
  flow.

## 14. Out of scope (this build)

- Advanced N:1 / submix routing UI in the Tapes tab (§6) — engine rule must allow
  it; UI is later.
- Full project-management UI breadth (browse / open / rename / delete /
  save-as). The project-as-tape-owner relationship, project-scoped storage, the
  naming convention, and the no-orphan guarantee are **foundational** (§2.2), not
  deferred — only the management-UI breadth is staged.
- Garbage-collection / compaction of tape regions not referenced by any phrase.
- Energy-arrangement scenes, multi-phrase song structure beyond the
  Record-driven coinit/new-loop transitions.
- The broader MIDI/CC + footswitch control *vocabulary* (the Quantiloop Pro
  single-pedal model, §8.4 — press / long-press / double-tap → action, CC for
  transport) is **future.** In scope now: the source-agnostic command layer, the
  on-screen phrase-button bank, and its per-button MIDI note/channel/port
  assignment via right-click/long-press (§8.5). The plan sequences these relative
  to the core create-a-phrase flow.

## 15. Decisions on the open questions (defaults applied — override if wrong)

1. **OTTO transport (§9) — RESOLVED, capability exists.** `TransportBarHost`
   already forwards play/stop to `OttoHost` (`playPauseClicked()` /
   `stopClicked()`, `app/TransportBarHost.h:42–44`). "Record-while-stopped → start
   transport" reuses that same OttoHost play command — no interim IDA transport
   needs inventing, and this edge is **not blocked.**
2. **Channel add/remove undoability (§10) — default YES.** Add/Remove Channel push
   labeled `UndoStack` entries, consistent with undo-is-first-class. Override if
   channel edits should sit outside the undo timeline.
3. **Project name at first launch — default `Untitled`, no prompt.** Boot straight
   into a usable project named `Untitled` (folder `yyyymmddhhmmss-Untitled` keeps
   it unique); the user renames later via project metadata (§2.2). Seamless — no
   naming dialog before the first tape. Override if you want a name prompt on first
   record.

*(Tape-lifecycle decisions resolved earlier — see §2.1/§2.2: tapes are
project-scoped, irreplaceable, never orphaned; New Song opens a new project and
never deletes; only a deliberate, warned erase destroys tapes; the project folder
is `yyyymmddhhmmss-<project>` (the grouper), tape files inside are `tape_<x>`.)*
