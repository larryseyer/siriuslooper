# Session Continuation — NEXT: operator eyes-on slice EC-Polish

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session end
   2026-05-24, all entries through TAPECOLOR Phase 5 + the
   "OTTO pin bumped to c4a8ec3" informational notice are acked. No new
   OTTO events landed this session. Next expected OTTO event is
   TAPECOLOR Phase 6 (tape-hiss noise floor).
2. **Slice EC-Polish shipped this session — operator eyes-on still pending**.
   `~/Desktop/IDA` alias points at the clean-rebuild artefact. Walk the
   "Slice EC-Polish operator eyes-on" sequence below.
3. Slice EC (the prior slice) + slice P save/load eyes-on are still
   carried — sandwich both into the same session.

## ▶ DONE THIS SESSION

### Slice EC-Polish — bus selection + full-screen EQ/CMP + curve view + GR meter + sends restyle

**HEAD on origin/master:** `1a311f3` — pushed. Clean rebuild verified,
ctest 678/679 green (the 1 not-run is the unchanged
`MainComponentPluginEditorTests_NOT_BUILT` sentinel).

Five operator-reported issues, all fixed in one slice:

1. **Bus / FX-return / aux / master selection.** Clicking a
   bus / FX-return strip on the input mixer (or an aux / master strip
   on the output mixer) now opens the detail panel with EQ + CMP tabs
   only (Pan/Width + Sends hidden via the new `ChannelDetail::setTabsAvailable`
   API). `InputMixerPane` + `OutputMixerPane` gained mutually-exclusive
   `selectedBus_` (plus `selectedMaster_` on the output side) alongside
   the existing `selectedStrip_` / `selectedPhrase_`. Listener forwards
   route by selected kind. MainComponent grew six new probes
   (`collectInputBusEqView` / `CmpView`, `collectOutputBusEqView` /
   `CmpView`, `collectOutputMasterEqView` / `CmpView`) keyed by
   `BusId.value()` — matches the Bus's own host-dispatch nodeKey.
   New bus-side EQ/CMP config + slot-add gestures plumb through the
   same audio-callback detach pattern as channel strips.

2. **Full-screen detail on EQ / CMP tab active.** Both panes now
   inherit `ChannelDetailListener` and re-layout on tab change. When
   the active tab is EQ or CMP, `detailPanel_` expands to the entire
   pane bounds and the strip row + INS + picker rows hide. Reverting
   to PanWid / Sends (or closing the detail) restores the strip
   layout. Helper: `isDetailFullScreen() const`.

3. **Sends knobs match Pan/Width styling.** Dropped the rounded-rect
   card outlines + fills. Layout matches OTTO's `ChannelDetailPanWidTab`:
   borderless rotary, knob centered in column, label below knob,
   dB readout below label. Knob sizing constants align (`kMinKnobSize=60`,
   `kMaxKnobSize=500`, `kColumnGap=16`).

4. **EqCurveView (new widget).** `ui/include/ida/EqCurveView.h` +
   `ui/src/EqCurveView.cpp`. Leaf `juce::Component` that paints the
   5-band response over a log-frequency / linear-dB grid using the
   same response math as OTTO's `EQPanel::calculateEQResponse` (HP/LP
   slope-octave subtraction, bell-curve approximation for the three
   shelves). Draggable band nodes for freq + gain; double-click resets
   the band to its `EqConfig` default. Listener fires the full
   `EqConfig` — same shape as the existing knob-change callback.
   Hosted by `ChannelDetailEQTab` as the dominant element in
   full-screen mode (auto-hidden in the 180px detail-band mode where
   it'd be too cramped). 5-band knob row stays beneath for precise
   numeric control + per-band Q + HP/LP slope toggle.

5. **CmpMeterView (new widget).** Same shape: transfer-curve plot
   (input dB vs. output dB) with threshold knee + ratio slope drag,
   plus a vertical gain-reduction meter column on the right. Mirrors
   OTTO's `CompressorPanel` visual idiom. GR meter reads 0 until the
   adapter surfaces live GR (queued — see [[continue.md]] EC7).
   Hosted by `ChannelDetailCMPTab`.

### Tests

- `ctest --test-dir build`: **678 pass / 1 not-run / 679 total** —
  unchanged baseline.
- No new test cases added. The new widgets are operator-verified UI
  (per [[feedback_clean_builds]] convention); engine response math is
  identical to OTTO's already-tested formula.

## ▶ NEXT — Slice EC-Polish operator eyes-on

Walk through, in this order:

1. **Launch IDA.** `~/Desktop/IDA` alias.
2. **Bus selection — input mixer.** Add a bus + an FX return via the
   blank-area right-click menu. Click each: detail panel opens with
   only EQ + CMP tabs in the tab bar (Pan/Width + Sends should not
   appear). "+ Add EQ to this strip" should be the initial empty
   state on a fresh bus.
3. **Bus selection — output mixer.** Add an aux bus. Click an aux:
   same EQ/CMP-only behavior. Click the master strip: same.
4. **Full-screen EQ.** Click any channel strip → click the EQ tab.
   The detail panel should expand to the entire mixer pane (strip
   row + bottom buttons gone). A frequency curve with 5 colored band
   nodes (HP grey, LOW red, MID accent, HIGH green, LP grey) should
   appear over a dB grid + frequency labels. Drag the MID node —
   curve responds, knobs below sync. Click PanWid → layout restores.
5. **Full-screen CMP.** Click CMP. Same expansion. Transfer curve +
   GR meter column on the right. Drag the threshold knee — curve
   responds + knob below syncs. Drag the slope above the knee
   (mouse-down on the slope line, drag down) — ratio responds.
6. **Sends styling.** Click a channel strip → click Sends tab.
   FX-return knobs should be borderless (no rounded card outline),
   sized like Pan/Width knobs, with the FX-return name BELOW the
   knob (not above) and dB readout below the name.

Flag any drift from the above. Most likely breakpoint: the response
math constant `kHpSlope = kLpSlope = 24 dB/oct` in `EqCurveView.cpp`
is a fixed-display assumption; the engine still respects the
`hpSlopeDbPerOct` field. If the operator toggles slope to 12 on the
knob row, the engine changes but the curve display doesn't — the
curve always paints a 24 dB/oct slope.

## ▶ ALSO PENDING — operator eyes-on, carried

- **Slice EC (EQ + CMP tabs functional)** — verification sequence in
  the prior continue.md snapshot still applies. The auto-seed + slot
  empty-state behavior is unchanged by EC-Polish, just the look of
  the editor that opens after slot-add.
- **Slice P save/load** — quit + relaunch + load round trip on a
  modified phrase strip. Carried from two sessions ago.

## ▶ BASELINE (start of next session)

- **HEAD on origin/master:** `1a311f3` (EC-Polish, pushed).
- **ctest baseline:** 678 pass / 1 not-run / 679 total.
- **OTTO submodule SHA:** `3c84a409` (unchanged this session).
- **lsfx_tapecolor submodule SHA:** `d8b06b1` (unchanged — Phase 5).
- **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified.

## ▶ QUEUED — explicit follow-ups

- **EC-Polish curve-display slope sync** — `EqCurveView` paints a
  fixed 24 dB/oct on HP/LP for the visual; the engine respects the
  per-band toggle. If the operator wants the displayed slope to
  match, plumb the toggle's `hpSlopeDbPerOct` / `lpSlopeDbPerOct`
  into the response math (small change in `EqCurveView::responseAt`).

- **EC-Polish Q drag on curve** — currently the curve only drags
  freq + gain. Adding wheel-or-modifier-drag for Q on shelf bands
  would round out the gesture (mirrors OTTO).

- **EC-Polish live GR readout** — `CmpMeterView::setGainReductionDb`
  exists but nothing pushes a value in. Wire it from the CmpAdapter
  once that adapter exposes a live GR atomic.

- **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
  Same options as before: payload on `EffectChainEntry` (preferred)
  or sidecar map in the session envelope.

- **DLY + RVB tabs** — analogous slices once Delay + Reverb
  internal-FX adapters land.

- **EC-Polish small-mode behavior** — in the 180px detail-band mode
  (non-fullscreen tab), the curve view is hidden and only the knob
  row shows. Worth confirming with the operator whether that's the
  right call or whether a mini-curve at the top of the small mode
  would read better.

- **Plugin scanner unblock** (`project_plugin_scanner_broken`) —
  still carried.

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md`.
- **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
- **Clean build before any GUI smoke** verified end-of-session.
- **No new auto-memory updates needed.** EC-Polish design choices
  flow from existing memory entries (OTTO visual idiom but IDA-native
  implementation; gestures over UI clutter; pro-audio convention
  defaults).
