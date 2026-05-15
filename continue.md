# Session Continuation — 2026-05-15 (capture-promotion shipped)

> **For a fresh chat picking this up cold:** read this whole file before
> doing anything. The user's `~/.claude/CLAUDE.md` and the project's
> auto-memory (`MEMORY.md` + `*.md` in the memory dir) are loaded
> automatically and contain the rules. This file is the *state*: what
> just happened, what is ready to verify, and what to start on next.

---

## 0. Project orientation (skip if you've worked here before)

**Sirius Looper** is a real-time looping / arrangement application for
musicians, built in JUCE/C++20 with a strict separation between a
JUCE-free conceptual-time core (`core/`) and the audio/UI layers
(`engine/`, `ui/`, `app/`). It is the sister app to **OTTO** — they
ship together but are sold individually (full OTTO standalone, limited
OTTO bundled in Sirius). The architectural premise is the
**always-running tape**: every input is continuously recorded, so
*retroactive* capture (Mark In / Mark Out *after* the music played) is
the natural gesture.

Authoritative reading, in order if you need it:

1. **`/Users/larryseyer/.claude/CLAUDE.md`** — global engineering
   rules (philosophy, commit discipline, hard-stop conditions, code
   standards). Already loaded.
2. **`docs/Sirius Looper Whitepaper V2.md`** — conceptual model,
   musical philosophy. Tells you *why* the architecture is this way.
3. **`docs/Sirius Looper User Guide.md`** — operator-facing
   *how-to*. New as of this session; has chapter 1 ("Capturing Phrases
   and Loops") plus a glossary that pins down Tape / Phrase / Loop /
   Pill / CaptureRegion / Mark In / Mark Out / Arm / Playhead / LMC.
4. **`docs/superpowers/specs/2026-05-15-capture-promotion-design.md`**
   — the design that this session shipped.
5. **`docs/superpowers/plans/2026-05-15-capture-promotion.md`** —
   the 10-task implementation plan (all tasks now complete).
6. **`todo.md`** — deferred-work register. Top entries are the
   strongest candidates for the next session.

**Project policies that override defaults** (full text in CLAUDE.md):

- **Work directly on `master`.** No feature branches unless explicitly
  instructed.
- **Commits are local.** GitHub remote operations (push, PR,
  gh-anything) are handled by the user in a *separate terminal* — do
  not push, do not create PRs. See memory
  `feedback-github-handled-in-separate-terminal`.
- **Single-line commit messages**, format `<type>: <short title>`. No
  Co-Authored-By trailer. The user has a script that derives a
  filename from the first line; multi-line breaks the path.
- **Never run `open ...` or any GUI-launching command.** Operator-side
  .app verification is something the *user* runs at their own terminal.
  Subagents and the controller do compile-only builds + headless unit
  tests.
- **Defer big design topics to their own session** rather than letting
  them bloat the current one. Capture in `todo.md` with full
  Files/Why-Deferred/What's-Needed structure. See memory
  `feedback-defer-big-design-to-own-session`.

---

## 1. Top-of-page summary — what just shipped

Capture promotion landed end-to-end across **17 commits** in a single
subagent-driven session. Every Mark Out now auto-promotes the captured
region into the session Constituent tree as a **Loop** (and a wrapper
**Phrase** when the playhead at Mark In is outside any existing
Phrase). The change pushes onto `UndoStack` with a
`CaptureRestorePoint` so `onUndo` restores `CaptureSession` to
`AwaitingOut` with the original in-point intact, and `onRedo`
symmetrically clears that state to prevent a duplicate-Loop bug. The
on-screen `CaptureBanner` is now tappable for one-tap recovery from a
too-early Mark Out. The pure `core/sirius::promotion::promote`
function ships **single-instance-only** behind a runtime guard that
throws loudly if any `ConstituentId` appears more than once anywhere
in the tree — the explicit "write protect" the operator asked for.

**Tests:** 233 pass / 4124 assertions (was 226 / 4071 last session).
Clean rebuild verified.

The **user guide** (`docs/Sirius Looper User Guide.md`) is new this
session, with chapter 1 ("Capturing Phrases and Loops") covering
glossary + the auto-promotion rule with two ASCII diagrams + the
build-then-layer workflow + recovering from an early Mark Out.

---

## 2. The 17 commits

| SHA       | Subject                                                                                                |
|-----------|--------------------------------------------------------------------------------------------------------|
| `e3bc84e` | feat: UndoStack — optional CaptureRestorePoint per entry for promotion undo                             |
| `1c40632` | fix: UndoStack — return CaptureRestorePoint by value to dodge vector-invalidation footgun               |
| `b994a37` | feat: promotion module skeleton + multi-instance write-protect                                          |
| `864d27f` | fix: PromotionTests — designated init for PhraseMetadata                                                |
| `fb776c2` | feat: promotion — add Loop child to host Phrase when Mark In lands inside one                           |
| `aa3e647` | chore: Promotion — drop unused Phrase.h include (Task 4 reintroduces it)                                |
| `bc2b4f3` | docs: Promotion — make post-clamp boundary invariant + loop-default-name choice visible                 |
| `190e92e` | feat: promotion — mint Phrase wrapper when Mark In is outside any Phrase                                |
| `49dc6ed` | refactor: Promotion — designated init returns + tighten mint test tape assertions                       |
| `5417a0d` | test: promotion — lock down straddle clamping to host Phrase boundary                                   |
| `d7f3cef` | test: promotion — defensive throw on degenerate captured region                                         |
| `a81ad9f` | feat: MainComponent — onMarkOut auto-promotes captures into the session tree                            |
| `aa96b1e` | refactor: PromotionResult — surface hostPhraseName to consumers; drop announceCapture re-walk           |
| `3946ef7` | feat: MainComponent — undo of a promotion restores AwaitingOut + original in-point                      |
| `5cafe4e` | feat: MainComponent — onRedo symmetrically clears AwaitingOut to prevent duplicate-Loop bug             |
| `519de5d` | feat: CaptureBanner — tap to undo within the visible window                                             |
| `1b1d14e` | docs: continue.md + todo.md DemoSession entry + onRedo edge-case comment + UG fix                       |

The earlier `9a414fb` and `807f103` commits (this session's brainstorm
+ plan output: design spec, user-guide chapter 1, todo deferral entry,
implementation plan) are the documents that the 17 implementation
commits realize.

---

## 3. The shipped capture/promotion behavior

What the operator can do today:

1. **Arm** an input → **Mark In** at the start of a phrase → **Mark Out**
   at the end. The system mints a fresh Phrase (PhraseMetadata
   `role="capture"`, empty name) at the song root containing one Loop
   child whose `TapeReference` carries the captured region's tape +
   LMC times. A Pill appears on the timeline immediately. Banner shows
   `Phrase captured · X.XX s · tape #YYY`.

2. **Mark In with the playhead inside an existing Pill, then Mark Out**
   → the system adds a new Loop child to that Phrase. No new Pill;
   the existing Pill quietly gains another layer. Banner shows
   `Loop added to <hostName> · X.XX s · tape #YYY`.

3. **Straddle** (Mark In inside a Phrase, Mark Out past the host's
   end): **Mark In wins** — the Loop becomes a child of the host
   Phrase whose span contains Mark In, with the Loop's *Constituent*
   boundaries clamped to the host. The `TapeReference` keeps the
   *unclamped* original LMC times so the audio beyond the host
   boundary remains referenceable.

4. **Undo** (bottom-bar button or banner-tap within the 1.5 s fade
   window) reverts the Constituent tree AND restores `CaptureSession`
   to `AwaitingOut` with the original Mark In + tape intact. The
   operator can immediately Mark Out at a different time, or Disarm
   to abandon — no need to re-arm and re-Mark-In.

5. **Redo** symmetrically clears AwaitingOut and replays the
   Constituent edit, returning to the Armed-no-pending-In state the
   original promotion produced.

6. **Multi-instance guard:** `promote()` throws `std::logic_error` if
   any `ConstituentId` appears more than once in the tree. Today the
   demo session has unique ids so this never fires; it's the explicit
   gate that catches the day shared-placement architecture lands and
   `arrangement::sequence` starts producing repeated placements with
   shared ids. Error message points the operator at `todo.md`.

### Promotion semantics — one-paragraph summary

Mark Out = auto-promote. The playhead position **at Mark In** decides
where the new Loop attaches: inside an existing Phrase span → Loop
becomes a child of that Phrase; outside any Phrase → mint a new
Phrase wrapper at the song root, containing the Loop. Loops are
leaves with `TapeReference`; Phrases are containers with
`PhraseMetadata`. A Loop is never standalone in the tree (the
convention; enforced by the promotion code path, not by the
permissive data model).

---

## 4. Architecture decisions made or preserved this session

| Decision | Rationale |
|---|---|
| `core/sirius::promotion` is a pure-function module mirroring `core/sirius::arrangement`. | Promotion is musical logic, not GUI. Lives in `core/`, JUCE-free, fully unit-testable. |
| `promote()` takes `IdAllocator = std::function<ConstituentId()>` callback rather than two id parameters. | `ConstituentId` is project-wide (not per-type). Host-Phrase case allocates one id, mint case allocates two. Callback boundary lets the function call as needed without burning an id. |
| Multi-instance write-protect is a runtime throw, not a compile-time constraint. | Data model is dynamically permissive (`Constituent` can carry any combination of metadata); the convention is enforced at the gateway. The throw points at the `todo.md` entry by name so the failure mode is recoverable through documented work. |
| Undo restoration of capture state is achieved by extending `UndoStack::Entry` with an optional `CaptureRestorePoint`, not by tracking parallel state. | `UndoStack` already manages per-entry data; adding restore-point keeps capture-state lifetime co-located with the entry it belongs to (eviction at depth-cap is automatic). |
| `MainComponent::onUndo` reads `currentEntryRestorePoint()` *before* `undo()`; `onRedo` reads it *after* `redo()`. | Undo wants to restore the state that existed *before* the entry being left. Redo wants to clear to the state that existed *after* the entry being landed on. Asymmetry is correct and documented in code comments. |
| The `M3 simplification` (1:1 conceptual ↔ LMC) is documented in `Promotion.h` and `Promotion.cpp`. | When real tempo maps land later, the boundary computation in `promote()` will need a `TempoMap::applyInverse` — the comments make this an unmissable known limitation. |
| The DemoSession intro/outro Phrase-AND-Loop hybrids are deferred, not retrofitted. | Restructuring would touch view-state snapshot tests; cleaner as a focused follow-up commit. Tracked in `todo.md`. |
| Whitepapers + license docs moved to `docs/`. | The user explicitly asked. Operator + license files (`continue.md`, `todo.md`, `LICENSE`) stay at root. |
| The user guide is its own document, separate from the white paper. | White paper = *why*; user guide = *how*. Separate audiences. New convention saved as memory. |

---

## 5. Test + build state — restoration commands

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                            # expect: 233 / 233 pass, 4124 assertions
./build/tests/SiriusTests "[promotion]"              # 7 / 7 promotion test cases
```

**Promotion test cases:**
- `[promotion][guard]` — multi-instance write-protect throws.
- `[promotion][host]` — Loop becomes child of host Phrase.
- `[promotion][mint]` (×2) — empty root mints Phrase + Loop; gap between Phrases mints a fresh Phrase.
- `[promotion][straddle]` — Loop bounds clamp to host; TapeReference times unclamped.
- `[promotion][defensive]` — zero-duration / reversed-bounds throws.
- `[undo][promotion]` — `UndoStack` round-trips `CaptureRestorePoint`.

The `.app` bundle is at:
```
build/app/SiriusLooper_artefacts/Release/Sirius Looper.app
```

(Do **not** `open` it as the controller. Hand the path to the user
for operator verification — see §6.)

---

## 6. Operator-side end-to-end verification (user runs in their terminal)

The next-session controller's *first* job is to ask the user to run
this verification before picking new work, since the
capture-promotion changes are not unit-test-reachable.

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests       # confirm 233 / 233
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Then in the app:

1. **Pass 1 (build sections):** Arm an input. Mark In, Mark Out at
   different playhead positions to mint three fresh Phrases as the
   playhead moves forward. Each Mark Out should produce a banner
   reading `Phrase captured · X.XX s · tape #YYY` and a new Pill on
   the timeline.
2. **Pass 2 (overdub):** Scrub the playhead to inside an existing
   Pill. Switch focus to a different input (per-row arm in the
   Preparation tab). Mark In, Mark Out. Banner reads
   `Loop added to <name> · X.XX s · tape #YYY`. No new Pill, but
   the diagnostics line's `Regions: N` count goes up.
3. **Recovery — banner tap:** Mark In + Mark Out. Within 1.5 s, tap
   the banner. Pill disappears. Mark Out is *immediately* available
   without re-arming (CaptureSession is back in AwaitingOut). Move
   the playhead, hit Mark Out — a new Pill appears at the new span.
4. **Recovery — bottom-bar Undo:** Same as #3 but with the bottom-bar
   Undo button instead of the banner. Same outcome.
5. **Redo after Undo:** After #3 or #4, hit Redo. The Pill returns.
   Hit Mark Out — verify it does NOT mint a duplicate Loop (this is
   the bug the `5cafe4e` symmetric `onRedo` fix prevents).
6. **Crashes:** check `~/Library/Logs/DiagnosticReports/Sirius Looper-*.crash`
   after the session. Should be empty.

If anything fails, the issue is likely subtle (banner z-order, click
intercepts, refresh-call ordering). None of these are in unit-test
reach. Surface the failure mode + steps to reproduce; debug from there.

---

## 7. Suggested next work

After the operator-side verification (§6), the highest-value picks
in `todo.md` (in order of strength):

### Pick A — DemoSession Phrase-vs-Loop reconciliation (small, ~1 hour)

Top entry in `todo.md`. The capture-promotion design teaches
"Loops are leaves; Phrases are containers." The middle phrase in
`DemoSession::buildDemoSession()` (the verse with two layered Loop
children) honors this. **The intro and outro do not** — both attach
`PhraseMetadata` *and* `TapeReference` to the same Constituent,
making them Phrase-AND-Loop hybrids that fit neither category cleanly.

Operationally `promote()` still works — capturing into intro/outro
adds a Loop child to a Constituent that already has its own
`TapeReference`. The banner reads correctly. But the data shape
contradicts the doc.

**To finish:** rewrite intro and outro to be Phrase-only shells, each
containing one `TapeReference`-bearing Loop child (mirror the verse's
structure). Update view-state test expectations that depend on the
prior shape (likely `PerformanceViewStateTests`,
`TimelineViewStateTests`, `PreparationViewStateTests`). Once green,
optionally tighten `findHostRecursive` in `core/src/Promotion.cpp` to
`isPhrase() && !tapeReference()` so the convention is guarded by both
data and code.

### Pick B — Shared-placement-with-per-instance-overlays architecture (large, multi-session)

Second entry in `todo.md`. The brainstorm-deferred big topic. The
operator's musical model is "the verse plays 3 times, sharing common
layers (drums, bass, rhythm) but with per-instance vocal variations."
Today `arrangement::sequence` creates per-placement Constituent
copies — each placement is a distinct Constituent object that happens
to share the same id. The convention requires shared-by-reference
with per-instance overlay buckets.

Six steps in the `todo.md` entry. Probably wants its own brainstorm →
spec → plan → execute cycle (the same flow that produced
capture-promotion this session).

### Pick C — Operator's choice from `todo.md`

Other deferrals in `todo.md`:
- Plugin scanner crash + redesign (waits on a crash report from
  `~/Library/Logs/DiagnosticReports/`).
- OTTO L&F integration (its own dedicated session; first question is
  module-home — 4 options ready in the entry).
- macOS Load-dialog TCC bug (Drag-and-drop is the workaround;
  Developer-ID signing is the proper fix).
- Session-as-directory format (V2 §7.8) — gated on the Load-dialog bug.
- Transient capture announcement / capture-history widget — partially
  superseded by auto-promotion this session, but the `todo.md` entry
  is still preserved for context.
- Various M2 audio-device wiring and M0 CI items the operator owes.

---

## 8. Milestone status snapshot

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | done |
| M2 — real-time foundation, membrane, ASRC | headless half done; operator owes device wiring + loopback calibration + in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **substantially advanced this session**: capture promotion is the M3 capture flow's actual completion. Captures are now persistent in the Constituent tree, not ephemeral RAM. Undo is non-destructive of capture state. CaptureBanner is glanceable + tappable. |
| M4 — persistence + capability tiers + overload protection | done within current single-file scope; §7.8 directory format still deferred |
| M5 — plugin hosting + parameter view | unchanged: operator-reported scanner crash + scan-strategy redesign deferred |
| M6 — video | unchanged |
| M7 — full UI | **advanced**: CaptureBanner is now interactive (tap-to-undo); promotion makes captures Pills on the timeline immediately |
| M8 — ensemble (incl. multi-tape capture) | unchanged: gated on the shared-placement architecture |

---

## 9. Open questions (carry-forward)

- **Where promoted Loop Constituents attach** — *closed* this session
  (playhead-at-Mark-In rules; host wins, mint at root if no host).
- **Standalone Loops on the timeline** — *closed* this session (the
  convention forbids them; every Loop has a Phrase parent).
- **Performer-side role-fillable phrase UX** — engine ships; runtime
  UX still on the suggested-features list.
- **Multiple grammatical links per Pill** — open.
- **M6 video format strategy** — unchanged.
- **M8 transport choice** — unchanged.

---

## 10. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded).
- This file (`continue.md`) — session state.
- `todo.md` — deferred items register.
- `docs/Sirius Looper Whitepaper V2.md` — conceptual model.
- `docs/Sirius Looper User Guide.md` — operator-facing how-to (new
  this session).
- `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` —
  the design that this session realized.
- `docs/superpowers/plans/2026-05-15-capture-promotion.md` — the
  10-task implementation plan (all complete).
- OTTO source at `/Users/larryseyer/AudioDevelopment/OTTO` —
  particularly `src/otto-plugin/ui/OTTOColours.h` and
  `OTTOLookAndFeel.h/cpp` plus `assets/Fonts/`. Relevant when L&F
  integration becomes the active topic.

### Project memory files

- `feedback_clean_builds.md` — always `rm -rf build` before GUI
  testing.
- `feedback_arm_disarm_is_required.md` — performer-facing arm/disarm
  gesture is mandatory.
- `feedback_defer_big_design_to_own_session.md` — when a major new
  design topic surfaces mid-session, write a comprehensive `todo.md`
  entry and stay on the current path.
- `feedback_github_handled_in_separate_terminal.md` — Claude
  commits locally on master only; user handles push/PR in a parallel
  terminal. **Do not push or open PRs.**
- `project_sirius_branding_and_otto.md` — sister apps with shared
  visual identity (deferred).
- `project_user_guide_alongside_whitepaper.md` — user guide doc lives
  in `docs/`, paired with the white paper; new this session.
