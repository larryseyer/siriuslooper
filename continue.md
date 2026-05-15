# Session Continuation — 2026-05-15 (TimelineView polish + two big deferrals)

## Top-of-page summary

The TimelineView shipped the prior session was operator-verified live
this session and three small polish items wired in: focused-tape id in
the diagnostics regions line, an amber playhead overlay across all
timeline rows, and a transient amber-bordered banner that announces
each successful Mark Out for ~1.5 s. Two big design topics surfaced
mid-session and were deliberately deferred to their own sessions per
the new "defer big design topics" feedback memory: a plugin scanner
crash + scan-strategy redesign (operator-reported crash mid-scan, plus
a UX request for a light/deferred scan), and OTTO Look-and-Feel
integration (sister apps, shared-submodule model picked, comprehensive
todo entry written with the four module-home options ready for the
first-question of that future session).

**226 tests pass, 4071 assertions — unchanged across all changes.**
Clean builds from `rm -rf build` through `cmake --build build`.

**Next session opens on Loop Constituent promotion** — the natural
priority since captured regions still live only in
`MainComponent::capturedRegions_` (ephemeral RAM, evaporates on app
exit). The brainstorm needs to settle four UX questions before code
gets written; the kickoff brief is in *"Suggested first move next
session §0"* below.

## What shipped this session

| Commit  | Subject |
|---------|---------|
| `ee34812` | feat: diagnostics — show focused tape id in last-region line |
| `be194cf` | docs: todo.md — plugin scanner crash + scan-strategy redesign deferred |
| `bae0b64` | docs: todo.md — OTTO L&F integration deferred (shared submodule model picked) |
| `54a87b0` | feat: playhead overlay on TimelineView |
| `7c27b3b` | feat: CaptureBanner — transient on-screen confirmation for Mark Out |
| `f55bae7` | fix: CaptureBanner — explicit hide, toFront on show, larger + bolder |

### Diagnostics: focused tape id in the Regions line

`app/MainComponent.cpp` — the Preparation tab's `Regions:` line now
appends `· tape #<id>` to the last-region summary. Demo tapes are
100/200/300/400; capturing while focused on the verse-rhythm row shows
`(last: 12.40 s → 16.00 s, 3.60 s long · tape #200)`. Makes the
focused-tape behaviour visible without a debugger and proves chord-arms
truly stamps only the focused tape (M8 deferral confirmed visually).

### Playhead overlay on TimelineView

`ui/include/sirius/TimelineView.h` + `ui/src/TimelineView.cpp` +
`app/MainComponent.cpp` — TimelineView gained `setPlayhead
(std::optional<Rational>)`. Renderer-only state (the existing class
comment "playhead is the renderer's concern, not the selector's" was
honoured — the `TimelineViewState` struct stayed structure-only). Paint
draws a 2 px amber vertical line from the ruler band down through all
rows + a small downward chevron in the ruler band. Suppressed when the
playhead is outside the LMC span. Wired through a new
`PreparationPane::setTimelinePlayhead` forwarder called from
`MainComponent::refreshTimeline()` — the slider's existing
`onValueChange → refreshPerformance + refreshPreparation →
refreshTimeline` path now updates the overlay live as the operator
scrubs.

### Mark Out announcement (CaptureBanner)

`app/MainComponent.h` + `app/MainComponent.cpp` — a new nested
`CaptureBanner` Component sits on top of the tabbed content (z-order:
added last in the ctor). 480×52 rounded dark rectangle with a 2 px
amber border and amber 16 pt bold mono text. `show(msg)` cancels any
in-flight fade, sets text + alpha 1.0, calls `toFront(false)`, then
fades over 1500 ms via JUCE's `ComponentAnimator::fadeOut`. Hooked from
`onMarkOut` only when `captureSession_.markOut(t)` actually returned a
region; message is `Loop N captured · X.XX s · tape #YYY`.

**First-pass bug + fix:** the first version was invisible. Two
issues — `addAndMakeVisible` forces visibility on (overriding the
constructor's `setVisible(false)`), so an empty banner was rendering at
startup; and there was no `toFront` to defend against tab repaints.
Fix in `f55bae7`: explicit `setVisible(false)` after the
`addAndMakeVisible` call, `toFront(false)` inside `show()`,
`cancelAnimation` before each fade so rapid back-to-back announcements
each get a full 1.5 s lifetime, plus the size/loudness bump. Operator
confirmed visible.

## Two big deferrals — see `todo.md`

### Plugin scanner crashes + scan-strategy redesign (`be194cf`)

Operator-reported crash mid-scan. The todo entry captures: (1) crash
protection options (out-of-process child scanning vs. in-process try/
catch + watchdog timer), (2) light scan mode (file metadata only, no
instantiation), (3) lazy-instantiate-on-first-use ("scan when loaded"
— Bitwig precedent), and the recommended sequence to tackle them. The
single immediate ask for a future session: **next time the scanner
crashes, grab `~/Library/Logs/DiagnosticReports/Sirius Looper-*.crash`
and attach it to the todo entry** — that file determines whether the
crash is in our scanner code, JUCE's loader, or a specific plugin's
constructor, and changes the design entirely.

### OTTO Look-and-Feel integration (`bae0b64`)

User flagged that Sirius should visually match OTTO since they're
sister apps that always ship together but are sold individually (full
OTTO standalone, limited OTTO bundled in Sirius). OTTO at
`/Users/larryseyer/AudioDevelopment/OTTO` already has a serious design
system — `OTTOLookAndFeel.h/cpp` (2154 lines, JUCE LookAndFeel_V4
subclass), `OTTOColours.h` (~240 lines, layered dark-theme palette +
8 player colors + transport/meter/state semantics), 10 font families in
`OTTO/assets/Fonts/`. Decision made this session: **shared-submodule
model** (one source of truth, palette tweaks in OTTO propagate to
Sirius). Open question deferred to next session: *where* the shared
module physically lives — four options pre-staged in the todo entry,
working recommendation is a new top-level repo. Memory file
`project_sirius_branding_and_otto.md` updated with the sister-apps
framing and visual-match commitment.

## Current test / build state

**226 tests pass, 4071 assertions** — identical to the prior session's
baseline; no selector or core code changed across the four feature
commits. Zero compiler warnings from any source we control.

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                            # 226 / 226
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

## Milestone status (changes from the prior session)

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | unchanged: done |
| M2 — real-time foundation, membrane, ASRC | unchanged: headless half done; operator owes device wiring + loopback calibration + in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **expanded**: TimelineView playhead overlay shipped; CaptureBanner makes the Mark Out gesture glanceable per white paper 14.5 |
| M4 — persistence + capability tiers + overload protection | unchanged: done within current single-file scope; §7.8 directory format still deferred |
| M5 — plugin hosting + parameter view | **regressed visibility**: operator-reported crash during scan; scan-strategy redesign deferred to its own session |
| M6 — video | unchanged |
| M7 — full UI | **partially advanced**: timeline now has a live playhead overlay, Mark Out has glanceable feedback |
| M8 — ensemble (incl. multi-tape capture) | unchanged |

## The standalone app today

Four tabs (Performance / Preparation / Plugins / Video). Bottom bar
unchanged:

```
[ Arm | Mark In | Mark Out ] [ Undo | Redo ] [ ============ playhead ============ ] [ time ]
```

The **Preparation tab** with the playhead at ~13 s during a verse
capture now shows:

```
┌─ action row ──────────────────────────────────────────────────────────────────┐
│ [Save] [Load] [Reload demo]                                          status   │
├─ tree readout (Constituent hierarchy) ────────────────────────────────────────┤
│ #1 group demo session 12 wn                                                   │
│ ...                                                                           │
├─ timeline ────────────────────────────────────────────────────────────────────┤
│      0s    2s    4s    6s    8s   10s   12s ▼ 14s   16s   18s   20s          │
│ [AUD]Intro pad   [Arm]   ┃ ╭─────intro─────╮         │                        │
│ [AUD]Verse rhy   [Disarm]┃                  ╭───────│verse───────╮            │
│ [AUD]Verse lead  [Arm]   ┃                  ┌──────│outline─────┐             │
│ [AUD]Outro pad   [Arm]   ┃                         │              ╭──outro┐   │
├─ diagnostics ─────────────────────────────────────────────────────────────────┤
│ Tier: ...   UI tick jitter: ...   Undo: ...   Capture: armed                  │
│ Regions: 1  (last: 12.40 s → 16.00 s, 3.60 s long · tape #200)                │
└───────────────────────────────────────────────────────────────────────────────┘
```

`▼` is the playhead chevron in the ruler band, `│` is the playhead line
across rows. The verse row is armed (Disarm button) and an outline
shows on the verse-lead row.

On a successful Mark Out, a 480×52 amber-bordered banner pops at
top-center reading `Loop N captured · 3.60 s · tape #200` and fades
over 1.5 s.

## Suggested first move next session

### 0. KICKOFF — Loop Constituent promotion gesture (brainstorm-first)

Captured regions today live only in `MainComponent::capturedRegions_`
— a `std::vector<CaptureRegion>` in RAM, not part of the session
graph. They evaporate on app exit. **Promotion is the actual
completion of the capture flow**: turn a captured region into a Loop
Constituent attached to the session tree, push the edit onto
`UndoStack`, and surface it as a Pill on the timeline.

Four UX questions to settle in the brainstorm *before* any code:

1. **Where does the promoted Loop attach?** Three plausible answers:
   - **Into the currently-focused Pill** (verse / intro / outro etc.)
     — natural if the operator is building a phrase by capturing onto
     it. Implies "you must focus a Pill before promoting."
   - **As a standalone Loop at the session root**, with later
     promotion-into-a-Pill as a separate gesture. Two-step but more
     forgiving.
   - **Heuristically — under the Pill that owns the focused tape at
     the playhead's LMC time** if one exists, otherwise root.
     Smartest but harder to predict.
2. **What's the operator gesture?**
   - A "Promote" button on the Preparation pane that consumes the
     most-recent region?
   - A click on the captured-region banner that just appeared?
   - A drag from a region list onto a target Pill?
   - A keyboard shortcut (single-key, eyes-free per white paper
     14.4)?
3. **Does promotion push to the undo stack?** Almost certainly yes —
   white paper 14.7 ("the sacred undo gesture"). What's the label?
   `"capture loop"`? `"promote region to loop"`?
4. **What about the rest of the deferred capture-flow UX?** The
   `todo.md` "Mark Out should announce" entry mentions a persistent
   capture-history widget on the Preparation tab — a scrolling list
   of regions with their in/out times, length, and a per-row
   "Promote" button. That feels like the natural surface for
   promotion gesture #2 above. Worth deciding whether to ship it as
   part of this work or split it.

The brainstorm naturally leads to a written design doc; per the
deferral memory, the **implementation** of promotion is its own
session after the brainstorm settles. So the next session is
brainstorm + design-doc-write + commit, then stop.

### 1. Smaller refinements still on the bench

These are the ones I'd reach for if the user wants a smaller in-session
win before the promotion brainstorm:

- **Bottom-bar time format** — the `bottomInfo_` label currently
  shows `"12.40 s"`. Pro-audio convention is `MM:SS.cs` or
  `M:SS.cc`. 5-minute change, visible improvement.
- **Playhead chevron polish** — currently a simple triangle. Could
  add a thin stem behind it that matches the row line so the head
  reads as part of the playhead, not floating above it.
- **Capture-history widget on Preparation tab** — listed in the Mark
  Out todo entry; useful in its own right and the natural home for a
  promotion gesture. Would partially front-run the promotion
  brainstorm, so possibly defer until after that brainstorm decides
  the surfacing model.

### 2. Outside-of-this-session deferrals (status only)

- **Plugin scanner crash + redesign** (`todo.md`): waits on crash
  report from `~/Library/Logs/DiagnosticReports/`.
- **OTTO L&F integration** (`todo.md`): waits on a dedicated session;
  first question is module-home (4 options ready).
- **macOS Load-dialog TCC bug** (`todo.md`): unchanged. Drag-and-drop
  is the quickest workaround; Developer-ID signing is the proper fix.
- **Session-as-directory refactor** (V2 §7.8): still gated on the
  Load-dialog bug.

## Open questions (carry-forward)

- **Where promoted Loop Constituents attach** — same question that
  was open last session, now the most-active question. The brainstorm
  in §0 above will close it.
- **Performer-side role-fillable phrase UX** — engine ships; runtime
  UX still on the suggested-features list.
- **Multiple grammatical links per Pill** — §8.5 allows N entrances /
  exits; the four-corner contract surfaces one of each. Preparation
  mode needs a way to expose the rest. Open.
- **Standalone Loops on the timeline** — Pills today are Phrases
  only. A Loop that isn't inside a Phrase produces no visual today.
  Becomes relevant the moment promotion ships (the §0 question 1
  decision determines whether this matters at all).
- **M6 video format strategy** — unchanged.
- **M8 transport choice** — unchanged.

## Key decisions made this session

| Decision | Rationale |
|----------|-----------|
| Playhead lives on the renderer, not the state struct | Honoured the existing class comment "playhead is the renderer's concern, not the selector's." Selector tests untouched; renderer carries the optional. |
| Banner is a sibling of `tabs_`, not a child of a specific tab | Mark Out can be triggered from any tab — banner appears regardless of which tab is foreground. Last-added child = top of z-order. |
| Banner is amber on dark (matches the playhead colour) | Both surfaces communicate "what's happening at the playhead now." Same hue ties them together. OTTO L&F integration will retune to the OTTO accent (teal). |
| Defer big design topics mid-session, capture in `todo.md` | User said it twice in one session; saved as the new `feedback-defer-big-design-to-own-session` memory. Two-topics-in-one-session bloats context. |
| Shared-submodule model for OTTO L&F | Sister apps with permanent shared visual identity — drift between them is a permanent tax; one source of truth is worth the one-time extraction cost. Module home is the open question for the next session. |
| Sirius must visually match OTTO (not just "be inspired by") | OTTO and Sirius are sold individually but always ship together; visual coherence is part of the product proposition. Saved as a project memory. |

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
- `todo.md` — Load-dialog bug, session-as-directory format, plugin
  scanner redesign (new), OTTO L&F integration (new), Mark Out
  capture-history widget + Loop promotion gesture.
- `docs/Sirius Looper Whitepaper V2.md` §6 (tapes), §8 (phrases), §14
  (the performer's instrument), Appendix E (Reaper terminology map).
- OTTO source at `/Users/larryseyer/AudioDevelopment/OTTO` —
  particularly `src/otto-plugin/ui/OTTOColours.h` and
  `OTTOLookAndFeel.h/cpp` plus `assets/Fonts/`.
- Project memory:
  - `feedback_clean_builds.md`
  - `feedback_arm_disarm_is_required.md`
  - `feedback_defer_big_design_to_own_session.md` (new this session)
  - `project_sirius_branding_and_otto.md` (updated this session)
