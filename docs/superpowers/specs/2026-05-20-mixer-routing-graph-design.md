# Mixer Routing Graph — Design Spec

Status: design approved (brainstorm 2026-05-20). Implementation is multi-phase
and runs in a fresh chat via `superpowers:writing-plans`. This spec is the
"why + what"; the plan is the "how + order".

## Context

Sirius's mixers are meant to be **full creative consoles** on each side of the
tape (white paper Part VI, §5.2/§6) with OTTO visual parity. Today the Input
Mixer has live channel strips (fader/mute/solo, dual peak+LUFS meter, pan/width
detail panel, RME mono/stereo toggle) but **no routing beyond per-strip
processing** — strips don't yet reach tapes, there are no buses, no sends, no FX
returns. The operator wants the performer to **dynamically build the routing
graph live**: add buses, FX returns, and tape/output destinations on the fly via
a blank-area long-press, and route channels through them.

This spec defines that routing graph for **both mixers** as one coherent model.
It deliberately **excludes** the internal reverb/delay DSP — that is a follow-on
spec (see Scope Boundaries) that drops OTTO's existing reverb + delay into FX
returns. This spec is "wiring"; the next is "the effects that live in the wiring."

This work **absorbs the previously-queued "tape-output routing" slice** (todo
item 3 under "Input Mixer — deferred slices"): channel→tape is just the input
mixer's main-out reaching its terminal.

## The node model

Four node kinds. Three of them (Channel, Bus, FX return) are user-visible strips;
the terminal is implicit.

- **Channel** — an input-source strip (Input Mixer) or a phrase strip (Output
  Mixer). Has exactly one **main-out** and any number of leveled **sends**.
- **Bus** — a sub-group. Its **input is channel (or bus) main-outs** routed into
  it. Carries an effect chain (insert FX — typically comp/EQ). Has its own sends.
  Has one main-out.
- **FX return** — an aux return. Its **input is sends only** (never a direct
  main-out route). Carries an effect chain (typically RVB/DLY, but any plugin).
  Has one main-out. (No sends from an FX return in v1 — see Open Items.)
- **Terminal** — **tape** on the Input Mixer (capture console); **output·master**
  on the Output Mixer (mixdown console). The Input Mixer NEVER routes to outputs
  and NEVER takes a tape as input; the Output Mixer NEVER routes to tapes
  (CLAUDE.md hard rule).

The defining distinction the operator drew: **buses get their input from channel
outputs; FX returns get their input from sends.** Structurally a bus and an FX
return are nearly identical (a summing node + an effect chain + one main-out);
they differ in how signal arrives and in their typical contents.

### Two ways signal moves

- **Main-out** — exactly one per Channel / Bus / FX-return. A *routing
  assignment* to a **bus** or a **terminal**. Default destination = the terminal
  (tape / output·master). This is the node's primary, full-level output.
- **Send** — many per Channel / Bus, each with a level. A *tap* to an **FX
  return**. Default **post-fader** (a per-send pre/post toggle is in scope; see
  Open Items for the default-only fallback). Sends never target buses or
  channels — only FX returns.

### Routing depth — flexible, defaults to terminal, acyclic

- A new bus or FX return **defaults its main-out to the terminal** (what most
  setups use, zero configuration).
- The main-out is **re-assignable**: a bus or FX-return may point its main-out
  into another bus (subgroups-of-subgroups, parallel chains).
- The graph is **acyclic-enforced**: the destination picker only offers
  assignments that cannot create a feedback loop. Enforcement is on the message
  thread, before the assignment reaches the audio thread.
- Evaluation order is a **topological sort** of the routing graph each time it
  changes (message thread), producing a fixed processing order the audio thread
  walks per block. (Pro Tools / Logic / Live convention.)

## Engine mapping

The Output Mixer **already has most of this**; the Input Mixer has **none of it**.

### What exists (reuse, do not reinvent)

`engine/include/sirius/OutputMixer.h` + `.cpp`:
- `BusId addBus(BusConfig)`, master auto-created at `BusId{0}`, `nextBusId_`.
- `void routeChannelToBus(OutputChannelId, BusId, float sendLevel)` +
  `sendLevelFor(...)` — a **dense send-level matrix** (32 channels × 64 buses).
- `void setBusEffectChain(BusId, EffectChain)`, `setEffectChainHost(IEffectChainHost*)`.
- `renderBuffer(...)` 4-step traversal: channels → ChannelStrip → send matrix →
  buses → master → outputs.
- Caps: `kMaxOutputChannels = 32`, `kMaxBuses = 64` (includes master).

`engine/include/sirius/Bus.h` + `.cpp`:
- A summing node with a pre-allocated mix buffer, an `EffectChain`, an
  `IEffectChainHost*` dispatch seam (M7 S3), and an `IWetCaptureSink` tap (M8 S4).
- `process(output, numChannels, numSamples) const noexcept` — RT-safe, runs the
  effect chain in-place via `host_->pumpSlot(...)`, accumulates to output.
- Stereo only (`kMaxBusChannelsHard = 2`) — matches the stereo hard invariant.

`engine/include/sirius/ChannelStrip.h` — per-strip gain/pan/width/mute + dual
peak+LUFS metering (Audio specialization). Used by every node's strip.

### What this spec adds

1. **Bus kind.** Add a kind to `BusConfig` (or a parallel field): `Bus` vs
   `FxReturn`. A bus accepts main-out routes; an FX return accepts sends. Both
   own an `EffectChain` (already there). This is the minimal split that makes the
   existing `Bus` serve both roles.

2. **Separate main-out routing from sends.** Today `routeChannelToBus` is a single
   leveled matrix. Model the two movements explicitly:
   - **Main-out**: each Channel/Bus/FX-return has one destination (a `BusId` or a
     terminal sentinel). Full level.
   - **Sends**: leveled taps from Channels/Buses into FX-return buses. The
     existing dense matrix is the natural home for sends; main-out is a separate
     one-destination-per-node assignment.

3. **Flexible acyclic routing + topological evaluation.** Replace the current
   fixed "channels → buses → master" order with a graph evaluation order computed
   (message thread) by topological sort over main-out + send edges, with cycle
   rejection. The audio thread walks the precomputed order.

4. **Input-side routing apparatus (net-new).** `InputMixer` currently has only a
   capture registry (`addChannel`/`setChannelInputSource` → tape). Add the same
   routing substrate: buses, FX returns, main-out (→ bus or **tape**), sends
   (→ FX returns). Terminal = tape via the existing `TapeWriter`/`TapeStore`
   path. **Factor the routing primitives into a shared substrate** both mixers
   use (e.g. a `MixerGraph` holding bus/FX-return registry + send matrix +
   main-out assignments + topo order), so the two mixers differ only in their
   terminal kind and their channel source.

5. **RT-safety.** All graph mutation is message-thread, bracketed by
   `removeAudioCallback`/`addAudioCallback` (the pattern `rebuildInputStrips()`
   already uses) so registry/order changes never race the audio thread. The
   per-block traversal stays allocation-free, lock-free, `noexcept`
   (docs/RT_SAFETY_CONTRACT.md §6). Pre-allocate bus mix buffers at their caps.

## UI design (OTTO parity, vendored)

### Creation gesture — blank-area long-press

Extend the long-press infrastructure already in `MainComponent::InputMixerPane`
(right-click / 500 ms long-press with drag-cancel; currently per-strip
Split/Collapse). A long-press / right-click on **blank pane area** (no strip
under the cursor) opens a **3-option menu**:
- **Input Mixer:** Add bus / Add FX return / Add tape
- **Output Mixer:** Add bus / Add FX return / Add output

Each choice creates the node in the engine (bracketed callback-swap) and adds its
strip. Buses, FX returns, and tape/output destinations are **unbounded**.

### Strips

Render buses and FX returns as `CompactFaderStrip` with `ChannelType::Bus` and
`ChannelType::FXReturn` — **both enum values already exist** in the vendored
strip. Bus/FX-return strips get fader/mute/solo + the dual peak+LUFS meter like
channel strips.

### Main-out routing picker

Re-enable the strip's **output combo** (currently `setOutputComboVisible(false)`
in the Input Mixer). Populate it with the valid (acyclic) destinations for that
node — buses + the terminal. Selecting one calls the engine's main-out
assignment. The combo is hidden today precisely because routing was a later
slice; this is that slice.

### Sends detail tab

Vendor OTTO's `ChannelDetailSendsTab` (`otto-plugin/ui/components/`) into
`ui/lookandfeel/components/` byte-faithfully (the established vendoring pattern,
as with `ChannelDetailPanWidTab`). It shows one send control per FX return; bind
each to the engine send level for the selected strip. It sits in the same detail
panel as the Pan/Width tab (the selected-strip detail region added this session).
Send count is dynamic (one per existing FX return), unlike OTTO's fixed 4.

## Persistence

Buses, FX returns, their effect chains, main-out assignments, and the send matrix
serialize into the session format (`persistence/src/SessionFormat.cpp`). Pre-graph
sessions load with an empty graph (channels default-routed to the terminal). The
internal-RVB/DLY follow-on adds nothing here beyond what plugin-state persistence
already covers.

## Scope boundaries

- **In scope:** the full routing graph (nodes, main-out, sends, acyclic flexible
  routing, topo evaluation) on **both** mixers; the creation gesture; bus/FX-return
  strips; the main-out routing picker; the Sends detail tab; persistence. FX
  returns host the **existing** out-of-process plugin mechanism (M7), so any
  reverb/delay plugin — internal-or-3rd-party — can be loaded immediately.
- **Out of scope (own spec, "right behind"):** integrating **OTTO's reverb +
  delay** (`effects::PlayerIRConvolution`, `effects::PlayerDelay`) as Sirius's
  **internal** RVB/DLY available to drop into an FX return. The operator's
  decision: "use OTTO's rvb and dly which already works and sounds great… wiring,
  then follow right behind with wiring up the OTTO rvb and dly." A separate
  brainstorm→spec covers how those engines are exposed (built-in DSP node vs
  bundled internal plugin) and the preset UI.
- **Also out:** EQ/dynamics as native ChannelStrip DSP (separate, pre-existing
  deferral); bus/FX-return on the Output Mixer **UI** only lands once the Output
  Mixer surface itself is built (the engine model here is shared and ready). Per
  the operator roadmap (input mixer → output mixer), the input mixer drives first.

## Proposed implementation phases (for writing-plans)

Each phase is independently buildable and reviewable. Engine phases are TDD;
UI phases are operator-verified.

1. **Engine routing-graph core (shared substrate).** Bus kind (Bus vs FxReturn);
   main-out-vs-sends split; flexible acyclic routing + topological evaluation
   order; build on the OutputMixer substrate and factor the shared `MixerGraph`.
   TDD: acyclic enforcement, send summing, main-out assignment, evaluation order,
   stereo invariant, RT-safety static-asserts.
2. **Input-side routing apparatus.** Add the shared substrate to `InputMixer`;
   terminal = tape; channel main-out → bus/tape; sends → FX returns. Absorbs
   tape-output routing. TDD.
3. **UI: creation gesture + bus/FX-return strips + main-out picker (Input Mixer).**
   Blank-area 3-option long-press menu; render Bus/FXReturn strips; re-enable +
   populate the main-out combo. Operator-verified.
4. **UI: Sends detail tab.** Vendor OTTO `ChannelDetailSendsTab`; wire per-strip
   sends to FX returns in the detail panel. Operator-verified.
5. **Persistence.** Serialize the graph into the session format; pre-graph
   sessions load clean. TDD.
6. **Output Mixer UI parity** (when the Output Mixer surface exists). Reuses the
   shared engine model + the same strips/gesture/tabs. Operator-verified.

(Then the follow-on spec: OTTO RVB/DLY into FX returns.)

## Testing strategy

- **Engine (headless TDD):** the routing graph is pure logic + DSP and fully
  headless-testable. Cover: bus-kind semantics, main-out one-destination
  assignment, send leveling/summing, cycle rejection (a routing assignment that
  would loop is refused), topological evaluation order correctness, terminal
  routing (tape vs output), stereo invariant, and `process`/traversal RT-safety
  static-asserts. New `tests/MixerGraphTests.cpp` (or extend
  `OutputMixerTests`/`InputMixerTests`).
- **UI (operator-verified):** creation gesture, strip rendering, main-out picker,
  sends tab — the agent cannot keep the GUI alive; the operator confirms visually.

## Open items (decide in the plan or follow-ons)

- **Pre/post-fader sends.** Default post-fader. A per-send pre/post toggle is
  desirable; if it complicates Phase 4, ship post-fader-only and add the toggle
  as a fast follow.
- **FX-return sends.** v1: FX returns have no sends (avoids the most common cycle
  source and keeps the model simple). Revisit if a use case appears.
- **Output Mixer surface.** Its UI pane may not exist yet; Phase 6 is gated on it.
- **Caps.** Output side caps at 32 channels / 64 buses; confirm the input side
  adopts matching caps (or sizes its dense matrix to its own channel count).
