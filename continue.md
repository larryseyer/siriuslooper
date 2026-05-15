# Session Continuation — 2026-05-15 (evening)

## Top-of-page summary

The kickoff design discussion that the prior session scheduled — the
**track / input UX** — converged on a clean separation between the
**performer-facing UI vocabulary** (the user's words, mostly Reaper-
derived) and the **data-model vocabulary** (the white paper's words,
unchanged). Two important UI-layer names were named on purpose this
session: **Track** (= a UI strip representing one input) and **Pill**
(= the visual representation of a Phrase Constituent — name and look
derived from OTTO, which Sirius will ship bundled inside as the
rhythm device / click track). Neither term enters `core/`.

With vocabulary settled, the session shipped the **data-model
groundwork** the kickoff brief asked for: an `InputKind` enum, an
`InputDescriptor` struct, and a `TapeId` retrofit on `CaptureRegion`
and `CaptureSession::markIn`. UI mockups deferred to the next session
by design.

**219 tests pass (up from 213); 4031 assertions (up from 3983); zero
compiler warnings from sources we control.** Build is clean from
`rm -rf build` through `cmake --build build`.

**Next session opens with the UI design work** — the actual Pill
placement on a song timeline, the track-strip visual grammar, the
per-track-vs-group arm question, and where Loop Constituents appear
in the UI relative to the Pill that contains them. Detailed kickoff
brief at **"Suggested First Move Next Session §0"** below.

This session's plan is preserved at
`/Users/larryseyer/.claude/plans/read-continue-md-and-the-delightful-torvalds.md`
and is the authoritative reference for the vocabulary decisions, the
Pill visual contract, and the storage-cost addendum.

## Vocabulary, settled

These three words now have stable agreed meanings. They are
**conversation + UI vocabulary**, not white-paper terms. The white
paper is treated as solid; this layer sits on top of it.

| Spoken / UI word | What it actually is in code |
|---|---|
| **Track** | A `Tape` plus its `InputDescriptor` (one row in the input topology). UI-layer label for an input strip; **no `Track` type in `core/`**. |
| **Pill** | The visual representation of a `Constituent` carrying `PhraseMetadata` (a Phrase, per white paper §8). UI-layer label only; the data type stays `Constituent`. Name + look come from OTTO (`/Users/larryseyer/AudioDevelopment/OTTO`); in Sirius the *shape* carries over, the *content* expands to the full Phrase Constituent. |
| **Region** | Reaper word the user may use casually; closest match in Sirius is a Phrase Constituent (i.e. a Pill). |

### Pill visual contract (recorded so the data model does not foreclose it)

A Pill is a horizontal capsule on a song timeline arranged in rows
similar to Reaper tracks (but rows are not "tracks" in the data sense).
Each Pill surfaces:

- **Top-left** — loops within this Phrase (count or compact list).
  Backed by §7.2 (a Phrase Constituent contains Loops).
- **Top-right** — phrase loop on/off. Backed by §10 repetition rules,
  collapsed glanceably to a toggle.
- **Bottom-left** — leading (entrance) phrase. Backed by §8.5
  grammatical links and §8.7 (entrance is part of the phrase).
- **Bottom-right** — exit phrase. Backed by §8.7.
- **Center, vertically and horizontally** — Pill name from
  `PhraseMetadata`.

Future UI question: §8.5 allows multiple grammatical links; four
corners imply one *primary* entrance and one *primary* exit. The
preparation-mode surface will need a way to expose the rest.

### Reserved future-commitment: OTTO bundled inside Sirius

OTTO will ship inside Sirius as the rhythm device / click track. The
Pill idiom carries over from OTTO directly. **Licensing is IDENTICAL
to OTTO's** — re-use the existing license verbatim; do not design a
new one. Saved as a project memory at
`.claude/projects/-Users-larryseyer-Sirius-Looper/memory/project_sirius_branding_and_otto.md`.

**Project shorthand in conversation is "Sirius"** (the on-disk name
`Sirius Looper` and the white-paper title stay as they are; this is
just spoken vocabulary).

## What shipped this session

| Commit | Subject |
|---|---|
| `e222208` | feat: InputKind + InputDescriptor; CaptureRegion + markIn carry TapeId |

### `InputKind` enum (new)

`core/include/sirius/InputKind.h` — seven values, one per category in
the white paper §6.2 enumeration of input sources: `Audio`, `Video`,
`Midi`, `Control`, `ParameterAutomation`, `Transport`, `System`. Header-
only. JUCE-free. Comment names the data-layer / structure-layer split
and pins down that `Tape<T>` does not know about `InputKind`.

### `InputDescriptor` struct (new)

`core/include/sirius/InputDescriptor.h` — free-standing metadata that
pairs a `TapeId` with `InputKind`, a `std::string displayName`, and
`std::optional<int> channelOrPortIndex` (intentionally optional —
`Transport` and `System` tapes have no channel or port concept).

The descriptor honors white paper §7.2: `Tape<T>` stays heavy,
immutable data and does not know about descriptors; the descriptor
points *at* a tape by id. A session-level descriptor registry can come
later when there is a UI to populate it.

### `CaptureRegion` retrofit + `CaptureSession::markIn` signature

`core/include/sirius/CaptureSession.h` and `core/src/CaptureSession.cpp`:

- `CaptureRegion` is now `{ TapeId tape, Rational inLmcSeconds,
  Rational outLmcSeconds }`. The original `{ in, out }` shipped without
  a tape identity — the bug I planted last session, now closed.
- `CaptureSession::markIn` grew a `TapeId` parameter:
  `bool markIn (Rational t, TapeId tape)`. `AwaitingOut` now pins both
  `pendingIn_` *and* `pendingTape_`; both are cleared together on
  `disarm` / `cancel` / a successful `markOut`.
- `markOut` signature is unchanged. It stamps the **last** pinned tape
  into the returned region — so a performer who marks an in on track 2,
  changes their mind, and re-marks on track 5 before committing,
  produces a region carrying track 5.
- New accessor `pendingTape()` mirrors `pendingIn()`.

### Test coverage

- `tests/InputDescriptorTests.cpp` (new, 5 test cases): construction
  + equality of fields, optional channel absent for Transport / System,
  parameter-automation / control / video round-trip, unicode-in-
  displayName preserved.
- `tests/CaptureSessionTests.cpp` updated: every existing test now
  passes a `TapeId` into `markIn`; a *new* case
  `"markOut stamps the tape that was pinned by the *last* markIn"`
  pins down the switch-tracks-before-committing semantics; the second-
  `markIn` test grew two sections covering same-tape and different-
  tape replacement.

### `MainComponent` integration (documented placeholder)

`app/MainComponent.cpp:onMarkIn` passes `sirius::TapeId{0}` as a
documented placeholder until the track UI lands and the bottom bar can
identify which input the gesture targets. The comment parallels the
existing playhead-as-LMC-stand-in pattern — a real value flows through
once the surrounding subsystem is wired.

## Conceptual moments worth preserving

These came up in conversation this session and matter for next time.

### 1. The "track" word is the one Reaper term Sirius rejects

Whitepaper Appendix E.2 explicitly carves out "track" as a trap word:
Reaper's track conflates four jobs Sirius deliberately splits —
recording destination (→ **tape**), content container (→
**Constituents**), FX chain (→ each Constituent's `effect_chain`),
automation (→ **parameter tapes**). The user explicitly elected to
keep "track" as a UI-only word for performer comfort while keeping
the four-way split intact in code. Both halves of the decision are
important.

### 2. "Tape" ≠ "Reaper Track"

Mid-session the user asked, "When you say 'tape', is that the same
thing as what I am calling a Reaper Track?" The right answer is no —
a tape is one of the four jobs a Reaper Track does (the recording-
destination job), and even on that one job tapes behave differently
(always recording, never armed, append-only, immutable). The other
three jobs live elsewhere in Sirius. When the user says "track" in
conversation, the data behind it is **one Tape + one InputDescriptor**;
the Constituents that play *from* that tape live separately.

### 3. Storage cost is not a new problem

Mid plan-approval the user paused to ask whether timestamping every
event makes files explode. The answer (now in the plan file under
"Storage cost") is that `TapeEvent` is the **API** shape, not the
**on-disk** format. Audio tapes will use codec-native files
(WAV/CAF/FLAC) with a tape header carrying `start_lmc` + `sample_rate`;
per-sample timestamps are *computed* on query, not stored. Storage
volume is the same order of magnitude as any DAW (~0.52 GB/hour/
channel uncompressed PCM; ~0.26 GB/hour/channel FLAC; §6.5 has the
arithmetic). Sparse-event tapes (MIDI / control / automation /
transport / system) *do* carry per-event timestamps because events
are irregular, but their volume is kilobytes per hour. The on-disk
tape format is **not yet built** — current implementation is RAM-
only `std::vector<Event>` — and is the right thing to commit to when
the V2 §7.8 session-as-directory refactor lands (currently deferred
behind the macOS Load-dialog TCC bug).

## Current test / build state

**219 tests pass, 4031 assertions** (prior baseline: 213 / 3983).
Zero compiler warnings from any source we control. Clean builds
throughout, per the user-memory rule about incremental builds being
unreliable in this project.

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                            # 219/219
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

## Milestone status (changes from the prior session)

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | unchanged: done |
| M2 — real-time foundation, membrane, ASRC | unchanged: headless half done; operator owes device wiring + loopback calibration + in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **expanded**: input-topology metadata (InputKind + InputDescriptor) shipped; CaptureRegion + markIn now carry TapeId |
| M4 — persistence + capability tiers + overload protection | unchanged: done within current single-file scope; §7.8 directory format still deferred |
| M5 — plugin hosting + parameter view | unchanged |
| M6 — video | unchanged |
| M7 — full UI | unchanged in code; **enabled** in data model — track-strip UI work is now unblocked from the data side |
| M8 — ensemble | unchanged |

## The standalone app today

Unchanged visually from the prior session. Four tabs (Performance /
Preparation / Plugins / Video) with the bottom control bar:

```
[ Arm | Mark In | Mark Out ] [ Undo | Redo ] [ ============= playhead ============= ] [ time ]
```

The only functional difference is invisible: regions produced by
`Mark Out` now silently carry `TapeId{0}` as a placeholder. The
Preparation-tab diagnostics block does not yet render the tape id;
that is part of the next session's UI work.

## Suggested first move next session

### 0. KICKOFF — UI design: track strip + Pill placement (user-requested)

**The data model is ready; this is now the UI session.** Specific
questions to walk through, in approximately this order:

1. **Track-strip visual layout.** Horizontal rows on a song timeline
   à la Reaper. Each row represents one Tape + InputDescriptor.
   What's on the strip? The user has previously implied: kind icon /
   color, display name, level meter for audio, frame preview for
   video, blinking dot for armed-and-capturing, retroactive-ring
   depth indicator. Mockup-comparison time.

2. **Heterogeneous-type visual grammar.** Audio, video, MIDI,
   control, parameter automation, transport, system tapes share one
   row idiom but need distinct visuals — color band, kind icon, and
   maybe row height. The Pill placement above the strip should be
   the same across kinds.

3. **Where Pills live.** On the song timeline, anchored to a row
   (which means a tape; but a Pill is a Phrase Constituent that may
   contain content from *multiple* tapes — so anchoring is a UI
   choice, not a data fact). Open question: do Pills span rows
   visually, or live on a "primary" row?

4. **Per-track vs group arm.** Today `CaptureSession` is monolithic
   and `MainComponent` passes `TapeId{0}` as a placeholder. The next
   step *might* be a per-tape `CaptureSession` map + group-arm
   gesture, *or* it might be a track selector that the global session
   pulls its TapeId from. Both are viable; the UI mockup will force
   a choice.

5. **Where Loop Constituents appear** when a CaptureRegion is
   promoted. Inbox? On the originating row? On a focused phrase?
   White paper does not pin this down; see Open Questions.

**Recommended deliverable shape:** three or four ASCII / image
mockups, side-by-side, of one tab (probably Preparation expanded
into a timeline view) showing the same content under different
layout choices. User picks a direction; only then does JUCE code
start. The point is the layout decision, not the code.

### 1. Once the UI direction is picked

The promotion of `CaptureRegion` → Loop Constituent (a `todo.md`
entry from the prior session) is now **partially unblocked**: a
region knows its `TapeId`, which is what a Loop's `TapeReference`
needs. Promotion is gated only on the UI choosing where the new
Loop attaches. That's another reason this is a UI-first session.

### 2. macOS Load-dialog TCC bug (still blocked)

Unchanged from the prior session. See `todo.md`:
*"2026-05-15 — Load dialog still cannot select `.sirius.json` on
macOS"*. Three concrete next steps remain on the table; the
Developer-ID + entitlements path is the most likely fix; the
`.png`-vs-`.json` asymmetry is the quickest diagnostic if you want
to investigate before adding signing.

### 3. Session-as-directory refactor (V2 §7.8)

Still gated on the Load-dialog bug being resolved first — same code
path. Also the natural moment to commit to the on-disk tape format
(see *"Storage cost"* discussion above).

## Open questions (carry-forward)

- **Track-strip UI** — promoted to §0 above. All sub-questions
  (per-track arm gestures, kind-distinct visuals, Pill anchoring,
  level meter inclusion) live there.
- **Where promoted Loop Constituents attach** — the user has not
  said. Likely answered together with the track-strip design.
- **Performer-side role-fillable phrase UX** — engine shipped last
  session (`RoleResolver`); the runtime UX (footswitch ordering,
  pad assignment) remains genuinely novel. Likely lives on / near
  the track strip once it exists.
- **M6 video format strategy** — unchanged.
- **M8 transport choice** — unchanged.
- **Multiple grammatical links per Pill** — the four-corner contract
  shows one entrance + one exit; §8.5 allows N of each. Preparation
  mode needs a way to expose the rest. Open.

## Key decisions made this session

| Decision | Rationale |
|----------|-----------|
| "Track" is UI-only; no `Track` type in `core/` | Whitepaper Appendix E.2 rejects the word in the data model because Reaper's track conflates four jobs Sirius keeps split. UI-only keeps the performer's vocabulary while honoring the split. |
| Data model first, no UI this session | Sequencing recommendation from the prior continue.md §0 — smallest unknown surface, lets UI work be informed by real types. |
| `CaptureSession` stays global; per-tape deferred to M2 | Per-tape capture gestures only make sense with a track UI to drive them. The data model required to make per-tape possible later is just `CaptureRegion` carrying `TapeId` — which we added. |
| New tape payload types (Audio / MIDI / Control / Transport / System) deferred | They're real DSP commitments gated on M2 (audio path) and on per-protocol design. `InputKind` + `InputDescriptor` is sufficient metadata to drive next session's UI design. |
| `InputDescriptor` is free-standing; `Tape<T>` unchanged | Honors white paper §7.2: tapes are heavy data, descriptors are light metadata. Tape<T> does not know about descriptors. |
| `markIn` grows `TapeId` param; `AwaitingOut` pins both pendingIn and pendingTape | The tape identity is part of the gesture from the moment it's made. `markOut` stamps the *last* pinned tape, which matches the switch-tracks-before-committing performer scenario. |
| `MainComponent` uses `TapeId{0}` as documented placeholder | Parallels the existing playhead-as-LMC-stand-in pattern. Real TapeIds flow through once track UI lands. |
| "Pill" reserved as the UI word for Phrase Constituent; OTTO 4-corner contract codified | OTTO ships bundled inside Sirius (confirmed); reusing the visual idiom buys familiarity at zero cost. Content expands beyond OTTO's MIDI patterns to the full Phrase Constituent. |
| Storage strategy discussion — answered, deferred | `TapeEvent` is the API, not on-disk. Audio uses codec files with computed timestamps; sparse-event tapes carry per-event timestamps. Real commitment lands with the V2 §7.8 directory refactor. |
| Project shorthand "Sirius"; licensing IDENTICAL to OTTO's | User explicit. Saved as a project memory. |

## Commands to restore working state next session

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests
# expect: 219 / 219 pass, 4031 assertions
```

Authoritative references for the next session:

- This file (`continue.md`).
- The session plan:
  `/Users/larryseyer/.claude/plans/read-continue-md-and-the-delightful-torvalds.md`.
- `todo.md` — open deferrals, Load-dialog bug, session-as-directory
  format, region-promotion plan.
- `Sirius Looper Whitepaper V2.md` §6 (tapes), §8 (phrases),
  §14 (the performer's instrument), Appendix E (Reaper terminology
  map).
- Project memory:
  `.claude/projects/-Users-larryseyer-Sirius-Looper/memory/project_sirius_branding_and_otto.md`.
