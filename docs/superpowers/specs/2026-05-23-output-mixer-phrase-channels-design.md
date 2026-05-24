# Output Mixer phrase-channel strips — slice 5 design

Status: **active.** Approved 2026-05-23 (Auto Mode session). The slice
decomposes into 5a/5b/5c; 5a starts immediately. The "phrase rename"
gesture and per-phrase automation-bound state are deliberately out of
scope (separate later slices).

Cross-refs:
- `docs/design/mixer-design.md` decision 3 (one channel per phrase),
  decision 10 (per-phrase automation Constituent-bound — long term),
  "Engine reality" section (Output Mixer comes up with no channels;
  render-path not yet wired; per-phrase meters stay silent until then).
- `docs/IDA_Whitepaper_V8.md` §5.2 / §6.6 / §7.1 (output mixer is a
  full creative mixer with per-Constituent channel strips).
- `engine/include/ida/OutputMixer.h` (existing `addChannel`,
  `setChannelStrip`, `routeChannelToBus`, `setBusMainOutToHardwareOutput`).
- `ui/include/ida/TimelineViewState.h` (`PillState` is the canonical
  phrase enumeration — id, name, primaryTape, etc.).
- `app/MainComponent.cpp` line 1319 (LEFT band reserved comment in
  `OutputMixerPane::resized()`).

## Goal

A phrase exists in the session → a strip exists on the Output Mixer's
LEFT band. The operator can set per-phrase gain / mute / destination
(master, aux bus, or hardware-output pair). Strip name is sourced
from `Constituent::name()` and is read-only in this slice. Live
metering stays silent until the render path lands.

## Hard invariants preserved

- **Stereo only** (whitepaper §6.1). Every phrase channel is a stereo
  pair; `ChannelStrip<Audio>` already enforces this.
- **Audio-thread contract** (`docs/RT_SAFETY_CONTRACT.md`). All new
  engine setters are message-thread only; the audio-thread render
  path is untouched in 5a and remains so in 5b until the render-path
  milestone lands.
- **Two mixers totally separate** (memory:
  `project_two_mixers_totally_separate`). No shared state across
  InputMixer and OutputMixer; the per-channel hardware-output routing
  added here is OutputMixer-internal.
- **OTTO as Output Mixer source** (memory:
  `project_otto_as_output_mixer_source`). Bundled OTTO's 32 stereo
  outputs are *additional* Output Mixer channel strips; this slice
  does not preclude that — OTTO channels and phrase channels coexist
  on the LEFT band when both arrive.

## Decomposition

### 5a — Engine surface (this session)

Two additions to `OutputMixer`, with Catch2 coverage:

1. **`removeChannel(OutputChannelId)` with id reuse.** Pairs with the
   existing `addChannel`. Must support phrase-add/remove churn
   without burning through `kMaxOutputChannels = 32`. Implementation:
   - Erase from `channels_` + `channelNodeIds_` (swap-erase OK; index
     positions are not exposed publicly).
   - Zero the channel's row of `sendMatrix_` so a re-used id starts
     with all-zero sends (the existing `addChannel` will overwrite
     the master slot to 1.0 on the re-mint).
   - Maintain a small free-list of OutputChannelId values; `addChannel`
     prefers it over incrementing `nextOutputChannelId_`. Free-list is
     a `std::vector<std::int64_t>` member (not a deque — tiny + bounded).
   - Unknown id → no-op return. No-op is silent; the UI never asks for
     unknown ids in steady state.

2. **`setChannelMainOutToHardwareOutput(OutputChannelId, int pairIndex)`
   + `channelMainOutHardwareOutPair(OutputChannelId)` read-back.**
   Mirror of the bus-side API added in slice 3. A phrase channel's
   main-out can be: master (default, via existing `routeChannelToBus`
   to `BusId{0}`), another aux bus (existing `routeChannelToBus` to a
   non-master bus), or direct to a stereo hardware-output pair
   (this new API). Implementation:
   - Look up the OutputChannel's `MixerNodeId` via the same parallel
     vectors used for buses.
   - Delegate to `MixerGraph::setMainOut(node, hardwareOutputTerminal)`.
   - Stash `pairIndex` using the same storage location/pattern slice 3
     used on the bus side (`MixerMainOut::hardwareOutPair` or the
     equivalent runtime field — 5a's implementation mirrors whatever
     the bus version does; default 0 preserves master-routed channels).
   - Read-back returns 0 for unknown ids (defensive default, same as
     bus version).
   - Negative `pairIndex` clamps to 0; audio-thread render code paths
     will bounds-check against the device's actual output count at
     render time (same pattern as the bus version) — but the render
     code itself isn't touched in 5a because phrase channels don't
     yet feed audio.

**Persistence:** `exportGraphState` / `importGraphState` extended to
include the `hardwareOutPair` per channel and (optionally) per-channel
main-out destination kind. The OutputChannelId → ConstituentId
mapping is *not* yet persisted in 5a — that lands in 5c.

**Tests** (`tests/OutputMixerTests.cpp`, tag `[output-mixer][slice5]`):
- `removeChannel` of an unknown id is a no-op.
- `removeChannel` of a real id releases the id; the next `addChannel`
  reuses it.
- `removeChannel` zeros the freed channel's sendMatrix row (a re-used
  id starts at unity-into-master per `addChannel`, not at the
  removed channel's previous send levels).
- `setChannelMainOutToHardwareOutput` rejects unknown ids; accepts
  master-bus-routed channels and switches them to HardwareOutput;
  `channelMainOutHardwareOutPair` round-trips through persistence.
- `setChannelMainOutToHardwareOutput` with `pairIndex < 0` clamps to 0.

### 5b — UI shell (next session)

`OutputMixerPane`:
- New `PhraseStripInfo { ConstituentId id; juce::String name; }` and
  `setPhraseStrips(const std::vector<PhraseStripInfo>&)`. Behaves like
  `setBusStrips` — clears + rebuilds the LEFT-band row.
- Per phrase strip: `CompactFaderStrip(channelIdx,
  ChannelType::Instrument)`, INS button, destination picker. Pickers
  reuse the slice-3 `DestChoice` machinery (kind ∈ {Bus,
  HardwareOutput}, per-strip cycle-pre-filtered choice list, pair
  index for HardwareOutput entries). Name is read-only in 5b — no
  `StripContextOverlay`.
- `resized()` fills the LEFT band left-to-right with phrase strips in
  PillState order. The existing aux+master right-anchored layout
  is unchanged.

Gesture relays (mirroring the aux-bus relay shape):
- `onPhraseGain(int phraseIdx, float gainLinear)`
- `onPhraseMute(int phraseIdx, bool muted)`
- `onPhraseInsertChainClicked(int phraseIdx)`
- `onPhraseDestinationChosen(int phraseIdx, DestChoice dest)`

`MainComponent`:
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
     + `setChannelStrip(...)` with a fresh `ChannelStrip<Audio>`; insert
     into the map.
  5. Build a `PhraseStripInfo[]` parallel to the pill order; push
     via `outputMixerPane_->setPhraseStrips(...)`. The strip-row
     ordering matches PillState ordering (DFS tree walk).
  6. Call `refreshOutputDestinations()` so the picker choice lists
     include the new phrase channels' per-channel cycle-filtered
     targets (currently no cycles for phrase→bus, but the same code
     path applies symmetrically).
- Hook `refreshOutputMixerPhraseChannels()` into the existing refresh
  cascade (`refreshPerformance` / `refreshPreparation` already fire
  on every session edit). One call site at the top of those refresh
  methods is enough.
- Wire the four new pane callbacks; each looks up the strip's
  ConstituentId via the parallel `PhraseStripInfo[]`, finds the
  OutputChannelId via the map, and calls the corresponding
  `OutputMixer` API bracketed by audio-callback pause.

**Operator smoke** (5b): Mark Out a capture → confirm a phrase strip
appears on the LEFT band. Move the fader, mute / unmute, switch the
destination through master / aux bus / hardware-output pair.
Remove the underlying phrase via Preparation → confirm the strip
disappears.

### 5c — Session persistence (after 5b smoke)

Without 5c, save+load drops phrase-channel mix state (the
ConstituentId → OutputChannelId mapping is in-memory only). 5c
extends `SessionSnapshot` to carry:
- The phrase-channel map as a list of `(ConstituentId, OutputChannelId)`
  pairs.
- The existing `OutputMixer::exportGraphState()` snapshot (already
  serialized through `SessionFormat`).

On load: walk the saved Constituent root, mint phrase channels in
pill order, restore strip state from the engine snapshot, rebuild the
map. The Constituent ids are stable across CoW edits and across
save/load, so the binding holds.

5c does NOT change the Constituent type. The long-term WP §6.7 work
(per-phrase mix lives ON the Constituent itself) remains a separate
later slice.

## Out of scope

- **Phrase rename.** Renaming a phrase = `Constituent::withName(...)`
  via the undo stack. Touches the constituent edit pipeline, not the
  mixer. Separate slice.
- **Live metering on phrase strips.** Stays silent. Renders zero on
  the dual peak+LUFS OTTO meter (consistent with
  `project_channel_meter_dual_otto`). When the render path lands,
  the existing `setLevel` / `setLUFSLevel` accessors light up
  immediately — no UI change needed.
- **FX returns on the LEFT band.** FX returns are session-bound, not
  phrase-bound — they live with master+aux on the RIGHT band when
  they arrive (mixer-design decision 9). Slice 5 doesn't add them.
- **OTTO-as-output-mixer-source phrase-channel interaction.** When
  the bundled OTTO presents 32 outputs as additional strips
  (`project_otto_as_output_mixer_source`), those are *additional*
  LEFT-band strips that coexist with phrase channels. Slice 5 doesn't
  block that; the LEFT band's `resized()` just accommodates both
  groups when both exist.

## Risks and decisions captured

- **Id reuse vs id sticky.** Free-list approach chosen so the
  32-channel cap doesn't burn out over a session with many
  phrase add/remove cycles. The alternative (never reuse ids, raise
  the cap) would force the audio-thread to defensively skip
  removed-but-not-shrunk channel slots, which complicates the render
  loop more than the free-list complicates `removeChannel`.
- **Per-channel `hardwareOutPair` storage.** Mirrors whatever slice 3
  did on the bus side (likely `MixerMainOut::hardwareOutPair` in the
  graph-snapshot type plus a runtime accessor). Default value 0
  preserves backward compatibility with existing persisted state —
  `importGraphState` reads the field with a fallback default.
- **Phrase channel ordering = PillState ordering.** PillState comes
  from a DFS walk of the Constituent tree, which is the same
  ordering the Preparation tab uses for its pill rendering. Reusing
  it keeps the mixer's left-to-right phrase strip order coherent
  with the timeline's left-to-right pill order, which is what an
  operator would expect.
