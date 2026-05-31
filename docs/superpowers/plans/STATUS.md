# IDA — Master Status Dashboard

> **Single source of truth for what's done and what's next. Open this file first.**
> To advance the project, tell Claude: **"Proceed with the next item in the master plan."**
> Detail lives in the linked sub-plans; finished plans are in `archive/`.
> The rules that keep this file current are in `CLAUDE.md` → *Master-plan bookkeeping*.
> **Legend:** `[x]` done · `[~]` in progress · `[ ]` not started.

## The next item

Active diversions run **ahead of** the paused engine order. The single next action is:

- **Diversion 2 · Slice 2 — IDA Project unit + project-scoped storage**
  ([detail plan](2026-05-30-slice-2-ida-project-and-storage.md)). First operator-testable
  result arrives after roughly Slices 1–6.

Return point after Diversion 2 completes: resume **Diversion 1** (mixer/GUI), then the engine order
at **M8 S7+**.

---

## Active diversions (run before resuming the engine order)

### Diversion 2 — Blank-Slate First-Run + Phrase Creation  ·  *implementation in progress (Slice 1 done)*
Roadmap: [`2026-05-30-blank-slate-first-run-implementation.md`](2026-05-30-blank-slate-first-run-implementation.md) ·
Spec: `../specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` ·
Doc-update: [`2026-05-30-whitepaper-spec-doc-update.md`](2026-05-30-whitepaper-spec-doc-update.md)

- [x] **Slice 1** — TapePool: empty pool + optional primary  · *detailed inline in the roadmap* · landed `66ca9c1`
- [ ] **Slice 2** — IDA Project unit + project-scoped storage  · [slice-2](2026-05-30-slice-2-ida-project-and-storage.md)
- [ ] **Slice 3** — Blank-slate boot + New Song  · [slice-3](2026-05-30-slice-3-blank-slate-and-new-song.md)
- [ ] **Slice 4** — Channel creation + assignment-gated recording  · [slice-4](2026-05-30-slice-4-channel-creation-and-arm-gating.md)
- [ ] **Slice 5** — Per-phrase state machine + command layer + transport  · [slice-5](2026-05-30-slice-5-phrase-loop-state-machine.md)
- [ ] **Slice 6** — Play all loops of a phrase (per-loop `T#P#L#` channels → per-phrase bus)  · [slice-6](2026-05-30-slice-6-play-all-loops.md)
- [ ] **Slice 7** — Tapes tab archive + reveal  · [slice-7](2026-05-30-slice-7-tapes-tab-archive.md)
- [ ] **Slice 8** — Phrase-trigger button bank  · [slice-8](2026-05-30-slice-8-phrase-button-bank.md)
- [ ] **Slice 9** — Phrase-mode data model (ADD/OVER per loop) + global settings  · [phrase-modes](2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md)
- [ ] **Slice 10** — ADD/OVER playback resolution + Output-Mixer track layout  · [phrase-modes](2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md)
- [ ] **Slice 11** — Top-bar ADD/OVER toggle UI  · [phrase-modes](2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md)
- [ ] **Slice 12** — Per-phrase MIDI trigger + live MIDI input (channel-9 control)  · [phrase-modes](2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md)
- [ ] **(after M13) Collapse / Expand a phrase** — deferred; consumes the M13 render-to-file path  · [phrase-modes](2026-05-31-phrase-modes-collapse-mode-ui-midi-trigger.md)

### Diversion 1 — White-paper Part VI mixer + GUI (OTTO integration)  ·  *in progress*
Tracked in `../../../continue.md` (Current focus) + `docs/design/mixer-design.md`. Completed slice-plans
are in `archive/` (OTTO embed, pane, audio pump, transport bar, strip routing/persistence; mixer-routing
graph phases; bus controls; MON strips; file input).

**Landed:**
- [x] OTTO embed + pane + audio pump + master meter/transport bar + output-strip DEST routing & persistence
- [x] Mixer routing graph (buses/sends/FX-returns) + bus controls + MON output strips + file-input strips
- [x] Input Mixer UI (channel + bus/FX-return strips, destination picker, dual peak+LUFS meter)
- [x] Output-strip EQ/CMP detail-tab foundation + insert-chain popup (internal-FX only so far)

**Remaining** (decomposed 2026-05-31; several have detailed entries in `../../../todo.md`):
- [ ] **OTTO output-strip detail panel** — EQ/CMP (+ pan/width/sends) wired per OTTO strip (slice "4c"; 4b shipped without it)
- [ ] **OTTO transport-start surface** so playback is audible (slice "4d") — *verify against the landed `TransportBarHost`/S3a; may need only the start wiring, not a new surface*
- [ ] **OTTO stereo-mix output** (sentinel index `-2`, sums OTTO's 4 PlayerOut sub-buses) — design+plan done (`../specs/2026-05-27-otto-stereo-mix-output.md`), implementation not started
- [x] **Master meter** (transport-bar, master mix point) — operator-confirmed working 2026-05-31
- [ ] **Master spectrum display does not work** (operator-confirmed 2026-05-31) — OTTO `TransportBar` `SpectrumDisplay` shows nothing although the feed is wired (`TransportBarHost` pulls `OttoHost::spectrumBinDb` → `setSpectrumBin`); diagnose when Diversion 1 resumes
- [ ] **Dual peak+LUFS FaderMeter on every OTTO output strip** (visual parity with phrase/MON/bus strips) — *verify: master meter works, per-output binding unconfirmed*
- [ ] **Functional insert (INS) chain on output-mixer CHANNELS** (phrase/MON/OTTO), not buses-only — needs the `IEffectChainHost` node-key-collision audit first (todo.md)
- [ ] **Cross-group solo** across phrase/MON/bus/OTTO strips — today solo is OTTO-band-local only; other strips' solo buttons are inert (todo.md)
- [ ] **Fix plugin-scanner GUI-lock** ("P7-scanner") — unblocks the 3rd-party VST/CLAP insert picker on every channel (todo.md; the insert UI is internal-FX-only until this lands)

**Diversion 1 done when:** both mixers ship full per-strip controls (gain/pan/width/EQ/CMP/sends/mute/cross-group-solo/inserts) that persist through save→load; OTTO visual parity (tab + transport + 32 named outputs + stereo-mix + per-output detail + dual meter + cross-group solo); and 3rd-party plugin inserts are available on every channel (scanner fixed).

---

## Long-range master roadmap (M1–M24)
Spine: [`2026-05-17-v7-alignment.md`](2026-05-17-v7-alignment.md). Engine-first order; **paused at M8 S7+**
while the diversions above run. Status basis: file/commit evidence gathered 2026-05-31.

**Part A–B — Foundation + Mixer**
- [x] **M1** — Audio I/O foundation + RT-safety contract
- [x] **M2** — Membrane→Mixer rename + SignalType + Channel concept
- [x] **M3** — Channel-driven tape allocation + per-input flags
- [x] **M4** — Direct Layer subsystem (manual routing)
- [x] **M5** — Output Mixer: per-channel strips + buses + sends
- [x] **M6** — NotificationBus (engine↔UI truthfulness)

**Part C — Plug-in hosting**
- [~] **M7** — Out-of-process plug-in hosting  · *S1–S8 done; S9 separate-window redesign in flight*
- [~] **M8** — Determinism + failure semantics + CLAP  · *S1–S6 done; S7+ pending — the engine return point*

**Part D — Modality completion**
- [ ] **M9** — MIDI 2.0 / UMP end-to-end
- [ ] **M10** — Mix snapshots
- [ ] **M11** — IDA Archive Format (clean break from JSON SessionFormat)
- [ ] **M12** — Video tier-aware rendering
- [ ] **M13** — File I/O: audio/SMF/UMP-JSONL/video readers+writers + export  · *unblocks phrase Collapse/Expand*

**Part E — Auto-routing & time discipline**
- [ ] **M14** — Automatic direct-routing inference
- [ ] **M15** — LMC discipline tiers (GPS / PTP / NTP / Ableton Link)

**Part F — Ensemble**
- [ ] **M16** — Ensemble consistency (vector clocks / causal queue / partition forking)
- [ ] **M17** — Ensemble security (libsodium / Noise / X25519)

**Part G — Quality bars**
- [ ] **M18** — Inclusive-design surfaces
- [ ] **M19** — Validation test harness (drift / micro-timing / polymetric / blind / archival)

**Part H — Plug-in format expansion**
- [ ] **M20** — VST3 host
- [ ] **M21** — AU host (prep for iOS)

**Part I — Operator UX**
- [ ] **M22** — Hide-internals UI vocabulary cleanup

**Part J — iOS**
- [ ] **M23** — iOS AUv3 port

**Part K — Docs final pass**
- [ ] **M24** — White paper ↔ user guide ↔ site ↔ inline doc-comments alignment

---

## How statuses were set (2026-05-31)
- **M1–M6 done, M7/M8 partial, M9–M24 not started** — by file presence/absence + commit evidence; M9+
  subsystems have no source files or landing commits yet.
- **Diversion 2** — design/spec/roadmap committed; **zero production code** yet (`continue.md`).
- **Diversion 1** — OTTO + mixer slices landed per git; remaining mixer/GUI scope is the one item not yet
  enumerated here (flagged above).
- This dashboard tracks **slice/milestone-level** completion; fine-grained step checkboxes live inside
  each sub-plan.
