# Session Continuation — 2026-05-15 (late evening)

## Top-of-page summary

The kickoff UI design discussion the prior session scheduled was held,
converged on **Mockup A refined** — Pills live on their primary tape's
row, per-row arm is the primary gesture, multi-tape phrases extend a
membership outline into secondary rows — and the implementation
shipped in this same session. The Preparation tab now contains a real
timeline view (`TimelineView`) above the existing tree readout and
diagnostics row, the demo session was promoted so its three sections
are real Phrases (the verse demonstrates the multi-tape case), and the
bottom-bar Mark In gesture now stamps the **focused tape's** TapeId
instead of the `TapeId{0}` placeholder.

**226 tests pass (up from 219); 4071 assertions (up from 4031); zero
compiler warnings from sources we control.** Clean from
`rm -rf build` through `cmake --build build`.

**Next session opens with operator-side GUI verification** — launching
the `.app`, eyeballing the timeline, exercising the per-row arm
gesture, and deciding which of the smaller refinements (level meter,
ring-depth indicator, retro-active capture surface, Loop-Constituent
promotion gesture) to wire first. Detailed kickoff brief at
*"Suggested first move next session §0"* below.

The plan file from this session is the new authoritative reference
for the data model + UI architecture decisions made:
`/Users/larryseyer/.claude/plans/most-elegant-implementation-decisions-finalized.md`
(if no plan was written, the brief in *"Decisions made"* below is the
authoritative summary).

## What the user picked and why (the design conversation, condensed)

The question the user posed was sharp: *most loopers are individual
entities — should we design for the most-used scenario or for edge
cases?* The answer was unambiguous:

- The dominant Sirius user is **one performer, several inputs**. Multi-
  tape phrases happen, but they are the exception — not the structural
  center.
- Multi-tape capture is properly an **M8 (ensemble) concern**.
  Designing the primary metaphor around band recording pays rent on a
  feature that doesn't ship for two milestones.
- Reaper is elegant precisely *because* its primary metaphor (a track
  owns its regions) scales from solo to band without changing. Group-
  record-arm is an *escalation*, not a separate UI mode.

So **Mockup A** ("Pills-on-row, per-track arm") became the base, with
two refinements that took it from "good" to "elegant":

1. **Kill the inbox.** Promotion is immediate — onto the row where the
   region lives. An inbox only appears for deliberate capture-without-
   committing, hidden when empty.
2. **Group capture via chord-arms, not a mode.** Click multiple arms,
   both go red, Mark In/Out captures a Phrase across both. The
   "connector idiom" from vanilla A becomes a property of the Pill
   itself — a membership outline drawn on secondary rows.

## Vocabulary, unchanged from the prior session

| Spoken / UI word | What it actually is in code |
|---|---|
| **Track** | A `Tape` plus its `InputDescriptor`. UI-layer label for an input strip; **no `Track` type in `core/`**. |
| **Pill** | The visual representation of a Phrase Constituent (a Constituent with `PhraseMetadata`). UI-layer label only; the data type stays `Constituent`. Four-corner OTTO contract. |
| **Region** | Reaper word the user may use casually; closest match in Sirius is a Phrase Constituent (i.e. a Pill). |

## What shipped this session

| Commit | Subject |
|---|---|
| `a8eb298` | feat: TimelineView — per-row arm and on-row Pills (refined Mockup A) |

### `TimelineViewState` (new, JUCE-free)

`ui/include/sirius/TimelineViewState.h` + `ui/src/TimelineViewState.cpp`:

- `TrackStripState` — one row's state: `tapeId`, `InputKind`,
  `displayName`, `isArmed`, `isFocused`.
- `PillState` — one Pill's state: `id`, `name`, `startLmcSeconds`,
  `endLmcSeconds`, `primaryTape` (mode-by-count), `memberTapes`
  (insertion order), `loopCount`, `phraseLoopActive` (driven by
  cardinality::Once vs everything else), `entranceName`, `exitName`.
- `TimelineViewState` — `startLmcSeconds`, `endLmcSeconds`, `rows`,
  `pills`.
- `selectTimelineView(root, sessionToLmc, inputs, armedTapes, focused)`
  — the selector. Walks the tree the same way `PerformanceViewState`
  does (accumulated `ParentToLmc`), emits one PillState per Phrase
  Constituent, builds a TapeAggregation per Pill to pick primary tape
  and enumerate members. Inputs without content still produce rows.

### `TimelineView` (new, JUCE leaf component)

`ui/include/sirius/TimelineView.h` + `ui/src/TimelineView.cpp`:

- Pure renderer over `TimelineViewState`. Three callbacks at the host
  level: `onArmClicked(TapeId)`, `onFocusClicked(TapeId)`.
- Layout: 22 px ruler band at top (2-second ticks), 200 px strip
  column on the left, content area on the right, 52 px per row.
- Strip head: kind-colour band on the left edge, kind glyph
  (AUD/VID/MID/...), display name, Arm button (rounded rect,
  hit-tested in `mouseDown`). Focused row has a brighter inner edge.
- Pill rendering: rounded-corner capsule on the primary row, OTTO
  4-corner atoms (loop count, "loop on" / "once", entrance, exit) +
  the phrase name centred. Membership outline drawn on each secondary
  row — empty rect with no atoms, so the secondary row's other
  content remains readable.
- Click handling: arm hit-box dispatches `onArmClicked`; any other
  click on the strip dispatches `onFocusClicked`.

### `DemoSession` updates

`app/DemoSession.cpp` + `.h`:

- Intro, verse, and outro are now real **Phrases** (carry
  `PhraseMetadata`). Intro and outro are single-tape leaf Phrases —
  the dominant case. Verse is a multi-tape Phrase (rhythm tape +
  lead tape) — the future-proofed case the membership outline
  exercises.
- `DemoSession` now carries a `std::vector<InputDescriptor> inputs`
  for the four demo tapes (intro pad on TapeId 100, verse rhythm on
  200, verse lead on 300, outro pad on 400). All four are
  InputKind::Audio for now; non-audio kinds wait on M5/M6/M8.

### `MainComponent` wiring

`app/MainComponent.cpp` + `.h`:

- New private state: `std::vector<InputDescriptor> inputs_`,
  `std::unordered_set<std::int64_t> armedTapeIds_`, `TapeId focusedTape_`
  initialised from the first demo descriptor.
- New methods: `refreshTimeline()`, `toggleArm(TapeId)`,
  `setFocused(TapeId)`, `armedTapesVec()`.
- `PreparationPane` (nested in MainComponent.cpp) now owns the
  TimelineView. Layout: action row up top, tree readout above the
  timeline, timeline gets the dominant share (~60 %) of the
  remaining vertical space, diagnostics row at the bottom.
- `onArmToggle` (bottom-bar) now toggles the focused tape's arm via
  `toggleArm`. `onMarkIn` passes `focusedTape_` to
  `CaptureSession::markIn` — the `TapeId{0}` placeholder is gone.
- `armedTapeIds_` is the per-tape arm map. Group capture (chord
  arms) is **visual-only today** — only the focused tape's id
  stamps the region; the data model already supports multi-tape via
  per-Pill `memberTapes`, but `CaptureSession` is still monolithic
  (M8 work to grow it). This is honest deferred plumbing, not a
  painted-in box.

### Test coverage

`tests/TimelineViewStateTests.cpp` — seven test cases:

1. Empty session yields a timeline with the session's LMC bounds and
   no Pills.
2. Each InputDescriptor produces a row even when nothing references
   its tape.
3. A single-tape Phrase becomes one Pill anchored to its lone tape
   (the dominant case).
4. A multi-tape Phrase picks the most-referenced tape as primary and
   exposes the full membership in insertion order.
5. Armed and focused tapes propagate to their rows.
6. A Phrase's span is its parent-conceptual position through the
   session tempo map, not its tape slice (pins down the
   conceptual-vs-tape distinction so a refactor can't silently
   substitute one for the other).
7. A Phrase with `cardinality::Once` reports `phraseLoopActive ==
   false` (pins the ↻ toggle's mapping to cardinality).

## Current test / build state

**226 tests pass, 4071 assertions** (prior baseline: 219 / 4031).
Zero compiler warnings from any source we control. Clean builds
throughout.

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                            # 226/226
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

## Milestone status (changes from the prior session)

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | unchanged: done |
| M2 — real-time foundation, membrane, ASRC | unchanged: headless half done; operator owes device wiring + loopback calibration + in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **expanded**: TimelineView shipped, Preparation tab now shows a real song timeline with strips + Pills; arm/focus state per-tape |
| M4 — persistence + capability tiers + overload protection | unchanged: done within current single-file scope; §7.8 directory format still deferred |
| M5 — plugin hosting + parameter view | unchanged |
| M6 — video | unchanged |
| M7 — full UI | **partially advanced**: timeline view is the first non-tree-readout component; Performance tab unchanged |
| M8 — ensemble (incl. multi-tape capture) | unchanged in code; **enabled** structurally — `memberTapes` is plumbed through the selector and the renderer; a future `CaptureSession` grown to per-tape will populate Pills with multi-tape membership without UI changes |

## The standalone app today

Four tabs (Performance / Preparation / Plugins / Video). Bottom bar
unchanged:

```
[ Arm | Mark In | Mark Out ] [ Undo | Redo ] [ ============= playhead ============= ] [ time ]
```

The **Preparation tab** is the visibly different one:

```
┌─ action row ──────────────────────────────────────────────────────────────────┐
│ [Save] [Load] [Reload demo]                                          status   │
├─ tree readout (Constituent hierarchy) ────────────────────────────────────────┤
│ #1 group demo session 12 wn                                                   │
│   #10 loop intro 3 wn                                                         │
│   #20 phrase verse 6 wn                                                       │
│     ...                                                                       │
├─ timeline (this session's work) ──────────────────────────────────────────────┤
│             0s    2s    4s    6s    8s   10s   12s   14s   16s   18s   20s   │
│ [AUD]Intro pad   [Arm]   ┃ ╭─────intro─────╮                                  │
│ [AUD]Verse rhy   [Arm]   ┃                  ╭──────────verse──────────╮       │
│ [AUD]Verse lead  [Arm]   ┃                  ┌─────(outline)──────────┐        │
│ [AUD]Outro pad   [Arm]   ┃                                            ╭──outro┐│
├─ diagnostics ─────────────────────────────────────────────────────────────────┤
│ Tier: ...   UI tick jitter: ...   Undo: ...   Capture: disarmed   Regions: 0  │
└───────────────────────────────────────────────────────────────────────────────┘
```

`outline` is the membership outline on the verse-lead row — a thin
rectangle, no header atoms, showing that the verse Pill's content
also reaches that row.

## Suggested first move next session

### 0. KICKOFF — operator GUI verification of the timeline (user-requested)

The implementation is headless-verified by 226 tests but
operator-verified by zero. Launch the `.app`:

```bash
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Walk through these checks:

1. **The Preparation tab shows the timeline.** Four rows (Intro
   pad / Verse rhythm / Verse lead / Outro pad), three Pills (intro,
   verse, outro). Verse Pill is on the Verse-rhythm row with a thin
   outline on the Verse-lead row.
2. **Per-row arm works.** Click the Arm button on any row → the
   button turns red, the row's left-edge indicator brightens
   (focus), the bottom-bar Arm button also reflects the focused row.
3. **Focus shifts on click.** Click anywhere on a non-armed row's
   strip head → that row becomes focused without changing arm state.
4. **Mark In stamps the focused tape.** Arm one row, scrub the
   playhead, Mark In, Mark Out. Check the diagnostics row: the new
   region carries the focused row's tape id (visible in the "Regions:
   N (last: ...)" line — *currently* the line doesn't print the
   tape id; consider adding that as a small polish item).
5. **Chord arms light up multiple rows.** Click Arm on two rows.
   Both go red. Today only the focused tape's id stamps the region —
   verify that's what happens (this is the documented M8 deferral,
   not a bug).

### 1. Smaller refinements likely to come up

These are small, well-scoped items the operator will probably want
once the timeline is on screen. Pick whichever the user asks for; do
not prefetch:

- **Tape id in the diagnostics "Regions" line.** Currently shows
  `(last: 12.40 s → 16.00 s, 3.60 s long)`. Append `· tape #200` so
  the new focused-tape behaviour is visible without launching a
  debugger.
- **Playhead overlay on the timeline.** A vertical line at the
  playhead's X coordinate across all rows. Today the playhead is
  only visible as the slider in the bottom bar.
- **Level-meter atom on strip heads.** Stubbed in the design but
  not in the renderer — drives off `audio levels` that M2 will
  produce. Gate on M2 audio wiring.
- **Retroactive-ring depth indicator.** Also gated on M2; today
  the ring exists in `engine/`, but its depth is not surfaced.
- **Mark Out announces visibly** — still on `todo.md`. With the
  timeline now in place, a transient banner over the timeline (or a
  flash on the just-stamped region) is the natural surface for the
  announcement.
- **Loop Constituent promotion gesture.** When `markOut` returns a
  region, the operator should be able to attach it to a Pill (or
  spawn a new Pill) with one gesture. Today regions live only in
  `MainComponent::capturedRegions_` — RAM, not session state.
  `todo.md` 2026-05-15 "Mark Out should announce" describes the
  shape.

### 2. macOS Load-dialog TCC bug (still blocked)

Unchanged. `todo.md` entry remains the authoritative reference;
Developer-ID signing is the most promising fix; drag-and-drop is the
quickest workaround.

### 3. Session-as-directory refactor (V2 §7.8)

Still gated on the Load-dialog bug being resolved first.

## Open questions (carry-forward)

- **Where promoted Loop Constituents attach** — same as before;
  with the timeline visible, this becomes: which Pill receives the
  Loop, and how does the operator pick it. Likely a click-on-Pill
  gesture or a "promote to current Pill" button on the Preparation
  pane.
- **Performer-side role-fillable phrase UX** — engine ships; the
  runtime UX still lives on the suggested-features list.
- **Multiple grammatical links per Pill** — the four-corner contract
  surfaces one entrance + one exit; §8.5 allows N of each.
  Preparation mode needs a way to expose the rest. Open.
- **Standalone Loops on the timeline** — Pills today are Phrases
  only. A Loop that isn't inside a Phrase produces no visual on the
  timeline. The dominant case post-promotion is "every Loop is inside
  a Pill," so this is acceptable; an interim "orphan loop block"
  visual is a deferred polish item if the operator ever hits the
  pre-promotion state.
- **M6 video format strategy** — unchanged.
- **M8 transport choice** — unchanged.

## Key decisions made this session

| Decision | Rationale |
|----------|-----------|
| Design for the dominant case (single-tape Pill) | Most Sirius users are solo performers; multi-tape is an M8 concern. Optimising the primary metaphor for the rare case adds visual debt for zero gain in the common case. |
| Mockup A refined (Pills-on-row + chord-arms-to-group) | Reaper-faithful spatial vocabulary, scales from solo to band without changing the primary metaphor. Inbox killed because immediate promotion is the dominant case. |
| `TimelineViewState` is JUCE-free, paralleling PerformanceViewState / PreparationViewState | The selector is the testable half; the view is operator-verified. Mirrors the existing UI split exactly. |
| Pill = Constituent with PhraseMetadata; Loops are not Pills | Honors continue.md's prior decision. Standalone Loops don't appear on the timeline today (acceptable since promotion is immediate). |
| Primary tape = mode-by-count among descendants; member tapes = insertion order | Insertion order gives the renderer a stable membership-outline draw order; mode-by-count anchors the Pill to the tape that contributed most content. |
| `phraseLoopActive` mapped from cardinality: Once → false, everything else → true | Honest UI rendering of repetition-rule semantics. |
| Per-tape arm state lives in `MainComponent`, not in `core/` | Arm is runtime UI state, not session data. Per the user's prior decision to keep "track" UI-only. |
| `CaptureSession` stays monolithic; chord-arms is visual-only for now | Multi-tape capture is M8 work; the data model's `memberTapes` is plumbed so a future per-tape CaptureSession populates Pills with no UI changes. |
| Demo's intro/verse/outro promoted to real Phrases | The demo now exercises both the dominant (single-tape) and future-proofed (multi-tape, on the verse) cases visibly. |
| Bottom-bar Arm now targets the focused tape | Preserves the one-handed gesture (Arm → Mark In → Mark Out) while letting per-row arm be the primary selection surface. |

## Commands to restore working state next session

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests
# expect: 226 / 226 pass, 4071 assertions
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Authoritative references for the next session:

- This file (`continue.md`).
- `todo.md` — open deferrals, Load-dialog bug, session-as-directory
  format, "Mark Out should announce" deferral.
- `Sirius Looper Whitepaper V2.md` §6 (tapes), §8 (phrases), §14
  (the performer's instrument), Appendix E (Reaper terminology map).
- Project memory:
  `.claude/projects/-Users-larryseyer-Sirius-Looper/memory/project_sirius_branding_and_otto.md`.
