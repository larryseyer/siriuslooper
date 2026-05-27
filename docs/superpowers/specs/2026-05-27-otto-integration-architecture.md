# OTTO Integration Architecture — Design Spec

Status: design synthesis after the 2026-05-27 brainstorm. Captures the locked architectural decisions reached during the multi-pass brainstorm that paused mid-flight when M-OTTO-4 slice 4d ("transport-start so OTTO is audible") was discovered to require porting OTTO's Conductor + Pattern + MIDI-dispatch machinery — work that disappears entirely if `OttoHost` simply embeds OTTO's full `juce::AudioProcessor`-derived `OTTOProcessor` instead of just `PlayerManager`. This spec is the BS-5 output of `superpowers:brainstorming`; the input to operator review (BS-6); the precursor to `superpowers:writing-plans` (BS-7) which will produce the rigorous slice plan with verification criteria from §7 below.

This spec is the **"what + why"** of OTTO's integration shape. Per-slice implementation lands across the seven slices listed in §7 + sequenced in §8. The doctrinal anchor is **whitepaper V10 §5.7** ("OTTO as bundled rhythm engine and tempo-map source"). V10 was written in tandem with this brainstorm and shipped first — §5.7 is the doctrinal commitment, this doc is the engineering realization of it.

---

## Context

By 2026-05-27, OTTO's bridge had reached M-OTTO-3 (transport observed) + M-OTTO-4 slice 4b (operator-facing "Add OTTO source" picker creates visible per-output strips). Slice 4d — transport-start so the strips are actually audible — turned out to be much larger than its 2026-05-26 estimate: it required porting OTTO's per-block driving logic (Conductor + Pattern + MIDI dispatch + everything `OTTOProcessor::processBlock` does) into the IDA-side bridge, because the M-OTTO-2 skeleton embedded only OTTO's `PlayerManager`+`TransportTracker`, not the conductor that drives them.

The brainstorm's central realization: **the entire conductor port disappears if `OttoHost` embeds `OTTOProcessor` directly.** `OTTOProcessor` IS a `juce::AudioProcessor`; calling `prepareToPlay` + `processBlock` per buffer gives IDA OTTO's complete pattern playback, conductor, MIDI machinery, and FX chain for free. The same OTTOProcessor instance also vends `createEditor()`, which means OTTO's existing operator UI lands inside IDA with zero duplication.

That single architectural shift cascaded: with `OTTOProcessor` embedded, OTTO becomes operator-facing inside IDA as a top-level tab (not a hidden engine), which requires a persistent IDA-wide transport bar (OTTO IS the transport), which makes M-OTTO-5 (preset state) load-bearing rather than "low priority" (because the operator can now browse OTTO presets from inside IDA's preset manager). The brainstorm worked through every architectural fork; all were resolved. The whitepaper additions (§4.2 note, §5.4 paragraph, new §5.7) shipped to make those commitments doctrinal.

This spec is the synthesis of all of that into a single engineering reference.

---

## What this spec supersedes from the predecessors

- `docs/superpowers/specs/2026-05-22-otto-integration-design.md` — Decisions 1 (OTTO as 32-stereo-input source) + 2 (cross-project inbox) + 3 (InternalFx adapters) + 4 (assets) **still stand** and are not re-derived here. The 2026-05-22 doc's silence on the *transport + UI + preset* triangle is what this spec fills.
- `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md` — the milestone graph M-OTTO-1 through M-OTTO-6 still stands, but two items shift:
  - M-OTTO-6 ("OTTO operator UI") becomes a **single slice (S2 below)** rather than "its own design pass + plan" because `OTTOProcessor::createEditor()` ships the entire UI inside one factory call. The 2026-05-26 doc's framing of M-OTTO-6 as "very large + own design project" was correct against a PlayerManager-only bridge; with OTTOProcessor embedded it's a small tab-wiring slice.
  - M-OTTO-5 ("preset state round-trip") becomes **load-bearing**, not "low priority until a real operator workflow needs OTTO settings to persist." The reason: with OTTO's UI embedded as a top-level tab and a unified preset manager exposing OTTO Patterns/Kits/Songs as first-class IDA preset categories, saving an IDA project that doesn't capture OTTO's state is a half-baked feature per [[feedback_sirius_done_right_and_complete]].
- `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md` — sibling spec, untouched. Lands as **S7** in §7 below.

---

## 1. Architecture

### 1.1 The shape

```
                   ┌─────────────────────────────────────────────────────┐
                   │                    IDA process                       │
                   │                                                      │
                   │   ┌───────────────┐                                  │
                   │   │  MainComponent │  ← top-level tabs                │
                   │   │   ┌─────────┐ │     [Tapes][Input Mixer]         │
                   │   │   │ OttoPane│ │     [Output Mixer][OTTO][...]    │
                   │   │   └────┬────┘ │                                  │
                   │   └────────┼──────┘                                  │
                   │            │ hosts juce::AudioProcessorEditor*       │
                   │   ┌────────▼──────────────────────────┐              │
                   │   │  OttoHost (otto-bridge/)          │              │
                   │   │   ┌─────────────────────────────┐ │              │
                   │   │   │ OTTOProcessor (otto-plugin) │ │ ← created    │
                   │   │   │ ├ PlayerManager             │ │   in pimpl   │
                   │   │   │ ├ GlobalMixer (32 outs)     │ │              │
                   │   │   │ ├ TransportTracker          │ │              │
                   │   │   │ ├ Conductor / Pattern / MIDI│ │              │
                   │   │   │ └ Internal FX (EQ/CMP/...)  │ │              │
                   │   │   └─────────────────────────────┘ │              │
                   │   └────────┬──────────────────────────┘              │
                   │            │ getOttoOutputLeft/Right(idx)            │
                   │            ▼                                          │
                   │   ┌───────────────────────────────────┐              │
                   │   │  OutputMixer ← OTTO strips                       │
                   │   │  (existing surgical-append seam)                 │
                   │   └───────────────────────────────────┘              │
                   │                                                      │
                   │   ┌───────────────────────────────────┐              │
                   │   │  Persistent IDA-wide TransportBar│              │
                   │   │  (subscribes IOttoTransportListener)            │
                   │   └───────────────────────────────────┘              │
                   │                                                      │
                   └─────────────────────────────────────────────────────┘
```

### 1.2 The five architectural commitments

1. **OTTO is part of IDA, not a plugin.** One process. One main window. OTTO's framing in code, docs, commits, and operator-visible language is "OTTO is integrated" — never "OTTO is hosted." The `OttoHost` class name predates this commitment and is acceptable as an owns-the-runtime label; new code should not introduce parallel "guest" / "plugin" framing. (See [[project_otto_is_part_of_ida_not_a_plugin]].)

2. **`OttoHost` embeds `OTTOProcessor`, not just `PlayerManager`.** The bridge owns a `std::unique_ptr<OTTOProcessor>` inside its pimpl. The audio thread drives `OTTOProcessor::processBlock` per buffer. The 32 per-output accessors (`getOttoOutputLeft/Right`) read through the hosted processor's `PlayerManager::getGlobalMixer()` — same accessor shape that slice 4b shipped, so OutputMixerPane and the OTTO strips are unchanged downstream.

3. **OTTO's UI is a top-level IDA tab.** `OttoPane` (new, in `app/`) embeds the `juce::AudioProcessorEditor*` returned by `OTTOProcessor::createEditor()`. Zero UX duplication — every OTTO feature (patterns, kits, songs, energy, fills, MIDI editor) is immediately available. Visual coherence is automatic because IDA and OTTO already share the L&F substrate (`ui/lookandfeel/` consumed from the OTTO submodule).

4. **A persistent IDA-wide transport bar drives OTTO's transport.** Visible from every tab (Tapes, Input Mixer, Output Mixer, OTTO, Settings). From the operator's perspective OTTO *is* the transport when OTTO is active. The bar subscribes to the existing `IOttoTransportListener` for state mirroring and invokes OTTO's transport actions on the message thread for control.

5. **OTTO does not host third-party plugins inside IDA.** Plugin hosting is IDA's responsibility, on the Output Mixer strips. OTTO's internal FX (its own EQ / CMP / Rvb / Dly / TAPECOLOR) operate **inside** OTTO before audio crosses the output-mixer boundary into IDA's strips. (See [[project_otto_does_not_host_plugins]].) Any "load 3rd-party plugin" or `OutputRouter::Mode` controls in OTTO's embedded `PluginEditor` are hidden when running inside IDA — OTTO is always effectively in multi-out mode (IDA reads all 32 outputs).

### 1.3 What stays unchanged

- **The LMC discipline hierarchy (V10 §4.2 tier list)** — GPS / PTP / NTP / Ableton Link / local CPU monotonic. OTTO is NOT in the tier list and never can be. OTTO supplies *musical-time facts* (BPM, time signature), not clock discipline. The LMC's accuracy against UTC remains the discipline hierarchy's responsibility regardless of OTTO's playing state.
- **The mixer architecture (V10 §6).** OTTO outputs appear as additional Output Mixer channel strips per Decision 1 of the 2026-05-22 design; the mixer's modality-agnostic shape, dual effects architecture, and physical-output ownership are unchanged.
- **Free-running looper mode.** When OTTO is not playing, IDA is the free-running looper that predates OTTO's involvement (V10 §5.4 + §5.7). Loops meet only at other loops' boundaries; no session-level tempo grid. This is **first-class**, not a degraded mode.

---

## 2. Components

### 2.1 `ida::OttoHost` (evolved — `otto-bridge/`)

**Responsibility.** Owns one OTTO runtime instance for the lifetime of an IDA session. Hides all OTTO-side types behind a pimpl so consumers depend only on the IDA-flavoured public surface.

**Ownership / lifetime.** Constructed by `MainComponent` at session init; destroyed at session teardown. Construction-order rule: declared **before** any listener that observes its transport events, so the listener's destruction (after `OttoHost`'s) does not race a still-firing drainer.

**Public API surface (evolved from slice 4b).**
- Existing surface, **preserved** — slice 4b's external API shape is unchanged:
  - `void prepare (double sampleRate, int maxBlockSize)` — wraps `OTTOProcessor::prepareToPlay`.
  - `bool isPrepared() const noexcept`.
  - `void addTransportListener / removeTransportListener (IOttoTransportListener*)`.
  - `void drainForTesting()`.
  - `static constexpr int kNumOttoOutputs = 32;` (+ range-begin constants for instruments / FX returns / player buses).
  - `void renderBlock (int numSamples) noexcept override` (audio-thread).
  - `const float* getOttoOutputLeft / getOttoOutputRight (int ottoOutputIndex) const noexcept` (audio-thread).
- Internal change (the architecturally significant one): `Impl` owns `std::unique_ptr<OTTOProcessor>` instead of `PlayerManager` + `TransportTracker` directly. `renderBlock` calls `processor_->processBlock(scratchBuffer_, scratchMidi_)` and re-reads the 32 stereo pairs through `processor_->getPlayerManager().getGlobalMixer()`.
- Added surface (for S2 + S4):
  - `juce::AudioProcessor& getProcessor()` — returns `*processor_`. Used by OttoPane to call `createEditor()`. **Internal-use; not part of the consumer contract** — OttoPane is the only intended caller.
  - `juce::MemoryBlock serializeState() const` + `bool restoreState (const juce::MemoryBlock&)` — wrap OTTO's existing `getStateInformation` / `setStateInformation`. Message-thread only.

**RT-safety obligations.** `renderBlock` and the per-output accessors are audio-thread; everything else is message-thread. `processor_->processBlock` is OTTO-side RT-safety; OTTO's `CLAUDE.md` AUDIO THREAD RULES are the contract (the EventBus RT-safety brief currently `needs-ack` in the inbox lands inside OTTO and is the one outstanding violation against those rules).

### 2.2 `ida::OttoPane` (new — `app/`)

**Responsibility.** Top-level IDA tab. Hosts OTTO's `AudioProcessorEditor` as a child component. Hides the not-applicable affordances per §1.2 commitment 5.

**Ownership / lifetime.** Owned by `MainComponent`'s tab container. Constructed once at session init (or lazily on first tab activation). Holds a `std::unique_ptr<juce::AudioProcessorEditor>` returned by `ottoHost_->getProcessor().createEditor()`. **`OttoHost` must outlive `OttoPane`** — editor destruction must precede processor destruction.

**Public API surface.** None beyond `juce::Component`. The pane is a leaf consumer.

**RT-safety obligations.** None — message-thread only.

**Hidden controls (V10 §5.7 commitment).** OTTO's `PluginEditor` exposes a "load 3rd-party plugin" path on player strips and an "Output Mode" preference (`Stereo` / `PerPlayer` / `PerDrum`). Inside IDA OTTO is always effectively in multi-out mode (IDA reads all 32 outputs); the operator never chooses an output mode, and any plugin chaining happens on IDA's Output Mixer strips. The exact hiding mechanism is implementation-time work — likely an "embedded-in-IDA" runtime flag on `OTTOProcessor` that `PluginEditor` consults — and is enumerated as an explicit open item in §6 below.

### 2.3 `ida::TransportBar` (new — `app/` or `ui/`)

**Responsibility.** Persistent IDA-wide transport surface visible from every tab. Shows play state, BPM, time signature, and the current bar/beat position (when OTTO is playing). Provides Play/Stop controls that route to OTTO's transport.

**Ownership / lifetime.** Owned by `MainComponent`, peered with the tab container (sibling, not child of any tab). Constructed once at session init.

**Public API surface.** None beyond `juce::Component`. Internal subscription is via `IOttoTransportListener`.

**RT-safety obligations.** None — message-thread only. Receives drained transport snapshots; emits transport actions via OTTO's existing message-thread path.

**Boundary-conversion rule (V10 §5.7).** `IOttoTransportListener::onOttoTransport` delivers `TransportSnapshot` with `bpm` as `double` today. The TransportBar (and any other consumer that needs musical time) converts that `double` to `Rational` at receipt and stores the Rational thereafter. The existing `TransportSnapshot` POD keeps the `double` field as the publish-side wire format — IDA does not extend OTTO's wire format; IDA pinches off the conversion at the consumer boundary. (Future refinement: introduce a `RationalTransportSnapshot` IDA-side derivative if multiple consumers grow tired of converting at the boundary themselves — out of scope for this spec.)

**Position derivation.** Bar/beat display is computed from LMC sample count × Rational BPM at the segment that contains the current sample — never from OTTO's `positionInBeats` double (which is `TransportSnapshot::Kind::BpmChanged` event metadata at best, treated as a hint for cross-checks only). The LMC accessor is `Lmc::sampleCount()` (returns `std::int64_t` — already exists in `engine/include/ida/Lmc.h`); TransportBar pulls from it on a timer tick.

### 2.4 `ida::PresetManager` (extended — location TBD at S4 time)

**Responsibility.** IDA's preset manager (which today serves IDA's own presets) gains three branded categories: **OTTO Patterns**, **OTTO Kits**, **OTTO Songs**. These are first-class categories alongside IDA's own — same browser, same UI affordances. Loading an OTTO preset routes to `OttoHost::restoreState`. Saving an IDA project captures OTTO's `serializeState()` blob inside IDA's session JSON envelope under an `otto_state` key.

**Ownership / lifetime.** PreSetManager's existing ownership shape (TBD; verify at S4 start) — the extension is additive.

**Public API surface (additions).** Enumeration of OTTO preset categories; per-category list + load + save bindings. Exact API shape designed at S4 time against the current PresetManager API.

**RT-safety obligations.** None — preset load/save is message-thread + worker-thread per IDA's existing pattern.

**Forward-compat.** OTTO's state format is JSON per OTTO's spec. Version stamps in the envelope cover OTTO version bumps; missing `otto_state` on load → OTTO starts in default state (the existing post-init state).

### 2.5 `OutputMixerPane` OTTO strips (existing — preserved by S1)

Slice 4b already shipped operator-facing "Add OTTO source ▶" picker + visible OTTO band + select-highlight + remove gesture in `OutputMixerPane`. S1's `OTTOProcessor` embed is **invisible** at this layer — the per-output accessors keep the same shape. S5 + S6 (per-strip detail panel + routing + persistence) extend OutputMixerPane along the existing phrase-strip pattern.

---

## 3. Data flow

### 3.1 Audio (per block, audio-thread)

```
AudioCallback::process
  ├─ ...
  ├─ ottoHost_->renderBlock(numSamples)        // ← was: drives PlayerManager directly
  │     └─ processor_->processBlock(scratchBuf, scratchMidi)
  │           └─ OTTO's Conductor + Pattern + MIDI + PlayerManager + GlobalMixer
  │
  └─ for each OTTO strip in OutputMixer:
        L = ottoHost_->getOttoOutputLeft(stripIndex)
        R = ottoHost_->getOttoOutputRight(stripIndex)
        mix into the strip's input bus
```

S1's diff at this layer is **internal** to OttoHost — `renderBlock`'s body changes from "drive PlayerManager + manual conductor stub" to "drive OTTOProcessor::processBlock"; the accessor shape and the AudioCallback's call site are unchanged. ctest `[otto-host-render]` baseline (6 cases / 157 assertions) is preserved as a regression guard.

### 3.2 Transport (OTTO → IDA, audio-thread → message-thread)

```
OTTO audio-thread (inside processor_->processBlock):
  TransportTracker::update()
    └─ EventBus<TransportEvent>::publish     // ← currently allocates (inbox EventBus brief)
         └─ OttoHost subscription callback (runs on whichever thread publishes)
              └─ marshal payload into the audio→message SPSC ring

OttoHost message-thread drainer (Timer::timerCallback):
  ├─ pop snapshots from SPSC ring
  └─ for each listener:
        listener->onOttoTransport(snapshot)   // TransportBar, others
```

**Boundary-conversion rule application point:** at `onOttoTransport` in TransportBar (or whichever IDA-side consumer needs Rational tempo math). `snapshot.bpm` (double) → an exact `Rational` (e.g. `120.5` → `Rational{1205, 10}`). A double-to-Rational-by-decimal-precision helper does not exist in `core/include/ida/Rational.h` today — the helper signature, precision policy (operator default: track-as-published precision; minimum 1/10 to absorb OTTO's typical BPM steps), and overflow guard are S3 design work, not pre-decided here. `snapshot.positionInBeats` is not in `TransportSnapshot` today and **should not be added** — IDA derives position from LMC sample count.

### 3.3 Transport (IDA → OTTO, message-thread)

```
TransportBar::handlePlayButton()
  └─ ottoHost_->play()                       // ← new on OttoHost; forwards to processor_
        └─ processor_->[OTTO's existing transport-start path]
              └─ TransportTracker emits TransportEvent::Started
                    └─ ... data flow §3.2 fans it back to TransportBar
                          (which updates its own UI state from the round-trip)
```

The round-trip via OTTO's TransportTracker is intentional — it keeps TransportBar's display state authoritative against OTTO's actual state, not its commanded state. The latency is one audio block + one drainer tick (<10 ms).

### 3.4 Preset state (message-thread + worker-thread)

```
Session save:
  ├─ IDA's session writer collects per-component state
  ├─ ottoHost_->serializeState() → juce::MemoryBlock
  └─ write into session envelope at "otto_state" key

Session load:
  ├─ read "otto_state" from envelope
  ├─ if present: ottoHost_->restoreState(blob)
  └─ if absent: OTTO stays at default-constructed state

Preset browser load (OTTO Pattern/Kit/Song category):
  ├─ resolve preset → bytes
  └─ ottoHost_->restoreState(bytes)
```

OTTO's `getStateInformation` is already byte-array based and captures patterns + kits + songs + mixer state in one blob. No new OTTO-side serialization work is required.

---

## 4. Error handling

### 4.1 OTTO processBlock failure

In-process embedding eliminates the IPC failure modes the out-of-process plugin host worries about. `OTTOProcessor::processBlock` does not return an error code — failure modes are: (a) `processor_` is null because S1 has not yet run, in which case `renderBlock` is a no-op (matches the existing `isPrepared` guard); (b) OTTO throws (it shouldn't; OTTO's audio thread is `noexcept`-clean per its CLAUDE.md, but `processBlock` is not declared `noexcept` for JUCE-compat reasons), which propagates out of `renderBlock` — and `renderBlock` is declared `noexcept`, so the program terminates. This matches the existing M-OTTO-3 contract and is the correct behavior: an OTTO crash in the audio thread is unrecoverable; degrading silently would mask the bug.

### 4.2 OTTO editor creation failure

`OTTOProcessor::createEditor()` is declared to always return a non-null pointer (the OTTOEditor class always constructs). If it ever returned null, `OttoPane` shows a placeholder ("OTTO UI failed to initialize") and the rest of IDA continues to function. The OTTO tab remains selectable; the empty placeholder is the operator's signal to file a bug.

### 4.3 Preset state restore failure

`OTTOProcessor::setStateInformation` returns `void`; failure modes are internal-to-OTTO (parse error, version mismatch). `OttoHost::restoreState` wraps it with a best-effort contract: try the restore, then read back via `getStateInformation` and verify it matches; on mismatch, post a `Warning`-severity Notification through the existing `NotificationBus` and leave OTTO in whatever state the failed restore left it. The operator's recovery action is to load a different preset or restart the session.

### 4.4 Transport event burst overflow

The audio→message SPSC ring is fixed-capacity (M-OTTO-3 sized it at 256 slots, sufficient for the realistic event rate of a few per second). Overflow drops the oldest events and increments a counter. M-OTTO-3 already implements this; no new error path for the OTTOProcessor embed.

### 4.5 LMC discipline (unchanged)

OTTO is not in the LMC tier list (V10 §4.2 + §5.7). LMC discipline failure modes (GPS loss, PTP loss, etc.) cascade to the next tier per the existing rules. OTTO's playing state is orthogonal — if the LMC falls to local CPU monotonic, OTTO still plays at its configured BPM; the session-level tempo map is still populated; only the LMC's accuracy against UTC degrades.

---

## 5. Testing

### 5.1 Headless (Catch2) — engine seams

**S1 (`OTTOProcessor` embed):**
- `[otto-host-render]` — preserve all 6 cases / 157 assertions as a regression baseline. The OTTOProcessor embed must produce identical per-output buffers for the existing test inputs.
- New `[otto-host-processor]` — verify `OttoHost` survives prepare → processBlock cycles with the OTTOProcessor embed; verify the 32 per-output accessors return non-null pointers post-`prepare`, null pre-`prepare`.
- `[otto-host-transport]` — preserve all 6 cases / 30 assertions. Event flow through the SPSC marshal must be unchanged.

**S3 (transport bar — boundary conversion):**
- New `[transport-bar][boundary-conversion]` — feed synthetic `TransportSnapshot{bpm=120.5}` to the TransportBar's `onOttoTransport`; assert the stored Rational is exactly `Rational{1205, 10}`; assert no float intermediate has been preserved.

**S4 (preset manager):**
- New `[preset-manager][otto-state]` — round-trip an arbitrary OTTO state blob through `serializeState` → session-envelope JSON → `restoreState`; assert `getStateInformation` after restore matches the original.
- New `[preset-manager][otto-categories]` — assert that "OTTO Patterns / Kits / Songs" categories enumerate from OTTO's preset directory and present in the same data shape as IDA's own categories.

**S5 + S6 (OTTO strip detail panel + routing):**
- Reuse the existing OutputMixer test patterns (`[output-mixer][routing]`, `[output-mixer][persistence]`); add OTTO-strip-specific cases that mirror the phrase-strip patterns.

**S7 (stereo mix):**
- Per the existing `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md` spec.

### 5.2 Operator-verified (GUI cannot be headless-tested per CLAUDE.md)

**S2 (OttoPane lands):**
- Launch IDA, click OTTO tab, verify OTTO's full UI is visible and interactive.
- Click OTTO's Play button, verify audio is audible through master (this is the M-OTTO-4 audibility gap closing).
- Verify the "load 3rd-party plugin" / "Output Mode" controls are hidden per §2.2.

**S3 (transport bar):**
- Verify bar is visible from every tab (Tapes, Input Mixer, Output Mixer, OTTO, Settings).
- Hit Play from the bar, verify OTTO starts; verify TransportBar's BPM display matches OTTO's; switch tabs, verify state persists.

**S4 (preset manager):**
- Open IDA's preset browser, verify OTTO Patterns/Kits/Songs categories are present and browseable.
- Load an OTTO Pattern from the browser; verify OTTO's UI reflects the loaded pattern.
- Save IDA project with OTTO at a non-default preset; load; verify OTTO is restored.

### 5.3 Whitepaper conformance

After each slice, re-run the whitepaper conformance check (the existing `docs/superpowers/plans/2026-05-24-whitepaper-v9-conformance.md` shape, retargeted at V10): the LMC discipline tier list is unchanged, the mixer architecture is unchanged, OTTO is not framed as a plugin/guest in any new code or doc.

---

## 6. Open items (resolve at implementation time, not in this spec)

These were surfaced during the brainstorm and explicitly deferred to slice-time decision-making rather than pre-deciding now:

1. **Mechanism for hiding OTTO-UI affordances inside IDA** (S2). Options: (a) a runtime flag on `OTTOProcessor` that `PluginEditor` consults; (b) IDA-side overlay that catches and absorbs clicks on the hidden regions; (c) an OTTO-side conditional compile guarded on an `OTTO_EMBEDDED_IN_IDA` define. Recommendation: (a) — the cleanest, requires a cross-project commit to OTTO via the inbox protocol, but isolates the embedded-vs-plugin distinction to one bool.
2. **TransportBar placement** (S3) — top of main window vs bottom. Operator's pro-audio-convention default is bottom (matches DAWs); confirm at S3 design time.
3. **Double-to-Rational helper for BPM conversion** (S3). The helper is new code in `core/include/ida/Rational.h`. Open: helper signature (`Rational fromExactDecimal(double v, int64 maxDenom = 10)`?); precision policy (track-as-published, minimum 1/10?); overflow guard (cap denominator to avoid `int64` overflow on pathological inputs). Decide at S3 design time; small enough that it's not a separate slice. `Lmc::sampleCount()` accessor already exists; no work there.
4. **PresetManager API extension shape** (S4). Depends on PresetManager's current shape, which has not been audited as part of this spec. S4's first task is that audit.
5. **OTTO-side EventBus RT-safety brief** (independent — currently `needs-ack` in the inbox). Not a blocker for S1–S7; the existing SPSC marshal absorbs the OTTO-side alloc cost. Lands when OTTO's Claude picks it up.
6. **Asset bundling for the installer.** Decision 4 of the 2026-05-22 design; out of scope for this spec; tracked separately.

---

## 7. The seven slices (input to `superpowers:writing-plans`)

These are the operator-and-Claude sketch from the brainstorm. `superpowers:writing-plans` (BS-7) will produce the rigorous slice-by-slice plan with verification criteria, exact files, and tested-seam contracts after this spec is approved.

| Slice | Goal | Size | M-OTTO milestone touched | Operator-visible? |
|---|---|---|---|---|
| **S1** | `OttoHost` embeds `OTTOProcessor` instead of `PlayerManager`. `renderBlock` + 32-output accessors read through the hosted processor. `[otto-host-render]` baseline preserved. | medium | M-OTTO-4 | no — engine swap |
| **S2** | `OttoPane` top-level tab hosts `OTTOProcessor::createEditor()`. Operator presses OTTO's Play, audio flows through master. Hide not-applicable UI affordances per §6 item 1. | medium | M-OTTO-4 audibility + M-OTTO-6 | **yes — M-OTTO-4 audible** |
| **S3** | Persistent IDA-wide `TransportBar`. Boundary-conversion rule applied at the listener consumer (TransportBar). Bar/beat from LMC sample count × Rational BPM. | small-medium | M-OTTO-6 polish | yes |
| **S4** | `PresetManager` extended with OTTO Patterns/Kits/Songs categories. OTTO state inside IDA session envelope under `otto_state`. Round-trip via `serializeState`/`restoreState`. | medium | M-OTTO-5 | yes |
| **S5** | OTTO output strip detail-panel binding (EQ / CMP / Pan / Width). Mirror of phrase-strip pattern in OutputMixerPane. `selectedOtto_` joins the mutual-exclusion logic. | medium | M-OTTO-4 polish (original 4c scope, part 1) | yes |
| **S6** | OTTO output strip routing + persistence (DEST picker per OTTO strip; survives save/load). | medium | M-OTTO-4 polish (original 4c scope, part 2) | yes |
| **S7** | OTTO Stereo Mix output. Per `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md`. | small-medium | M-OTTO-4 extension | yes |

---

## 8. Recommended sequencing

**Critical path: S1 → S2 → S3 → S4.** This is the audible-end-to-end sequence:
- S1 swaps the engine (invisible regression-baseline-preserving slice).
- S2 makes OTTO audible (M-OTTO-4's long-standing gap closes).
- S3 makes OTTO controllable from anywhere in IDA (transport bar).
- S4 makes OTTO persistable (session save/load round-trips OTTO state; preset browser exposes OTTO content).

After S4, IDA-with-OTTO is the operator-usable product. S5–S7 then fill out the polish + variation surface:
- S5 + S6 land in tandem (per-OTTO-strip detail panel + routing + persistence — the original "4c" slice scope, now properly split).
- S7 stereo-mix lands after S5+S6 so the new strip type ships with detail panel + routing + persistence from the start (no half-baked landings per [[feedback_sirius_done_right_and_complete]]).

---

## 9. Cross-project considerations

- **OTTO submodule SHA bumps.** Currently pinned at `4cdbad3e`. S1's OTTOProcessor embed requires no OTTO-side change — `OTTOProcessor` already has the public surface S1 needs (`prepareToPlay`, `processBlock`, `createEditor`, `getStateInformation`/`setStateInformation`). If §6 item 1's "runtime flag for embedded-mode" lands, that's a cross-project edit per the inbox protocol — small focused commit + inbox entry + SHA bump in IDA.
- **EventBus RT-safety.** The 2026-05-27 inbox brief to OTTO is independent of S1–S7. IDA's M-OTTO-3 SPSC marshal absorbs the OTTO-side alloc cost; when OTTO's Claude lands the EventBus rewrite, IDA gets a free RT-safety win on the transport listener path with no IDA-side code changes (per the brief's "no IDA-side code change required" guidance).
- **OTTO version cadence.** No OTTO-version-break is required by this spec. If a future OTTO release breaks `OTTOProcessor`'s public API, the inbox surfaces it and IDA's bridge adapts on submodule bump.

---

## 10. Conformance check against V10 §5.7

| V10 §5.7 commitment | This spec's realization |
|---|---|
| OTTO is part of IDA, not a plugin/guest | §1.2 commitment 1; §2 framing throughout |
| OTTO always present, operator chooses by hitting Play | §1.3 "free-running looper mode"; §3.2 transport flow gates audio output on OTTO playing |
| Cross-modality rendering instrument: 32 stereo outputs | §1.1 diagram; §2.1 `kNumOttoOutputs`; §2.5 existing OutputMixer OTTO strips |
| Session-level tempo-map source when playing | §3.2 transport flow (tempo map populated from `TransportSnapshot`); §6 explicit non-extension of TransportSnapshot wire format |
| Boundary-conversion rule (double bpm → Rational at receipt; positionInBeats discarded as authoritative) | §2.3 TransportBar boundary; §3.2 application point; §3.2 "should not be added" pin on TransportSnapshot |
| LMC discipline hierarchy unchanged; OTTO not in tier list | §1.3 "what stays unchanged"; §4.5 LMC discipline error path |
| OTTO does not host third-party plugins inside IDA | §1.2 commitment 5; §2.2 hidden controls |
| OTTO's internal FX stay inside OTTO | §1.2 commitment 5; §2.1 OttoHost owns the full OTTOProcessor (including FX) |
| OTTO's presets unified into IDA's preset manager as first-class branded categories | §2.4 PresetManager extension; §3.4 preset data flow |
| OTTO's UI is a top-level tab alongside Tapes / Input Mixer / Output Mixer / Settings | §1.1 diagram; §2.2 OttoPane |
| IDA functions completely without OTTO; free-running looper mode is first-class | §1.3; §4.5 LMC orthogonality |

---

## 11. Self-review checklist (done at write time)

- [x] Every §1 locked decision from `continue.md` shows up somewhere in the spec.
- [x] References V10 §5.7 as the doctrinal anchor; does not contradict it (§10 conformance table).
- [x] References the existing OttoHost public surface (slice 4b's accessors) as the API shape S1 must preserve (§2.1).
- [x] Names the predecessor specs and identifies what they got wrong vs. what stands (top "What this spec supersedes" section).
- [x] Five sections present (Architecture §1, Components §2, Data flow §3, Error handling §4, Testing §5).
- [x] Open items called out explicitly (§6) rather than pre-decided.
- [x] Seven slices enumerated as input to `superpowers:writing-plans` (§7) with sequencing rationale (§8).
- [x] No "TODO" / "FIXME" / "TBD" left undocumented — every "designed at S* time" is explicit and bounded.

---

*Spec authored 2026-05-27 as the BS-5 output of the OTTO integration brainstorm. Next: operator review (BS-6), then `superpowers:writing-plans` (BS-7) to produce the implementation plan. The brainstorm's doctrinal anchor (whitepaper V10 §5.7) shipped first and is the load-bearing reference; this spec is the engineering realization of those commitments.*
