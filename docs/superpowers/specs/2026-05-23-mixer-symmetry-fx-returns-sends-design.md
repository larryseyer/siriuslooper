# Mixer symmetry, FX returns, and per-channel sends — design

Status: **active.** Operator-approved 2026-05-23 (all five open questions
resolved). Decomposes into engine (slices E1–E3), UI (slice U), and
persistence (slice P). E1 starts the next session.

Cross-refs:
- `docs/IDA_Whitepaper_V8.md` §6 (mixer architecture), §6.1 (stereo-only),
  §6.4 (sends + returns), §6.8 (FX return semantics).
- `docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`
  (slice 5a/5b/5c — predecessor; this work builds on the phrase-channel
  surface).
- `external/OTTO/src/otto-plugin/ui/components/ChannelDetail*.h` —
  the four tab components (PanWid, EQ, CMP, Sends) and the tabbed wrapper
  `ChannelDetail`. IDA mirrors all four; OTTO is the visual reference.
- Memories: `project_two_mixers_totally_separate` (the two consoles share
  no state — but DO share *types* per-instance), `project_internal_fx_first_class`,
  `project_mixer_routing_destinations_and_plugins`,
  `feedback_sirius_done_right_and_complete`.

## Goal

Bring both mixers to OTTO visual + functional parity for the per-channel
detail surface, and complete the mixer-symmetry promise: **InputMixer and
OutputMixer are structurally identical (channels, aux buses, FX returns,
sends, inserts, main-out picker, pan/EQ/comp detail tabs) — they differ
only in their I/O endpoints** (input mixer consumes physical inputs +
sends to tapes/buses/outputs; output mixer consumes phrase renders + sends
to buses/outputs).

## Resolved decisions (operator, 2026-05-23)

### D1 — `BusKind::FxReturn` on **both** mixers
OutputMixer gains the same `BusKind::FxReturn` overload InputMixer
already has. The two consoles become structurally identical; the only
thing distinguishing them is the I/O bookend (input mixer takes physical
inputs, output mixer takes phrase renders).

### D2 — Tabbed detail panel mirroring OTTO
The single `ChannelDetailPanWidTab` currently shown on selection becomes
a tabbed `ChannelDetail` component with **four** tabs in OTTO order:
`Pan/Width`, `EQ`, `CMP` (compressor), `Sends`. All four exist in OTTO
already and are vendored via the submodule — IDA wires them, doesn't
fork. Both mixers use the same `ChannelDetail` component (instanced
twice — see `project_two_mixers_totally_separate`: types shared per-
instance, never via a singleton).

### D3 — Sends are **post-fader by default** with a per-channel pre-fader toggle
Channel mute silences post-fader sends (the channel's gain * mute is
applied before the send tap). Pre-fader mode bypasses the channel's
fader + mute, so sends survive a muted channel — useful for sending a
dry vocal to a reverb that's monitored on cans while the dry signal is
muted in the main mix. The toggle lives on the Sends tab (one toggle per
send, OR one toggle per channel that affects all sends — see U3 below).

### D4 — Send level zero = FX return processing skipped
When every channel's send level into a given FX return is zero (and the
FX return's own gain is zero, OR every connected node's send is zero),
the engine skips that FX return's insert-chain processing entirely. The
RT optimization mirrors `BypassPath` in OTTO's MasterBus — a single
`std::atomic<bool>` "active" flag the audio thread reads per buffer.
**Picker label inference (slice 5b)** stops trying to derive HardwareOutput
from "all sends zero"; instead a new engine accessor
`channelMainOut(OutputChannelId)` (mirror of `busMainOut`) tracks the
explicit picker choice, and the inference rule disappears.

### D5 — Cycle prevention for sends
A channel cannot send to an FX return whose graph main-out eventually
routes back to that channel. The check is delegated to
`MixerGraph::wouldSendCycle(fromChannelNode, toFxReturnNode)`, mirroring
the existing `wouldMainOutCycle`. The Sends tab pre-filters its FX-return
list per-channel so the operator never sees a target the engine would
reject. FX returns can have main-outs (to master / another bus / direct
out) — same options as aux buses; but they cannot have sends (see D2 in
operator answer 5). FX returns get the same pan/EQ/CMP detail tabs as
channels — sends tab is hidden for them.

## Engine decomposition

### Slice E1 — `BusKind::FxReturn` on OutputMixer (engine surface)
- `OutputMixer::addBus(BusConfig{ ..., kind = BusKind::FxReturn })` becomes
  a first-class path: mirror `InputMixer::addBus`'s FX-return branch
  (`MixerNodeKind::FxReturn` in the graph, separate enumeration in the
  bus iteration order so `setBusStrips` can branch on kind).
- `OutputMixer::busKindAt(int)` accessor (mirror of `InputMixer::busKindAt`).
- `routeChannelToBus` becomes the channel-into-bus path; a new
  `routeChannelToFxReturn(OutputChannelId, BusId, float level)` (or:
  same function reused — buses and FX returns share an id space; the
  send goes into whatever bus type the id resolves to) is the channel-
  into-FX-return send path. Decide whether one function handles both
  (simpler) vs two (cleaner intent). **Lean toward one** — the engine
  doesn't care about the bus kind for send-matrix accumulation; the
  kind only matters for UI pre-filter (channels' main-out picker excludes
  FX returns; channels' sends tab includes FX returns only).
- Tests: `[output-mixer][fx-return]` covering add/remove an FxReturn bus,
  send-matrix routing into it, persistence round-trip.

### Slice E2 — per-channel send semantics + pre/post-fader toggle
- Add `bool channelSendIsPreFader (ChannelId)` accessor + setter to
  **both** mixers (output gets it via this slice). Persisted on
  `InputChannelState` / `OutputChannelState` (new `bool preFaderSends`
  field, defaults `false` = post-fader). Single toggle per channel for
  now (all of that channel's sends share the mode); per-send toggle is
  a future polish if operators ask.
- Audio-thread render path: when computing the per-send accumulator,
  the channel's source is `postStrip` (post-fader; current behavior)
  OR `preStrip` (pre-fader; bypass gain + mute). `preStrip` requires
  capturing the pre-`ChannelStrip<Audio>::process` scratch — bump the
  per-channel scratch allocation to hold both. Pre-fader scratch is
  conditional: only allocate per-channel when at least one channel uses
  pre-fader mode (keeps the common-case memory footprint unchanged).
- Tests: `[*-mixer][send][pre-fader]` covering muted-channel-still-sends-
  pre-fader, gain-bypass, persistence round-trip of the flag.

### Slice E3 — `channelMainOut(OutputChannelId)` accessor + send-zero bypass
- Add the missing accessor pair on OutputMixer (mirror of `busMainOut`):
  - `enum class ChannelMainOutDest { Bus, HardwareOutput }`
  - `ChannelMainOutDest channelMainOut (OutputChannelId)`
  - `BusId channelMainOutBus (OutputChannelId)` — valid when kind == Bus
- Store as a parallel vector `channelMainOutKind_` / `channelMainOutBus_`
  set by the existing `routeChannelToBus` (which now sets the main-out
  to the target bus AND zeros the send-matrix radio-style for the
  channel's *non-send* destination — sends to FX returns are kept) and
  by `setChannelMainOutToHardwareOutput` (which sets kind to
  HardwareOutput).
- Add `Bus::sendInputActive() noexcept` (or equivalent) for the FX-return
  bypass optimization: a per-bus `std::atomic<int> activeSenderCount_`
  incremented on `routeChannelToBus(ch, fxReturn, level)` when level
  goes 0→nonzero, decremented when level goes nonzero→0. `Bus::process`
  early-returns when `activeSenderCount_ == 0`. This is the D4 RT
  optimization.
- Tests: `[output-mixer][channel-main-out]` covering the accessor,
  `[bus][send-bypass]` covering the active-sender counter.

## UI decomposition

### Slice U — Tabbed `ChannelDetail` on both mixers
Replace the bare `ChannelDetailPanWidTab detailPanel_` member on
InputMixerPane + OutputMixerPane with `otto::ui::ChannelDetail
detailPanel_` (the tabbed wrapper). Both panes inherit the tab listeners
they actually relay:

- `ChannelDetailPanWidTabListener` — already wired in slice 5b polish.
- `ChannelDetailEQTabListener` — new; wire pane callbacks `onPhraseEQ` /
  `onChannelEQ` to MainComponent which writes back to the channel's
  insert chain (or a dedicated EQ slot — TBD with OTTO's component).
- `ChannelDetailCMPTabListener` — same shape as EQ.
- `ChannelDetailSendsTabListener` — fires per-(channel, fxReturn) send-
  level changes + the channel's pre-/post-fader toggle. MainComponent
  bridges to `mixer->routeChannelToBus(chId, fxReturnId, level)` and
  the new `channelSendIsPreFader` setter.

The Sends tab needs the operator to pick the **active channel** AND the
visible list of FX returns — both mixers expose only their own FX returns
(no cross-mixer sends in this slice; that's whitepaper §6.x territory if
ever).

Tabs hidden per row type:
- **Channel strips** (input pairs, output phrase strips): all 4 tabs visible.
- **FX return strips**: pan/width + EQ + CMP visible; Sends hidden (per
  operator answer 5).
- **Aux bus strips**: pan/width hidden (aux buses are stereo, no pan);
  EQ + CMP visible (existing per-bus inserts already provide this;
  decide whether the tabs replace inserts or augment them).
- **Master strip**: same as aux bus.

## Persistence

### Slice P — round-trip the new state
Extend `core/include/ida/MixerGraphState.h`:
- `OutputChannelState.preFaderSends: bool` (default false).
- `OutputChannelState.mainOutKind: enum (Bus, HardwareOutput)` (default Bus).
- `OutputChannelState.mainOutBus: int64_t` (default 0 = master).
- `InputChannelState.preFaderSends: bool` (already has `mainOut`; the
  pre-fader flag is the only new field).
- `MixerBusState.kind` already exists; ensure FX returns round-trip on
  the OutputMixer side (new code path in `OutputMixer::importGraphState`).

5c (slice-5b's session persistence dependency) can land alongside or
after Slice P; the persistence shapes don't conflict.

## Order & dependencies

```
E1 (engine FX-return on OutputMixer)
  ├─ E2 (pre-fader send toggle, both mixers)
  └─ E3 (channelMainOut accessor + send-zero bypass)
       └─ U (tabbed ChannelDetail UI on both mixers)
            └─ P (persistence — round-trip the new state)
```

E1 must land before U; E3 must land before U (the picker label inference
rewrite gates the UI). E2 can land in parallel with E3 if helpful. P
closes the loop.

## Out of scope (this design)

- **Cross-mixer sends** (input channel → output FX return, or vice
  versa). The two consoles stay separate per
  `project_two_mixers_totally_separate`; any cross-routing happens
  through the tape layer or a future explicit bridge.
- **Per-send pre-fader toggle.** D3 ships ONE toggle per channel; per-
  send is a polish slice if operators ask.
- **Auto-naming** of new FX returns (operator names them like any other
  bus via the existing rename flow).
- **Send-list reordering** in the Sends tab (alphabetic OR FX-return
  creation order is fine for v1).

## Risks captured

- **Render-path change for pre-fader sends.** The per-channel scratch
  must carry both the pre-strip and post-strip signals when pre-fader
  is enabled. RT-safety contract review required before E2 ships;
  doubling the channel-scratch allocation in the constructor is the
  obvious approach but bloats memory for the common-case all-post-fader
  config. Conditional allocation (per-channel mode bit, allocate scratch
  on first transition) is the right design but needs care so the
  audio thread sees the new buffer atomically.
- **`MixerNodeKind::FxReturn`** already exists on InputMixer; OutputMixer's
  ctor reserves a graph-node spot for the master only. E1 needs to verify
  `addBus`'s FX-return branch already handles the graph-side correctly
  (`InputMixer::addBus` is the reference).
- **Tab callbacks may proliferate.** With four tabs × two panes × N
  gestures each, MainComponent's wiring section grows. Consider a
  helper struct or per-tab lambda factory to keep it readable.
