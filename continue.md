# Session Continuation — 2026-05-31 (NEXT CHAT: execute the blank-slate first-run plan, Slice 1)

## What this session produced (design + plan complete; whitepaper amended)

The "how do we truly test IDA" thread became a full **design + implementation plan**
for the **Blank-Slate First-Run + Phrase Creation** flow, registered as **Diversion 2**
in the long-range V7 roadmap. All committed and pushed; nothing from the design chat was lost.

- **Approved design (the "why") — read first:**
  `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md`
  (Status: approved.)
- **Whitepaper `docs/IDA_Whitepaper_V10.md` AMENDED this session:** "always-running tape"
  scoped to "running while assigned" at **§8.1** + nine anchors (resource-aware capture).
  The doc-update plan (`...2026-05-30-whitepaper-spec-doc-update.md`) is therefore essentially
  executed (whitepaper done, spec approved, looper-invariant memory annotated overturned).
- **Implementation roadmap (8 dependency-ordered slices) — THIS is what to execute next:**
  `docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md`
  Per-slice detailed plans: `docs/superpowers/plans/2026-05-30-slice-{2..8}-*.md`
  (Slice 1 is detailed inline in the roadmap). **Start at Slice 1.** Execute via
  `superpowers:subagent-driven-development`.
- **Long-range plan:** the diversion is registered at the top of
  `docs/superpowers/plans/2026-05-17-v7-alignment.md` ("⚠ Active resequencing"), with the
  return point (resume Part VI mixer/GUI → engine **M8 S7+**).

## The model in one breath

Blank boot (no demo) → create a channel + pick its physical input → its hidden tape records
(**a tape records iff ≥1 input is assigned; the LMC always runs**) → **Record** (per-phrase
state machine: coinit births phrase + loop 0 together) → the phrase appears on a **phrase-button
bank** (`< 1 2 3 4 5 6 7 8 >`, below the beat counter; rec=red / play=green / overdub=amber) +
an Output Mixer strip, and **plays all its loops** → undo/redo. Tapes are **project-scoped**
(`yyyymmddhhmmss-<name>/tape_x`), **irreplaceable, never orphaned**; deletion only behind a
deliberate warning. MIDI / single-pedal (Quantiloop) control is future via a **source-agnostic
command layer**. Demo song boot is retired.

## Cross-slice findings to heed before coding (from the parallel detailed-planning pass)

- **Slice 6 is a correctness fix, not an enhancement:** the phrase-playback map is keyed by
  pill id but `PlaybackResolver` uses the leaf-loop id → the current single-loop path likely
  doesn't sound for any real phrase. Re-key by loop id + sum loops in
  `AudioCallback::renderPlaybackStep` (no OutputMixer/Bus change; one channel per phrase kept).
- **Slice 4 is mostly removal:** "records iff assigned" is already structural; the work is
  killing the looper-floor guard + unpinning `TapeId{1}` (real pin = the `MixerGraph` front-Tape
  terminal, not just the `removeTape` guard).
- **`tape-<id>.idatape` filename is hardcoded in 3 sites** (`audio/src/TapeRecordWriter.cpp:104`,
  `app/MainComponent.cpp:6059` & `:6081`): Slice 2 builds the convergence; Slices 3/4 rewire;
  **Slice 7's Reveal must target the writer's path** or it opens an empty folder. `.idatape`
  extension kept.
- **`primary()`-optional ripple** also hits `MainComponent.cpp:8265/8286/~8861` + `mirrorTapePool`.
- **Transport:** `TransportBarHost::playPauseClicked()` *toggles* — record-while-stopped must call
  `OttoHost::play()` unconditionally (Slice 5, via an injected `ITransportControl`).
- **Slice 8 defines the Slice 5 seam** (`IPhraseStateSource` + `IPhraseCommandSink` in `core/`);
  reuses `app/StripContextOverlay.h` (right-click + 500 ms long-press + rename) and
  `selectTimelineView().pills`.
- **OPEN — operator sign-off:** channel add/remove undo has no natural `UndoStack` lane
  (tree-shaped), spec §15.2 defaulted "yes" — decide before Slice 4.

## Repo state (verified)

- Commits this session, all pushed: spec `d5960c0`; plans `976b471`; whitepaper `c5a1f88`;
  this handoff (HEAD). origin/master in sync.
- Clean tree except `external/sfizz` (pre-existing untracked — leave it).
- Memory `project_looper_at_least_one_tape_invariant` annotated **OVERTURNED** (the ≥1-tape
  floor is removed by this work — do NOT implement floor enforcement).
- No code changed yet — this was design + planning. First operator-testable result arrives
  after roughly Slices 1–6 (record → phrase → playback).

## Commands to run first (next chat)

```bash
cd /Users/larryseyer/IDA
git log --oneline -6
sed -n '1,80p' docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md  # goal + cross-slice findings
```
Then begin **Slice 1** (TapePool empty + optional primary) via `superpowers:subagent-driven-development`.
