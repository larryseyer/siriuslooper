# Session Continuation — NEXT: TAPECOLOR Slice 2 (per-tape tri-state) + Slice 3 (whitepaper §6.7)

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. All entries through TAPECOLOR
   Phase 5 + the OTTO pin notice still acked. Next expected OTTO event is
   Phase 6 (tape-hiss noise floor).
2. New auto-memory entry this session: **[[project_tapecolor_placement]]** —
   TAPECOLOR has two deployment modes (Mode A per-tape tri-state, Mode B
   insert-anywhere as 5th internal FX); both default OFF; one DSP unit; OTTO's
   per-bus model does NOT carry into IDA.
3. Operator eyes-on still pending: launched-app verification of recent
   commits including `b1f2d08` (OutputMixer bus→FX-return sends) and
   `2e1c17f` (TAPECOLOR Slice 1 insert adapter).

## ▶ DONE THIS SESSION

Two commits on `origin/master`:

### `b1f2d08` — OutputMixer bus→FX-return sends + symmetric audio render
* `OutputMixer::setBusSend(source, fxReturn, level)` + `busSendLevel(...)` —
  mirrors `InputMixer` API exactly. Storage = `MixerGraph::sendEdges`
  (same as input side; no parallel matrix). Self-send / wrong-kind /
  cycle all rejected via `MixerGraph::setSend`. Sender-count maintained
  on the target FX-return for the send-zero bypass.
* Audio render: bus-source send tap added to step 3 of BOTH
  `OutputMixer::renderBuffer` and `InputMixer::renderInputGraph` (the
  InputMixer had the same render-path gap — its setter was wired but the
  audio path never applied it). Tap is post-channel-routes,
  pre-bus-effect-chain. Topo-sort guarantees target after source.
* MainComponent: output mixer `onBusSelect` now passes real `isFxReturn`
  + FX-return list/levels; `onBusSendChanged` routes to
  `OutputMixer::setBusSend` (mirror of the InputMixer wiring).
* 7 new tests in `OutputMixerTests.cpp` (`[bus-send]`): default zero,
  set+clamp, self-send rejected, wrong-kind rejected, cycle rejected,
  render-path applies the contribution, level=0 removes it.

### `2e1c17f` — TAPECOLOR Slice 1: insert-anywhere adapter (default OFF)
* `InternalFxId::kTapeColor = 4` + wire-stable `"TAPECOLOR"` token.
* `TapeColorAdapter` (engine/src/fx/) wraps `lsfx::TapeColorProcessor`.
  **Unlike the other four internal-FX adapters**, ctor does NOT flip
  `enabled=true` — TAPECOLOR is default OFF everywhere per the 2026-05-24
  operator design lock. A freshly-inserted slot is silent passthrough
  until the operator turns it on through a (future-slice) param UI.
* Factory dispatch (`engine/src/InternalFxFactory.cpp`) adds the
  `kTapeColor → TapeColorAdapter` case. `OutOfProcessEffectChainHost`
  uses the factory generically; no host-side edit needed.
* CMake: `IdaEngine` and `IdaTests` now link `lsfx::lsfx_tapecolor`.
* 7 new tests: 5 adapter (`[tapecolor-adapter]`) + 2 factory
  (`[internal-fx][factory]` — usable-adapter smoke + string round-trip).

### Tests baseline
* `ctest --test-dir build`: 695 pass / 1 not-run / 696 total (14 new
  tests across the two slices: 7 bus-send + 5 TAPECOLOR adapter +
  2 TAPECOLOR factory).

## ▶ TAPECOLOR — design lock (2026-05-24)

Operator confirmed placement on 2026-05-24. The full design lives in
`[[project_tapecolor_placement]]` memory. TL;DR:

* **Mode A (per-tape, tri-state)**: One TAPECOLOR per tape, stored on the
  TapePool entry. Tri-state: `None` (default) / `BeforeWrite` (baked into
  FLAC at record) / `AfterRead` (clean on disk, color on playback — the
  default when turning it on). Sits OUTSIDE the channel chain — between
  the tape transport and the channel-strip input. The ONLY FX allowed in
  the tape path.
* **Mode B (insert-anywhere)**: 5th internal FX, default OFF. **Slice 1
  shipped in `2e1c17f`.**
* One DSP unit shared by both modes. Mode A + Mode B simultaneously on the
  same signal is legal (silent — no UI warning, no default).

## ▶ NEXT — TAPECOLOR Slice 2: per-tape tri-state + audio-path hooks

Implementation:

1. Extend the per-tape config (TapePool entry) with a TAPECOLOR tri-state
   field: `enum class TapeColorMode { None, BeforeWrite, AfterRead }`
   defaulting to `None`. Serialize wire-stably.
2. Each tape carries its own `lsfx::TapeColorProcessor` instance. Lifetime
   bound to the tape: ctor on add-to-pool, dtor on remove-from-pool.
3. `TapeWriter`: when the tape's mode is `BeforeWrite`, route the incoming
   audio block through TAPECOLOR before the FLAC encoder. Audio-thread
   safe (TapeColorProcessor::process is alloc-/lock-/log-free per its
   Phase-5 audit).
4. Playback path: when the tape's mode is `AfterRead`, run TAPECOLOR on
   the decoded block before it lands in the Output Mixer's phrase-channel
   scratch.
5. Operator UI: surface the tri-state on the tape-config detail panel
   (carries with the eventual TapePool surface UI).
6. Tests: per-mode end-to-end (None bit-identical, BeforeWrite alters the
   on-disk content, AfterRead alters playback but disk stays clean).

## ▶ NEXT — TAPECOLOR Slice 3: whitepaper §6.7 + user guide

`docs/IDA_Whitepaper_V8.md`:
* New §6.7 "Tape coloration (TAPECOLOR)".
* §6.7.1 — tape-bound (None / Before-write / After-read).
* §6.7.2 — 5th internal FX (insert-anywhere).
* Cross-links from §10 (Tape capture) and §11 (Render).

Plus a user-guide section (per `[[project_user_guide_alongside_whitepaper]]`).

## ▶ ALSO QUEUED — FX-return Sends → Edit-FX button swap

Carried from the previous session's NEXT-NEXT. Operator paused this to
discuss TAPECOLOR; it's still legitimate followup work. When an FX-return
strip is selected, the Sends tab's label becomes "Edit FX" and content
becomes a single big button that opens the FX edit surface. Currently
FX-return selection still hides Sends entirely on the output side
(InputMixerPane already shows real sends since `b1f2d08`).

Implementation:
1. `ChannelDetail`: per-tab label override.
2. `ChannelDetailSendsTab::setEditFxMode(bool)`: hide send cards +
   pre-fader toggle, show a single Edit-FX button.
3. New listener method `sendsTabEditFxRequested()`.
4. Wire from both panes; MainComponent routes to the FX edit surface.

## ▶ QUEUED — explicit follow-ups

* **TAPECOLOR param UI** — Edit-FX surface for kTapeColor adapter (currently
  default-OFF with no operator-facing parameter control; needs to expose the
  full Phase-5 parameter set: drive, mix, wow, flutter, scrape, machine IR
  index, quality, etc.).
* **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
* **Per-band bypass for EQ** — engine flag.
* **Q drag on curve** — EqCurveView gesture polish.
* **Live GR readout** — wire `CmpMeterView::setGainReductionDb` from
  `CmpAdapter`.
* **DLY + RVB tabs** — analogous slices once internal-FX adapters land.
* **Plugin scanner unblock** (`project_plugin_scanner_broken`).

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `2e1c17f` (TAPECOLOR Slice 1: insert-anywhere
  adapter).
* **ctest baseline:** 695 pass / 1 not-run / 696 total.
* **OTTO submodule SHA:** `3c84a409` (unchanged).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (Phase 5, unchanged).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified.

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (Slice 3 will add §6.7).
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
* **New auto-memory** this session: `project_tapecolor_placement`
  (supersedes the deleted `project_tapecolor_per_bus_model`).
