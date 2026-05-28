# OTTO Audio Pump (S3c) — Design Spec

Status: BS-5 output of the 2026-05-28 brainstorm on the IDA-side OTTO audio
pump. Operator deferred remaining design choices ("I will defer to your
choices from here on out... whatever path makes the most sense logically to
achieve an elegant and professional result"). This spec locks the
architecture and is the BS-6/BS-7 input for `superpowers:writing-plans`.

Supersedes the "future transport drive" caveat in the S1 design comment at
`otto-bridge/src/OttoHost.cpp:148-160` and `:230-238`, which deliberately
parked `processBlock` invocation pending design.

---

## Context

S3b (2026-05-28) shipped IDA's `otto::paths::AssetsRoot` wire end-to-end:
OTTO's path ladders now resolve under `IDA_OTTO_ASSETS_DIR`, the OTTO tab's
kit picker surfaces real sample-based kits, AssetsRoot is verified by
operator T14 step 2.

T14 step 3 ("Play in the bar produces audible audio") failed with kits
loaded. The blocker is architectural and pre-existing, not asset-related.
Documented in `continue.md` §3 and `todo.md` 2026-05-28 entry.

The blocker: `ida::OttoHost::renderBlock` calls only
`PlayerManager::processGlobalMixer(numSamples)`. That populates per-channel
bus buffers but does NOT drive any of OTTO's audio-thread housekeeping. In
particular:

- `OTTOProcessor::processAudioMessages` (drains the SPSC `uiToAudioQueue_`
  where `Play` / `Stop` / `TempoChange` land) is called from exactly one
  site: line 673, top of `OTTOProcessor::processBlock`. Since
  `processBlock` never runs in IDA, the queue never drains.
- `OTTOProcessor::updateTransportState` (which would broadcast
  `TransportEvent` back through the EventBus to IDA's existing listener at
  `OttoHost.cpp:94`) also runs only inside `processBlock`. So even if the
  queue drained, IDA's bar would never see OTTO's transport state flip.
- `playerManager_.collectMidiEvents` (generates pattern MIDI from active
  players' patterns) and the subsequent `playerManager_.processMidiEvents
  (playerMidiEvents_)` call (which dispatches NoteOn into sfizz so voices
  start rendering) BOTH live inside `processBlock`. Without them, sfizz
  has no active voices — `processGlobalMixer` sums silence into the
  per-channel buffers, IDA's accessors return silence, and the audio
  device renders zeros.

This spec is the fix.

---

## 1. Architecture

### 1.1 The natural seam in `processBlock`

`OTTOProcessor::processBlock` (line 665–1122) splits cleanly into two
halves along the existing routing branch at line 1070:

**Half A — housekeeping + per-channel render preparation** (lines 665–1069
plus the post-routing housekeeping at 1111–1118): drain audio messages,
update transport tracker, pin engines, ingest MIDI from the host, compute
beat range, advance song timeline at bar boundaries, fire fills, resolve
per-pill energy, generate pattern MIDI, dispatch MIDI into sfizz, advance
the conductor, advance `totalSamplePosition_`, sync `fillMode` for OTTO's
UI. Every step is RT-safe per OTTO's existing audio-thread contract.

**Half B — master mixdown** (lines 1070–1109): `outputRouter_.routeAudio`
sums per-player audio into the host's `buffer` per the current
`OutputRouter::Mode`, applies the de-click fade envelope, pushes the
master spectrum, and clears the buffer in the stopped-and-no-tails branch.
This half writes to the JUCE `AudioBuffer` the caller passed in.

IDA owns its own Output Mixer. When OTTO is embedded, Half B is
architecturally wrong (it would write a competing master mixdown into a
buffer IDA never asked for). Half A is exactly what IDA needs.

### 1.2 The split contract (new OTTO API)

OTTO grows two public methods:

```cpp
// New: housekeeping prefix (lines 665–1069 of current processBlock).
// Drains audio messages, updates transport, dispatches MIDI to sfizz,
// advances conductor + song timeline. Does NOT touch `buffer`.
// Audio-thread-safe: same invariants as processBlock.
void OTTOProcessor::processBlockBeforeRouting (juce::MidiBuffer& midiMessages,
                                               int numSamples);

// New: housekeeping suffix (lines 1111–1118 of current processBlock).
// totalSamplePosition advance + per-player fillMode → pluginState sync.
// Audio-thread-safe.
void OTTOProcessor::processBlockAfterRouting (int numSamples);
```

OTTO's existing `processBlock(buffer, midi)` is refactored to:

```cpp
void OTTOProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    cpuMeter_.beginMeasurement();

    processBlockBeforeRouting (midiMessages, buffer.getNumSamples());

    // The routing branch (was lines 1070–1109) stays here verbatim.
    // (outputRouter_.routeAudio, de-click envelope, spectrum push,
    // buffer.clear() in the stopped-without-tails branch.)

    processBlockAfterRouting (buffer.getNumSamples());

    cpuMeter_.endMeasurement();
}
```

OTTO standalone runs observationally identical to today (the same code
runs in the same order; only the indirection through two public method
calls is new). Verified by the new equivalence test in §5.

`cpuMeter_` pairing stays in the wrapper. IDA's pump deliberately does not
touch the CPU meter — OTTO's tab CPU% display will read 0 (or the last
standalone value, if any). Acceptable: IDA owns the audio thread when
embedded, and OTTO's tab CPU% is informational at best.

`ScopedNoDenormals` stays in the wrapper as today. OTTO standalone keeps
its guard. IDA's `OttoHost::renderBlock` adds its own guard at the same
position (top of body) — see §1.3 — so the two new helpers are always
called under a denormals-off `MXCSR` state regardless of caller.

### 1.3 IDA's pump shape

`OttoHost::renderBlock` grows a `juce::MidiBuffer&` parameter and a
three-call body:

```cpp
void OttoHost::renderBlock (int numSamples,
                            juce::MidiBuffer& midiMessages) noexcept
{
    if (! impl_->prepared || numSamples <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    auto& proc = *impl_->processor;

    // Half A prefix: drain Play/Stop/TempoChange, update transport,
    // dispatch MIDI to sfizz, advance song timeline + conductor.
    proc.processBlockBeforeRouting (midiMessages, numSamples);

    // Per-channel sum into GlobalMixer's per-channel/per-FX-return/
    // per-player-bus accessors that IDA's Output Mixer reads via
    // getOttoOutputLeft/Right. Replaces processBlock's routeAudio path
    // entirely — Half B never runs in IDA.
    proc.getPlayerManager().processGlobalMixer (numSamples);

    // Half A suffix: totalSamplePosition advance + fillMode sync.
    proc.processBlockAfterRouting (numSamples);
}
```

`IDA::audio::AudioCallback` updates its single call site to pass through
its existing MIDI buffer (today this is at least the empty buffer; longer
term it carries IDA's file-input MIDI + any future external MIDI).

### 1.4 Transport authority

The existing direction is preserved exactly. IDA's `TransportBar` Play
button drives `TransportBarHost::playPauseClicked()` → `OttoHost::play()`
→ `processor->sendToAudioThread(AudioMessage{TransportControl, Play})` →
queued on OTTO's SPSC `uiToAudioQueue_`. The next `renderBlock` call drains
the queue inside `processBlockBeforeRouting`. OTTO's conductor flips to
playing. `updateTransportState` publishes a `TransportEvent` via the
EventBus singleton. IDA's existing handler at `OttoHost.cpp:94-111`
catches it, pushes a `TransportSnapshot` into the message-thread SPSC,
the 30 Hz `Timer::timerCallback` drains it, listeners fire,
`TransportBar` reflects state.

No clock virtualization, no conductor neutering, no "external host
transport" mode in OTTO. The existing message-passing contract is enough
once `processBlockBeforeRouting` actually runs.

### 1.5 OutputRouter::Mode is irrelevant on IDA's path

In OTTO standalone, `OutputRouter::routePerPlayer` and `routePerDrum`
internally call `players.processGlobalMixer(numSamples)`
(`OutputRouter.cpp:83,110`). In Stereo mode, the per-channel accessors
are not populated by routeAudio.

Under design B, IDA never calls `routeAudio`, so OutputRouter::Mode is
not consulted on the IDA path. IDA's call to `processGlobalMixer`
directly populates the per-channel accessors regardless of mode. OTTO
standalone's mode handling is untouched.

OTTO's inbox-queued next-pass work ("PerPlayer/PerDrum → master
disabled" per the 2026-05-28 TapeColorProcessor entry) is independent of
this design and lands on its own OTTO timeline.

---

## 2. Components

### 2.1 OTTO side

`external/OTTO/src/otto-plugin/PluginProcessor.h`: declare
`processBlockBeforeRouting` and `processBlockAfterRouting` as public
methods alongside the existing `processBlock`. Both `noexcept`-compatible
(matching the existing audio-thread contract; not literally `noexcept`
because `processBlock` itself isn't, but practically RT-safe).

`external/OTTO/src/otto-plugin/PluginProcessor.cpp`: implement the split
by moving lines 665–1069 verbatim into `processBlockBeforeRouting`'s body,
moving lines 1111–1118 verbatim into `processBlockAfterRouting`'s body,
and rewriting `processBlock` to call the two helpers plus the existing
routing-branch tail (1070–1109) plus the `cpuMeter` pair and the
`ScopedNoDenormals` guard. The two helpers do NOT install their own
`ScopedNoDenormals` — they assume the caller has one. OTTO standalone
satisfies this via `processBlock`'s guard; IDA satisfies it via the
guard added at the top of `OttoHost::renderBlock` (see §1.3).

### 2.2 IDA side

`otto-bridge/include/ida/OttoHost.h`: update `renderBlock` signature to
add `juce::MidiBuffer&`.

`otto-bridge/src/OttoHost.cpp`: rewrite `renderBlock` body per §1.3. Add
the `ScopedNoDenormals` guard. Preserve the existing prepared/numSamples
null guards. Replace the comment block at lines 230–240 with a brief
explanation of the new pump.

`audio/src/AudioCallback.cpp`: update the single call site to pass the
MIDI buffer the audio callback already has. Currently this is at
line 87 `ottoRenderSource_->renderBlock(numSamples)`. Replace with
`ottoRenderSource_->renderBlock(numSamples, midiBuffer)` where
`midiBuffer` is whatever MIDI buffer the callback already manages. If
the callback does not currently route MIDI, pass a stack-local empty
`juce::MidiBuffer` for now — the slice that wires file-input MIDI into
OTTO is downstream.

### 2.3 No new files

The split is entirely intra-file in OTTO. IDA's render path is signature-
extended but no new source files. Test files are listed in §5.

---

## 3. Data flow

**Audio** (per block, audio thread):
1. `AudioCallback` receives device buffer + MIDI buffer from JUCE.
2. `AudioCallback` calls `ottoRenderSource_->renderBlock(N, midi)`.
3. `OttoHost::renderBlock` calls (in order)
   `processBlockBeforeRouting(midi, N)` → drains messages, fires sfizz
   NoteOns, advances conductor.
4. `OttoHost::renderBlock` calls `processGlobalMixer(N)` → sums per-player
   audio into channel/bus buffers exposed by GlobalMixer accessors.
5. `OttoHost::renderBlock` calls `processBlockAfterRouting(N)` →
   `totalSamplePosition_ += N`, fillMode sync.
6. IDA's Output Mixer reads `getOttoOutputLeft/Right(i)` for each of the
   32 stereo outputs and mixes them into its master path (existing wire).

**MIDI in** (per block, audio thread): `AudioCallback`'s MIDI buffer →
`OttoHost::renderBlock(N, midi)` → `processBlockBeforeRouting` consumes
events per `processBlock`'s existing MIDI ingest path (channels 1–8 to
drum players, channel 10 program change to song recall, MIDI clock for
sync). The same buffer is filtered in-place by OTTO (the existing
JUCE-standalone-transport-message filter at lines 832–846) and
populated with OTTO's pattern-generated NoteOn/NoteOff events for
forwarding (lines 1044–1054).

**MIDI out** (per block, audio thread): after `renderBlock` returns, the
caller's MIDI buffer contains OTTO's pattern-generated events. IDA's
`AudioCallback` may route them to an external MIDI bus or discard. For
this slice, routing pattern MIDI to an IDA MIDI output is **out of
scope** (see §7); the buffer plumbing is in place for a future slice
to wire it.

**Transport events** (per block, audio thread → message thread):
`processBlockBeforeRouting` calls `updateTransportState` which publishes
`TransportEvent` via OTTO's EventBus singleton. IDA's existing
subscription handler at `OttoHost.cpp:94-111` translates to
`TransportSnapshot` and pushes to the SPSC ring. The 30 Hz
`Timer::timerCallback` drains and fans out to `IOttoTransportListener`s.
`TransportBarHost`'s listener updates the bar's play state. No new wiring.

**Transport control** (UI thread → audio thread): `TransportBar` Play /
Stop / Tempo edits → `TransportBarHost` callbacks → `OttoHost::play()` /
`stop()` / `setTempo()` (unchanged) → `processor->sendToAudioThread`
queues an `AudioMessage` on OTTO's SPSC. The next `renderBlock` call's
`processBlockBeforeRouting` drains it. No new API.

---

## 4. Error handling

The two helpers inherit OTTO's audio-thread contract verbatim — no
allocations, no logs, no locks, no I/O, no throw. They contain the same
code that already runs there, just relocated. No new failure modes.

`OttoHost::renderBlock`'s prepared/numSamples guards are preserved.
Empty `MidiBuffer` is harmless (the per-message loop in
`processBlockBeforeRouting` iterates zero times).

`ScopedNoDenormals` is added to the top of `OttoHost::renderBlock`. Cost:
one `MXCSR` write. Required because OTTO's `processBlock` body assumes
denormals-off; the split helpers run under the same assumption.

---

## 5. Test strategy

### 5.1 OTTO side (new, lands in T15 alongside the split)

Tagged `[processBlock-split]`, registered via OTTO's
`tests/CMakeLists.txt` or scratch-harness path used for `[assets-root]`
in T11 (OTTO's host-top-level configure is pre-existingly broken; see
S3b §6).

**Test: split-equivalence**
- Setup: two `OTTOProcessor` instances, both prepared at identical
  sample rate / block size. Identical sequence of `AudioMessage`s
  queued (`Play`, `TempoChange{132.0}`, `Stop`).
- Action: instance A runs `processBlock(buffer, midi)` for N blocks.
  Instance B runs `processBlockBeforeRouting + outputRouter_.routeAudio
  + processBlockAfterRouting` (the same three-stage manual sequence
  the refactored `processBlock` runs internally).
- Assert: identical `transportTracker_.getState()` per block.
  Identical `buffer` contents per block (byte-equal `memcmp`).
  Identical `totalSamplePosition_` after N blocks. (Pins that the
  split is observationally indistinguishable for OTTO standalone.)

**Test: before-routing alone drives transport**
- Setup: prepared `OTTOProcessor`. Queue an `AudioMessage{Play}`.
- Action: call `processBlockBeforeRouting(emptyMidi, 64)`.
- Assert: `transportTracker_.getState().isPlaying == true`. Pins that
  Half A is sufficient to flip transport without touching `buffer`.

### 5.2 IDA side (new + extended, lands in T16)

**Extended: `[otto-host-render]`** — every existing assertion preserved,
plus the `renderBlock` call now takes an empty `MidiBuffer` argument.

**Extended: `[otto-host-transport]`** — existing smoke (no-crash on
`play`/`stop`) preserved. Add: after `host.play()` + a `renderBlock`
call + `drainForTesting()`, the registered test listener received
exactly one `TransportSnapshot{Kind::Started, isPlaying=true}`.

**New: `[otto-host-pump]`** — end-to-end pump correctness.
- Setup: prepared host, test listener registered.
- `host.play(); host.renderBlock(64, emptyMidi); host.drainForTesting();`
  → assert listener received `{Started, isPlaying=true}`.
- `host.setTempo(132.0); host.renderBlock(64, emptyMidi);
  host.drainForTesting();` → assert listener received
  `{BpmChanged, bpm≈132.0}`.
- `host.stop(); host.renderBlock(64, emptyMidi); host.drainForTesting();`
  → assert listener received `{Stopped, isPlaying=false}`.
- After `host.play()` + one `renderBlock` call, the per-channel accessor
  `host.getOttoOutputLeft(0)` returns non-null. Audio content is not
  asserted (headless tests have no kit loaded), but the pointer being
  live pins that `processGlobalMixer` ran and wrote channel state.

### 5.3 Operator verification (T17)

Clean rebuild + launch IDA. Steps 1–2 (bar visible everywhere; OTTO tab
kit picker shows kits) carry over from T14 unchanged. Step 3 (Play in
bar produces audio) flips from FAIL to PASS. Operator presses Play in
the bar with an LSAD kit loaded on Player 1; assertion: audible
drumming.

---

## 6. Commit shape (subagent-driven-development chain)

Matches the S3b chain (T11–T13) shape: focused OTTO commits + one IDA
atomic + operator verification.

**T15 (OTTO)** — split + equivalence test. One commit minimum; may grow
a follow-on commit if code-quality review flags the cpuMeter or
ScopedNoDenormals seam. Cross-project inbox entry `[FROM IDA → OTTO]`
appended summarizing the split contract and audio-thread invariant
inheritance.

**T16 (IDA)** — atomic commit:
- Bump `external/OTTO` to T15's HEAD.
- Update `OttoHost::renderBlock` signature + body per §1.3.
- Update `AudioCallback` call site.
- Extend `[otto-host-render]`, `[otto-host-transport]`. Add
  `[otto-host-pump]`.
- Add `ScopedNoDenormals` to `OttoHost::renderBlock`.
- Update the design comment at `OttoHost.cpp:148-160` / `:230-238`
  pointing at this spec.

**T17 (operator)** — clean rebuild, launch, verify Play produces audio.

If T17 succeeds, S3c is shipped. If T17 fails, failure is narrow
(`[otto-host-pump]` pins behavior at the API surface; a silent
regression would be an unrelated IDA-side issue — Output Mixer wire,
audio device, asset path).

---

## 7. Out of scope (parked for future slices)

- **Wire OTTO's pattern MIDI to an IDA MIDI output bus.** The
  `MidiBuffer` parameter on `renderBlock` accommodates this. The
  actual routing (which IDA output bus receives OTTO's generated
  pattern events) needs IDA-side MIDI-output architecture work; not
  required to make Play audible.
- **Wire IDA file-input MIDI INTO OTTO.** Same plumbing in place. The
  source-side wiring (file-input MidiBuffer → AudioCallback MidiBuffer)
  is a separate slice.
- **OTTO EventBus alloc-free refactor.** Already queued in the OTTO
  inbox (2026-05-27 entry). Not blocking IDA — TransportEvent fires
  through the existing locked path correctly; it just allocates per
  call. Fix lands on OTTO's timeline.
- **OTTO mode-aware master-disable.** Already queued in the OTTO
  inbox (2026-05-28 TapeColorProcessor entry). Not blocking IDA under
  design B — IDA never calls `routeAudio` so OutputRouter master state
  is irrelevant on IDA's path.
- **OTTO tab CPU% / master spectrum / master meter inside IDA.** These
  surfaces go dead by design when embedded. IDA's transport bar owns
  the authoritative equivalents. No work needed; honest behavior.

---

## 8. Doctrinal anchors

- Whitepaper V10 §5.7 (OTTO as bundled rhythm engine).
- "OTTO as Output Mixer source" memory: bundled OTTO presents 32 stereo
  outputs as additional Output Mixer channel strips; IDA's Output Mixer
  alone owns physical-output routing.
- "OTTO does NOT host plugins" memory: OTTO's internal FX stay inside
  OTTO; IDA hosts 3rd-party plugins on its output-mixer strips.
- OTTO `CLAUDE.md` "AUDIO THREAD RULES" — the helpers inherit all seven
  invariants.
