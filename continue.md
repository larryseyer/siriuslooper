# Session Continuation — NEXT: slice E1 (FX returns on OutputMixer engine)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Last sweep
   2026-05-23 covered TAPECOLOR Phase 4 + 5 (acked in IDA commit
   `6e1e4d8`). Next expected OTTO event is TAPECOLOR Phase 6
   (tape-hiss noise floor).
2. Read the new mixer-symmetry design doc —
   `docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
   Operator approved all five open questions (D1–D5) on 2026-05-23.
   Decomposition: **E1 → E2 || E3 → U → P**. This session starts E1.
3. The old Output Mixer phrase-channel design doc
   (`...-output-mixer-phrase-channels-design.md`) still exists; slice 5c
   (session persistence for the ConstituentId → OutputChannelId map) is
   open from it — bundle 5c into Slice P below, OR ship 5c first as a
   tiny standalone — operator's call when P lands.

## ▶ DONE LAST SESSION

Three commits landed on `origin/master`:

- **`db0252c`** — `feat: OutputMixer slice 5b — phrase strip UI +
  MainComponent mirror (LEFT-band PhraseStripInfo row, radio-style
  destination picker, ConstituentId→OutputChannelId binding,
  refreshOutputMixerPhraseChannels hooked into refreshPreparation)`.
  Phrase strips now appear on the Output Mixer's LEFT band in pill
  order; destination picker offers Master + every aux bus + every
  hardware-output pair with radio-style semantics. **Operator-confirmed
  working.**
- **`0ce1a46`** — `feat: OutputMixer slice 5b polish — pan/width
  detail panel on phrase strips (engine audioStripForChannel accessor
  + ChannelDetailPanWidTab wiring)`. Clicking a phrase strip reveals
  the pan/width tab at the top of the pane; engine
  `OutputMixer::audioStripForChannel` (1-line accessor) gives the UI
  read/write access to the live `ChannelStrip<Audio>` atomics. New
  `[output-mixer][slice5b][channel-strip-accessor]` test. **Operator-
  confirmed working.**
- **`2795356`** — `docs: todo.md — FX-return sends on both mixers
  (design slice deferred with 5 open questions)`. Subsequently
  upgraded to a full design spec after the operator answered all five.

Operator-driven design call: **both mixers become structurally
identical** (channels, aux buses, FX returns, sends, inserts, main-out
picker, tabbed detail panel) — they differ only in their I/O bookends.
OTTO's `ChannelDetail` wrapper (PanWid + EQ + CMP + Sends tabs)
becomes IDA's mixer detail surface on both panes. Pre-fader send toggle
per channel. Send-zero turns FX-return processing off (RT optimization).

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **649 pass / 1 not-run / 650 total** (the
  not-run is the `MainComponentPluginEditorTests_NOT_BUILT` sentinel —
  unchanged). The +1 vs the prior 648-pass baseline is the slice-5b
  polish `audioStripForChannel` test.
- `master` HEAD on origin: `44b79b6` (design spec + todo + continue
  refresh — last commit of the session).
- OTTO submodule SHA: `3c84a409`.
- lsfx_tapecolor submodule SHA: `d8b06b1` (Phase 1+2+3+4+5).

## ▶ NEXT — slice E1 (FX returns on OutputMixer engine)

Spec: E1 section in
`docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
**Engine-only — no UI, no MainComponent wiring.** TDD: RED → GREEN →
refactor per slice, then commit.

### Surface to add

1. **`OutputMixer::addBus(BusConfig{ ..., kind = BusKind::FxReturn })`**
   becomes a first-class path mirroring `InputMixer::addBus`'s FX-return
   branch:
   - Use `MixerNodeKind::FxReturn` (not `MixerNodeKind::Bus`) when
     calling `graph_.addNode`.
   - Otherwise identical to today's bus-add path. Default main-out =
     master (same as plain aux bus).
2. **`OutputMixer::busKindAt(int)`** accessor — mirror of
   `InputMixer::busKindAt`. Returns `BusKind` for the bus at the given
   1-based-skipping-master index (or however InputMixer indexes —
   match its convention exactly).
3. **`OutputMixer::busKind(BusId)`** convenience accessor — same data,
   keyed by BusId. (Optional; add if UI ends up needing it more than
   indexed access.)

### Persistence

`MixerBusState.kind` already round-trips via `MixerBusKind`. Verify the
`OutputMixer::importGraphState` bus loop maps `MixerBusKind::FxReturn`
→ `BusKind::FxReturn` in the `BusConfig` it constructs (currently it
hardcodes the conversion — check this is correct end-to-end).

### Tests (`tests/OutputMixerTests.cpp`, tag `[output-mixer][fx-return]`)

- `addBus` with `BusKind::FxReturn` mints an FX-return bus distinct
  from plain Bus kind.
- `busKindAt` round-trips FX-return at the right index.
- A channel can route into an FX-return bus via the existing
  `routeChannelToBus` (no new function needed — the engine doesn't
  care about kind for send-matrix accumulation; the kind only matters
  for UI pre-filter).
- `exportGraphState` / `importGraphState` round-trips an FX-return bus
  end to end (kind, name, main-out, inserts).

### Commit shape

One focused commit on master:
`feat: OutputMixer slice E1 — BusKind::FxReturn (parity with InputMixer)`.

## ▶ AFTER E1 — E2 || E3 can land in parallel sessions

- **E2** = per-channel pre-fader send toggle, both mixers, with the
  conditional-scratch render-path change. RT-safety review required
  before any audio-thread edit; see "Risks captured" in the spec.
- **E3** = `channelMainOut(OutputChannelId)` accessor + send-zero
  bypass via `Bus::activeSenderCount_`. Closes the slice-5b picker-
  label inference gap (the "all sends = 0 ⇒ HardwareOutput" rule
  disappears).
- **U** (tabbed `ChannelDetail` UI on both mixers) gates on BOTH E1
  and E3. Do not start U until both have shipped.
- **P** (persistence round-trip) closes the loop and can absorb the
  old slice-5c work (the ConstituentId → OutputChannelId map).

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
   IdaTests IDA -j`. E1 is engine-only so an incremental build is
   sufficient; Slice U will need a clean build before operator eyes-on.
