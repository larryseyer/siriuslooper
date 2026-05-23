# Session Continuation — NEXT: Output Mixer slice 3 (cycle-aware filtering + per-output-pair picker + bus rename)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Last sweep
   2026-05-23 (this session): Phase 3 TAPECOLOR mirror request acked
   and resolved. Nothing new expected until OTTO ships Phase 4.

## ▶ DONE PREVIOUS SESSION — Output Mixer slices 1 + 2, TAPECOLOR Phase 3 mirror

Three pieces of work landed in the prior session.

### Slice 1 — master bus strip
Commit `41944be`. `OutputMixerPane` + `Bus* OutputMixer::busForId`
accessor + `openInsertChainPopupForMasterBus()` + tab registration +
30 Hz refresh. Operator confirmed visually.

### Slice 2 — aux bus row + add-bus + destination picker
Commit `7195335`. Engine: `OutputMixer::busCount()`,
`setBusMainOutToHardwareOutput()`, `MainOutDest` enum,
`busMainOut()`, `busMainOutBus()`. GUI: aux-bus row to the LEFT of
master (master moved to rightmost per pro mixing-console
convention), per-strip INS + destination picker, blank-area
right-click / long-press → "Add bus" menu. Listener disambiguation:
master uses sentinel id `-1`, aux strips use 0..N-1.

### TAPECOLOR Phase 3 mirror (cross-project)
OTTO commit `be8240ef` (ack) + IDA commit `941aee5` (submodule
bumps). `external/lsfx_tapecolor` bumped `c4a8ec3` → `ec6425c6`
(DC blocker + emphasis EQ + ConvolutionStage async swap + 8
placeholder IRs). `juce_dsp` + `juce_audio_formats` arrived
transitively via existing `juce_audio_utils` link — no IDA CMake
edits needed. New `lsfx_tapecolor_ir_data` binary-data target
INTERFACE-linked automatically. Still pure plumbing on IDA's side
— no `lsfx::TapeColorProcessor` instantiated anywhere until OTTO
Phase 13.

## ▶ NEXT — Output Mixer slice 3

Operator asked, during the slice 2 review, whether
"Master / other-bus / Direct out" matched the spec. The whitepaper
sweep produced a concrete answer:

> §5.2 — "Each channel in the output mixer similarly chooses among
> physical outputs, file destinations, MIDI outputs, video outputs,
> and internal buses."

> §6.6 (bundled-OTTO paragraph) — "the output mixer alone decides
> **which physical outputs** each channel reaches… with a larger
> interface, the performer parks each channel on any available
> output pair in any combination."

So the slice 2 single "Direct out" entry was a 2-output-interface
collapse. Slice 3 honours the spec on multi-output hardware.
Operator confirmed: roll **both** the cycle-aware filtering AND
the per-output-pair picker into slice 3, with bus rename as well.

### Slice 3 scope

1. **Per-output-pair picker.** Replace the single "Direct out" entry
   with one entry per physical stereo output pair the audio device
   exposes. Source: `audioDeviceManager_.getCurrentAudioDevice()->
   getActiveOutputChannels()` paired up (1-2, 3-4, …). Engine work
   required: extend `OutputMixer` so a bus's main-out can target a
   *specific* output-pair index, not just `MixerTerminal::HardwareOutput`
   in aggregate. Likely needs a new `MixerTerminal::HardwareOutputPair{N}`
   concept or per-bus pair-index state. Match the InputMixer's
   per-strip output-pair handling if there's a precedent to mirror
   (search `setBusMainOutToHardwareOutput` and any pair-index plumbing
   on the input side first; engine extension cost depends on what's
   already in `MixerGraph`).

2. **Cycle-aware destination filtering.** Add
   `OutputMixer::busMainOutToBusWouldCycle(BusId from, BusId to)
   const noexcept` mirroring `InputMixer::busMainOutToBusWouldCycle`
   (delegates to `MixerGraph::wouldMainOutCycle`). Use it in
   `refreshOutputDestinations` to pre-filter cycle-bound targets so
   the picker never offers a route the engine would reject.
   Currently silent failure on cycle — refreshes label to current
   state but is opaque to the operator.

3. **Bus rename.** Aux buses ship as "Bus 1", "Bus 2", … with no
   way to change them. Spec implication is operator-driven names
   (the `BusConfig::name` field exists precisely for the
   operator-facing bus picker). Pattern options:
   - Right-click strip → "Rename…" → modal text-entry dialog
   - Double-click the strip's name label → inline edit
   InputMixerPane has no precedent yet; pick the most-elegant
   pro-audio convention and apply symmetrically to both mixers
   (per `feedback_default_to_professional_elegant`). Engine work:
   add `OutputMixer::renameBus(BusId, std::string)` writing to
   `BusConfig::name` (message-thread only; not on the audio thread).
   Mirror on InputMixer if InputMixerPane wants the same gesture
   (defer that to a follow-up if it widens scope).

### Order of operations inside slice 3

1. Engine first — `busMainOutToBusWouldCycle`, `renameBus`, the
   per-output-pair routing surface (the chunkiest of the three —
   may need a MixerGraph audit before designing).
2. UI next — pre-filter cycle targets, expand the picker choices,
   wire the rename gesture.
3. Build + ctest (hold 639/640 baseline) + commit + push.

### Deferred to later slices

- **Channel strips** ("one per phrase" per §6.6) — waits on M6+
  Constituent rendering; the per-channel row stays empty in slice 3.
- **File / MIDI / video output destinations** — wait on render/
  export and MIDI/video subsystem milestones.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **639 pass / 1 not-run / 640 total**
  (1 not-run is the `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel — unchanged from baseline). Test #169 (`permanent bypass:
  kill every generation`) is flaky on first run, passes on re-run
  — pre-existing plug-in-host process-kill timing dependency,
  unrelated to Output Mixer work.
- `master` HEAD on origin: `941aee5` (submodule bumps for TAPECOLOR
  Phase 3 mirror).
- OTTO submodule SHA: `be8240ef` on `origin/main` (Phase 3 ack).
- lsfx_tapecolor submodule SHA: `ec6425c` on `main` (Phase 1+2+3).

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (underscores —
  `project_whitepaper_path`).
- **Operator actions still pending** (between sessions; agent
  cannot perform; tracked in `todo.md`): notarytool keychain
  `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
- **Clean build before any GUI smoke** (`feedback_clean_builds`):
  `rm -rf build && cmake -B build -S . -G Ninja
   -DCMAKE_BUILD_TYPE=Release && cmake --build build --target
   IdaTests IDA -j`.

## ▶ PARALLEL PLAN — IDA-specific plugin docs (docs-only, no engine touch)

A separate session (2026-05-23) wrote a complete docs/specs plan
covering every plugin IDA ships beyond the four shared with OTTO:

- 5 IDA-only audio inserts (Phaser, Chorus, Pitch Changer, Audio
  Gate, TAPECOLOR) — reuses existing `IInternalFxAdapter`.
- 8 MIDI inserts (Channel Filter, Input Velocity Control, MIDI
  Remap, Quantize, Limit Notes to Scale, plus Velocity / Timing /
  Swing Energy-bound) — needs new `IMidiFxAdapter` subsystem.
- 4 offline FX (Audio Peak Quantize, Autotune, Short Notes Removal,
  Thin Controller Data) — needs new offline analyze+apply subsystem.
- MIDI Learn across every transport + every mixer control — needs
  new binding-registry subsystem.

**Plan file:** `~/.claude/plans/there-are-several-things-moonlit-twilight.md`.

**Scope:** whitepaper amendments (§6.6/6.7/6.8/6.11/6.12/new 6.13/
15.6/17.4), 6 new design docs under `docs/design/` (umbrella,
audio-insert, midi-insert, offline, midi-learn, energy-binding),
user-guide stub, stale CLAUDE.md V7→V8 path fix, IDA→OTTO inbox
entry. **No code** — every implementation lives in its own future
slice.

**No collision with slice 3.** Slice 3 touches `engine/` + Output
Mixer UI; this plan touches only `docs/**`, `CLAUDE.md`, and
`external/OTTO/CROSS_PROJECT_INBOX.md`. Either can land in either
order.

**When to execute:** operator's call. Recommended after slice 3
ships (or whenever a docs-focused session has bandwidth) — the plan
is self-contained and the executor just reads it top-to-bottom.
