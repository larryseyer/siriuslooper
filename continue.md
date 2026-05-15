# Session Continuation — 2026-05-15 (capture-promotion shipped end-to-end)

## Top-of-page summary

Capture promotion landed end-to-end across 17 commits in a single
subagent-driven session: every Mark Out now auto-promotes the captured
region into the session Constituent tree as a Loop (and a wrapper
Phrase when the playhead at Mark In is outside any existing Phrase),
the change pushes onto `UndoStack` with a `CaptureRestorePoint` so
`onUndo` restores `CaptureSession` to `AwaitingOut` with the original
in-point intact, `onRedo` symmetrically clears that state to prevent a
duplicate-Loop bug, and the on-screen `CaptureBanner` is now tappable
for one-tap recovery from a too-early Mark Out. The pure
`core/sirius::promotion::promote` function ships single-instance-only
behind a runtime guard that throws loudly if any `ConstituentId`
appears more than once anywhere in the tree — the explicit "write
protect" the operator asked for. The first chapter of the new
`docs/Sirius Looper User Guide.md` (workflow-first, with glossary +
ASCII diagrams of the auto-promotion rule) is the operator-facing
documentation of all of this.

**233 tests pass, 4124 assertions** — up from 226 / 4071 last session.
Clean rebuild from `rm -rf build` through `cmake --build build`. All
green.

**Next session opens on operator-side end-to-end verification of the
shipped behavior**, plus picking the first item from `todo.md` to
tackle. The two top-of-`todo.md` items (DemoSession Phrase-vs-Loop
reconciliation, and the larger shared-placement-with-overlays
architecture) are both concrete enough for their own session.

## What shipped this session

This session followed the brainstorm → spec → plan →
subagent-driven-execution flow end to end. Skills used:

- `superpowers:brainstorming` — settled the four UX questions in
  `continue.md` §0 (where promoted Loops attach, what gesture
  triggers, undo label, capture-history scope) and the deeper "what
  is a Phrase vs a Loop" question. Output: the design spec at
  `docs/superpowers/specs/2026-05-15-capture-promotion-design.md`,
  the first chapter of the user guide at
  `docs/Sirius Looper User Guide.md`, and a comprehensive `todo.md`
  entry for the deferred shared-placement architecture.
- `superpowers:writing-plans` — produced the 10-task implementation
  plan at `docs/superpowers/plans/2026-05-15-capture-promotion.md`
  (TDD, exact code blocks, exact commands, exact commit messages).
- `superpowers:subagent-driven-development` — executed the 10 tasks
  with fresh implementer subagents per task plus per-task spec and
  code-quality review. 16 task commits + 1 task-10 polish commit.

### The 17 commits (capture-promotion deliverable)

| SHA       | Subject                                                                                                   |
|-----------|-----------------------------------------------------------------------------------------------------------|
| `e3bc84e` | feat: UndoStack — optional CaptureRestorePoint per entry for promotion undo                                |
| `1c40632` | fix: UndoStack — return CaptureRestorePoint by value to dodge vector-invalidation footgun (review fix)     |
| `b994a37` | feat: promotion module skeleton + multi-instance write-protect                                             |
| `864d27f` | fix: PromotionTests — designated init for PhraseMetadata to silence -Wmissing-field-initializers           |
| `fb776c2` | feat: promotion — add Loop child to host Phrase when Mark In lands inside one                              |
| `aa3e647` | chore: Promotion — drop unused Phrase.h include (Task 4 reintroduces it)                                   |
| `bc2b4f3` | docs: Promotion — make post-clamp boundary invariant + loop-default-name choice visible (review fix)       |
| `190e92e` | feat: promotion — mint Phrase wrapper when Mark In is outside any Phrase                                   |
| `49dc6ed` | refactor: Promotion — designated init returns + tighten mint test tape assertions (review fix)             |
| `5417a0d` | test: promotion — lock down straddle clamping to host Phrase boundary                                      |
| `d7f3cef` | test: promotion — defensive throw on degenerate captured region                                            |
| `a81ad9f` | feat: MainComponent — onMarkOut auto-promotes captures into the session tree                               |
| `aa96b1e` | refactor: PromotionResult — surface hostPhraseName to consumers; drop announceCapture re-walk (review fix) |
| `3946ef7` | feat: MainComponent — undo of a promotion restores AwaitingOut + original in-point                         |
| `5cafe4e` | feat: MainComponent — onRedo symmetrically clears AwaitingOut to prevent duplicate-Loop bug (review fix)   |
| `519de5d` | feat: CaptureBanner — tap to undo within the visible window                                                |
| (TBD)     | docs: continue.md + todo.md DemoSession reconciliation entry + onRedo edge-case comment + UG fix           |

The `9a414fb` and `807f103` commits from earlier this session contained
the design spec, user-guide chapter 1, todo.md deferral entry, and the
implementation plan — they're the brainstorm/plan output that this
session's 17 implementation commits realize.

### What the operator can do today

The capture flow is now *complete* at the M3 single-instance level.

1. **Arm** an input. **Mark In** at the start of a phrase. **Mark Out**
   at the end. The system mints a fresh Phrase (PhraseMetadata
   `role="capture"`, empty name) at the song root, containing one Loop
   child whose `TapeReference` carries the captured region's tape +
   LMC times. A new Pill appears on the timeline immediately. The
   `CaptureBanner` shows `Phrase captured · X.XX s · tape #YYY`.
2. **Mark In with the playhead inside an existing Pill, then Mark Out**
   — the system adds a new Loop child to that Phrase. No new Pill;
   the existing Pill quietly gains another layer. Banner shows
   `Loop added to <hostName> · X.XX s · tape #YYY`.
3. **If the captured region straddles a Phrase boundary** (Mark In
   inside, Mark Out past the host's end), Mark In wins — the Loop
   becomes a child of the host Phrase whose span contains Mark In,
   with the Loop's *Constituent boundaries* clamped to the host. The
   `TapeReference` keeps the unclamped original LMC times so the
   audio beyond the host boundary remains referenceable.
4. **Undo** (bottom-bar or banner-tap within the 1.5s window) reverts
   the Constituent tree AND restores `CaptureSession` to `AwaitingOut`
   with the original Mark In + tape intact. The operator can
   immediately Mark Out at a different time, or Disarm to abandon —
   no need to re-arm and re-Mark-In.
5. **Redo** symmetrically clears AwaitingOut and replays the Constituent
   edit, returning to the Armed-no-pending-In state the original
   promotion produced.
6. **Multi-instance guard** — `promote()` throws `std::logic_error` if
   any `ConstituentId` appears more than once in the tree. Today the
   demo session has unique ids so this never fires; it is the explicit
   gate that catches the day shared-placement architecture lands and
   `arrangement::sequence` starts producing repeated placements with
   shared ids. The error message points the operator at `todo.md`.

### The 16 task commits' review trail

Per the subagent-driven-development skill, every task ran through:
implementer subagent → spec compliance review → code quality review →
fix loop if any review flagged Important/Critical issues → mark
complete. This caught and fixed:

- **Task 1:** vector-invalidation footgun in `currentEntryRestorePoint()`
  — switched from returning `const optional&` to returning `optional`
  by value (commit `1c40632`).
- **Task 2:** `-Wmissing-field-initializers` on `PhraseMetadata{...}` —
  switched all promotion test sites to designated init (commit
  `864d27f`); pattern propagated through Tasks 3-4.
- **Task 3:** clangd unused-include flag for `Phrase.h` (commit
  `aa3e647`); two subtle invariants worth documenting in code (post-
  clamp boundary correctness + loop-default-name intent — commit
  `bc2b4f3`).
- **Task 4:** code reviewer recommended designated initializers for
  `PromotionResult` returns + asserting tape-time round-trip in mint
  test (commit `49dc6ed`).
- **Task 7:** `announceCapture` was throwing away `HostHit::hostName`
  and recomputing it via tree walk — surfaced through `PromotionResult`
  as a new `hostPhraseName` field, dropped the recomputation (commit
  `aa96b1e`).
- **Task 8:** code reviewer caught a real reachable bug — `onRedo`
  didn't clear `AwaitingOut` on redo of a promotion entry, so
  `Undo + Redo + MarkOut` would create a duplicate Loop. Fixed
  symmetrically (commit `5cafe4e`).

### Key architectural decisions made / preserved this session

| Decision | Rationale |
|---|---|
| `core/sirius::promotion` is a pure-function module mirroring `core/sirius::arrangement`. | Promotion is musical logic, not GUI; lives in `core/`, JUCE-free, fully unit-testable, composes with the rest of the immutable-Constituent pattern. |
| `promote()` takes an `IdAllocator = std::function<ConstituentId()>` callback rather than two id parameters. | `ConstituentId` is project-wide (not per-type), and the host-Phrase case allocates one id while the mint case allocates two. Callback boundary lets the function call as needed without burning an id. |
| Multi-instance write-protect is a runtime throw, not a compile-time constraint. | The data model is dynamically permissive (`Constituent` can carry any combination of metadata); the convention is enforced at the gateway. The throw points at the `todo.md` entry by name so the failure mode is recoverable through documented work. |
| Undo restoration of capture state is achieved by extending `UndoStack::Entry` with an optional `CaptureRestorePoint`, not by tracking parallel state. | The `UndoStack` already manages per-entry data (root + label); adding restore-point keeps capture-state lifetime co-located with the entry it belongs to (eviction at depth-cap is automatic). |
| `MainComponent::onUndo` reads `currentEntryRestorePoint()` *before* `undo()`; `onRedo` reads it *after* `redo()`. | Undo wants to restore the state that existed *before* the entry being left. Redo wants to clear to the state that existed *after* the entry being landed on. Asymmetry is correct and documented in code comments. |
| The `M3 simplification` (1:1 conceptual ↔ LMC) is documented twice: in `Promotion.h` doc-comment and in `Promotion.cpp` body comment. | When real tempo maps land later, the boundary computation in `promote()` will need a `TempoMap::applyInverse` — the comments make this an unmissable known limitation. |
| The DemoSession intro/outro Phrase-AND-Loop hybrids are deferred, not retrofitted. | Restructuring would touch view-state snapshot tests and would be cleaner as a focused follow-up commit than as a tail addition to the capture-promotion session. Tracked in `todo.md`. |

## Current test / build state

**233 tests pass, 4124 assertions.** Clean rebuild verified.

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                            # 233 / 233
./build/tests/SiriusTests "[promotion]"              # 7 / 7 promotion test cases
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Promotion test cases:
- `[promotion][guard]` — multi-instance write-protect throws.
- `[promotion][host]` — Loop becomes child of host Phrase.
- `[promotion][mint]` (×2) — empty root mints Phrase + Loop; gap between Phrases mints a fresh Phrase.
- `[promotion][straddle]` — Loop bounds clamp to host; TapeReference times unclamped.
- `[promotion][defensive]` — zero-duration / reversed-bounds throws.
- `[undo][promotion]` — `UndoStack` round-trips `CaptureRestorePoint`.

## Milestone status

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | unchanged: done |
| M2 — real-time foundation, membrane, ASRC | unchanged: headless half done; operator owes device wiring + loopback calibration + in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **substantially advanced**: capture promotion is the M3 capture flow's actual completion. Captures are now persistent in the Constituent tree, not ephemeral RAM. Undo is non-destructive of capture state. CaptureBanner is glanceable + tappable. |
| M4 — persistence + capability tiers + overload protection | unchanged: done within current single-file scope; §7.8 directory format still deferred |
| M5 — plugin hosting + parameter view | unchanged from prior session: operator-reported scanner crash + scan-strategy redesign deferred to its own session |
| M6 — video | unchanged |
| M7 — full UI | **advanced**: CaptureBanner is now interactive (tap-to-undo); promotion makes captures Pills on the timeline immediately |
| M8 — ensemble (incl. multi-tape capture) | unchanged: gated on the shared-placement architecture |

## Suggested first move next session

### 0. Operator-side end-to-end verification (controller-coordinated, ~15 min)

Per `CLAUDE.md`, no agent ever launches the .app. The operator runs:

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests       # confirm 233 / 233
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

In the app:

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
5. **Redo:** After #3 or #4, hit Redo. The Pill returns. Hit Mark Out
   — verify it does NOT mint a duplicate Loop (this is the bug the
   `5cafe4e` symmetric `onRedo` fix prevents).
6. **Crashes:** check `~/Library/Logs/DiagnosticReports/Sirius Looper-*.crash`
   after the session. Should be empty (no crashes).

If anything fails: report back; the issue is likely subtle (e.g.,
banner z-order, click intercepts, or a `refresh*` call missing).
None of these are in unit-test reach.

### 1. Pick a `todo.md` item

Two reasonable next-session topics, both concrete and well-scoped:

- **DemoSession Phrase-vs-Loop reconciliation** (small, ~1 hr): split
  intro/outro into Phrase shells with single Loop children. May touch
  a few view-state snapshot tests. The reward is that the demo data
  embodies the convention the user guide teaches, and `findHostRecursive`
  could optionally tighten the host check to `isPhrase() && !tapeReference()`.
- **Shared-placement-with-per-instance-overlays architecture** (large,
  multi-session): the brainstorm-deferred big topic. The `todo.md`
  entry has the six steps to settle. Probably the right next-big-thing
  per the brainstorm flow's "defer big design topics to own session"
  pattern.

### 2. Outside-of-this-session deferrals (status only)

- **Plugin scanner crash + redesign:** still waiting on a crash report
  from `~/Library/Logs/DiagnosticReports/`.
- **OTTO L&F integration:** still its own dedicated session; first
  question is module-home (4 options ready in `todo.md`).
- **macOS Load-dialog TCC bug:** unchanged.
- **Session-as-directory refactor (V2 §7.8):** still gated on the
  Load-dialog bug.

## Open questions (carry-forward)

- **Where promoted Loop Constituents attach** — *closed* this session
  (playhead-at-Mark-In rules; host wins, mint at root if no host).
- **Standalone Loops on the timeline** — *closed* this session (the
  convention forbids them; every Loop has a Phrase parent).
- **Performer-side role-fillable phrase UX** — engine ships; runtime
  UX still on the suggested-features list.
- **Multiple grammatical links per Pill** — open.
- **M6 video format strategy** — unchanged.
- **M8 transport choice** — unchanged.

## Authoritative references for the next session

- This file (`continue.md`).
- `todo.md` — DemoSession reconciliation (new, top), shared-placement
  architecture (new, second), OTTO L&F, plugin scanner crash, Load
  dialog, session directory format, etc.
- `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` —
  the design spec.
- `docs/superpowers/plans/2026-05-15-capture-promotion.md` — the
  implementation plan (tasks now all completed).
- `docs/Sirius Looper User Guide.md` — operator-facing chapter 1
  (Capturing Phrases and Loops). The whole user-guide doc is new
  this session.
- `docs/Sirius Looper Whitepaper V2.md` §6 (tapes), §8 (phrases),
  §14 (the performer's instrument), Appendix E (Reaper terminology
  map). Note: whitepapers + license docs moved into `docs/` this
  session.
- Project memory:
  - `feedback_clean_builds.md`
  - `feedback_arm_disarm_is_required.md`
  - `feedback_defer_big_design_to_own_session.md`
  - `project_sirius_branding_and_otto.md`
  - `project_user_guide_alongside_whitepaper.md` (new this session;
    documents the user-guide-vs-whitepaper split convention)

## Commands to restore working state next session

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests
# expect: 233 / 233 pass, 4124 assertions
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```
