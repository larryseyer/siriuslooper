# Session Continuation — NEXT: OutputMixer bus-to-bus sends + FX-return Edit-FX swap

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. All entries through TAPECOLOR
   Phase 5 + the OTTO pin notice still acked. Next expected OTTO event is
   Phase 6 (tape-hiss noise floor).
2. New auto-memory entry this session: **[[project_input_output_mixers_identical]]**
   — input and output mixers are architecturally identical (same data
   model + APIs + UI surfaces), differing only in I/O endpoints. Any
   capability added to one side gets the equivalent on the other in the
   same slice. This drives the NEXT items.
3. Operator eyes-on still pending: launched-app verification of
   commits `3bbb860`, `a9e6432`, `9036784` (default-select, back button,
   bus pan/width/sends, master pan/width).

## ▶ DONE THIS SESSION

Five commits on `origin/master`:

### `a9e6432` — CMP tab matches EQ idiom
* `ENABLE` → `CMP ENABLED`; `SIDECHAIN HPF` moved from top-right corner
  to the right end of the knob row; `kKnobMinSize` aligned with EQ.

### `48261e6` — continue.md refresh
* Logged the line-difference question; both curves stroke at
  `juce::PathStrokeType (2.0f)` with same accent (no code-level delta).

### `3bbb860` — default-select + back button
* InputMixerPane: fires `onSelect(0)` after first non-empty `setStrips`
  (guarded by `defaultSelectionDone_`). Operator sees "In 1" populated
  on launch.
* OutputMixerPane: new `triggerDefaultMasterSelection()` called from
  MainComponent after all output-pane callbacks land. Master is the
  default selection.
* Both panes: new `backButton_` only visible in EQ/CMP full-screen mode,
  positioned top-right of the detail-panel area. Click → `deselectAll()`
  (same as Escape).

### `9036784` — bus pan/width engine + Pan/Wid + Sends tabs on bus/master/FX-return
* `Bus` gained `panNormalized_` / `width_` atomics, `setPan` / `pan` /
  `setWidth` / `width` accessors. `processInline` + `processChain` share
  a new `applyGainPanWidthStereo` helper. Defaults (pan=0.5, width=1.0)
  bypass the cos/sin/mid-side math entirely so existing Bus tests stay
  bit-identical at unity center.
* `InputMixer::busSendLevel(BusId source, BusId fxReturn)` added —
  mirrors `channelSendLevel`. `setBusSend` already existed.
* InputMixerPane / OutputMixerPane: `showBusDetailFor` now takes
  pan/width/sends + `isFxReturn`; `showMasterDetailFor` takes pan/width.
  Tab masks: bus → `{true,true,true,true}`; FX return → `{true,false,true,true}`;
  master → `{true,false,true,true}`. New pane callbacks `onBusPan`,
  `onBusWidth`, `onBusSendChanged`, `onMasterPan`, `onMasterWidth`
  wired through `panWidTabPanChanged` / `panWidTabWidthChanged` /
  `sendsTabSendChanged`.
* Three new Bus tests (default-identity, hard-left pan, width=0
  mono-collapse).
* Tests: 681/682 pass (1 not-run = baseline).

### Tests baseline
* `ctest --test-dir build`: 681 pass / 1 not-run / 682 total (3 new
  Bus pan/width tests this session).

## ▶ NEXT — OutputMixer bus-to-bus sends symmetry

`InputMixer` ships level-controlled bus→FX-return sends via
`setBusSend` (uses `graph_.setSend`). `OutputMixer` is asymmetric here:
it uses `sendMatrix_` indexed by `(channel × bus)` only — `routeBusToBus`
exists but is single-target main-out, not a level-controlled multi-target
send.

Per [[project_input_output_mixers_identical]] the asymmetry is a real
gap. Slice scope:

1. Decide storage model: extend `sendMatrix_` to `(channelCount +
   busCount) × busCount` or add a parallel `busSendMatrix_`. Parallel
   is probably cleaner — keeps channel-send hot path unchanged.
2. Add `OutputMixer::setBusSend(BusId source, BusId fxReturn, float level)`
   + `busSendLevel(...)`. Same clamping + cycle semantics as
   `routeChannelToBus`'s FX-return branch.
3. Extend the audio render path: after each bus's `process()` writes to
   its main-out destination, iterate its bus-source sends and
   additively contribute to those FX-return mix buffers. Order matters
   — work the topological evaluation order so a send target gets all
   contributions before it processes.
4. Update `OutputMixerPane::onBusSelect` wiring (currently passes
   `isFxReturn=true` to suppress Sends — once the engine ships, surface
   Sends with real levels).
5. Tests: bus-to-bus send level applied; cycle detection; default zero.

**On the user's "single shared instance / aliasing" suggestion:**
that would conflict with [[project_two_mixers_totally_separate]] (no
shared engine state, per-instance generic types). The asymmetry to fix
is at the **API surface**, not the runtime — make the InputMixer and
OutputMixer APIs identical so both panes can wire the same callbacks
against per-instance engines. Keep the two engine instances; bring
their public surfaces into parity.

## ▶ NEXT — FX-return Sends → Edit-FX button

Operator's ask (matches OTTO): when an FX-return strip is selected,
the Sends tab's **label** becomes "Edit FX" and its **content** becomes
a single big button that opens the FX edit surface. Currently FX-return
selection hides the Sends tab entirely.

Implementation:

1. `ChannelDetail`: per-tab label override (e.g.
   `setSendsTabLabel(juce::String)`).
2. `ChannelDetailSendsTab::setEditFxMode(bool)`: hide send cards +
   pre-fader toggle, show a single Edit-FX button.
3. New listener method `sendsTabEditFxRequested()`.
4. Wire from both panes; MainComponent routes to the FX edit surface.

## ▶ QUEUED — explicit follow-ups

* **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
* **Per-band bypass for EQ** — engine flag.
* **Q drag on curve** — EqCurveView gesture polish.
* **Live GR readout** — wire `CmpMeterView::setGainReductionDb` from
  `CmpAdapter`.
* **DLY + RVB tabs** — analogous slices once internal-FX adapters land.
* **Plugin scanner unblock** (`project_plugin_scanner_broken`).

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `9036784` (bus pan/width engine + Pan/Wid
  + Sends tabs on bus/master/FX-return).
* **ctest baseline:** 681 pass / 1 not-run / 682 total.
* **OTTO submodule SHA:** `3c84a409` (unchanged).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (unchanged).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified.

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md`.
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
* **New auto-memory** this session: `project_input_output_mixers_identical`.
