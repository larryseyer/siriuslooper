# Session Continuation — NEXT: slice E2 || E3 (per-channel sends; channelMainOut + send-zero bypass)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session start
   2026-05-23, all five entries through TAPECOLOR Phase 5 were already
   acked. Next expected OTTO event is TAPECOLOR Phase 6 (tape-hiss noise
   floor).
2. Read the mixer-symmetry design doc —
   `docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
   Operator approved all five open questions (D1–D5) on 2026-05-23.
   Decomposition: **E1 → E2 || E3 → U → P**. **E1 is DONE this session
   (see DONE LAST SESSION below).** Next is E2 || E3 (parallel-safe).
3. The old Output Mixer phrase-channel design doc
   (`...-output-mixer-phrase-channels-design.md`) still exists; slice 5c
   (session persistence for the ConstituentId → OutputChannelId map) is
   open from it — bundle 5c into Slice P below, OR ship 5c first as a
   tiny standalone — operator's call when P lands.

## ▶ DONE LAST SESSION

Slice **E1 — `BusKind::FxReturn` on OutputMixer engine** landed (engine-
only; no UI, no MainComponent wiring). Spec: E1 section of
`docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.

What shipped:

- `OutputMixer::addBus(BusConfig{ ..., kind = BusKind::FxReturn })` was
  already routed to `MixerNodeKind::FxReturn` in the graph and to
  `MixerBusKind::FxReturn` in export/import — pre-existing but
  untested. Slice E1 adds the missing message-thread **accessors** that
  expose this surface to the UI:
  - `OutputMixer::busIdAt(int)` — was missing entirely on OutputMixer
    despite InputMixer having had it for a long time (asymmetric gap
    found while writing E1 tests).
  - `OutputMixer::busKindAt(int)` — mirror of `InputMixer::busKindAt`;
    the UI pre-filter accessor the spec calls out.
- Three new tests under tag `[output-mixer][fx-return]` (25 assertions):
  - `addBus(FxReturn)` mints a distinct kind; `busKindAt` round-trips
    at the right index; defensive out-of-range returns `Bus`.
  - `routeChannelToBus` accumulates into an FX-return identically to a
    plain bus (engine doesn't branch on kind for the send-matrix —
    kind only gates UI pre-filter).
  - `export/importGraphState` round-trips an FX-return bus end to end
    (kind + inserts), with `busKindAt` re-reporting `FxReturn` after
    reconstruction (confirms import wires `BusConfig.kind` correctly,
    not just the persistence shape).

TDD cycle observed: RED via compile error (`busKindAt`/`busIdAt` missing
on OutputMixer) → GREEN (header + cpp accessors, 14+12 lines) → suite
green.

Cap-context: **both mixers become structurally identical** (channels,
aux buses, FX returns, sends, inserts, main-out picker, tabbed detail
panel) — they differ only in their I/O bookends. OTTO's `ChannelDetail`
wrapper (PanWid + EQ + CMP + Sends tabs) becomes IDA's mixer detail
surface on both panes. Pre-fader send toggle per channel. Send-zero
turns FX-return processing off (RT optimization).

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **652 pass / 1 not-run / 653 total** (the
  not-run is the `MainComponentPluginEditorTests_NOT_BUILT` sentinel —
  unchanged). The +3 vs the prior 649-pass baseline is the three new
  `[output-mixer][fx-return]` test cases from slice E1. (Heads-up: test
  85, `permanent bypass: kill every generation, slot bypasses after
  kMaxRestartAttempts`, was observed flaky once during this session's
  ctest — fails in bulk run, passes in isolation. Unrelated to E1;
  pre-existing.)
- `master` HEAD on origin: bumped this commit (E1 land).
- OTTO submodule SHA: `3c84a409` (unchanged).
- lsfx_tapecolor submodule SHA: `d8b06b1` (Phase 1+2+3+4+5; unchanged).

## ▶ NEXT — slice E2 || E3 (parallel-safe; pick either, the other unblocks after)

Spec sections E2 and E3 in
`docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.
Both are engine-only. Both gate slice U. Either can land first; landing
both unblocks U. **TDD per slice (RED → GREEN → refactor → commit).**

### Slice E2 — per-channel pre-fader send toggle, BOTH mixers

- New accessor pair on InputMixer **and** OutputMixer:
  `bool channelSendIsPreFader(ChannelId|OutputChannelId)` + setter.
- Persisted as `preFaderSends: bool` (default false = post-fader) on
  `InputChannelState` and `OutputChannelState` in
  `core/include/ida/MixerGraphState.h`.
- Audio-thread render-path change: when computing the per-send
  accumulator, source is `postStrip` (post-fader, today's default) OR
  `preStrip` (bypass fader+mute). `preStrip` needs a per-channel
  scratch — bump conditionally so common-case all-post-fader memory
  footprint stays unchanged. **RT-safety contract review required**
  before any edit reachable from the audio callback; see "Risks
  captured" in the spec.
- Tests: `[*-mixer][send][pre-fader]` — muted-channel-still-sends
  pre-fader; gain-bypass; persistence round-trip of the flag (both
  mixers).
- Single toggle per channel (all of that channel's sends share the
  mode); per-send toggle is a future polish slice if operators ask.

### Slice E3 — `channelMainOut(OutputChannelId)` accessor + send-zero bypass

- Add the missing accessor pair on OutputMixer (mirror of `busMainOut`):
  - `enum class ChannelMainOutDest { Bus, HardwareOutput }`
  - `ChannelMainOutDest channelMainOut(OutputChannelId)`
  - `BusId channelMainOutBus(OutputChannelId)` — valid when kind == Bus
- Storage: parallel vectors `channelMainOutKind_` / `channelMainOutBus_`
  set by `routeChannelToBus` (writes main-out to target bus AND zeros
  the channel's non-send destinations radio-style; sends to FX returns
  are kept) and by `setChannelMainOutToHardwareOutput` (writes kind to
  HardwareOutput).
- `Bus::sendInputActive() noexcept` — per-bus
  `std::atomic<int> activeSenderCount_` incremented when a send level
  goes 0→nonzero, decremented when nonzero→0; `Bus::process` early-
  returns when the count is 0. This is the D4 RT optimization that
  makes "send-level zero = FX-return processing skipped" real.
- Slice-5b picker-label inference rewrite: stop deriving HardwareOutput
  from "all sends = 0"; read `channelMainOut` directly. The inference
  rule disappears entirely.
- Tests: `[output-mixer][channel-main-out]` + `[bus][send-bypass]`.

### Commit shape (each slice = one focused commit)

- `feat: OutputMixer/InputMixer slice E2 — per-channel pre-fader send toggle`
- `feat: OutputMixer slice E3 — channelMainOut accessor + send-zero bypass`

### After E2 AND E3 land

- **U** (tabbed `ChannelDetail` UI on both mixers — Pan/Width, EQ,
  CMP, Sends tabs from OTTO's `ChannelDetail*.h`, both instanced
  twice per `project_two_mixers_totally_separate`).
- **P** (persistence round-trip — `preFaderSends`, `mainOutKind`,
  `mainOutBus` on `OutputChannelState`; also a good landing zone for
  the open slice-5c `ConstituentId → OutputChannelId` map).

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
