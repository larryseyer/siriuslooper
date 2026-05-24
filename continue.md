# Session Continuation — NEXT: slice 5a (engine surface for phrase channels)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Last sweep
   2026-05-23 covered TAPECOLOR Phase 4 + 5 (resolved in IDA commit
   `6e1e4d8`, OTTO commit `3c84a409`). Next expected OTTO event is
   TAPECOLOR Phase 6 (tape-hiss noise floor).
2. Read the slice 5 design doc —
   `docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`.
   Operator already approved the 3-sub-slice decomposition (5a/5b/5c).
   5a is engine-only; 5b is UI shell + mirror; 5c is session persistence.

## ▶ DONE LAST SESSION

Three commits landed on `origin/master`:

- **`5acf039`** — `feat: InputMixer slice 4 — bus rename parity +
  StripContextOverlay lift`. `InputMixer::renameBus`, shared
  `app/StripContextOverlay.h`, InputMixerPane wires bus + FX-return
  rename via right-click / iOS long-press. New `[input-mixer][slice4]
  [rename]` test. **Operator-confirmed.**
- **`de3fb76`** — continue.md slice-4 handoff.
- **`7bb5cad`** — `docs: slice 5 design — Output Mixer phrase-channel
  strips (5a/5b/5c)`. The spec referenced above; operator-approved.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **643 pass / 1 not-run / 644 total** (the
  not-run is the `MainComponentPluginEditorTests_NOT_BUILT` sentinel —
  unchanged).
- `master` HEAD on origin: `7bb5cad` (slice 5 design doc).
- OTTO submodule SHA: `3c84a409`.
- lsfx_tapecolor submodule SHA: `d8b06b1` (Phase 1+2+3+4+5).

## ▶ NEXT — slice 5a (engine surface)

Spec section: 5a in
`docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`.
Engine-only; **no UI, no MainComponent wiring**. Plan to execute it
straight via TDD (RED → GREEN → refactor per slice), then ship.

Two `OutputMixer` additions:

1. **`removeChannel(OutputChannelId)`** with id reuse via a free-list.
   - Swap-erase from `channels_` + `channelNodeIds_`.
   - Zero the freed channel's row of `sendMatrix_`.
   - Push the freed id onto a `std::vector<std::int64_t>` free-list;
     `addChannel` prefers it before incrementing `nextOutputChannelId_`.
   - Unknown id → silent no-op.

2. **`setChannelMainOutToHardwareOutput(OutputChannelId, int pairIndex)`
   + `channelMainOutHardwareOutPair(OutputChannelId)` read-back.**
   - Mirror of the slice-3 bus version (`setBusMainOutToHardwareOutput`).
   - Use the same storage location/pattern slice 3 used for the bus
     side (likely `MixerMainOut::hardwareOutPair` in the graph-
     snapshot type plus a runtime accessor) — check
     `engine/src/OutputMixer.cpp` around the slice-3 implementation
     before deciding.
   - Negative `pairIndex` clamps to 0; the audio-thread render path
     isn't touched in 5a (phrase channels don't feed audio yet).
   - Extend `exportGraphState` / `importGraphState` so the per-channel
     pair round-trips. The OutputChannelId → ConstituentId mapping is
     NOT persisted in 5a — that lands in 5c.

**Tests** (`tests/OutputMixerTests.cpp`, tag `[output-mixer][slice5]`):
- `removeChannel` of an unknown id is a no-op.
- `removeChannel` of a real id releases the id; the next `addChannel`
  reuses it.
- `removeChannel` zeros the freed channel's sendMatrix row — a re-used
  id starts at unity-into-master per `addChannel`, not at the removed
  channel's previous send levels.
- `setChannelMainOutToHardwareOutput` rejects unknown ids; accepts a
  master-bus-routed channel and switches it to HardwareOutput;
  `channelMainOutHardwareOutPair` round-trips through persistence.
- `setChannelMainOutToHardwareOutput` with `pairIndex < 0` clamps to 0.

**Commit shape:** one focused commit on master,
`feat: OutputMixer slice 5a — phrase-channel engine surface
(removeChannel + per-channel hardware-output routing)`.

## ▶ AFTER 5a — 5b is a NEW session

5b is UI shell + MainComponent mirror; needs operator eyes-on smoke
at the end (a Mark Out should produce a phrase strip on the Output
Mixer's LEFT band, etc. — full smoke steps in the design doc's 5b
section). Do not bundle 5a + 5b into one session — losing the
engine-only checkpoint costs more than it saves.

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
   IdaTests IDA -j`. 5a is engine-only so an incremental build is
   sufficient; 5b will need a clean build before operator eyes-on.
