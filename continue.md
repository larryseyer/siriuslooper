# Session Continuation — NEXT: slice 5b (Output Mixer phrase-strip UI shell + MainComponent mirror)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Last sweep
   2026-05-23 covered TAPECOLOR Phase 4 + 5 (acked in IDA commit
   `6e1e4d8`, OTTO commit `3c84a409`). Next expected OTTO event is
   TAPECOLOR Phase 6 (tape-hiss noise floor).
2. Read the slice 5 design doc —
   `docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`.
   Slice 5a is shipped (commit `c310c95`); the open work is slice 5b
   (UI shell + MainComponent mirror) and then 5c (session persistence).

## ▶ DONE LAST SESSION

One commit landed on `origin/master`:

- **`c310c95`** — `feat: OutputMixer slice 5a — phrase-channel engine
  surface (removeChannel + per-channel hardware-output routing)`.
  New `OutputMixer::removeChannel(OutputChannelId)` with id reuse via
  a free-list (so phrase add/remove churn doesn't burn through
  `kMaxOutputChannels = 32`); new
  `OutputMixer::setChannelMainOutToHardwareOutput(OutputChannelId, int)`
  + `channelMainOutHardwareOutPair(OutputChannelId)` mirror of the
  slice-3 bus-side API; per-channel `hardwareOutPair` round-trips
  through `OutputChannelState` persistence. Five new
  `[output-mixer][slice5]`-tagged tests cover unknown-id no-op,
  free-list reuse, sendMatrix-row zeroing on remove, persistence
  round-trip, and negative-pair clamping. **Engine-only — no UI, no
  MainComponent wiring; audio-thread render path untouched in 5a.**

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **648 pass / 1 not-run / 649 total**
  (the not-run is the `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel — unchanged). The +5 vs the prior 643-pass baseline are
  the slice-5a tests.
- `master` HEAD on origin: `c310c95` (slice 5a).
- OTTO submodule SHA: `3c84a409`.
- lsfx_tapecolor submodule SHA: `d8b06b1` (Phase 1+2+3+4+5).

## ▶ NEXT — slice 5b (UI shell + MainComponent mirror)

Spec section: 5b in
`docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`.
**5b touches MainComponent + OutputMixerPane → ends with operator
eyes-on smoke** (Mark Out → phrase strip appears on Output Mixer's
LEFT band; fader/mute/destination picker behave; remove the phrase
from Preparation → strip disappears). Do not bundle 5b smoke into
the same session as new engine work — losing the smoke checkpoint
costs more than it saves.

### `OutputMixerPane` additions

- New nested type `PhraseStripInfo { ConstituentId id; juce::String name; }`
  and `setPhraseStrips(const std::vector<PhraseStripInfo>&)`. Behaves
  like `setBusStrips` — clears + rebuilds the LEFT-band row.
- Per phrase strip: `CompactFaderStrip(channelIdx,
  ChannelType::Instrument)`, INS button, destination picker. Pickers
  reuse the slice-3 `DestChoice` machinery (kind ∈ {Bus,
  HardwareOutput}, per-strip cycle-pre-filtered choice list, pair
  index for HardwareOutput entries). Name is read-only in 5b — no
  `StripContextOverlay`.
- `resized()` fills the LEFT band left-to-right with phrase strips in
  PillState order. The existing aux+master right-anchored layout on
  the RIGHT band is unchanged.

Gesture relays (mirroring the aux-bus relay shape from slice 3):
- `onPhraseGain(int phraseIdx, float gainLinear)`
- `onPhraseMute(int phraseIdx, bool muted)`
- `onPhraseInsertChainClicked(int phraseIdx)`
- `onPhraseDestinationChosen(int phraseIdx, DestChoice dest)`

### `MainComponent` additions

- New `phraseChannelByConstituent_`: `std::unordered_map<int64_t,
  OutputChannelId>` keyed by `ConstituentId.value()`.
- New `refreshOutputMixerPhraseChannels()`:
  1. Call `selectTimelineView(...)` for the current pill list (same
     enumeration the Preparation tab uses).
  2. Compute the set of pill ConstituentIds.
  3. For each entry in `phraseChannelByConstituent_` whose key isn't
     in the new set: `outputMixer_->removeChannel(id)`; erase from
     the map. Bracket the audio callback (same pattern as every other
     OutputMixer mutation in MainComponent).
  4. For each pill not yet in the map: `outputMixer_->addChannel(Audio)`
     + `setChannelStrip(...)` with a fresh `ChannelStrip<Audio>`;
     insert into the map.
  5. Build a `PhraseStripInfo[]` parallel to the pill order; push
     via `outputMixerPane_->setPhraseStrips(...)`. The strip-row
     ordering matches PillState ordering (DFS tree walk).
  6. Call `refreshOutputDestinations()` so the picker choice lists
     include the new phrase channels' per-channel cycle-filtered
     targets.
- Hook `refreshOutputMixerPhraseChannels()` into the existing refresh
  cascade (`refreshPerformance` / `refreshPreparation` already fire
  on every session edit). One call site at the top of those refresh
  methods is enough.
- Wire the four new pane callbacks; each looks up the strip's
  ConstituentId via the parallel `PhraseStripInfo[]`, finds the
  `OutputChannelId` via the map, and calls the corresponding
  `OutputMixer` API bracketed by audio-callback pause.

### Operator smoke (end of 5b)

`rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
 && cmake --build build --target IdaTests IDA -j` (clean build required
for GUI verification per `feedback_clean_builds`). Then:
1. Launch the Desktop `IDA` alias.
2. Mark Out a capture → a phrase strip appears on the Output Mixer's
   LEFT band.
3. Move the fader, mute / unmute, switch destination through
   master / aux bus / hardware-output pair.
4. Remove the underlying phrase via Preparation → strip disappears.
5. (No live metering on phrase strips in 5b — stays silent until the
   render-path milestone lands.)

## ▶ AFTER 5b — 5c is a NEW session

5c extends `SessionSnapshot` to carry the `(ConstituentId,
OutputChannelId)` mapping so save+load preserves the phrase-channel
mix. Without 5c, mix state survives the engine snapshot but the
binding to which constituent it belongs to is in-memory only. Spec
details: 5c section of the design doc.

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
