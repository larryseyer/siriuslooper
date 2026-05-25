# MON capability for Input Mixer buses + FX returns

**Date:** 2026-05-25
**Status:** Design lock pending operator review
**Whitepaper amendments landed:** §6.6 (bullet on input-side buses reaching the
direct layer), §7.2 (generalized from per-channel to per-input-side-node),
Glossary `MON` entry.

---

## 1 Problem

An input-mixer **aux bus** that sums multiple channels (a drum subgroup, a vocal
subgroup) currently has no architectural path to be heard. Its main-out targets
are `Tape`, `Bus`, or `HardwareOutput` — but per the I/O-ownership rule
(§5.2 / §6.6 / §7.1), the input mixer never writes physical outputs directly;
input-side audio reaches outputs ONLY via the direct layer → an output-mixer
channel. The per-channel `MON` toggle is the documented bridge. The per-bus
case was never spelled out. Same gap on `FxReturn` buses.

Result today: an operator creates an input aux bus to sum drums, routes the bus
to tape — and hears nothing in the room. The capture works; monitoring doesn't.

## 2 Goal

Make any input-side audio-producing node (channel, aux bus, FX return) able to
opt into MON with one toggle. Mirror the per-channel MON model exactly so the
operator's mental model is "all input-side MON works the same way."

## 3 Non-goals

- **MIDI / video bus MON.** No MIDI or video buses exist on the input mixer
  yet. Defer.
- **Bus MON inference** (§7.3 future direction). Explicit-toggle only.
- **Sub-millisecond raw-direct mode.** Same architectural decision as §7.2 —
  not provided.
- **Routing an input bus to a hardware output directly.** That's a separate
  architectural cleanup (input mixer must not write hardware outputs); not in
  scope here.

## 4 Architecture

### 4.1 Engine

Mirror of the per-channel MON path:

| Per channel (exists) | Per bus / FX return (new) |
|---|---|
| `InputMixer::setChannelMonitorMode(ChannelId, MonitorMode)` | `InputMixer::setBusMonitorMode(BusId, MonitorMode)` |
| `InputMixer::channelMonitorMode(ChannelId)` | `InputMixer::busMonitorMode(BusId)` |
| `InputMixer::channelMonitorOutputChannel(ChannelId)` → `OutputChannelId?` | `InputMixer::busMonitorOutputChannel(BusId)` → `OutputChannelId?` |
| `ChannelEntry { mode, monitorOutputChannel, ... }` | `BusEntry { mode, monitorOutputChannel }` — new parallel array on `InputMixer`, indexed alongside `buses_` |
| Tap pointer: `ChannelStrip::postStripPointer(side)` | Tap pointer: `Bus::postProcessingPointer(side)` — accessor on `Bus` exposing the existing `processedBuffer_` |
| `OutputMixer::setChannelAudioSource(outId, L, R)` | Same call, no engine-side change — the OutputMixer doesn't know or care where the L/R pointers come from |

`Bus::processedBuffer_` already exists; the engine writes its summed-and-
processed output there every block. `postProcessingPointer(side)` is a new
const accessor — no allocation, no new state, just exposes the existing seam
the way `ChannelStrip::postStripPointer` does for channels.

`setBusMonitorMode(busId, On)`:
1. Idempotent — second On call while already On is a no-op (mirror of channel).
2. If no `OutputMixer` is attached, stashes the mode and returns. A later
   `attachOutputMixer` + replay path engages the route (mirror of the
   `attachOutputMixer` deferral noted in `InputMixer.cpp:212`).
3. Calls `outputMixer_->addChannel(SignalType::Audio)` to mint the channel.
4. Calls `outputMixer_->setChannelAudioSource(newId, postProcessingPointer(0),
   postProcessingPointer(1))` so the OutputMixer's render pulls the bus's
   processed buffer every block.
5. Records the minted `OutputChannelId` on the `BusEntry`.

`setBusMonitorMode(busId, Off)`:
1. If currently Off, no-op.
2. Calls `outputMixer_->removeChannel(recordedOutputId)`.
3. Clears the `BusEntry`'s recorded OutputChannelId.

### 4.2 Persistence

`MixerBusState` (engine-vocabulary serialization struct) grows one new field:

```cpp
MonitorMode monitorMode = MonitorMode::Off;   // default = current behaviour
```

JSON wire format: default-suppress per existing convention (only emit when
`!= Off`). Token strings: reuse `"Off"` / `"On"` already in
`monitorModeToken` / `monitorModeFromString` — no new tokens.

`InputMixer::exportGraphState` captures `entry.monitorMode = busMonitorMode(id)`
per bus. `importGraphState` deserializes the field but does NOT engage the
route during the import pass — the OutputMixer isn't attached during that
window (mirror of the channel-side deferral at lines 7106-7111 of
`MainComponent.cpp`).

Load handler (`MainComponent::chooseFileAndLoad`) replay step, mirror of the
channel MON replay:

```cpp
if (loadedInputMixer.has_value())
    for (const auto& b : loadedInputMixer->buses)
        if (b.monitorMode == ida::MonitorMode::On)
            inputMixer_->setBusMonitorMode (
                ida::BusId (b.busId),
                ida::MonitorMode::On);
```

### 4.3 UI

**InputMixerPane bus strip** gets a MON toggle button, visually identical to
the per-channel MON button (same icon, same active/inactive states). Gesture
relays:

```cpp
inputMixerPane_->onBusMonitorModeChanged = [this] (int busIdx, ida::MonitorMode mode) {
    if (busIdx < 0 || busIdx >= (int) busStripIds_.size()) return;
    const auto busId = busStripIds_[busIdx];
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    inputMixer_->setBusMonitorMode (busId, mode);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());
    refreshOutputMixerMonChannels();   // mints/removes the MON-band strip
};
```

**OutputMixerPane MON band** — `refreshOutputMixerMonChannels` extended to
walk `busStripIds_` (input-bus ids) after `inputStripChannelIds_`. Each bus
with `busMonitorMode == On` and a valid `busMonitorOutputChannel()` adds an
entry to `MonStripInfo` with the source bus's name as the label:

```cpp
for (std::size_t i = 0; i < busStripIds_.size(); ++i) {
    const auto bId = busStripIds_[i];
    if (inputMixer_->busMonitorMode (bId) != ida::MonitorMode::On) continue;
    if (!inputMixer_->busMonitorOutputChannel (bId).has_value()) continue;
    if (auto* bus = inputMixer_->busForId (bId))
        infos.push_back ({ /*sentinel ChannelId*/ {}, juce::String (bus->config().name) });
    newBusIds.push_back (bId);   // new parallel vector monStripInputBusIds_
}
```

A new parallel vector `monStripInputBusIds_` (alongside the existing
`monStripInputChannelIds_`) records which MON-band rows are bus-sourced vs
channel-sourced, so the meter refresh and detail-panel selection paths can
resolve each row to its OutputMixer channel correctly.

### 4.4 Independence rules (mirror §6.3.1 + §7.2)

- MON × tape orthogonality holds per-bus (the new whitepaper §7.2 paragraph).
  A bus can route to tape AND be MON-on simultaneously.
- Strip mute silences both the tape route and the MON tap (§7.2 — same rule).
- MON on bus is independent of MON on its source channels. A channel routed
  into a MON-on bus can itself be MON-off; only the summed bus is heard.

## 5 Test surface

Engine (`tests/InputMixerTests.cpp`):

1. `setBusMonitorMode(On)` mints a new OutputMixer channel; `On → Off` removes it.
2. Idempotent: `On` twice mints one channel, not two.
3. Unknown `BusId` is a silent no-op (defensive — mirror of channel).
4. `busMonitorOutputChannel` returns the right id while On, `nullopt` while Off.
5. Audio pulled through the MON channel matches the bus's `processedBuffer_`
   contents (smoke test, no host needed).
6. `MonitorMode` survives `exportGraphState` → JSON → `deserialize` →
   `importGraphState`; replay engages the route once `attachOutputMixer` runs.

Default-suppress JSON test: a bus with `monitorMode == Off` doesn't emit the
key (keeps the wire format compact and pre-bus-MON sessions byte-identical).

## 6 Operator-visible behaviour

After this slice:

1. Create an input aux bus → strip appears with MON button (default Off).
2. Click MON → the bus's processed output appears as a new strip in the
   Output Mixer's MON band, labelled with the bus's name.
3. Adjust the MON strip's fader/pan/inserts independently of the bus's
   own processing.
4. Save → quit → load → MON state on the bus restores; the MON strip
   reappears on the Output Mixer.
5. Same flow on an FX return.

## 7 Scope guardrails

- **Engine + persistence + UI in one slice.** "Sirius done right and
  completely" — half-baking this (engine landed but no UI toggle) leaves
  the gap visible.
- **Tests-first per CLAUDE.md TDD policy.** Engine work is fully headless-
  testable; the UI wiring is operator-verified per existing GUI policy.
- **Mirror existing channel-MON code paths line for line.** Don't invent new
  patterns. Where the channel side has a quirk (e.g. attach-deferral
  semantics), match it on the bus side.
- **No OutputMixer changes** beyond the existing `addChannel` /
  `removeChannel` / `setChannelAudioSource` surface.

## 8 Open questions for operator review

1. **Label on the MON strip.** Plan above uses the bus's user-given name.
   Alternative: `"BUS <id>"` for terseness. Bus name wins for operator
   recognizability — flagged as the recommended default but happy to switch.
2. **Bus MON detail panel.** The MON-strip's detail panel today carries
   Pan/Width + Sends + EQ + CMP. Same surface for bus MON. Confirm.

---

*Author: IDA's Claude. Reviewer: operator. Whitepaper amendments landed in
the same change as this design doc.*
