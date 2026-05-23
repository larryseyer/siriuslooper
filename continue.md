# Session Continuation — NEXT: Output Mixer slice 2 (aux bus row + add-bus) OR T6 persistence

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Ack any
   `[FROM OTTO → IDA]` entries. Last sweep 2026-05-23: both 2026-05-23
   TAPECOLOR entries acked/resolved; nothing new expected until OTTO
   ships Phase 3.

## ▶ DONE THIS SESSION — Output Mixer tab slice 1

Slice 1 of the operator's mixer→transport roadmap (Option A from the
prior `continue.md`). The Output Mixer tab now exists with a single
**Master** bus strip — the smallest complete capability that mirrors
the proven Input bus-row shape.

- **`OutputMixer::busForId(BusId)` accessor** (`engine/include/ida/OutputMixer.h:117-122`,
  `engine/src/OutputMixer.cpp:174-180`). Mirrors `InputMixer::busForId`;
  message-thread setup, atomic-backed reads safe for 30 Hz UI refresh.
- **`OutputMixerPane` nested class** (`app/MainComponent.cpp` after
  `InputMixerPane`, ~95 LOC). Single `CompactFaderStrip` typed `Bus`,
  named "Master", with INS button stacked below mirroring T5's
  stacking. No destination picker (master is terminal). No solo
  (no-op on the canonical sink). No detail panel.
- **`openInsertChainPopupForMasterBus()`** (`app/MainComponent.cpp`
  after `openInsertChainPopupForBus`). Same shape as the input bus
  variant — detach/`setEffectChain`/re-attach bracket on
  `outputMixer_->busForId(BusId{0})`.
- **Tab registration + 30 Hz refresh** wired inside the existing
  `tier_ >= Professional` gate; `refreshOutputMixer()` joins
  `refreshInputMixer()` on the timer.
- **Build:** `cmake --build build --target IdaTests IDA -j` green.
- **Tests:** `ctest --test-dir build` → 639/640 pass, 1 not-run
  (`MainComponentPluginEditorTests_NOT_BUILT` sentinel — baseline
  unchanged).

### Audible verification NOT done this session

Operator should eyes-on confirm on next launch:
- Output Mixer tab visible after Input Mixer.
- Master strip shows fader + dual peak/LUFS meter + INS button.
- Fader move audibly changes master output level.
- Mute toggles output.
- INS opens InsertChainPopup; drop RVB → hear master-bus reverb tail.
- Meter responds with signal.

If something misbehaves the most likely suspects are:
- `Bus::setGain` is non-atomic w.r.t. the CompactFaderStrip's default
  range — `0..1` linear is the engine contract, matches strip default.
- Master peak/LUFS depend on the master bus running its `process()`
  path; if no channels exist (slice 1 default), the master mixBuffer is
  zero and meters correctly read silence — that's the expected state
  until a channel is added or DirectLayer feeds the path.

## ▶ NEXT — operator's mixer→transport roadmap continues

### Option A — Output Mixer slice 2 (aux bus row + add-bus)

Add the aux-bus row to the left of the master strip plus a blank-area
right-click → "Add bus". Mirrors the Input pane's `onAddBus` shape;
calls `outputMixer_->addBus(BusConfig{2, "Bus N", BusKind::Bus})` inside
a remove/addAudioCallback bracket. Each aux strip gets the same
fader/mute/INS treatment + a destination picker (master / another bus
/ hardware output). Unblocks T4 Sends UI more fully than slice 1 does.

### Option B — Output Mixer slice 3 (phrase channels)

Channels in the Output Mixer are "one per phrase" per whitepaper §6.6.
Phrases as a runtime concept land at M6+ (Constituent rendering). This
slice waits until at least one phrase can be defined. Skip until then.

### Option C — T6 P4/P5 persistence wiring

The EffectChain already round-trips through `SessionFormat`; with the
master bus's INS now mutable from the UI, T6 would verify save/load on
a session that has master inserts configured. Smaller scope than A.

**Recommendation:** A. T4 (Sends UI) depends on multiple buses
existing, and aux-bus add/remove on Output mirrors a proven Input
pattern almost line-for-line.

## ▶ BASELINE

- `ctest --test-dir build`: **639 pass / 1 not-run / 640 total**.
- `master` HEAD on origin: pending this session's commit.
- OTTO submodule SHA: `41dcae25` on `origin/main` (unchanged).
- lsfx_tapecolor submodule SHA: `c4a8ec3` on `main` (unchanged; no DSP
  instantiation on IDA's side until OTTO Phase 13).

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
