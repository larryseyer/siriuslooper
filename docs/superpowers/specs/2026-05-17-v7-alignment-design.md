# V7 alignment — Design

**Date:** 2026-05-17
**Scope:** Full alignment of the IDA codebase to the V7 white paper (`docs/IDA_Whitepaper_V8.md`), the V2→V7 transition guide (`docs/sirius-looper-v2-to-v7-transition.md`), and the V3-V7 architectural commitments those documents carry.
**Status:** Brainstorm complete; spec + plan integrated into a single artifact at `docs/superpowers/plans/2026-05-17-v7-alignment.md`. **Awaiting M1 execution start in a new chat.**

## Why this is a single artifact instead of the conventional spec+plan pair

The shared-placement design (`2026-05-16-shared-placement-design.md` + `2026-05-16-shared-placement.md`) is a single-feature pair where spec and plan have minimal cross-reference. The V7 alignment is different: 24 milestones across 11 parts, with heavy forward-and-backward references between architectural decisions and per-milestone file lists. Splitting the document risks drift between the design rationale and the execution detail. Keeping them in one file in `plans/` with this pointer stub in `specs/` preserves the paired-file convention without doubling maintenance.

## The plan file is canonical

Every section that would normally live in a `-design.md` spec — context, architectural decisions, current-state inventory, coverage matrix mapping every V3-V7 transition-guide item to a milestone, parked decisions, execution doctrine — is in the plan file. Do not duplicate any of that content here; update the plan file when the design evolves.

## One-paragraph summary

The codebase is V2-era in naming but pre-V2 in substance: the mature parts (Constituent / RenderPipeline / Promotion / Tape template / TapeStore / SessionFormat JSON / view-state classes) carry forward into V7 unchanged, but there is no audio device callback, no MIDI, no Mixer class, no Direct Layer, no buffer flow through any membrane, no plug-in hosting, and `Tape<T>` is never instantiated outside tests. V7 alignment is therefore mostly **additive**, not refactor. The plan organizes the work into 24 milestones across 11 parts (A-K), with each milestone carrying explicit `Execution mode` (`orchestrator+subagents` by default; `ralph inner loop after PRD` for the four milestones whose internal shape genuinely fits ralph's iteration model: M13 file I/O, M19 validation harness, M22 UI vocabulary cleanup, M24 docs alignment). Macros: foundation first (M1 audio I/O before any architectural skeleton); out-of-process plug-in hosting from the first plug-in instance (M7); solo-first, ensemble last; clean break to SAF in M11 (no parallel-format era); TDD per milestone; macOS standalone → iOS AUv3 → Windows → Linux strict order; UI vocabulary cleanup last so it doesn't churn while the engine model moves.

## Pointer

→ **Full content: `docs/superpowers/plans/2026-05-17-v7-alignment.md`**
