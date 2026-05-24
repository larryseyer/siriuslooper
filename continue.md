# Session Continuation — NEXT: slice U (tabbed ChannelDetail UI on both mixers) || slice P (session persistence)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session start
   2026-05-24, all entries through TAPECOLOR Phase 5 + the "OTTO pin
   bumped to c4a8ec3" informational notice are acked. Next expected
   OTTO event is TAPECOLOR Phase 6 (tape-hiss noise floor).
2. Read the mixer-symmetry design doc —
   `docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
   E1 + **E2 + E3 all landed this session** (see DONE LAST SESSION).
   Remaining: **slice U** (tabbed `ChannelDetail` UI on both mixers)
   and **slice P** (session persistence of the slice-5c
   `ConstituentId → OutputChannelId` map; `preFaderSends` and
   `mainOutKind` are already on disk via E2 + E3).

## ▶ DONE LAST SESSION

Two engine slices in one session: **E2 (per-channel pre-fader send
toggle, BOTH mixers)** and **E3 (channelMainOut accessor on
OutputMixer + send-zero bypass on Bus + radio-style
routeChannelToBus)**.

### Slice E2 — per-channel pre-fader send toggle on BOTH mixers

- `OutputMixer::channelSendIsPreFader(OutputChannelId)` + setter,
  parallel `channelPreFaderSends_` storage (vector<char> for the
  std::vector<bool> avoidance), default false.
- `InputMixer::channelSendIsPreFader(ChannelId)` + setter, sparse
  `unordered_map<int64_t, char>` (matches the rest of InputMixer's
  map-based registries).
- Persistence: `preFaderSends` on `InputChannelState` and
  `OutputChannelState` (default false; emitted only when true so the
  on-disk shape stays compact). SessionFormat.cpp round-trips both.
- Audio thread: a pre-strip scratch is filled BEFORE
  `ChannelStrip::process` mutates the post-strip scratch. Send-matrix
  accumulator (OutputMixer Step 2; InputMixer renderInputGraph send
  loop) picks per-channel between the two scratches. The strip still
  runs in both modes — only the send tap source switches; meters and
  the dry-out path stay post-fader.
- Memory cost: OutputMixer adds 2 MB resident (32 ch × 2 strip ch ×
  8192 samples × 4 B); InputMixer adds 64 KB. Unconditional
  allocation in the ctor; mid-block toggles are atomic-free (the
  flag is the only thing read per-block).
- Tests: 9 new test cases / 102 assertions under `[send][pre-fader]`
  spanning both mixers (defaults + setter round-trip + muted-channel
  behavior + persistence).

### Slice E3 — `channelMainOut` accessor + send-zero bypass

- New `OutputMixer::channelMainOut(OutputChannelId)` returning
  `MainOutDest { Bus, HardwareOutput }` and
  `channelMainOutBus(OutputChannelId)` (mirror of `busMainOut` /
  `busMainOutBus`). Default Bus(master).
- **`OutputMixer::routeChannelToBus` semantics CHANGED**: for a
  Bus-kind target it now sets the channel's main-out AND
  radio-zeros every other Bus-kind send (including master). FX-
  return sends are preserved (sends-tab surface is independent of
  main-out picker). For an FxReturn-kind target the call is purely
  additive — main-out untouched.
- `OutputMixer::setChannelMainOutToHardwareOutput` flips the kind
  to HardwareOutput AND zeros every Bus-kind send (so master+aux
  sends never coexist with HardwareOutput).
- `Bus::sendInputActive()` + `Bus::adjustActiveSenderCount(int)` +
  atomic `activeSenderCount_`. Counts bumped/decremented by mixer
  mutators on every 0↔nonzero send transition and main-out hop;
  reads are `memory_order_relaxed` (mutator contract serializes
  with the audio thread). Bus::process itself is unchanged — the
  bypass lives in the mixer render loops:
  `if (! bus.sendInputActive()) continue;` for both
  `OutputMixer::renderBuffer` and `InputMixer::renderInputGraph`.
  Standalone Bus tests don't need to touch the counter.
- InputMixer mutator wrappers
  (`setChannelMainOutToBus`/`setBusMainOutToBus`/`setChannelSend`/
  `setBusSend`/...): now compute the old vs new target and adjust
  bus counts. `removeChannel` walks send edges + main-out edge
  before tearing down the node so counts don't leak.
- MainComponent picker rewrite: the slice-5b inference rule ("if
  every send is zero, then HardwareOutput") is GONE. The phrase-
  strip destination panel reads `channelMainOut` directly. The
  radio mutator (`onPhraseDestinationChosen`) dropped its manual
  zero-loop — `routeChannelToBus` does it now.
- Persistence: `OutputChannelState.mainOutKind` (enum
  `OutputChannelMainOutKind { Bus, HardwareOutput }`) +
  `mainOutBus`. SessionFormat.cpp emits `mainOutKind:
  "HardwareOutput"` only when non-default; same minimal-shape
  pattern as `preFaderSends`. Import flow: replay sends first,
  then `setChannelMainOutToHardwareOutput` if the persisted kind
  is HardwareOutput.
- Tests: 9 new test cases / 50 assertions under
  `[channel-main-out][send-bypass]`. Plus 4 existing tests
  updated to use `BusKind::FxReturn` (additive) where they were
  written under the old additive-Bus semantics.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **670 pass / 1 not-run / 671 total**
  (the not-run is the `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel — unchanged). +18 vs the 652 baseline = E2's 9 cases +
  E3's 9 cases.
- `master` HEAD on origin: will be bumped this commit (E3 land).
- OTTO submodule SHA: `3c84a409` (unchanged).
- lsfx_tapecolor submodule SHA: `d8b06b1` (Phase 1+2+3+4+5;
  unchanged).
- 4 OutputMixer tests were updated to reflect the new radio
  semantics. Search `BusKind::FxReturn` in tests/OutputMixerTests.cpp
  if you need the exact list.

## ▶ NEXT — slice U (UI) then slice P (the bits not already persisted)

Spec sections U and P in
`docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
**E1 + E2 + E3 all landed; engine surface is complete.** U is the
heavy slice now (tabbed `ChannelDetail` on both mixers); P shrunk
to just the slice-5c map persistence (preFaderSends + mainOutKind
already round-trip).

### Slice U — Tabbed `ChannelDetail` on both mixers

- Replace the bare `ChannelDetailPanWidTab detailPanel_` on
  `InputMixerPane` + `OutputMixerPane` with
  `otto::ui::ChannelDetail detailPanel_` (the tabbed wrapper from
  OTTO via the submodule).
- Wire each tab listener: `PanWidTabListener` (already live),
  `EQTabListener`, `CMPTabListener`, `SendsTabListener`.
- Sends tab UI: pre-filter the FX-return list by the OutputMixer /
  InputMixer's `busKindAt` + `busMainOutToBusWouldCycle` (cycle
  prevention — spec D5). Tabs hidden per row type: channel strips
  show all 4; FX-return strips hide Sends; aux-bus strips hide
  pan/width.
- Sends tab fires per-(channel, fxReturn) send-level changes +
  per-channel pre/post-fader toggle. MainComponent bridges to
  `routeChannelToBus(chId, fxReturnId, level)` (additive, since
  the target is FxReturn-kind — see E3 semantics) and
  `setChannelSendIsPreFader`.
- Both panes use the same `ChannelDetail` type instanced twice
  per `project_two_mixers_totally_separate`.
- Operator eyes-on required (per `feedback_clean_builds`):
  `rm -rf build && cmake -B build -S . -G Ninja
   -DCMAKE_BUILD_TYPE=Release && cmake --build build --target
   IdaTests IDA -j`.

### Slice P — Slice-5c (`ConstituentId → OutputChannelId`) persistence

- The phrase-channel map is a MainComponent-side cache today and
  is rebuilt from constituent ids on session load. Persist it so
  phrase strips don't reshuffle across reloads. Old design doc
  reference:
  `docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`
  (slice 5c).
- The other two fields the spec listed for slice P
  (`preFaderSends` + `mainOutKind`) already round-trip end-to-end
  (E2 + E3). slice P is now a single-concern slice.

### Commit shape

- `feat: ChannelDetail UI slice U — tabbed detail on both mixers`
- `feat: slice 5c map persistence` (or fold into U if minimal)

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
   IdaTests IDA -j`. E2 + E3 are engine-only so the operator-
   facing change between sessions is invisible until U lands;
   incremental builds are fine until then.
