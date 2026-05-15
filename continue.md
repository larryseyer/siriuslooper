# Session Continuation — 2026-05-15 (afternoon)

## Top-of-page summary

Sirius Looper is a JUCE 8.x project built milestone by milestone against the
approved plan at
`/Users/larryseyer/.claude/plans/we-have-written-a-declarative-pearl.md`.

This session shipped the **role-fillable phrase resolution** subsystem and
the **performer-facing capture state machine** (arm/disarm + mark-in /
mark-out) with full UI wiring, then closed out the plan's "Remaining open
item" by completing the Whitepaper V2 worked-example consistency check.
**213 tests pass; the build is warning-free for any source we control.**

Three items are deferred to `todo.md` with full diagnostic detail:

1. The macOS Load-dialog TCC bug (still unresolved — see below).
2. The capture flow's visual-confirmation gap and region-promotion-to-
   Constituent step (added this session).
3. Session-as-directory format from Whitepaper V2 §7.8 (M4 expansion).

## ⚠ Open bug carried into the next session

**Load dialog still cannot select `.sirius.json` files on macOS.** Even
with the four `NS*FolderUsageDescription` keys in the Info.plist (commit
`ff6935c`), the macOS NSOpenPanel greys out the saved file. The working
hypothesis is that the ad-hoc-signed bundle is below macOS's trust
threshold to receive a TCC permission prompt, so the plist keys are
present but inert — no TCC record ever gets created. Save side works.

The diagnostic ladder, every prior attempt, and the three concrete
next-step options (Developer ID signing + entitlements, investigate the
`.png`-vs-`.json` asymmetry, or ship a drag-and-drop fallback path) live
in `todo.md` under the **"2026-05-15 — Load dialog still cannot select
`.sirius.json` on macOS"** entry. Start there.

## What shipped this session (commits past the deferred bug)

| Commit | Subject |
|---|---|
| `1713345` | feat: role-fillable phrase resolution — `findCandidatesFor` + `resolveFirst` |
| `17915c4` | feat: CaptureSession state machine + Arm/Disarm button in bottom bar |
| `2499273` | feat: Mark In / Mark Out buttons drive the capture session from the playhead |
| `839bc73` | chore: defer capture-completion visual feedback + region promotion to `todo.md` |
| `6d121d7` | chore: complete V2 worked-example consistency check; record §7.8 directory-format divergence |

### Role-fillable phrase resolution (M3 follow-on)

`core/include/sirius/RoleResolver.h` exposes two pure functions over
`Constituent::ChildPtr` pools:

- `findCandidatesFor(slot, pool)` — returns every `ConstituentId` in
  `pool` whose Constituent carries `PhraseMetadata`, is marked
  `isRoleFillable`, and matches `slot.role()`. Stable order (pool's
  enumeration order). Engine deliberately enumerates rather than picks
  — Whitepaper 8.4 reserves the actual choice for the performer.
- `resolveFirst(slot, pool)` — convenience: returns `slot` filled with
  the first eligible candidate, or unchanged if none. The trivial
  default policy.

9 tests, 24 assertions. Pool order, role mismatch, `isRoleFillable=false`,
non-phrase Constituents, null entries, and the no-match fallback are
all pinned down.

### Capture state machine + UI (white paper 14.5 / 14.6)

`core/include/sirius/CaptureSession.h` is a pure state machine, JUCE-free.

States: `Disarmed` (default), `Armed`, `AwaitingOut`. Events: `arm()`,
`disarm()`, `markIn(Rational)`, `markOut(Rational) → optional<CaptureRegion>`,
`cancel()`. `CaptureRegion = { Rational inLmcSeconds, outLmcSeconds }`.
Default-constructed state is `Disarmed` — nothing is captured by
surprise. 13 tests, 63 assertions; every transition (including
no-ops, in-point replacement, and the t<=in rejection in markOut)
pinned down.

UI wiring in `app/MainComponent.cpp`:

- Bottom bar (visible across all four tabs): **Arm | Mark In | Mark Out |
  Undo | Redo | playhead | time**.
- **Arm** flips red/grey, label flips Arm/Disarm.
- **Mark In** enabled iff `isArmed()`. Multiple presses replace the
  pending in-point.
- **Mark Out** enabled iff `isCapturing()`. Successful close pushes the
  `CaptureRegion` into `MainComponent::capturedRegions_`.
- Preparation-tab diagnostics surface state textually:
  `Capture: armed, no in-point set    Regions: 2  (last: 1.20 s → 4.50 s, 3.30 s long)`.

**LMC time source:** the playhead value, until M2 audio wiring lands.
Documented in `onMarkIn` / `onMarkOut` comments.

**What's not yet wired:** when M2 wires the real audio path, the
inbound membrane will gate writes to tape on `captureSession_.isArmed()`
and `markIn`/`markOut` will receive real LMC times from the clock —
not the playhead. A `CaptureRegion` will then become an undoable edit
that adds a Loop Constituent to the current tree. Today the regions
are RAM-only visualization. See `todo.md` "Mark Out should announce
the new region visibly" — that entry has the promotion plan.

### Whitepaper V2 Appendix C consistency check (plan's Remaining open item)

The plan's "Remaining open item" was to verify V2 worked examples
(C.1 twelve-bar blues, C.2 4-against-7 polymetric phrase) against
the M1 `Constituent` / `Tape` struct definitions. **Check is
complete: every field used in C.1 and C.2 is representable in the
current structs.** Detailed mapping is in `todo.md`. One real
divergence found and recorded: V2 §7.8 specifies a session as a
directory (`my-session.sirius/` with `session.json`, `tapes/`,
`calibration/`, `lmc-discipline.json`); code writes a single
`session.sirius.json`. Refactor is described in `todo.md` and is
gated on first resolving the Load dialog TCC bug.

## Current Test / Build State

**213 tests pass, 3983 assertions.** Zero compiler warnings from any
source we control. Clean builds throughout this session — incremental
builds were proven unreliable for this project (clangd / LaunchServices
caching can mask real changes), and the rule is now memorialised in
[user memory](file:///Users/larryseyer/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/feedback_clean_builds.md).

Run them yourself:

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                           # 213/213
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

## Milestone status (changes from the prior session)

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | unchanged: operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | unchanged: done; V2 consistency check now also done |
| M2 — real-time foundation, membrane, ASRC | unchanged: headless half done; operator owes device wiring, loopback calibration, in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | **expanded**: role-fillable resolution shipped (RoleResolver) |
| M4 — persistence + capability tiers + overload protection | unchanged: done within current single-file scope; V2 §7.8 directory format deferred |
| M5 — plugin hosting + parameter view | unchanged: operator owes real-plugin scan + automation round-trip |
| M6 — video | unchanged: data model + membrane done; operator owes the full FFmpeg pipeline |
| M7 — full UI | **expanded**: arm/disarm + mark-in/out gestures shipped; gesture-loop wiring and real latency measurement still operator-side |
| M8 — ensemble | unchanged: data model done; operator owes the real network transport and two-node test |
| App wiring | **expanded**: bottom bar now drives the capture session; Load dialog filter bug still pending |

## What's Inside Each Library (additions this session)

```
core/         Added: RoleResolver (findCandidatesFor + resolveFirst),
              CaptureSession (Disarmed/Armed/AwaitingOut state machine
              producing CaptureRegion on a successful markIn/markOut
              pair). Both JUCE-free; both purely unit-tested.
app/          MainComponent now owns a CaptureSession and a vector of
              captured regions; bottom bar exposes Arm / Mark In /
              Mark Out alongside the existing Undo / Redo and playhead.
tests/        Added: RoleResolverTests.cpp (9 cases),
              CaptureSessionTests.cpp (13 cases).
```

The other libraries are unchanged from the prior continuation.

## The Standalone App Today

Four tabs as before (Performance / Preparation / Plugins / Video) and
the bottom control bar with the new capture-related buttons:

```
[ Arm | Mark In | Mark Out ] [ Undo | Redo ] [ ============= playhead ============= ] [ time ]
```

Performance tab — unchanged from prior continuation; PerformanceView
centered, updates as you drag the playhead.

Preparation tab — Save / Load / Reload-demo buttons + status label up
top; PreparationView in the middle; the diagnostics block at the
bottom is now four lines:

```
Tier: Survival  (..., ring 5s)
UI tick jitter: mean 0.42 ms, worst 1.78 ms, 100.0% within 30 ms
Undo: 1 / 1
Capture: disarmed    Regions: 0
```

Plugins / Video tabs unchanged.

## Suggested First Move Next Session

1. **The Load dialog TCC bug remains the top blocker for save/load
   ergonomics.** See `todo.md` for the three concrete next-step options
   — Developer ID + entitlements is the most likely path; the asymmetry
   investigation (why `.png` was selectable while `.json` and `.md` were
   not, in the same dialog) is the quickest diagnostic if you'd rather
   not introduce signing yet.

2. After that, the priority deferral is **promotion of a CaptureRegion
   into a Loop Constituent** (the third subitem of the "Mark Out should
   announce the new region visibly" entry in `todo.md`). That makes
   captures actually useful — currently a `Mark Out` produces a region
   that evaporates on app exit. Promotion would push an undoable edit
   onto the undo stack, attach the new Loop into the active phrase, and
   would close the capture loop end-to-end. The visual-confirmation
   work is subordinate to it.

3. Then the **session-as-directory refactor** from V2 §7.8. Gated on the
   Load-dialog bug being resolved first — that work touches the same
   Save/Load code path.

## Open Questions (carry-forward)

- **M6 video format strategy** — unchanged. Custom video tape format +
  intra-frame codec choice; best decided after the FFmpeg spike.
- **M8 transport choice** — unchanged. Plan deliberately does not
  commit. OSC over UDP and Ableton Link's discovery layer are still
  the two plausible candidates.
- **Capture-region promotion UX** — *new* this session. When a region
  is promoted to a Loop Constituent, where does it attach? Into the
  currently-focused Constituent? Into a "captures" inbox? Into the
  root? White paper does not specify; this is a UX-flow decision the
  operator will want input on.

## Key Decisions Made This Session

| Decision | Rationale |
|----------|-----------|
| Role resolution returns the eligible *set*, with `resolveFirst` as a trivial default policy | White paper 8.4 — the performer picks, not the engine. |
| CaptureSession defaults to `Disarmed` | Safe-by-default; nothing captured by surprise. User feedback: "User MUST be able to arm and disarm." |
| Arm button lives in the bottom bar, not a tab | Always reachable (14.6). |
| Mark In / Mark Out are siblings of Arm, not modal alternatives | Same reasoning — coarse, decisive, always reachable; the buttons grey out when not valid for the current state. |
| Playhead value used as LMC stand-in for the markIn/markOut buttons | The app has no separate LMC clock until M2 wires real audio; the playhead is the only Rational-seconds source. Documented in code. |
| `CaptureSession::markOut` rejects t <= in | Likely accidental tap; let the performer try again with a valid out, don't silently close a zero-or-negative-length region. |
| `disarm` from `AwaitingOut` discards the pending in-point | Hard stand-down. If the performer wants to keep the in-point but pause, they should `cancel` (returns to Armed) instead. |
| V2 §7.8 directory format documented in `todo.md`, not refactored now | Stacking that refactor on top of the unresolved Load-dialog TCC bug increases the unknown surface. Sequence them: bug first, then refactor. |
