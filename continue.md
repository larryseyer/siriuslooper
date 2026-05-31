# Session Continuation — 2026-05-31 (NEXT: START IMPLEMENTING — Slice 1)

> **Master status dashboard:** `docs/superpowers/plans/STATUS.md` — the single checklist of every
> milestone + slice (what's done, what's next). Read it with this file at session start. To advance:
> "proceed with the next item in the master plan." Bookkeeping rules: `CLAUDE.md` → *Master-plan
> bookkeeping*. (Finished plans now live in `docs/superpowers/plans/archive/`.)

## Start here — no more design, execute

Design + planning for the **Blank-Slate First-Run + Phrase Creation** diversion is
**complete, approved, committed, and pushed**; the whitepaper is amended. **Next action:
implement, beginning at Slice 1**, via `superpowers:subagent-driven-development`, against:

- **Roadmap (now 12 slices + cross-slice findings; Slice 1 detailed inline):**
  `docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md`
- **Per-slice detailed plans:** `docs/superpowers/plans/2026-05-30-slice-{2..8}-*.md`
- **Slices 9–12 (added 2026-05-31) — phrase ADD/OVER modes + top-bar mode toggle + live MIDI
  triggering:** `docs/superpowers/plans/2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md`.
  Mode is **per-loop, set from a global top-bar toggle (and MIDI), recorded at loop creation — a phrase
  may mix ADD and OVER loops.** ADD layers (own `T#P#L#` channel, loops-to-fill by default); OVER masks
  its span on playback and shares the phrase track (no channel). Companion **Collapse/Expand is deferred
  to after M13** (needs the §6.11 offline render-to-file path). Slice 1 is still the next action — these
  append after Slice 8 (no renumbering).
- **Approved design ("why"):**
  `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md`
- Registered as **Diversion 2** in the long-range roadmap
  `docs/superpowers/plans/2026-05-17-v7-alignment.md` (⚠ Active resequencing; return point =
  Part VI mixer/GUI → engine M8 S7+).

## Slice 1 (do this first) — TapePool: empty pool + optional primary

Headless TDD in `tests/TapePoolTests.cpp`. Make `TapePool` default-empty; `primary()` →
`std::optional<TapeId>` (nullopt when empty); `remove()` can empty the pool and **drops the
`TapeId{1}` pin**; explicit ctor + `SessionFormat` accept an empty pool. **Guard every
`primary()` caller** so the `IDA` target still links: `InputMixer`, `MainComponent` ctor
seeding + `refreshInputDestinations`, and (found during planning) `MainComponent.cpp:8265 /
8286 / ~8861` + `mirrorTapePool`. Full red→green→commit steps are inline in the roadmap.

## The locked model (one breath) — all decisions final

Blank boot (no demo) → create an **input channel** (labeled by stereo pair `1&2`, `3&4`, …)
and pick its physical input → its hidden tape records (**a tape records iff ≥1 input is
assigned**; the **LMC always runs**; tapes are **project-scoped** `yyyymmddhhmmss-<name>/tape_x`,
**irreplaceable, never orphaned**, deletion only behind a deliberate warning) → **Record**
(per-phrase state machine; coinit births phrase + loop 0 with identical bounds) → the phrase
appears on the **phrase-button bank** (`< 1 2 3 4 5 6 7 8 >` below the beat counter;
rec=red / play=green / overdub=amber; right-click = long-press → Clear/Copy/Paste/Rename/Assign
MIDI…) **and** on the **Output Mixer as one channel per loop** (`T<tape>P<phrase>L<loop>`)
summed at a **per-phrase bus → master**. **No output-channel cap** (dynamic/grow-on-demand
alloc); each output channel's physical-out **defaults to the monitor stereo pair**, assignable
to any pair. **Play = all loops.** Undo/redo throughout. MIDI / single-pedal (Quantiloop)
control is future via a **source-agnostic command layer**. Demo-song boot is retired.

## Cross-slice findings to heed (full detail in the roadmap)

- **Slice 6 = per-loop output channels (operator-locked) + a correctness fix.** One OutputMixer
  channel per loop, **keyed by leaf-loop id** (matches `PlaybackResolver` — fixes the
  pill-id-vs-loop-id keying bug by construction); loops sum at a **per-phrase bus**. **Remove
  `kMaxOutputChannels = 32`** → dynamic alloc (it backs RT pre-allocation — the real work);
  default each channel's physical-out to the monitor pair. Supersedes the old "one channel per
  phrase, sum in the callback" plan — re-derive Slice 6 against spec §8.6.
- **Slice 4 is mostly removal:** "records iff assigned" is already structural; kill the
  looper-floor guard + unpin `TapeId{1}` (real pin = `MixerGraph` front-Tape terminal).
- **`tape-<id>.idatape` filename hardcoded in 3 sites** (`audio/src/TapeRecordWriter.cpp:104`,
  `app/MainComponent.cpp:6059` & `:6081`): Slice 2 builds the convergence; Slices 3/4 rewire;
  Slice 7's Reveal must target the writer's path. `.idatape` extension kept.
- **Transport:** `TransportBarHost::playPauseClicked()` *toggles* — record-while-stopped must
  call `OttoHost::play()` unconditionally (Slice 5, via injected `ITransportControl`).
- **Slice 8 defines the Slice 5 seam** (`IPhraseStateSource` + `IPhraseCommandSink` in `core/`);
  reuses `app/StripContextOverlay.h` (right-click + 500 ms long-press + rename) +
  `selectTimelineView().pills`. Colours: rec→`error`, play→`success`, overdub→`warning`,
  stopped→dim `phraseColour`, empty→`transportInactive`.

## Open — needs operator sign-off (before Slice 4)

- **Channel Add/Remove undo** has no natural `UndoStack` lane (it is Constituent-tree-shaped,
  not channel-shaped); spec §15.2 defaulted "yes." Decide whether channel edits are undoable.

## This session (2026-05-31, bookkeeping pass — no code)

- Added **Slices 9–12** to the diversion roadmap (phrase ADD/OVER modes, top-bar toggle, live MIDI
  triggering) + a post-M13 Collapse/Expand item — see the standalone plan and the merge findings.
- Stood up the **master status dashboard** `docs/superpowers/plans/STATUS.md` (every M1–M24 + every
  active-diversion slice, with checkboxes) and a **Master-plan bookkeeping protocol** in `CLAUDE.md`
  ("proceed with the next item" + tick-box/register-sub-plan/refresh-continue as the definition of done).
- **Archived 40 completed plans** into `docs/superpowers/plans/archive/` (working set now 11 live plans).
- **Decomposed Diversion 1** (mixer/GUI) remaining work into real checkboxes in STATUS.md; recorded
  operator verifications: **master meter works**, **master spectrum display does NOT** (diagnose when
  Diversion 1 resumes).

## Repo state (verified)

- HEAD pushed, origin/master in sync. This session's commits: `4c7644e` (Slices 9–12 merge), `9ec12ba`
  (STATUS dashboard + bookkeeping + archive 40), `1c5ea42` (Diversion 1 decomposition), `039d99c`
  (spectrum-broken/master-meter-confirmed). Prior: `d5960c0` (spec), `976b471` (8 slice plans), `c5a1f88`
  (whitepaper always-running→assigned).
- Clean tree except `external/sfizz` (pre-existing untracked — leave it).
- **No production code changed yet** — still design + planning. **Next action is unchanged: Slice 1.**
  First operator-testable result arrives after roughly Slices 1–6 (record → phrase → playback).
- `docs/superpowers/plans/STATUS.md` is now the single source of truth for what's next — read it first.
- Memory `project_looper_at_least_one_tape_invariant` annotated **OVERTURNED** (the ≥1-tape
  floor is removed by this work — do NOT implement floor enforcement).

## Commands to run first (next chat)

```bash
cd /Users/larryseyer/IDA
git log --oneline -8
sed -n '1,140p' docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md  # goal, file map, findings, Slice 1
```
Then **begin Slice 1** (TapePool empty + optional primary) via `superpowers:subagent-driven-development`.
Clean `rm -rf build` before any operator GUI hand-off (per `[[feedback_clean_builds_only_for_testing]]`).

## Session kickoff prompts

Two drivers, same detail plans. Default to **attended**; use **unattended** only
for a batch of purely-headless, unambiguous tasks. Full rationale lives in the
"Master-plan bookkeeping" section of `CLAUDE.md` (context-is-volatile rule).

**Attended (paste at session start — master orchestrates, subagents implement):**

> Read `continue.md` and `docs/superpowers/plans/STATUS.md`. Take the first
> unchecked `[ ]` item (active diversions before the paused engine order). Execute
> it with `superpowers:subagent-driven-development`: dispatch ONE fresh subagent
> per task in its detail plan, TDD each, run the two-stage review (spec
> compliance, then code quality), and do NOT implement tasks in this session
> yourself — keep your context thin so it stays under ~35%. After each task: tick
> `STATUS.md`, refresh `continue.md`, commit + push. Stop and surface any blocker
> per the hard-stop rules instead of guessing. If the item has no detail sub-plan
> yet, write one with `superpowers:writing-plans`, register its link in
> `STATUS.md`, then execute.

**Unattended (operator runs in a separate terminal — never launched by Claude):**

> `./run_ralph.sh` — already wired to `prd.json` / `progress.txt` with watchdog,
> monitor, and the 45-min / $35-per-iter / stagnation halt gates. Confirm the next
> slice's tasks are headless + unambiguous before launching; ralph halts on its
> own gates and on `<promise>HALTED</promise>`.
