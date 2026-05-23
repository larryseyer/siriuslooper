# Session Continuation — NEXT: Output Mixer slice 3 (phrase channels?) OR persistence (T6) OR cycle-aware destination filtering

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Ack any
   `[FROM OTTO → IDA]` entries. Last sweep 2026-05-23: both 2026-05-23
   TAPECOLOR entries acked/resolved; nothing new expected until OTTO
   ships Phase 3.

## ▶ DONE THIS SESSION — Output Mixer slice 1 + slice 2

Two consecutive slices of the operator's mixer→transport roadmap
landed in the same session. Operator confirmed slice 1 visually
between slices.

### Slice 1 — master bus strip
- `Bus* OutputMixer::busForId(BusId)` accessor (mirrors InputMixer).
- `OutputMixerPane` nested class — one Master bus strip with fader,
  dual peak/LUFS meter, mute, INS button.
- `openInsertChainPopupForMasterBus()` reusing the T5 detach/`setEffectChain`/re-attach bracket.
- Tab registered behind the `Professional`-tier gate; `refreshOutputMixer()`
  joined the 30 Hz timer.
- Operator eyes-on confirmed: tab visible, single master strip
  rendered, INS button present, destination picker correctly absent
  (master is the terminal — there's nowhere "downstream" to pick).

### Slice 2 — aux bus row + add-bus + destination picker
- **Engine additions** to `OutputMixer` (`engine/include/ida/OutputMixer.h` +
  `engine/src/OutputMixer.cpp`):
  - `setBusMainOutToHardwareOutput(BusId)` — bus→hardware-out direct
    (bypasses master). Mirrors InputMixer's one-liner.
  - `busCount() const noexcept` for UI cap checks.
  - `enum class MainOutDest { Bus, HardwareOutput }` +
    `busMainOut(BusId)` + `busMainOutBus(BusId)` for picker label /
    tick state. Same shape as InputMixer's introspection minus Tape.
- **OutputMixerPane** extended with aux-bus row infrastructure:
  `BusInfo`, `DestKind`, `DestChoice`, `StripDest` types
  (Output-side: no Tape, no FXReturn — buses only). Per-strip INS +
  destination-picker buttons. Blank-area right-click / long-press →
  "Add bus" menu. Master strip moved to RIGHTMOST position via
  `removeFromRight` (pro mixing-console convention); aux buses lay
  out left-to-right to its left, with a visual divider.
  Listener disambiguation: master uses sentinel id `-1`, aux strips
  use 0..N-1.
- **MainComponent** wiring (`app/MainComponent.cpp`):
  - `outputBusStripIds_` parallel-ID vector (mirrors `busStripIds_`).
  - `rebuildOutputBusStrips()` — walks `busCount()` skipping master,
    brackets `Bus::prepare` in remove/addAudioCallback.
  - `refreshOutputDestinations()` — per-bus choice list = `[Master,
    every-other-aux, Direct out]`; current dest resolved via
    `busMainOut` + `busMainOutBus`.
  - `openInsertChainPopupForOutputBus(int)` mirrors the master variant.
  - Callbacks: `onAddBus`, `onBusGain`, `onBusMute`,
    `onBusInsertChainClicked`, `onBusDestinationChosen` wired in
    the tab-init block. Topology mutations bracketed by
    remove/addAudioCallback.
- **Build:** `cmake --build build --target IdaTests IDA -j` green.
- **Tests:** `ctest --test-dir build` → 639/640 pass / 1 not-run.
  Test #169 (`permanent bypass: kill every generation`) flaked once
  on first run, passed on re-run — pre-existing plug-in-host
  process-kill timing dependency, unrelated to this work.

### Audible verification still pending

Slice 2 operator eyes-on next launch:
- Right-click (or long-press) on blank area in Output Mixer tab →
  "Add bus" menu appears. Selecting creates a new strip to the LEFT
  of master (master stays rightmost).
- New aux strip has fader, mute, INS button, and a destination picker
  reading "Master" by default.
- Picker opens with 3 options on the first bus (Master is ticked,
  Direct out also offered). After adding a second bus, each strip's
  picker also offers the OTHER aux bus.
- Routing a bus to Direct out → bus's signal hits hardware outputs
  bypassing master (master meter goes silent for that bus's
  contribution).
- INS on an aux bus opens InsertChainPopup; chain saves per-bus.

If cycle-creating routes silently no-op, that's the slice 2 known
limitation (engine rejects, UI just refreshes — see "OPTIONS" below
for the planned slice 3 fix).

## ▶ NEXT — three options

### Option A — slice 3: cycle-aware destination filtering + bus rename

Add `OutputMixer::busMainOutToBusWouldCycle(BusId from, BusId to)`
mirroring `InputMixer::busMainOutToBusWouldCycle`. Use it in
`refreshOutputDestinations` to PRE-EXCLUDE the targets that would
cycle, so the picker never offers a route that'll be silently
rejected. Combine with a bus-rename gesture (double-click strip
name? right-click strip → "Rename…"?) — currently buses ship as
"Bus 1", "Bus 2", … and operator has no way to change them.
Smallest, tightest follow-up.

### Option B — slice 4: phrase channels (depends on M6+)

Channels in the Output Mixer are "one per phrase" (whitepaper §6.6).
Phrases as a runtime concept require M6+ Constituent rendering.
Until at least one phrase can be defined at runtime, the channels
column has nothing to populate. SKIP until M6+ engine work lands.

### Option C — T6 persistence verify

EffectChain + bus topology already round-trip via `SessionFormat`.
With slice 2 in place, T6 would: create some aux buses, configure
their destinations + INS chains, save session, reload, verify the
Output Mixer tab reconstructs identically. Smallest-scope
"correctness lap" — no new UX, just an audit.

**Recommendation:** A. Cycle-aware filtering is a small, tight
polish that completes the Output Mixer routing UX. B is blocked on
engine milestones; C is verifiable but less visible than A.

## ▶ BASELINE

- `ctest --test-dir build`: **639 pass / 1 not-run / 640 total**.
  Flaky #169 (process-kill timing) sometimes fails on first run,
  passes on re-run — pre-existing.
- `master` HEAD on origin: this session's two commits (slice 1 +
  slice 2), pushed.
- OTTO submodule SHA: `41dcae25` on `origin/main` (unchanged).
- lsfx_tapecolor submodule SHA: `c4a8ec3` on `main` (unchanged; no
  DSP instantiation on IDA's side until OTTO Phase 13).

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (underscores —
  `project_whitepaper_path`).
- **Operator actions still pending** (between sessions; agent cannot
  perform; tracked in `todo.md`): notarytool keychain `ida-notary`
  setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
- **Clean build before any GUI smoke** (`feedback_clean_builds`):
  `rm -rf build && cmake -B build -S . -G Ninja
   -DCMAKE_BUILD_TYPE=Release && cmake --build build --target
   IdaTests IDA -j`.
