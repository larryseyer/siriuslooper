# Session Continuation — NEXT: CMP tab reshape to match EQ tab layout

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session end
   2026-05-24, all entries through TAPECOLOR Phase 5 + the OTTO pin
   informational notice are acked. No new OTTO events this session.
   Next expected OTTO event is TAPECOLOR Phase 6 (tape-hiss noise floor).
2. **EQ tab now matches OTTO byte-for-byte** (band selector row + slope
   buttons + channel pill row + selected-band node emphasis). The
   operator signed off on the EQ at the end of the session and asked
   to reshape the **CMP tab to follow the same idiom** — see the NEXT
   section below for the design intent. **This is the entry point for
   the new chat.**
3. Carried-from-prior eyes-on (slice EC + slice P save/load) still
   pending — knock those out alongside CMP-shape verification.

## ▶ DONE THIS SESSION

Three commits, all on `origin/master`:

### `1a311f3` — slice EC-Polish (initial)
Bus / FX / aux-bus / master selection on both mixers, full-screen
detail when EQ / CMP is active, sends-knob restyle, new `EqCurveView`
(IDA-native curve display) + `CmpMeterView` (transfer curve + GR
meter), bus EQ/CMP wired through MainComponent collectors keyed by
`BusId.value()`.

### `673c603` — trap-fix
Bus ctor auto-seeds `[EQ, CMP]` (mirrors `ChannelStrip<Audio>` slice-EC
seed), `Bus::setEffectChainHost` re-dispatches the seeded chain when
the host attaches (new `Bus::dispatchAllSlotsToHost` helper),
`isDetailFullScreen` gates on slot presence (no full-screen empty
state), Escape key on both panes calls `deselectAll()`. Three tests
updated to encode the new bus auto-seed contract.

### `ed21e82` — visual + selection polish
`StripContextOverlay` short-tap routes to bus select (overlay was
swallowing every click on the name band → bus / FX strips appeared
unselectable). Sends knob sized to PanWid (PRE FADER moved to top-left
corner, dB readout combined into name label below knob). EQ curve
stripped: no fill underneath, no per-band text labels above nodes.

### `4730498` — EQ matches OTTO
EQ tab refactored to OTTO's iPad layout point-for-point:
* `EQ ENABLED` toggle (top-left)
* Curve view (dominant, plain stroked line, selected-band node
  rendered larger + outlined per OTTO's `drawCurveForeground`)
* Band selector row: 5 buttons `HP / Low / Mid / High / LP`,
  radio-grouped, accent tint when selected
* Contextual control row below band selector:
   * HP/LP: 4 slope buttons `6 / 12 / 18 / 24` dB-per-oct + `Bypass`
   * Low/Mid/High: `FREQ / GAIN / Q` knobs + `Bypass`
* Channel pill row at the bottom of the pane (full-screen mode only) —
  every strip + bus + master, radio-grouped, accent on selected.
  Mirrors OTTO's `Kick / Snare / Stick / Hats…` selector.

Engine: `EqAdapter` now translates all four `FilterSlope` values
(6 / 12 / 18 / 24) — previously clamped to 12 / 24.

### Tests
- `ctest --test-dir build`: **678 pass / 1 not-run / 679 total** —
  baseline holds throughout. Three tests updated (`BusTests`,
  `MixerGraphPersistenceTests`, `OutputMixerTests`) to encode the
  bus auto-seed contract.

## ▶ NEXT — Reshape CMP tab to match EQ tab idiom

The operator's end-of-session ask: **"make the CMP look like the EQ"**
with two reference screenshots (EQ done; CMP unchanged). The clear
intent:

1. **Same top affordance shape** — replace the bare `ENABLE` checkbox
   + `SIDECHAIN HPF` toggle with EQ-style headers. `ENABLE` already
   reads parallel to `EQ ENABLED`; verify the labels match (`CMP
   ENABLED` would be most consistent). `SIDECHAIN HPF (100 Hz)` is
   secondary chrome — keep as a corner toggle but ensure it visually
   matches the EQ tab's lack of a parallel right-corner control (EQ
   has nothing in the top-right). Options:
   * Move sidechain-HPF into a contextual control area (below the
     transfer curve, alongside the 6 knobs) so the header band only
     holds `CMP ENABLED`.
   * Or keep it top-right but match the toggle styling exactly.
2. **Transfer curve dominant** — already is. Confirm the curve uses
   the same accent color + line weight as the EQ curve.
3. **Channel pill row at the bottom** — should appear in CMP
   full-screen mode just like EQ. The pill row is panel-owned (lives
   in `InputMixerPane` / `OutputMixerPane`), so it should already be
   working — VERIFY.
4. **Parameter row visual parity** — the EQ tab's contextual row uses
   shelf-band knobs (3 across) styled with accent rings. The CMP tab
   currently shows 6 knobs in a single row (THRESH / RATIO / ATTACK /
   RELEASE / MAKEUP / MIX). Decide:
   * Keep 6 knobs visible at once (current behavior — likely what the
     operator wants given they didn't critique knob visibility), but
     ensure styling matches EQ exactly: same knob sizes, same accent
     ring style, same readout placement.
   * OR mirror EQ's "parameter selector" idiom — a row of 6 parameter
     buttons + a contextual control row showing just the selected
     parameter's knob. **Probably NOT what the operator wants** since
     CMP doesn't have logical "bands" the way EQ does; six independent
     params don't group the same way. Default to the first option.

### Concrete scope guess (verify with operator before deep work)

* Rename `ENABLE` → `CMP ENABLED` to match `EQ ENABLED`.
* Audit knob styling: confirm `THRESH / RATIO / ATTACK / RELEASE /
  MAKEUP / MIX` knobs read at the same size + accent intensity as
  EQ's `FREQ / GAIN / Q` (the EQ contextual knobs use the OTTO
  rotary look-and-feel; CMP should be identical).
* Confirm channel pill row appears in CMP full-screen.
* Move `SIDECHAIN HPF (100 Hz)` from top-right to the parameter row
  (e.g. as a small toggle pinned right of `MIX`).
* No engine changes needed.

### Files to touch
* `ui/src/ChannelDetailCMPTab.cpp` (rename label, restyle layout,
  reposition sidechain toggle).
* `ui/include/ida/ChannelDetailCMPTab.h` (only if header constants
  shift; probably no changes).
* `app/MainComponent.cpp` — only if the channel pill row in
  CMP full-screen mode is broken (it shouldn't be — pill row is
  driven by `isDetailFullScreen()` which is tab-agnostic).

## ▶ ALSO PENDING — operator eyes-on, carried

* **Slice EC original verification** — the EQ + CMP gestures from
  slice EC haven't been formally walked through in the order this
  session listed. Most are exercised implicitly by the EQ refactor
  verification, but slice P save/load was never operator-confirmed
  end-to-end.
* **Bus pan + width engine + bus-side sends** — queued in `todo.md`
  (2026-05-24 entry) as a self-contained next slice. Independent of
  the CMP-tab work above.

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `4730498` (EQ matches OTTO).
* **ctest baseline:** 678 pass / 1 not-run / 679 total.
* **OTTO submodule SHA:** `3c84a409` (unchanged this session).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (unchanged).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified.

## ▶ QUEUED — explicit follow-ups

* **CMP-tab reshape to match EQ** — see NEXT section above.

* **Bus pan + width + bus-side sends** — `todo.md` entry 2026-05-24,
  self-contained next slice (engine: add `pan_` / `width_` atomics +
  audio-thread apply + `InputMixer::setBusSend` with cycle detection;
  UI: drop `setTabsAvailable({false,false,true,true})` on bus selection
  so PanWid + Sends tabs become visible; wire bus pan/width/sends through
  MainComponent listener forwards).

* **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
  Two paths: payload on `EffectChainEntry` (preferred — config moves
  with slot if chain is reordered) or sidecar map in the session envelope.

* **Per-band bypass for EQ** — slice EC-Polish added a `Bypass` button
  to the EQ tab's contextual row, but the engine has only `eqEnabled`
  (global). For HP/LP, the button currently parks freq at 20 /
  20 kHz; for shelves, parks gain at 0. A real per-band bypass flag
  in `PlayerEffectsConfig` would be a small follow-up.

* **Q drag on curve** — `EqCurveView` drag updates freq + gain only.
  Adding wheel-or-modifier-drag for Q on shelf bands rounds out the
  gesture (mirrors OTTO).

* **Live GR readout** — `CmpMeterView::setGainReductionDb` exists but
  nothing pushes a value in. Wire from `CmpAdapter` once that adapter
  exposes a live GR atomic.

* **DLY + RVB tabs** — analogous slices once Delay + Reverb
  internal-FX adapters land.

* **Plugin scanner unblock** (`project_plugin_scanner_broken`) — still
  carried from prior sessions.

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md`.
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename. All carried in `todo.md`.
* **Clean build before any GUI smoke** verified end-of-session.
* **No new auto-memory updates needed.** This session's design choices
  flow from existing memory entries (OTTO visual idiom but IDA-native
  implementation; gestures over UI clutter; pro-audio convention
  defaults; "Sirius done right and complete").
