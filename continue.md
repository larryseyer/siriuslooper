# Session Continuation — NEXT: operator eyes-on CMP layout; then bus pan/width/sends

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session end
   2026-05-24 (second turn), all entries through TAPECOLOR Phase 5 +
   the OTTO pin informational notice are still acked. No new OTTO
   events. Next expected OTTO event is TAPECOLOR Phase 6 (tape-hiss
   noise floor).
2. **CMP tab now matches EQ idiom** (commit `a9e6432`). Top band reads
   `CMP ENABLED` only; sidechain HPF lives on the right end of the knob
   row alongside MIX. Channel pill row already appears in CMP
   full-screen (was tab-agnostic from the EQ slice). Operator should
   eyes-on this in the launched .app and confirm.
3. **Open question from operator:** "Notice the line difference also."
   Code-level inspection of `EqCurveView.cpp:382` and
   `CmpMeterView.cpp:223` shows BOTH curves stroke at
   `juce::PathStrokeType (2.0f)` with the SAME accent
   `juce::Colour (0xFFD9534F)`. No divergence found at the source level.
   If the operator's eyes-on still sees a visual delta after the
   refreshed build, the next investigation is anti-aliasing /
   `PathStrokeType` end-cap settings or grid bleed-through. Re-open
   with a fresh screenshot if so.
4. Carried-from-prior eyes-on still pending: slice EC original
   walkthrough + slice P save/load.

## ▶ DONE THIS TURN

### `a9e6432` — CMP tab matches EQ idiom
* `ENABLE` → `CMP ENABLED` label (matches `EQ ENABLED`).
* `SIDECHAIN HPF (100 Hz)` toggle moved from top-right corner to the
  right end of the knob row, pinned at `kSidechainToggleW = 160` ×
  `kSidechainToggleH = 32`, vertically centered. This frees the top
  band to mirror the EQ tab's "toggle on the left, nothing on the
  right" idiom.
* `kKnobMinSize` lowered 40 → 36 to match EQ contextual row.
* No engine changes. `isDetailFullScreen()` at `MainComponent.cpp:953`
  already gates on `CMP::hasCmpSlot()` so the channel pill row already
  shows in CMP full-screen.
* Build: clean rebuild from scratch, 678 pass / 1 not-run = baseline
  holds.

## ▶ NEXT — Operator eyes-on of CMP layout

Operator launches `~/Desktop/IDA`, picks any input strip, taps `INS` so
EQ + CMP auto-seed (via slice EC-Polish), switches to the CMP tab, and
confirms:
* Top band: `CMP ENABLED` on the left, nothing in the top-right.
* Knob row: 6 knobs (THRESH / RATIO / ATTACK / RELEASE / MAKEUP / MIX)
  filling the left, `SIDECHAIN HPF (100 Hz)` toggle pinned to the right.
* Full-screen: channel pill row appears at the bottom.
* Knob sizes + readouts read consistent with the EQ tab's contextual
  shelf-knob row.

If the operator still calls out a curve-line difference between EQ and
CMP after the new build, the code-level check came up clean (same
2.0f stroke, same `0xFFD9534F` accent). Investigate anti-aliasing or
grid bleed-through next.

## ▶ QUEUED — explicit follow-ups

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

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `a9e6432` (CMP tab matches EQ).
* **ctest baseline:** 678 pass / 1 not-run / 679 total.
* **OTTO submodule SHA:** `3c84a409` (unchanged this session).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (unchanged).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified.

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md`.
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename. All carried in `todo.md`.
* **Clean build before any GUI smoke** verified end-of-session.
* **No new auto-memory updates needed.** This session's design choices
  flow from existing memory entries (OTTO visual idiom but IDA-native
  implementation; sidechain on the right mirrors EQ Bypass on the right;
  pro-audio convention defaults).
