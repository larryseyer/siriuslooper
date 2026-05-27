# OTTO Integration — Scope & Sequencing Roadmap

Status: roadmap, written 2026-05-26 against:
- `docs/superpowers/specs/2026-05-22-otto-integration-design.md` (the four
  foundational decisions — what each integration surface looks like)
- Empirical state check 2026-05-26 (what's actually wired today)
- Memory `project_otto_is_the_transport_source` (the transport
  dimension that surfaced this session, missing from the 2026-05-22 doc)

This is the **sequencing** companion to the 2026-05-22 design. The
2026-05-22 doc answers "what does each piece look like?"; this doc
answers "in what order do we land them, and what's the dependency
graph?" Each milestone below references the corresponding decision in
the 2026-05-22 doc.

---

## Context

Three follow-on slices (transport sync, MIDI file-input, video
file-input) are blocked on "OTTO importable into IDA." That phrase
needs a concrete definition — what's the actual milestone, what work
unblocks it, in what order.

The 2026-05-22 design predates the operator's 2026-05-26 clarification
that **OTTO is the transport source for IDA** (memory
`project_otto_is_the_transport_source`). The transport dimension adds
a fifth decision-area on top of the four already designed: how IDA
consumes OTTO's `TransportTracker` and surfaces it to file inputs and
future LMC-synced features.

## Today's empirical state (2026-05-26)

Verified by CMake grep + filesystem inspection.

| Surface | Status | Detail |
|---|---|---|
| OTTO L&F + UI components | **wired** | `ui/CMakeLists.txt:55-65` consumes OTTOLookAndFeel + FaderMeter + CompactFaderStrip + ChannelDetailPanWidTab directly from `external/OTTO/src/otto-plugin/ui/`. |
| OTTO header-only Player FX | **wired** | `engine/CMakeLists.txt:58-64` includes `external/OTTO/src/otto-core/include/`. PlayerEQ + PlayerCompressor + PlayerDelay accessible via the InternalFx adapter pattern (Decision 3 of the 2026-05-22 design). |
| OTTO assets (IRs, samples, fonts) | **wired** | `OTTO_ASSETS_DIR` CMake var (`CMakeLists.txt:22-23`) → `IDA_OTTO_ASSETS_DIR` macro consumed by `engine/src/RvbAdapter.cpp`. Default `/Users/larryseyer/AudioDevelopment/OTTO/assets/`. |
| `lsfx_tapecolor` submodule | **wired** | Linked into IdaEngine + app via `lsfx::lsfx_tapecolor`. Shared with OTTO (same upstream). |
| `otto-core` library | **NOT wired** | IDA's CMake does NOT `add_subdirectory(external/OTTO)`. OTTO's `PlayerManager`, `GlobalMixer`, `TransportTracker` are inaccessible — IDA can `#include` headers but cannot link the implementations. |
| OTTO instantiation in IDA | **NOT present** | Grepping `PlayerManager`, `GlobalMixer`, `TransportTracker`, `OTTOProcessor` returns zero hits in IDA's `app/`, `engine/`, `audio/`, `host/`. |
| 32-output ingestion in OutputMixer | **NOT present** | OutputMixer has no "OTTO output strip" concept. The surgical-append seam in `OutputMixerPane` landed this session prepares the UI side, but the engine + audio-thread plumbing is absent. |
| Transport subscription | **NOT present** | No IDA code subscribes to `EventBus<TransportEvent>`. |

**Net:** the visual surface (L&F) and the header-only DSP (Player FX) are
in. The audio engine integration is not. Five distinct pieces of work
sit between today's state and "OTTO importable."

## The five OTTO surfaces IDA consumes

Per the 2026-05-22 design + today's transport addition:

1. **Visual surface** (L&F, components) — already wired. No remaining work.
2. **Header-only DSP** (PlayerEQ/PlayerCompressor/PlayerDelay/PlayerIRConvolution + lsfx_tapecolor) — already wired as a build dependency; consumption per-FX is part of Decision 3 of the 2026-05-22 design (InternalFx adapters, EQ → CMP → DLY → RVB sequence).
3. **Audio engine** (PlayerManager + GlobalMixer rendering into 32 stereo outputs) — Decision 1 of the 2026-05-22 design, not yet wired.
4. **Transport** (TransportTracker + EventBus) — **new in this doc**, missing from the 2026-05-22 design. Unblocks file-input transport sync (todo entry B), MIDI sync sub-feature (C), video sync sub-feature (D), and any future LMC-driven feature.
5. **Preset / state** (OTTO's `getStateInformation`/`setStateInformation` JSON) — Decision 4-adjacent, not yet wired. Round-trips OTTO settings through IDA's session envelope.

## Milestones

Each milestone is a concrete slice with a single observable outcome.
Ordered by dependency, not by perceived size.

### M-OTTO-1 — Link `otto-core` (mechanical)

**Goal:** IDA's build produces a binary linked against `otto-core`. No
behavior change.

**Work:**
- `CMakeLists.txt`: `add_subdirectory(external/OTTO/src/otto-core)` (or
  the whole `external/OTTO` tree with non-core targets excluded — read
  OTTO's top-level CMakeLists to see which is cleaner).
- Wire `otto-core` into a target IDA already builds (probably `engine/`
  or a new `otto-bridge/` target). The library it produces should be
  linked into the IDA app.
- Verify clean rebuild succeeds (`rm -rf build && cmake ... && cmake
  --build`). No new ctest failures. The IDA binary boots.
- **No code in IDA uses `otto-core` symbols yet** — this slice just
  proves the link works.

**Risks:** otto-core may have build dependencies IDA doesn't already
have (sfizz, Ableton Link). Read OTTO's CMake before scoping.

**Size:** small. 1 commit. Operator-verifiable by build success.

**Unblocks:** M-OTTO-2 through M-OTTO-5.

---

### M-OTTO-2 — `OttoHost` skeleton + instantiation

**Goal:** IDA owns one `OttoHost` instance whose lifetime matches the
session. The instance contains a `PlayerManager` + `GlobalMixer` +
`TransportTracker` but does not yet render audio or publish transport.

**Work:**
- New module — proposal: a small `otto-bridge/` library between
  `engine/` and `app/`. Per the existing layer convention (engine =
  JUCE-free at public headers, JUCE-coupled .cpp), `otto-bridge/` is
  the same shape: JUCE-free public header (`OttoHost.h` exposing
  opaque types + an `IOttoTransportListener` interface), JUCE-coupled
  .cpp that holds the OTTO instance.
- `MainComponent` constructs an `OttoHost` in its constructor; dtor
  tears it down.
- `prepareToPlay(sampleRate, blockSize)` propagates into OTTO.
- No audio output, no transport, no UI surfacing yet.

**Risks:** Layer placement (engine vs new module). The new-module path
is cleaner — engine's RT-safety contract would otherwise need to
explicitly say "OTTO is exempt" everywhere, and a separate module
makes the JUCE coupling localised.

**Size:** medium. 2-3 commits (CMake skeleton, OttoHost ctor/dtor, wire
into MainComponent).

**Unblocks:** M-OTTO-3, M-OTTO-4.

---

### M-OTTO-3 — Transport subscription + IDA-side listener API

**Goal:** IDA observes OTTO's transport state. A first consumer
(probably a simple debug indicator or a no-op subscriber test) proves
the wiring.

**Work:**
- `OttoHost` exposes an `addTransportListener(IOttoTransportListener*)`
  message-thread API.
- Internally, `OttoHost` subscribes to `EventBus<TransportEvent>` and
  fans events out to IDA-side listeners.
- Listeners receive an IDA-flavoured callback (don't leak OTTO types
  into the listener interface — translate `TransportEvent` to a
  small IDA struct `{ enum Kind { Started, Stopped, BpmChanged };
  double bpm; bool isPlaying; }`).
- A `FileInputRegistry` (or a sibling controller) becomes the first
  subscriber. **This is the engine half of todo entry B** (Transport
  sync); the descriptor field + UI selector follow the existing
  design questions in that entry.

**Risks:** EventBus subscription threading — does the event fire on
the OTTO audio thread or on a worker? If audio-thread, IDA's listener
fan-out must itself be RT-safe (atomic snapshot + worker-thread
delivery). Read TransportTracker's event-publishing code carefully.

**Size:** medium. 2-3 commits.

**Unblocks:** todo entry B (Transport sync); the transport-sync
sub-features inside C (MIDI) and D (Video).

---

### M-OTTO-4 — 32 OTTO outputs as Output Mixer channels (Decision 1)

**Goal:** Audio actually flows from OTTO through IDA's Output Mixer.
The 32 stereo OTTO outputs appear as 32 channel strips in
`OutputMixerPane`, each routable to any physical output via the
existing destination picker.

**Work:**
- `OttoHost::renderBlock(numFrames)` runs in IDA's audio callback:
  drives `playerManager_->processBlock()`, captures the 64-channel
  multi-output buffer (`float* multiOutputs[64]`).
- `OutputMixer` gains an `addOttoOutput(ottoOutputIndex)` →
  `OutputChannelId` method that creates a channel sourced by a
  pointer to one of the 32 stereo slots.
- `MainComponent`-level setup creates 32 such channels at session
  init, calls `OutputMixerPane::appendPhraseStrip` for each (the
  surgical-append seam from 2026-05-26's just-landed slice — this is
  what that slice exists for).
- Per-strip routing through `setChannelMainOutToHardwareOutput` already
  works; no new picker logic.

**Risks:** Audio-thread budget — OTTO does meaningful DSP per block.
Measure CPU cost on a baseline session before declaring this slice
done. If the cost is prohibitive, the worker-thread + ring-buffer
fallback is the alternative (OTTO renders on worker, IDA's audio
thread pulls); larger refactor.

**Size:** large. 4-5 commits across CMake (already done in M1), bridge
code, `OutputMixer` API extension, MainComponent wiring, tests.

**Unblocks:** "OTTO importable" complete in the audio sense. The
render/export path through OTTO follows automatically per the
2026-05-22 Decision 1 carve-out.

---

### M-OTTO-5 — OTTO state + preset serialization (Decision 4 follow-on)

**Goal:** OTTO's settings (preset, transport position, mixer levels)
round-trip through IDA's session envelope. Saving + loading an IDA
session restores OTTO's full state.

**Work:**
- `OttoHost` exposes `serializeState() → juce::String` and
  `restoreState(juce::String)`.
- IDA's session save/load adds an `"otto_state"` key to the JSON
  envelope.
- Backward-compat: missing key on load → OTTO starts in default
  state (the existing post-init state).

**Risks:** OTTO's state format is JSON per the README. Forward-compat
across OTTO version bumps may require version stamps.

**Size:** small-medium. 1-2 commits.

**Unblocks:** session-save fidelity for projects that use OTTO.

---

### M-OTTO-6 — OTTO operator UI (controls + preset browser)

**Goal:** Operator interacts with OTTO from inside IDA. Patterns,
preset selection, per-player levels, basic browsing.

**Work:** Substantial — this is the largest milestone by a wide
margin. Likely its own design pass + plan. Out of scope for this
roadmap doc; named here as the closing piece.

**Size:** very large. Own design + plan.

**Unblocks:** customer-facing "you can buy and use IDA + OTTO together."

---

## Dependency graph

```
                M-OTTO-1 (link otto-core)
                       │
                       ▼
                M-OTTO-2 (OttoHost skeleton)
                  │           │
                  ▼           ▼
         M-OTTO-3        M-OTTO-4
        (transport)     (32 outputs)
            │               │
            │               ▼
            │           [render/export inherits OTTO]
            │
            ▼
   [todo entry B unblocked]
   [B sub-features in C/D unblocked]

      M-OTTO-5 (state) ── independent of M-3 / M-4, depends on M-2
      M-OTTO-6 (UI)    ── depends on M-2 + M-3 + M-4 + M-5
```

## Suggested execution order

1. **M-OTTO-1** first (mechanical, derisks all downstream work). If
   `otto-core` has surprise dependencies, surface that now.
2. **M-OTTO-2** immediately after (skeleton — gates M-3 and M-4).
3. **M-OTTO-3** in parallel with **M-OTTO-4** (independent pieces);
   M-OTTO-3 is smaller and ships first.
4. **M-OTTO-5** any time after M-OTTO-2. Low priority until a real
   operator workflow needs OTTO settings to persist.
5. **M-OTTO-6** last and largest — operator UI is its own design
   project.

At the end of M-OTTO-4, "OTTO importable" is operationally true:
audio flows, transport observable, persistence trivial to extend. The
three previously-blocked todo entries (B/C/D) all become buildable.

## What this doc deliberately doesn't cover

- **How each per-FX adapter (EQ/CMP/RVB/DLY) wraps OTTO's header-only
  Player FX.** That's Decision 3 of the 2026-05-22 doc, fully
  designed and partially implemented (the in-process adapter
  pattern is live).
- **Asset bundling for installer.** Decision 4 of the 2026-05-22
  doc. Independent of the audio-engine work above; the installer
  pipeline can land any time.
- **OTTO's UI inside IDA** — M-OTTO-6 is named, not designed.
- **The cross-project inbox + edit autonomy mechanics.** Decision 2
  of the 2026-05-22 doc, live.

## Open questions surfaced by this scoping (not yet decided)

1. **Layer placement for `OttoHost`** — engine/, host/, or new
   `otto-bridge/`? Recommendation in M-OTTO-2 is a new module; the
   operator can override.
2. **Audio-thread vs worker-thread OTTO rendering.** M-OTTO-4 assumes
   audio-thread direct invocation. If OTTO's per-block CPU budget
   threatens to push IDA past its safe-buffer threshold, the worker-
   thread + ring-buffer fallback is the alternative. Measure first.
3. **TransportTracker event-publish threading.** Whether
   `EventBus<TransportEvent>` fires on the audio thread, a worker, or
   the message thread determines IDA's listener-fan-out shape. Read
   OTTO's TransportTracker.cpp before locking M-OTTO-3.
4. **OTTO version cadence.** Bumping the `external/OTTO` submodule
   SHA across milestones is routine; bumping it across an OTTO API
   break is not. Establish a rule: IDA's integration code never
   depends on un-released OTTO behavior, and the inbox surfaces any
   needed API additions.

## Verification (per milestone)

| Milestone | Pass criterion |
|---|---|
| M-OTTO-1 | `cmake --build build --target IDA` succeeds. `otool -L` on the IDA binary shows `otto-core` (or its inlined contents). ctest stays at baseline. |
| M-OTTO-2 | IDA boots, `OttoHost` instance constructs + destructs cleanly through the session lifecycle. No new audio, no new UI, no crashes. |
| M-OTTO-3 | A test subscriber (or a single debug counter) increments on observed transport events when OTTO's transport state would change. Concrete operator-eyes-on path: write a simple test that flips OTTO's transport via its API and confirms the IDA listener receives the corresponding callback. |
| M-OTTO-4 | Sound from OTTO is audible through IDA's master output. Each of the 32 OTTO strips is routable to any physical output pair. Adjusting an OTTO strip's fader affects the audio level audibly. |
| M-OTTO-5 | Save an IDA session with OTTO at a non-default preset; load it; OTTO restored to that preset. |
| M-OTTO-6 | Own design + verification plan. |

---

*This doc captures the sequencing the 2026-05-22 OTTO integration
design left implicit. The 2026-05-22 doc remains the source of truth
for what each integration surface looks like; this doc says what
order we land them and why.*
