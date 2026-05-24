# Session Continuation — NEXT: operator smoke of Output Mixer slice 3, then slice 4 scope

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Last sweep 2026-05-23.
   No new entries; everything is acked or informational. Phase 4 from
   OTTO is the next expected event.

## ▶ DONE PREVIOUS SESSION — Output Mixer slice 3

Commit `0e4924e`. Engine + UI + tests; pushed to `origin/master`.

### Engine (engine/src/OutputMixer.cpp, engine/include/ida/OutputMixer.h)
- `busMainOutToBusWouldCycle(BusId, BusId) const noexcept` — pre-check
  predicate mirroring `InputMixer`; delegates to
  `MixerGraph::wouldMainOutCycle`.
- `setBusMainOutToHardwareOutput(BusId, int pairIndex)` — new canonical
  overload, accepts master (`BusId{0}`); single-arg overload kept as a
  wrapper.
- `busHardwareOutPair(BusId) const noexcept` — read-back accessor.
- `renameBus(BusId, std::string)` — message-thread writer; rejects
  master + unknown ids. `Bus::setName` added to support it.
- `renderBuffer` Step 3 now writes aux→HardwareOutput direct to physical
  outputs at the bus's pair offset (bypassing master — the real "Direct
  out" semantic; slice 2 had folded it through master). Step 4 master
  also honours its pair index. `resolveHardwarePair` lambda handles
  bounds-fallback (requested pair > device channel count falls back to
  pair 0) and the degenerate mono output case.
- `MixerMainOut::hardwareOutPair` field added in
  `core/include/ida/MixerGraphState.h`; `SessionFormat.cpp` emits/reads
  optional `hardwareOutPair` field with back-compat default 0. Full
  export→import round-trip preserves pair indexes.

### UI (app/MainComponent.cpp)
- `OutputMixerPane::DestChoice` gains `pairIndex`; `StripDest` gains
  `currentPairIndex`. Ticking uses a `destMatches` helper that includes
  the pair on HardwareOutput entries.
- Per-strip context overlay (`StripContextOverlay` nested class): an
  invisible click-catcher sitting on each aux strip's top name band.
  Catches right-click (desktop) and long-press (iOS — 500 ms timer
  matching the slice-2 "Add bus" pattern). On "Rename…" swaps in an
  inline `juce::TextEditor` using the TapesPane row pattern: Return /
  focus-lost commit, Escape cancels, `committed_` flag prevents
  double-fire, OTTO `bg3`/`textPrimary`/`accent` colours. Pair
  `feedback_ios_long_press_pairs_right_click` is now realised in
  Output Mixer.
- Master strip gains a `masterDestButton_` showing per-pair entries
  only (no Master/bus entries — master is the terminal). Visible only
  when the device exposes more than one stereo pair; hidden on
  2-channel devices.
- `refreshOutputDestinations` rebuilt: enumerates active output pairs
  from `audioDeviceManager_.getCurrentAudioDevice()->
  getActiveOutputChannels()` ("Out 1-2", "Out 3-4", …), pre-filters
  cycle-bound bus targets via the new predicate, populates per-aux
  picker, populates master per-pair picker.
- `onBusRename`, `onMasterDestinationChosen` callbacks added; the
  aux `onBusDestinationChosen` now passes `dest.pairIndex` through to
  the engine.

### Tests (tests/OutputMixerTests.cpp)
Three new Catch2 cases (slice3 tag):
1. Cycle predicate true/false/unknown.
2. Per-output-pair routing + persistence round-trip + master on
   non-zero pair.
3. Rename writes BusConfig::name, rejects master, persists.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **642 pass / 1 not-run / 643 total** (the
  not-run is the `MainComponentPluginEditorTests_NOT_BUILT` sentinel —
  unchanged). Net +3 from the prior 639/640 baseline as predicted.
- `master` HEAD on origin: `0e4924e` (Output Mixer slice 3).
- OTTO submodule SHA: `be8240ef` (Phase 3 ack, unchanged).
- lsfx_tapecolor submodule SHA: `ec6425c` (Phase 1+2+3, unchanged).

## ▶ OPERATOR SMOKE PENDING (slice 3 verification)

Clean rebuild done this session. Launch `IDA.app` (Desktop alias) and
verify on macOS:
- Aux strip picker lists Master, every other aux bus, and one entry
  per active hardware output pair from the audio device.
- Routing aux A → aux B → aux A: the second pick of A is **not
  offered** for B's picker (cycle pre-filter).
- Right-click an aux strip's name band → "Rename…" → type → Return
  or click-away → name persists, shows in other strips' pickers,
  survives undo/redo and session reload. Escape cancels.
- Long-press an aux strip's name band (touch / trackpad force-touch)
  yields the same menu.
- On a multi-out device: master strip exposes a per-pair picker (no
  bus entries); routing master to "Out 3-4" lands the stereo mix at
  physical outputs 3-4.
- On a 2-channel device: master destination button is hidden.
- Routing aux to "Out 3-4" lands at physical outputs 3-4 bypassing
  master entirely.
- Master has no "Rename…" context menu (right-click anywhere on it
  produces no per-strip menu — gestures only on aux strips this slice).

If the smoke surfaces a bug, fix on top with a follow-on commit (don't
amend `0e4924e`).

## ▶ AFTER OPERATOR APPROVES — pick the next slice

Two ready candidates; either can land independently.

### Slice 4 — InputMixer rename parity
The handoff for slice 3 deferred bus rename on the input side ("defer
to a follow-up if it widens scope"). Engine work: `InputMixer::
renameBus(BusId, std::string)` mirroring `OutputMixer::renameBus`
(message-thread, rejects master if input ever ships a master concept —
today input has no master, so just unknown-id rejection). UI work:
mirror the `StripContextOverlay` pattern onto InputMixerPane strips
plus the FX-return strips. Picks up the same iOS long-press pair.
Probably one session.

### Parallel docs plan — IDA-specific plugin specs
The unchanged `~/.claude/plans/there-are-several-things-moonlit-twilight.md`
plan from 2026-05-23 is still ready to execute. Pure docs/specs; no
engine touch; safe to land in any order vs. slice 4.

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
   IdaTests IDA -j`. (Already done this session; the slice-3 .app
  in `build/app/IDA_artefacts/Release/IDA.app` is fresh.)
