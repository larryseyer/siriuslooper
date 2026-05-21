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

- **Channel** — an input-source strip (Input Mixer; source = hardware input) or
  a phrase strip (Output Mixer; source = phrase render). Has exactly one
  **main-out** and any number of leveled **sends**. The main-out destination set
  differs by mixer: an **Input** channel routes to a **bus, a tape, or a hardware
  output** (RME-TotalMix direct-out); an **Output** channel routes to a **bus or
  a hardware output** (never tape).
- **Bus** — a sub-group. Its **input is channel (or bus) main-outs** routed into
  it. Carries an effect chain (insert FX — typically comp/EQ). Has its own sends.
  Has one main-out.
- **FX return** — an aux return. Its **input is sends only** (never a direct
  main-out route). Carries an effect chain (typically RVB/DLY, but any plugin).
  Has one main-out. (No sends from an FX return in v1 — see Open Items.) **Both
  mixers carry their own dedicated RVB and DLY returns** (independent per
  console), plus any operator-created returns.
- **Terminal** — the Input Mixer has **two terminal sinks: tape (capture) and
  hardware output (direct-out)**; the Output Mixer has **one: output·master**.
  The Input Mixer can route a channel/bus straight to a hardware output (RME
  direct-out) but still **NEVER takes a tape as input**; the Output Mixer **NEVER
  routes to tape**. (This amends the earlier "Input Mixer never routes to outputs"
  rule — see the white paper Part VI note. Stereo-only and "no tape as input"
  remain hard rules.)

**Per-node insert chains.** EVERY node — input/output channel, bus, and FX/RVB/DLY
return — carries an insert chain of **up to 8 inserts**. This is NOT bus-only:
every channel hosts inserts too. Each slot holds **either** a third-party
**VST/CLAP plugin** (the existing M7 out-of-process host) **or** one of Sirius's
**own built-in FX** (EQ, Compressor, Reverb, Delay, …). The insert-slot model is
therefore a union — external hosted plugin OR internal Sirius effect — so an
`EffectChainEntry` must be able to name an internal effect, not only a
`PluginDescriptor`. Buses already own an `EffectChain` + `IEffectChainHost`;
channels gain the same. The 8-slot cap is a deliberate, reasonable ceiling
(`EffectChain` currently has no cap).

The internal-FX **DSP** (the actual EQ/Comp/Rvb/Dly engines — Sirius's own,
seeded by OTTO's reverb + delay) remains the follow-on effort; this spec
establishes the unified insert-slot model and the 8-slot chain on every node so
both external plugins and internal FX drop into the same slots.

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
  strips; the main-out routing picker; the Sends detail tab; persistence. The
  **8-slot insert chain on every node** (channels, buses, returns) and its
  **union slot model** (external VST/CLAP via the existing M7 host **or** an
  internal Sirius effect). Any reverb/delay plugin — internal-or-3rd-party — can
  be loaded immediately via the M7 host.
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

## Implementation phases (for writing-plans)

Each phase is independently buildable, reviewable, and shippable on its own.
Engine phases are headless TDD; UI phases are operator-verified. The two mixers
are independent consoles (no shared state) — they reuse generic *types*
(`MixerGraph`, `Bus`, `ChannelStrip`, `EffectChain`), each holding its own
instances. The order is: engine foundation → input apparatus → inserts →
persistence → input UI → output UI, with the internal-FX DSP as the explicit
"right behind" follow-on.

**MANDATORY per-phase handoff.** Each phase runs in its own chat. The LAST step
of every phase — after the work is committed and pushed — is to **update
`continue.md`** so the next chat resumes from "read continue.md" alone: what the
phase shipped (commits), what's verified (ctest count, clean-rebuild status), and
which phase is next with its first concrete moves. A phase is not done until
`continue.md` reflects it. (Reinforces the standing rule
`feedback_update_continue_md_every_session`.)

**Phase 1 — Engine routing-graph core (shared `MixerGraph` type).** ✅ SHIPPED
(origin/master). Bus-vs-FxReturn kind; main-out-vs-sends split; flexible acyclic
routing + topological evaluation; `OutputMixer` integration. Single implicit
terminal.

**Phase 2 — Multi-terminal `MixerGraph`.** Engine, TDD. Generalize the graph from
one implicit terminal to a set of **typed terminal sinks** (`Tape`,
`HardwareOutput`). The Output Mixer's instance keeps a single `HardwareOutput`
terminal — **behavior-preserving**, re-proven by its existing `[output-mixer]`/
`[mixer-graph]` cases. This is the foundation the input side needs (it routes to
*both* tape and hardware output). Cover: terminal-set registration, main-out to a
chosen terminal, acyclic enforcement across multiple terminals, evaluation order
with >1 terminal, OutputMixer regression-equivalence.

**Phase 3 — Input-side routing apparatus.** Engine, TDD. `InputMixer` gains its
**own** `MixerGraph` (tape + hardware-output terminals), **own** buses, **own** FX
returns (including a **default RVB and DLY return**), **own** send matrix, and the
RT-safe topological traversal — mirroring `OutputMixer`'s render but with source =
device-input strips and a dual terminal. Main-out: channel → bus / tape / hardware
output; bus / FX-return → bus / tape / output. Sends: channel|bus → FX return
(post-fader default). **Absorbs the old "tape-output routing" slice** (channel →
tape is just a main-out to the tape terminal). Output Mixer untouched. Cover:
main-out one-destination across all three input destinations, send leveling/
summing, cycle rejection, topo order, per-terminal delivery (tape capture vs
hardware-output direct monitoring), stereo invariant, traversal RT-safety
static-asserts, default-graph behavior-equivalence with today's per-channel tape
write.

**Phase 4 — Per-node insert chains.** Engine + host, TDD. Channels (both mixers)
gain an `EffectChain` + `IEffectChainHost` dispatch exactly as `Bus` already has,
so inserts run on **every** node, capped at **8 slots** (the cap applies to buses
and returns too; `EffectChain` has no cap today). External **VST/CLAP** via the
existing M7 out-of-process host. (Built-in Sirius FX as slot contents arrive in
the follow-on — see below — so this phase ships no selectable-but-dead effects.)
Cover: per-channel chain dispatch, 8-slot enforcement, bypass/reorder, RT-safety,
behavior-equivalence for empty chains.

**Phase 5 — Routing-graph persistence.** Engine + persistence, TDD. Serialize each
mixer's buses, FX returns, main-out assignments, send levels, terminal
assignments, and per-node insert chains into `SessionFormat`. Pre-graph sessions
load clean (empty graph; channels default-routed to their terminal). Cover:
round-trip equality, forward-compat load of pre-graph sessions, both mixers.

**Phase 6 — Input Mixer UI: creation + routing.** Operator-verified. Blank-area
long-press / right-click menu to create bus / FX-return nodes; render Bus and
FXReturn `CompactFaderStrip`s (dual peak+LUFS meter); the **bottom-of-strip
destination picker** (bus / tape / hardware output) wired to the engine main-out
assignment, offering only acyclic destinations.

**Phase 7 — Input Mixer UI: sends + inserts.** Operator-verified. Vendor OTTO
`ChannelDetailSendsTab` (per-FX-return send levels for the selected strip); the
**insert-chain management UI** (add / remove / reorder / bypass up to 8 slots;
choose a VST/CLAP from the scanned list — built-in FX appear once the follow-on
registers them).

**Phase 8 — Output Mixer UI parity.** Operator-verified, **gated on the Output
Mixer surface existing** (per the operator's input-first roadmap). Reuses the
shared engine model + the same strips / creation gesture / detail tabs / picker
(destinations: bus / hardware output).

**Follow-on (its own spec, "right behind"): internal Sirius FX.** EQ, Compressor,
Reverb, Delay — seeded by OTTO's `effects::PlayerIRConvolution` +
`effects::PlayerDelay` — as **built-in effects**. Adds the **union insert-slot
model** (an `EffectChainEntry` slot holds an external plugin **or** a built-in
effect), their persistence, and the built-in-FX picker in the insert UI. This is
where "make OUR FX available" lands; the phases above build the chains and slots
those effects drop into.

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
