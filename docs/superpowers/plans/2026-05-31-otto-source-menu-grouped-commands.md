# Plan: Simplify the "Add OTTO source" menu in IDA's Output Mixer

## Context

When the operator right-clicks empty space in the Output Mixer, a menu offers
**Add bus / Add FX return / Add OTTO source**. Today the "Add OTTO source"
submenu lists all 32 OTTO outputs individually by name and filters out
already-added ones — the operator hand-picks one output at a time.

The operator wants this simplified. Instead of granular per-element picking, the
menu should offer a small set of **intent-level grouped commands**: grab a whole
family's elements at once, grab a single family bus, grab all family buses, or
grab the master. Picking one drum element by hand is explicitly no longer wanted.

The desired "Add OTTO source" submenu (replaces the current 32-item list):

| # | Menu item | OTTO outputs the command adds (as IDA channels) |
|---|-----------|--------------------------------------------------|
| 1 | All drum elements    | 0–15  **+ RevDelay 24–27** |
| 2 | All percs elements   | 16–19 **+ RevDelay 24–27** |
| 3 | All shaker elements  | 20–21 **+ RevDelay 24–27** |
| 4 | All hands elements   | 22–23 **+ RevDelay 24–27** |
| 5 | All OTTO elements    | 0–23  **+ RevDelay 24–27** |
| 6 | Single drum bus      | 28 |
| 7 | Single percs bus     | 29 |
| 8 | Single shakers bus   | 30 |
| 9 | Single hands bus     | 31 |
| 10| All player buses     | 28, 29, 30, 31 |
| 11| OTTO master (stereo) | master tap (see Slice C) |

Rule from the operator: items **1–5** add OTTO's RevDelay returns (24–27)
alongside the dry element taps, because elements 0–23 are pre-FX (dry) — the
operator needs the wet reverb/delay too. Items **6–11** already carry rvb/dly in
their summed signal, so they add **no** extra returns.

### Verified facts (from codebase exploration)

- **OTTO 28–31 are fixed family sub-buses** (Drums=28, Percs=29, Shakers=30,
  Hands=31), confirmed in `otto-bridge/include/ida/OttoHost.h` constants and
  OTTO's `GlobalMixer` category-bus publication. The current friendly name
  "PlayerOut1–4" is a misnomer; idx 28 always carries the drum family mix.
- **Element family ranges:** Drums 0–15, Percs 16–19, Shakers 20–21, Hands 22–23
  (`OttoHost.h` + `ottoFriendlyName` in `app/MainComponent.cpp`).
- **Persistence is keyed on the integer OTTO source index** (`setOttoSource` /
  `getOttoSource`, `ottoSourceIndex` in saved state), restored by replaying
  `addOttoOutputStrip(idx)` on load. Friendly names are runtime-derived, never
  persisted → renaming strips is safe; no migration needed.
- **`addOttoOutputStrip(int)` is idempotent** — returns the existing channel id
  if that OTTO index already has a strip. Batch adds can safely re-issue indices.
- **`addOttoOutputStrip` brackets the audio callback internally** (one
  `removeAudioCallback`/`addAudioCallback` pair per call). A naive loop would
  bracket N times → N audio glitches; the batch path must bracket once.
- **Master is NOT one of the 32 outputs.** `OttoHost` has no master accessor
  today (only `getOttoOutputLeft/Right(index)` for 0–31, plus `snapshotMaster()`
  which is meter metadata, not a buffer). OTTO's master sum lives downstream of
  all 32 taps and IDA deliberately skips it (see Slice C). Item 11 therefore
  needs a new tap.

## Design

The pane's menu items map to **lists of OTTO source indices**; a single new
batch seam adds them. The pane decides what each item means; `MainComponent`
performs one bracketed batch add. This keeps all OTTO-knowledge in the pane and
all engine-mutation in `MainComponent`, mirroring the existing split.

Introduce a master sentinel constant (e.g. `kOttoMasterSourceIndex`, a value
outside `[0,32)` such as `32`) so master flows through the same index-keyed
strip/persistence machinery as the 32 real outputs.

### Slice A — Menu restructure + batch-add seam

`app/MainComponent.cpp`, `OutputMixerPane::showBlankAreaMenu`:

- Replace the current per-output enumeration loop with the 11 grouped items
  above (a JUCE `PopupMenu` submenu). Each item builds a `std::vector<int>` of
  OTTO source indices and calls a **new** pane callback
  `onAddOttoSources(std::vector<int>)`.
- Helper inside the pane to compose the lists:
  - elements-of-family + `{24,25,26,27}` for items 1–5,
  - `{28}` / `{29}` / `{30}` / `{31}` for items 6–9,
  - `{28,29,30,31}` for item 10,
  - `{kOttoMasterSourceIndex}` for item 11.
- Optional polish (match current UX): disable a grouped item, or tick it, when
  every index it would add is already present (reuse `ottoStripIndices()`); not
  required for correctness since the batch add is idempotent.

`MainComponent`, new wiring + batch method:

- Add `onAddOttoSources` lambda paralleling the existing `onAddOttoOutputStrip`
  wiring. It calls a new `MainComponent::addOttoOutputStrips(const std::vector<int>&)`.
- Factor the **engine body** of the existing `addOttoOutputStrip(int)` into a
  non-bracketing helper `addOttoOutputStripUnbracketed(int) -> OutputChannelId`
  (the `addChannel` → `setChannelStrip` → `setChannelAudioSource` →
  `setOttoSource` → map-insert → `updateOttoActiveBusMask` sequence). Keep the
  existing single-add public method as a thin bracketed wrapper around it so
  nothing else changes.
- `addOttoOutputStrips` brackets the audio callback **once**, loops the helper
  over the indices (idempotent skip handled by the existing
  `ottoChannelByOutputIndex_` lookup), then un-brackets and, **once at the end**,
  appends the UI strips, runs `recomputeOttoOutputStripMutes()` and
  `refreshOutputDestinations()` (these are pane/relayout-level and must not run
  per-strip inside the loop).
- The old `onAddOttoOutputStrip` single seam can be retired if the new submenu
  is the only caller (verify no other caller before deleting — no dead code).

### Slice B — Rename family-bus strips (cosmetic, no persistence impact)

`OutputMixerPane::ottoFriendlyName`, indices 28–31:

- `28 → "Drum Bus"`, `29 → "Percs Bus"`, `30 → "Shakers Bus"`, `31 → "Hands Bus"`
  (replacing "PlayerOut1–4", which mislabels fixed family buses).
- Add `kOttoMasterSourceIndex → "OTTO Master"`.
- Names are runtime-derived from the index; saved projects (which store the
  index) reload with the new labels automatically.

### Slice C — OTTO master tap (item 11)

**Verification RESOLVED (2026-05-31, read-only).** Does OTTO's `GlobalMixer`
expose a stable master stereo accessor, and does `OttoHost` already surface a
master tap? **NO to both** — and the master path is more involved than first
assumed. Evidence (file:line):

- `GlobalMixer` has **no** master accessor. Only:
  `getPlayerOutputLeft/Right(category)` (sub-buses 28–31, `GlobalMixer.h:273-274`),
  `getChannelOutputLeft/Right(ch)` (0–23, `:281-288`),
  `getFxReturnOutputLeft/Right(index)` (24–27, `:291-292`).
- OTTO's master sum lives in **`PlayerManager`**
  (`sumGlobalMixerToMasterBus()` → `getMasterBus()` →
  `MasterBus::getOutputLeft/Right()`, buffer allocated once in
  `MasterBus::prepare()` — stable). But `OttoHost::renderBlock`
  **deliberately skips** OTTO's master mixdown ("Skips OTTO's master mixdown
  path … which IDA's Output Mixer owns", `otto-bridge/include/ida/OttoHost.h:112-113`),
  so those master buffers are **allocated but never populated** during IDA's
  render.
- `OttoHost` has only `getOttoOutputLeft/Right(0..31)` (`OttoHost.h:132-133`)
  and `snapshotMaster()` (meter metadata `{leftDb,rightDb,peakDb,lufs}`,
  `OttoHost.h:172-173`) — no master buffer accessor.

**Two implementation options — decide at the start of Slice C:**

- **(b) PREFERRED — sum the master IDA-side from the existing 0–31 taps.** Keeps
  "IDA's Output Mixer owns the master" true, needs **no** `external/OTTO/` edit,
  and avoids double-summing OTTO's internal master against IDA's. The master
  strip's audio source sums the appropriate OTTO output taps into a stable
  IDA-owned stereo buffer (mirroring how IDA already consumes per-output taps).
- **(a) ALTERNATIVE — drive + expose OTTO's master.** Add
  `PlayerManager::getMasterOutputLeft/Right()` returning
  `masterBus_.getOutputLeft/Right()` (~3 lines, via the **Cross-Project Inbox
  Protocol**: `Ida-Origin:` trailer, push OTTO, bump submodule SHA) **and** make
  `renderBlock` actually run OTTO's master sum when a master strip exists.
  Heavier, and re-introduces an OTTO-owned master IDA said it owns. Use only if
  (b) proves insufficient.

Remaining Slice C steps (apply to whichever option is chosen):

1. **OttoHost passthrough / source** (`otto-bridge/include/ida/OttoHost.h` +
   `.cpp`): under option (b) the master buffer is IDA-owned and summed in the
   render path from the cached 0–31 pointers; under option (a) add
   `getOttoMasterLeft/Right()` caching OTTO's master pointers post-`prepare()`.
2. **Master strip binding** in `addOttoOutputStripUnbracketed`: when the index
   is `kOttoMasterSourceIndex`, bind the master source instead of
   `getOttoOutputLeft/Right(index)`; everything else (channel alloc, strip,
   `setOttoSource(sentinel)`, map insert) is identical. Confirm
   `updateOttoActiveBusMask()` tolerates the sentinel (master consumes the full
   mix; ensure it does not mark a bogus per-bus index active).
3. **Persistence:** the load-time rebind replays `addOttoOutputStrip(idx)` over
   persisted indices — works unchanged once the sentinel is handled in the
   binding helper. Add a guard so `getOttoSource` round-trips the sentinel.

### Tests

`tests/` (Catch2). The renaming/menu live in `app/MainComponent.cpp` (UI,
operator-verified, not unit-tested), but the engine-facing seams are testable:

- **OttoHost master source** (`tests/OttoHostTests.cpp`): after `prepare`, the
  master tap (IDA-summed or OTTO-passthrough per the chosen option) returns
  non-null stable pointers; null/sentinel before prepare.
- **Batch add + idempotency** (extend `tests/OutputMixerTests.cpp` or a new
  `OttoSourceBatchTests.cpp`): adding a family group creates the expected
  channel count; re-adding the same group adds zero new channels (idempotent);
  `getOttoSource` provenance matches each requested index including the master
  sentinel; export/import round-trips the sentinel.
- Confirm no existing test asserts the literal strings "PlayerOut1–4"
  (exploration found none under `tests/`; re-grep before renaming).

## Critical files

- `app/MainComponent.cpp` — `OutputMixerPane::showBlankAreaMenu`,
  `ottoFriendlyName`, `appendOttoStrip`/`appendOttoStripImpl`,
  `ottoStripIndices`, the `onAddOttoOutputStrip` wiring (~the output-mixer
  lambda block), and `MainComponent::addOttoOutputStrip`.
- `app/MainComponent.h` — declare `addOttoOutputStrips` /
  `addOttoOutputStripUnbracketed`; `kOttoMasterSourceIndex` (or place in
  `OttoHost.h`).
- `otto-bridge/include/ida/OttoHost.h` + `otto-bridge/src/OttoHost.cpp` —
  master tap (IDA-summed source, or OTTO passthrough under option (a)) +
  render-path caching.
- `external/OTTO/src/otto-core/include/otto/mixer/GlobalMixer.h` /
  `PlayerManager` — **only under option (a)** (cross-project).
- `tests/OttoHostTests.cpp`, `tests/OutputMixerTests.cpp` (or new batch test).

## Verification

```bash
rm -rf build                                   # clean: CMake caches stale configs
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
ctest --test-dir build                         # baseline 449/450
cmake --build build --target IDA               # macOS standalone
```

Then operator eyes-on (GUI is operator-verified, not unit-tested) — Claude
builds + launches, gives terse numbered steps:

1. Right-click empty space in the Output Mixer → "Add OTTO source" shows the 11
   grouped items, no 32-item list.
2. "All drum elements" → 16 element strips + 4 RevDelay strips appear; audio
   from each is correct; no dropout during the batch add.
3. "All drum elements" again → nothing duplicates (idempotent).
4. "Single drum bus" → one "Drum Bus" strip; "All player buses" → Drum/Percs/
   Shakers/Hands Bus strips.
5. "OTTO master" → one "OTTO Master" strip carrying the full OTTO mix.
6. Save project, reopen → every strip (elements, buses, master) reloads with
   correct routing and labels.

## Out of scope / deferrals

- IDA-side empty FX-return buses (the existing "Add FX return" item) are
  untouched — items 1–5 add OTTO's RevDelay **output taps**, not IDA buses.
- No granular single-element picker (operator explicitly dropped it).
- Energy/scene, transport, and other Output Mixer work are unrelated.
