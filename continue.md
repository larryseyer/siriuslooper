# Session Continuation — TAPECOLOR "overload" was a stale-build GHOST; J-A A/B experiment queued (2026-05-30)

## ▶ 0. TL;DR (read first)

This session chased a TAPECOLOR CPU "overload" that **turned out not to exist** on a clean build, and
**queued the experiment the operator actually wants next: Jiles-Atherton vs. asinh, by ear.**

- The prior handoff's "PRIORITY: a single TAPECOLOR instance overloads the CPU" was a **stale
  incremental-build artifact.** On a clean-built binary, IDA's load meter reads **flat ~23% at every
  TAPECOLOR quality tier** (disabled 24.4% / Eco 22.4% / HiQ 24.0%, shed:0, ~77% headroom). HiQ
  confirmed clean by ear. There is **nothing to fix** in TAPECOLOR cost.
- All the Eco-override code built mid-session was **reverted — net-zero code change.** OTTO's normal
  default (the quality tier you pick) is restored. Working tree clean except the pre-existing
  `m external/sfizz`.
- **NEXT CHAT'S JOB (operator's explicit ask): the Jiles-Atherton A/B experiment.** Spec is written at
  `docs/superpowers/specs/2026-05-30-jiles-atherton-ab-experiment.md`. The operator wants to *hear* the
  difference between a (correctly-normalized) J-A solver and the current asinh waveshaper, and decide.

This session produced **docs only** (this file, `todo.md`, the J-A spec) — committed + pushed.

---

## ▶ 1. State at end of session

| Thing | Value | Note |
|---|---|---|
| IDA working tree | clean except `m external/sfizz` | pre-existing 64-ch patch; leave alone |
| IDA code change this session | **none** (reverted) | Eco-override work discarded after the ghost finding |
| OTTO submodule | pinned `0e61a69f` (unchanged) | origin/main moved to `23a941cd` = **docs-only** (OTTO's own continue.md); pin NOT bumped (optional, harmless) |
| Nothing committed/pushed to OTTO | — | the mid-session override edits were discarded, never committed |
| Inbox | one `needs-ack` `[FROM IDA → OTTO]` EventBus brief (pending OTTO's impl) + one informational OTTO→IDA (acted-on) | no IDA action outstanding |
| Running app | freshly clean-built normal-default `IDA.app`, relaunched | |

---

## ▶ 2. What happened, in order

1. Started to land an "Eco-tier override" to fix the reported TAPECOLOR overload (built it OTTO-side +
   IDA-side + tests, all green). Operator verified TAPECOLOR enabled was clean.
2. Operator asked the right questions: an M4 Max should not choke on one tape emulation; should we even
   offer non-Eco? Dug into the actual DSP and found the **cost model in every handoff doc was stale**:
   - **No iterative Jiles-Atherton solver** — replaced 2026-05-25 by a static `asinh` waveshaper.
   - **No convolution IR** — retired Phase 9 ("fully algorithmic").
   - The only quality-dependent cost is `juce::dsp::Oversampling` wrapping a cheap `asinh`.
3. Measured with IDA's `OverloadProtection` load meter (Preparation pane "Load: X%"): **flat ~23% at
   disabled/Eco/HiQ.** No overload at any tier. The earlier crackle was a **stale-build ghost** (same
   class as a play-button-dead-on-launch the operator also hit and relaunch-fixed).
4. Operator decided: drop the override, restore OTTO's normal default. **Reverted everything (net-zero).**
5. Discussed the J-A question. The 2026-05-25 failure cause is **known + fixable** (un-normalized
   small-signal gain `c·Ms/(3a)` ≈ −13 dB → dull+distorted). Operator wants to hear J-A done right.
   Wrote the experiment spec; queued for a new chat.

---

## ▶ 3. NEXT CHAT — primary task: Jiles-Atherton A/B experiment

Read `docs/superpowers/specs/2026-05-30-jiles-atherton-ab-experiment.md` and execute it.
- Implement a correct J-A NR4 solver as a **switchable alternative** to asinh in `HysteresisProcessor`
  (OTTO-core), with **unity small-signal gain** (the fix for the 2026-05-25 failure), a **runtime A/B
  toggle**, and **loudness-matching** so the operator can blind-compare on real material.
- CPU is not a constraint (tape measured ~0% on desktop).
- It's OTTO-core DSP → cross-project: coordinate via `external/OTTO/CROSS_PROJECT_INBOX.md`
  (`Ida-Origin:` trailer + pin bump if IDA's terminal does it). OTTO's terminal owns this DSP line.
- `superpowers:brainstorming` the per-tape coefficients + normalization point first — that's where the
  last attempt failed. Operator's ears are the sole acceptance criterion.

---

## ▶ 4. Other carry-forwards (in todo.md)

- **S6 T8** — the OTTO output-strip DEST picker save→quit→reload round-trip was never operator-completed
  (S6 T4–T7 landed + reviewed last session; DEST picker works). Run the round-trip to close S6.
- **Codify the two-terminal coordination protocol in IDA's `CLAUDE.md`** (still open from the prior
  session §5 near-miss). Quick.
- **~22% idle audio-thread load** — profiling/optimization slice (suspects: per-channel LUFS metering,
  silent-channel processing, sfizz idle). Profile before optimizing; matters for iOS, not desktop.
- **Intermittent play-button-dead-on-launch** — startup race; not reproducible on demand; low priority.
- **OTTO `WetChain.cpp` stale comments** (J-A/trapezoidal/convolution describe deleted code) — OTTO-side
  comment cleanup.
- Older entries (INS on output channels, full output solo, MIDI/Video file-input, etc.) unchanged.

---

## ▶ 5. Hard-won lesson (reinforced)

The reported "TAPECOLOR overloads the CPU" was **fiction produced by an incremental build.** Always
clean-rebuild (`rm -rf build`) before trusting any operator-observed audio behavior — incremental builds
silently link a Frankenstein of object files across header changes. The canary: if crackle EVER returns
on a *freshly clean-built* binary, that's a new real signal worth chasing.

---

*End of session. Net code change: zero (investigated a ghost, reverted cleanly). Deliverable: the J-A
A/B experiment spec + corrected cost model in todo.md. Next chat: build the J-A experiment so the
operator can hear it. Working tree clean (except pre-existing external/sfizz); OTTO pin 0e61a69f.*
