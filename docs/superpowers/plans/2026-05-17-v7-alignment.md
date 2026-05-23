# IDA — V7 Alignment Roadmap

**Created:** 2026-05-17
**Triggered by:** White paper rewrite (V2 → V7) at `docs/IDA_Whitepaper_V8.md`, accompanied by `docs/sirius-looper-v2-to-v7-transition.md`.
**Source-of-truth white paper file:** `/Users/larryseyer/IDA/docs/IDA_Whitepaper_V8.md` (the V7 successor file; was previously `Sirius_Looper_Whitepaper_V6.md`).
**Companion transition doc:** `/Users/larryseyer/IDA/docs/sirius-looper-v2-to-v7-transition.md`.
**Replaces (in role, not on disk):** `continue.md §6` "white-paper alignment pass" was the deferred milestone — this plan **is** that milestone.

---

## Context

This plan exists because the IDA white paper grew from V2 (the version the code was last designed against) through V3, V4, V5, V6, to V7 in one large authoring push. The architectural shape from V2 is preserved — the paper is explicit that V3 was additive-and-clarifying, not a do-over — but the additions are substantial: input mixer / output mixer / direct layer (V3); MIDI 2.0 UMP-shaped tape events, inclusive design, validation harness (V4); plug-in determinism + failure semantics + realtime audit (V5); export targets + ensemble security defaults + plug-in format scope (V6); out-of-process plug-in hosting + IDA Archive Format + ensemble consistency + video tier-aware rendering + MIDI 2.0 closure (V7).

A V2→V7 transition guide was authored alongside V7 to keep migration cost finite. This plan **operationalizes that guide for the IDA codebase specifically** — every V3-V7 architectural item is mapped to a milestone, a milestone is mapped to concrete files in this repo, and each milestone carries its own execution mode (orchestrator+subagents by default; ralph as inner loop for the four milestones whose internal shape genuinely fits ralph's iteration model).

The pre-existing `continue.md` correctly identified that "the alignment pass is a design session, not a code session." This plan **is** the output of that design session. Execution begins in the next chat.

---

## How to use this document

**On the first new-chat session that opens this plan:**

1. Read this file end-to-end before touching code.
2. Read `continue.md`, the V7 white paper, and the transition guide. (The user-level rules and project auto-memory load automatically.)
3. Copy the contents of this file into two repo-resident artifacts so future sessions can find them via git rather than the global plans dir:
   - `docs/superpowers/specs/2026-05-17-v7-alignment-design.md` — Parts 0-3 plus the coverage matrix (the "what + why" spec)
   - `docs/superpowers/plans/2026-05-17-v7-alignment-plan.md` — Parts 4-7 (the "execution" plan)
   Commit both with a single-line message: `docs: V7 alignment design + plan (M0 setup)`.
4. Start M1. Do not start anywhere else; the dependency graph is real.

**On every subsequent new-chat session:**

1. Open the plan file in the repo, find the current milestone, read its full block.
2. Execute per the milestone's `Execution mode`.
3. When a milestone reaches `passes: true` (or the equivalent "all acceptance criteria met"), update its status in the plan file, commit, push, and move to the next dependency-eligible milestone.

---

## Execution doctrine (orchestrator / subagents / ralph triage)

| Layer | Tool | Why |
|---|---|---|
| Top-level milestone selection and integration | Main chat acting as **orchestrator** | Heterogeneous milestone shapes; real dependencies; needs judgment per integration point |
| Within-milestone parallelizable subtasks | **Subagent dispatch** via `superpowers:subagent-driven-development` (and specialist agents listed per milestone) | Several milestones have 3-5 independent threads of work; specialist routing improves quality |
| Within-milestone iteration on enumerable lists | **Ralph** (operator-launched, separate terminal) — only for M13, M19, M22, M24 | Enumerable list + same shape per item + clear `passes: true` check = ralph's sweet spot |

**Memory rule reaffirmed:** *"Never run agent loops autonomously. The user runs ralph in a separate terminal. Claude does not invoke it."* Ralph use in M13/M19/M22/M24 is **operator-initiated**: Claude writes the ralph PRD; operator launches `./run_ralph.sh`; ralph drives that milestone's inner loop to completion; control returns to the main chat for integration and the next milestone.

**Per-milestone `Execution mode` field** carries one of:

- `orchestrator+subagents` — default
- `orchestrator+subagents, ralph inner loop after PRD` — for M13, M19, M22, M24 specifically

---

## Cross-cutting architectural decisions

These apply across every milestone unless that milestone explicitly opts out.

1. **Foundation first, skeletons second.** Audio I/O wiring and the realtime-safety contract (M1) land before any mixer / direct-layer / channel-strip class. Building shape without substance locks in bad assumptions about buffer flow.
2. **Out-of-process plug-in hosting from the first plug-in instance.** V7 §5.6 / §15.6 / §17.4 mandate no in-process fallback. The IPC framework (M7) lands before any plug-in code executes on the audio thread, so no in-process debt accumulates.
3. **Solo-first, ensemble last.** Single-machine path solid before vector clocks, partition recovery, peer transport, or crypto. The white paper already commits to "graceful degradation to solo recording" — solo is the foundation, not a degraded mode.
4. **Clean break to SAF (M11).** No parallel-format era. The existing SessionFormat v2 JSON is demo-state-only with no real users (memory rule "no shipping language" confirms). The v1-rejection precedent at `persistence/src/SessionFormat.cpp:671` is the team's established posture on format churn. SAF replaces SessionFormat in one milestone.
5. **TDD per milestone.** Each milestone enumerates the tests that land *with* its code, not after. CLAUDE.md rule #9 ("tests verify intent, not just behavior") applies. Five subsystems with zero current tests — Mixer, MIDI, Direct Layer, Permissions, on-disk SAF — get tests built alongside the production code in their respective milestones.
6. **macOS standalone → iOS AUv3 → Windows → Linux**, strict completion order per memory `feedback_mac_first_linux_windows_last`. iOS is its own milestone (M23) after macOS V7 is otherwise complete. Windows + Linux are not in this plan's scope; they follow in a separate plan after M24.
7. **`feedback-hide-internals-from-musician` UI cleanup lands late (M22).** The operator-facing vocabulary refactor is scheduled after the engine model has stopped moving. Refactoring UI strings while the underlying data model is also moving doubles the work and risks losing operator-facing meaning twice.
8. **UMP-shaped MIDI from day one (M9).** Tape MIDI event payload is a UMP discriminated union from the first MIDI commit, per V4's explicit warning and V7 §6.12's full specification. No 3-byte (status, data1, data2) intermediate shape; no later widening.
9. **Plug-in IPC carries LMC timestamps in the header.** Every shared-memory message between engine and plug-in host process is LMC-stamped. The LMC is the single time discipline across processes; no per-process time domains.
10. **NotificationBus is the engine→UI truthfulness channel (M6).** Built once; used by failure semantics (V5 §17.9), inclusive design surfaces (V4 §16.10), capacity warnings, partition events. Engine writes via lock-free SPSC; UI drains on its own thread.
11. **Atomic save via tmp+rename** wherever durability matters: SAF container save (M11), manifest update, plug-in state migration. POSIX `rename`; Windows `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`. Same primitive everywhere.
12. **Capability tier governs per-milestone tunables.** Flush intervals (M11), video render strategy (M12), tape format selection (M11/M13), plug-in archival mode default (M8). `app/CapabilityTier.cpp` is the single source of truth for tier choice; each milestone wires its tier-dependent defaults through it.

---

## Current-state inventory summary

Source: the three Explore-agent inventories run 2026-05-17 against engine/core/host, persistence/net/tests, and app/ui.

**Mature, V7-preserves-intact (do not refactor):**

- `core/include/ida/Constituent.h` + `core/src/Constituent.cpp` — copy-on-write Constituent hierarchy
- `core/src/Promotion.cpp` — promotion logic (largest file at 338 LOC; mature)
- `core/include/ida/Tape.h` — append-only Tape<T> template
- `core/include/ida/Rational.h`, `Position.h`, `TempoMap.h`, `TimeDomain.h`, `Meter.h` — time and meter primitives
- `core/include/ida/RepetitionRules.h`, `Phrase.h`, `Arrangement.h`, `EffectChain.h`, `CaptureSession.h` — Constituent supporting types
- `engine/include/ida/RenderPipeline.h` + impl — the read-side walker that yields `ActiveRead { loop, tape, tapePosition, cycle }` from a Constituent tree against an LMC time
- `engine/include/ida/MonotonicClock.h`, `Lmc.h` — clock substrate (LMC stubbed to `LocalMonotonic` tier only — extended in M15)
- `engine/include/ida/LockFreeSpscQueue.h` — pre-built lock-free queue (used by M6, M7)
- `engine/include/ida/Asrc.h` — libsoxr variable-rate, allocation-free `process` (used by M1)
- `engine/include/ida/RetroactiveRing.h`, `OverloadProtection.h`, `AudioDeviceCalibration.h` — supporting infrastructure
- `persistence/include/ida/TapeStore.h` + impl — content-addressed `<sha256>.tape` blob store (carried into SAF as the `tapes/` subdirectory contents)
- `net/include/ida/LmcElection.h` + impl — Marzullo interval-intersection + tier dominance + anchor override (extended in M16)
- `net/include/ida/SessionMerge.h` + impl — union-merge CRDT (refactored in M16 to use vector clocks instead of single `editTimestamp`)
- `host/src/PluginScanner.cpp` — `juce::KnownPluginList` scanner (carried into M7 as the plug-in discovery source for the out-of-process loader)
- `ui/include/ida/PerformanceViewState.h`, `PreparationViewState.h`, `TimelineViewState.h`, `UndoStack.h` + impls — view-state classes (UI vocabulary refactored in M22, but data shape mostly preserved)
- `app/CapabilityTier.h` + impl — pure decision function for tier selection (extended in M11/M12/M15 as the tunable source)
- 43 existing test files / 7063 LOC — preserved

**Holes (built net-new by the milestones below):**

- No `juce::AudioAppComponent`, no `AudioIODeviceCallback`, no `AudioDeviceManager` — built in M1
- No `InputMixer`, `OutputMixer`, `Channel`, `Bus` classes — built in M2-M5
- No `SignalType` enum (only `InputKind` as descriptor metadata) — added in M2
- No buffer flow through any "membrane" — `Membrane.h` is two free functions for latency math — built in M2 as side effect of mixer construction
- No MIDI code anywhere (`juce::MidiBuffer`, UMP) — built in M9
- No monitor/passthrough/direct routing — Direct Layer built in M4
- No tape allocation call site (`Tape<T>` template instantiated only in tests) — built in M3
- No in-process plug-in hosting (scanner only; V7 mandates out-of-process anyway) — out-of-process built in M7
- No `MixSnapshot` Constituent subtype — built in M10
- No SAF directory format (current is single JSON via `SessionFormat.cpp`) — built in M11
- No video tier-aware rendering pipeline — built in M12
- No file I/O readers/writers beyond JSON session (no WAV/FLAC/SMF/UMP-JSONL/ProRes encoders) — built in M13
- No automatic direct-routing inference — built in M14
- No LMC discipline beyond `LocalMonotonic` — GPS/PTP/NTP/Link built in M15
- No vector clocks, no causal holding queue, no partition recovery, no network transport, no peer discovery, no MIDI Link — built in M16-M17
- No accessibility hooks (`juce::AccessibilityHandler`, color tokens, footswitch abstraction) — built in M18
- No validation test harness for the six V4 §15.5 criteria — built in M19
- No CLAP/VST3/AU hosting (post-M7 framework) — CLAP in M8, VST3 in M20, AU in M21
- No iOS AUv3 target — built in M23
- Operator-facing UI exposes "tape" vocabulary heavily (48 hits in `MainComponent.cpp` alone), violating `feedback-hide-internals-from-musician` — cleaned in M22

**Zero-test subsystems** (test files land in the milestone that builds the subsystem): Mixer (M2-M5), MIDI (M9), Direct Layer (M4), Permissions (M16), on-disk SAF format (M11).

---

## Part A — Foundation

### M1 — Audio I/O foundation + RT-safety contract audit

**Goal.** Wire `juce::AudioAppComponent` (or `AudioDeviceManager` + `AudioIODeviceCallback` directly) into the app, route inbound and outbound buffers through a single `AudioCallback` class, establish the LMC sample-clock relationship to that callback, and adopt the V5 §5.6 / §8.1 realtime-safety contract as the permanent rule for every audio-thread code path. Nothing flows yet — this is the foundation every later milestone builds on.

**Acceptance criteria.**

- App registers an audio device on startup at the user's default sample rate and buffer size; logs the device choice to NotificationBus once that exists in M6 (logs to `juce::Logger` until then).
- `AudioCallback::audioDeviceIOCallbackWithContext` receives inbound buffers, runs a no-op pass-through to outbound, and returns within buffer-time budget on Apple Silicon dev machine at 48 kHz / 256 sample buffer (~5.3 ms).
- LMC's sample-clock is fed from the audio device's callback start time; `Lmc::nowSeconds()` is monotone across buffers.
- Existing `Asrc`, `OverloadProtection`, `AudioDeviceCalibration`, `RetroactiveRing` classes are wired into the audio thread (currently they're isolated scaffolding).
- A `docs/RT_SAFETY_CONTRACT.md` lands in the repo enumerating the per-V5-§5.6 invariants and the audit checklist that every audio-thread PR must self-certify against.
- All existing 256 ctest cases stay green.

**Dependencies.** None. M1 is the foundation.

**Files touched (created, unless marked).**

> **Deviation note (filed 2026-05-17, M1 Session 2):** the plan
> originally listed `engine/include/ida/AudioCallback.{h,cpp}`,
> but `engine/CMakeLists.txt` has a deliberate "JUCE-free" design
> comment. Putting an `AudioIODeviceCallback` subclass in the engine
> would force `juce_audio_devices` into that layer and break the
> contract. A new top-level `audio/` library (`Ida::Audio`) was
> created instead — the "thin layer added on top" the engine comment
> itself anticipates. File paths below reflect the actual landed layout.

- `audio/include/ida/AudioCallback.h`, `audio/src/AudioCallback.cpp`, `audio/CMakeLists.txt` (new — `Ida::Audio` static library linking `juce_audio_devices` PUBLIC; engine stays JUCE-free)
- `engine/include/ida/EngineConfig.h` (new — plain config struct, JUCE-free, carrying `Asrc::Quality` + preferred sample-rate / buffer-size; M11 capability tiers will steer these)
- `app/MainComponent.{h,cpp}` (modified: owns `AudioDeviceManager`, `AudioCallback`, `Lmc`, and `SteadyMonotonicClock`; `PreparationPane` gains an Audio-device section with JUCE's `AudioDeviceSelectorComponent` and an `Enable monitoring` toggle)
- `app/CMakeLists.txt` (modified: link `Ida::Audio` and `juce::juce_audio_utils` — the picker lives in `audio_utils`, not `audio_devices`)
- `engine/include/ida/Lmc.h`, `engine/src/Lmc.cpp` (modified: Session 2 adds `advanceBySamples`, `nowSecondsFromSamples`, `sampleCount` — sample-clock surface alongside the existing `nowSeconds()` wall-clock reader)
- `engine/src/Asrc.cpp`, `engine/src/OverloadProtection.cpp`, `engine/src/AudioDeviceCalibration.cpp`, `engine/src/RetroactiveRing.cpp` (modified in Session 3: usage hookup, not internal change)
- `docs/RT_SAFETY_CONTRACT.md` (new — Session 2)
- `tests/AudioCallbackTests.cpp` (new — Session 1) — silence-by-default, identity pass-through, fewer-input-than-output silenced, extra-input dropped, sample-rate/buffer-size capture, EngineConfig round-trip
- `tests/LmcTests.cpp` (modified in Session 2: 5 new `[sample-clock]` cases — exact rational at 48 kHz, exact across non-standard buffer sizes, monotone within a rate-epoch, no-op on rate ≤ 0 / samples ≤ 0, zero-before-first-buffer)
- `tests/CMakeLists.txt` (modified: add `AudioCallbackTests`)
- `CMakeLists.txt` (modified: `add_subdirectory(audio)` so the new library participates in the build graph)

**Existing utilities to reuse.** `Asrc`, `OverloadProtection`, `RetroactiveRing`, `AudioDeviceCalibration`, `LockFreeSpscQueue`, `Lmc`, `MonotonicClock` — all already implemented; this milestone wires them.

**Tests landing in this milestone.**

- `AudioCallbackTests.cpp` — sample-clock monotonicity, callback RT-budget, pass-through fidelity (input bytes == output bytes for the identity case)
- Extends `LmcTests.cpp` (existing) — assert LMC advances from sample-clock callbacks match wall time within calibration tolerance

**Sessions 1-3 broken out.**

- Session 1: Land `AudioCallback` skeleton + `juce::AudioDeviceManager` registration in `app/Main.cpp`; identity pass-through verified in headless test; commit.
- Session 2: Wire LMC's sample-clock from `AudioCallback`; extend `LmcTests`; write `docs/RT_SAFETY_CONTRACT.md`; commit.
- Session 3: Wire `Asrc` / `OverloadProtection` / `RetroactiveRing` / `AudioDeviceCalibration` into the audio thread; verify `bash autotest.sh` stays 4/4 green; commit and push.

**Verification.**

```bash
bash bash/autotest.sh                            # full 4-phase, ~25s
ctest --test-dir build -R AudioCallback          # M1-specific tests
ctest --test-dir build -R Lmc                    # regression check
APP_BUNDLE="build-xcode/app/IDA_artefacts/Release/IDA.app" \
  bash bash/smoke-persistence.sh                 # smoke unchanged
```

**Risks & open decisions.**

- Identity pass-through means microphone input is audible on output as soon as the app launches. Default to muted output OR require user to click an "Enable monitoring" affordance in the Preparation tab — pick the latter; feedback loops in dev are a recurring landmine.
- macOS audio-input permission prompt: ensure entitlement `com.apple.security.device.audio-input` is in the signed bundle (already present per `continue.md §1`).

**Execution mode.** `orchestrator+subagents` (Backend Architect for the audio-callback shape, Performance Benchmarker for the RT-budget test design).

---

### M2 — Membrane → Mixer rename + SignalType + Channel concept

**Goal.** Rename the conceptual "membrane" surface to "mixer" per V3 §3.2; rename the existing `engine/Membrane.{h,cpp}` free functions to `LatencyTiming` to free the "membrane" name; introduce `SignalType` enum (Audio / MIDI / Video / File) used throughout the mixer/tape/Constituent code; introduce the `Channel` class as a first-class entity. No new audio-thread behavior yet — this milestone establishes the V3 vocabulary and type shapes the rest of Part B will populate.

**Acceptance criteria.**

- `engine/Membrane.h` and `.cpp` renamed to `engine/LatencyTiming.h` and `.cpp`; the two free functions (`inboundCaptureTime`, `outboundPresentTime`) renamed to `inboundCaptureTime` and `outboundPresentTime` in the new `ida::latency` namespace. Old `ida::membrane` namespace gone.
- `core/include/ida/SignalType.h` added with `enum class SignalType { Audio, Midi, Video, File }`. Used wherever a signal modality must be tracked.
- `core/include/ida/InputKind.h` extended: `InputKind` continues to exist as descriptor metadata; a new helper `signalTypeOf(InputKind)` maps to `SignalType` (Audio → Audio; Midi → Midi; Video → Video; everything else → File or a new `Control` SignalType TBD by milestone).
- `engine/include/ida/InputMixer.h` + `OutputMixer.h` added as empty skeleton classes with the V3 transition-guide interface sketch declared (methods declared, bodies are `// M3-M5` stubs that assert false at runtime). This is the architectural placeholder so M3-M5 grow into it.
- `engine/include/ida/Channel.h` added: `Channel { ChannelId, SignalType, InputId source, ProcessingChain processing, TapeMode tapeMode, [destinations] }`.
- `engine/include/ida/TapeMode.h` added: `enum class TapeMode { CommitToTape, NonDestructive, NoTape }`.
- Existing tests stay green (rename is mechanical; no behavior change).

**Dependencies.** M1 (audio I/O wired so the mixer classes have a real audio thread to be a placeholder for).

**Files touched.**

- `engine/include/ida/LatencyTiming.h` (new, replaces `Membrane.h`)
- `engine/src/LatencyTiming.cpp` (new, replaces `Membrane.cpp`)
- `engine/include/ida/Membrane.h`, `engine/src/Membrane.cpp` (deleted)
- `engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp` (new, skeleton)
- `engine/include/ida/OutputMixer.h`, `engine/src/OutputMixer.cpp` (new, skeleton)
- `engine/include/ida/Channel.h`, `engine/src/Channel.cpp` (new)
- `engine/include/ida/TapeMode.h` (new)
- `core/include/ida/SignalType.h` (new)
- `core/include/ida/InputKind.h` (modified: add `signalTypeOf` helper)
- `tests/MembraneTests.cpp` (renamed to `LatencyTimingTests.cpp`; updated namespace references)
- `tests/InputMixerTests.cpp`, `tests/OutputMixerTests.cpp`, `tests/ChannelTests.cpp` (new — skeleton tests that verify class instantiation and basic property setters, not behavior)
- `tests/CMakeLists.txt` (modified)
- `engine/CMakeLists.txt`, `core/CMakeLists.txt` (modified)
- All call sites that used `ida::membrane::` (grep before refactoring; expected hits in `engine/src/RenderPipeline.cpp`, `app/MainComponent.cpp`, plus tests)

**Existing utilities to reuse.** None new; this milestone establishes the shapes M3+ will populate.

**Tests landing in this milestone.**

- `LatencyTimingTests.cpp` — moved from `MembraneTests.cpp`, namespace updates only; existing assertions preserved
- `InputMixerTests.cpp`, `OutputMixerTests.cpp`, `ChannelTests.cpp` — instantiation + setter tests; no buffer-flow assertions yet (those land in M3-M5)

**Sessions 1-3 broken out.**

- Session 1: Rename `Membrane.{h,cpp}` → `LatencyTiming.{h,cpp}`; update all call sites; rename `MembraneTests.cpp`; verify all 256 tests still green; commit.
- Session 2: Add `SignalType.h`, `TapeMode.h`, `Channel.{h,cpp}`, skeleton `InputMixer.{h,cpp}` + `OutputMixer.{h,cpp}`; CMake wiring; commit.
- Session 3: Write skeleton tests for the new types; verify `bash autotest.sh` green; commit and push.

**Verification.**

```bash
grep -r "ida::membrane" core/ engine/ app/ ui/ host/ persistence/ net/ tests/   # expect zero hits
grep -r "InputMembrane\|OutputMembrane" .                                          # expect zero
bash bash/autotest.sh                                                              # 4/4 green
```

**Risks & open decisions.**

- `Control` SignalType: V3 lists Audio/MIDI/Video/File explicitly but the InputKind enum has `Control`, `ParameterAutomation`, `Transport`, `System`. Decision: those map to `SignalType::File` for now (parameter tapes are JSONL files in SAF). Revisit during M11 if SAF format design forces a split.
- The skeleton `InputMixer::process_buffer` body should assert-false rather than return silently, so a buggy call site in M1's audio thread is loud, not silent.

**Execution mode.** `orchestrator+subagents` (one Backend Architect pass over the type declarations before commit, since the InputMixer/OutputMixer/Channel shapes constrain M3-M14).

---

## Part B — Mixer architecture

### M3 — Channel-driven tape allocation + channel-layer processing chains + per-input flags

**Goal.** Implement V3 Steps 3-5 together: tape creation moves from "one tape per input" (currently, "no tapes per anything — never instantiated") to channel-driven; channels declare their tape mode (CommitToTape / NonDestructive / NoTape); per-input flags (`raw_direct_monitor`, `enabled`, `defaults`) gain meaning. The first real `Tape<T>` instance in product code lands in this milestone.

**Acceptance criteria.**

- `InputMixer::register_input` accepts `InputDescriptor` and per-input flags; flags persist on the `InputMixer`.
- `InputMixer::add_channel(InputId, SignalType, ChannelConfig)` returns a `ChannelId` and creates the underlying `Channel`.
- `InputMixer::set_channel_tape_mode` causes (a) `CommitToTape` → one tape allocated in `TapeStore` and registered against the channel; (b) `NonDestructive` → one tape allocated + a parallel parameter tape; (c) `NoTape` → no tape allocation.
- Channels implement a per-`SignalType` `ProcessingChain` — for now Audio is a no-op chain (real DSP lands in M5); MIDI / Video / File chains are stubs that pass through (real per-modality work lands in M9 / M12 / M13).
- `InputMixer::process_buffer` runs on the audio thread, walks every active channel, applies its processing chain, and routes per `tapeMode` to `TapeStore` writes (via a dedicated writer thread fed by lock-free queue — no synchronous I/O on the audio thread).
- New tests assert: channel count = tape count under each tape mode mix; tape contents = processed channel output for `CommitToTape`; tape contents = dry channel input for `NonDestructive` with parameter tape carrying the processing delta.
- All previous tests (now 256 + M1 + M2 additions) remain green.

**Dependencies.** M2.

**Files touched.**

- `engine/include/ida/Channel.h`, `engine/src/Channel.cpp` (modified: ProcessingChain, tape allocation hooks)
- `engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp` (modified: implementation bodies; `process_buffer` real)
- `engine/include/ida/ProcessingChain.h`, `engine/src/ProcessingChain.cpp` (new; SignalType-parameterized)
- `engine/include/ida/TapeWriter.h`, `engine/src/TapeWriter.cpp` (new; dedicated writer thread + SPSC queue from audio thread)
- `persistence/include/ida/TapeStore.h`, `persistence/src/TapeStore.cpp` (modified: add `appendBytes(TapeId, span<const std::byte>)` for incremental writes; current API writes whole blobs)
- `core/include/ida/InputDescriptor.h` (modified: add `bool rawDirectMonitor`, `bool enabled`, `ChannelDefaults defaults`)
- `tests/InputMixerTests.cpp` (modified: channel-driven tape allocation tests)
- `tests/TapeWriterTests.cpp` (new)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** `Tape<T>` (first product-code use), `TapeStore`, `LockFreeSpscQueue` (for writer-thread feed), `RetroactiveRing` (for the retroactive-capture buffer behind channels), `Asrc` (per-channel rate conversion if needed).

**Tests landing in this milestone.**

- `InputMixerTests`: channel-driven allocation per TapeMode; per-input `enabled` flag suppresses channel processing; `rawDirectMonitor` flag triggers Direct Layer route (Direct Layer arrives in M4; until then assert the route is *requested*, not satisfied).
- `TapeWriterTests`: SPSC queue from audio thread to writer thread; writer thread flushes to `TapeStore` at tier-appropriate interval (interval values are tier-stubbed; final values land in M11 with SAF).

**Sessions 1-3 broken out.**

- Session 1: `ProcessingChain.{h,cpp}` SignalType-parameterized stubs; per-`SignalType` no-op chain compiles and passes a smoke test; commit.
- Session 2: `TapeWriter.{h,cpp}` dedicated thread + SPSC queue; `InputMixer::process_buffer` real body wiring tape writes via the writer; tests assert RT-safety (no allocs, no locks, no I/O on audio thread); commit.
- Session 3: TapeMode behavior (CommitToTape / NonDestructive / NoTape) end-to-end; tests pass; commit and push.

**Verification.**

```bash
ctest --test-dir build -R "InputMixer|TapeWriter|Channel"
bash bash/autotest.sh                                            # 4/4 green
# Manual: launch app, register a channel in CommitToTape mode, play 10 seconds of audio,
# inspect TapeStore for the resulting .tape file. UI for this surfaces under Preparation
# tab; M22 will hide it from operator default view.
```

**Risks & open decisions.**

- `appendBytes` on `TapeStore`: current `TapeStore` is content-addressed (sha256 of complete blob). Incremental append breaks content-addressing. Decision: tape writes during recording go to a `<sessionUuid>/<tapeId>.tape.partial` working directory; on tape finalization (commit-to-Constituent), the file is hashed, renamed to `<sha256>.tape`, and registered in `TapeStore`. This preserves the immutability invariant.
- Tier-aware flush interval: stubbed in M3 with `Lavish:per-buffer, Comfortable:50ms, Tight:200ms, Survival:1000ms` constants per V7 §17.8 — wired through `CapabilityTier`. Final tunable surface in M11.

**Execution mode.** `orchestrator+subagents` (Backend Architect for `TapeWriter` thread design; one Code Reviewer pass over the audio-thread / writer-thread boundary before commit).

---

### M4 — Direct Layer subsystem (manual routing)

**Goal.** Build the V3 §2.3 / Step-6 Direct Layer: parallel signal path from input mixer to output mixer bypassing the tape entirely. Manual routing only in this milestone; automatic inference lands in M14.

**Acceptance criteria.**

- `engine/include/ida/DirectLayer.h` + impl exists. Manages two route types: `RawRoute` (from `InputId` directly to `OutputChannelId`) and `ProcessedRoute` (from `ChannelId` after processing to `OutputChannelId`).
- `DirectLayer::route_buffers` runs on the audio thread, takes raw-input buffers + processed-channel buffers, writes to output buffers. Allocation-free, lock-free, bounded-loop. Sub-millisecond per buffer at 256 samples / 48 kHz.
- `InputMixer` wired to call `DirectLayer::route_buffers` after `process_buffer` and before the audio-thread hands off to `OutputMixer`.
- `OutputMixer` (still skeleton from M2 until M5) accepts direct-layer contributions as a separate input alongside Constituent renders.
- Tests assert: raw route bypasses processing; processed route includes processing; both bypass tape; switching `tapeMode` independently of direct routing has no side effect.

**Dependencies.** M3.

**Files touched.**

- `engine/include/ida/DirectLayer.h`, `engine/src/DirectLayer.cpp` (new)
- `engine/include/ida/AudioCallback.h`, `engine/src/AudioCallback.cpp` (modified: insert DirectLayer hop)
- `engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp` (modified: surface direct-route requests to DirectLayer)
- `tests/DirectLayerTests.cpp` (new)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** None new.

**Tests landing in this milestone.**

- `DirectLayerTests`: RawRoute bypass (input bytes == output bytes); ProcessedRoute includes processing; bypass tape under both modes; RT-budget per buffer; allocation-free assertion.

**Sessions 1-3 broken out.**

- Session 1: DirectLayer class + route registry; manual `add_raw_route` / `add_processed_route` / `remove_route` API; tests for registry; commit.
- Session 2: `route_buffers` audio-thread impl; tests assert byte-equal pass-through for RawRoute and RT-budget; commit.
- Session 3: Wire into `AudioCallback`'s callback flow (input mixer → direct layer → output mixer skeleton); integration test through the full audio path; commit and push.

**Verification.**

```bash
ctest --test-dir build -R DirectLayer
bash bash/autotest.sh
```

**Risks & open decisions.**

- Output channel naming: in M4 the `OutputChannelId` is a manual integer; in M5 the OutputMixer will define `OutputChannel`s, at which point `OutputChannelId` becomes a real type. M4 defines it as an opaque ID with an `int` underlying.
- Auto inference is **explicitly deferred to M14**; M4 only ships manual routing. The transition guide (§2.3) notes the inference function "deserves its own design pass."
- **M4 Session 3 wires only the RawRoute path**; the `ProcessedChannelBufferView` span passed to `routeBuffers` is empty. Rationale: M3's `InputMixer::processBuffer` writes byte-serialized output to a TapeWriter queue and does not expose a post-processing float buffer for DirectLayer to consume. Exposing one would require either new InputMixer surface (out of M4 scope) or re-running ProcessingChain inside AudioCallback (bypasses the M3 design). M3's ProcessingChain is no-op anyway, so ProcessedRoute integration is moot until ProcessingChain has real DSP — that work lands with M5+.

**Execution mode.** `orchestrator+subagents`.

---

### M5 — Output Mixer expansion: per-channel strips + buses + sends + scope revision

**Goal.** Implement V3 Step 7 + V3 decision #57 (scope revision): the looper is now a complete production environment from physical input to physical output. Per-channel strips with gain/EQ/dynamics/sends (Audio strips first; MIDI/Video strips stubbed and filled in M9/M12); session-level effect buses; send/return architecture; master bus processing; Constituent renders + direct-layer contributions both flow into the output mixer; output routes to physical outputs and (eventually in M13) file/MIDI/video destinations.

**Acceptance criteria.**

- `OutputMixer::add_channel(ConstituentRef | DirectLayerRef, SignalType)` returns `OutputChannelId`; auto-created from active Constituents on render-pipeline tick.
- `OutputMixer::set_channel_strip(OutputChannelId, ChannelStripConfig)` accepts gain, pan, EQ, dynamics for Audio strips; equivalents per modality stubbed.
- `OutputMixer::add_bus(BusConfig, EffectChain)` returns `BusId`; channels route to buses via `route_channel_to_bus(OutputChannelId, BusId, SendLevel)`.
- Master bus exists implicitly as `BusId{0}`; all channels and buses ultimately route there.
- `OutputMixer::render_buffer` runs on the audio thread, traverses channels → buses → master, applies strip+bus+master processing, writes to physical-output buffers. Allocation-free, lock-free.
- Existing `EffectChain` (`core/include/ida/EffectChain.h`) is reused as the bus-level effect chain type; Constituent-local effects continue to use the same type.
- Tests assert: channel gain attenuates output; pan distributes correctly; send-to-bus + bus-effect produces wet signal; master bus is the last stage; RT-budget for a 32-channel / 8-bus configuration on the dev machine.
- **Input-side AudioChain bodies become real here too.** M3 ships `ProcessingChain` as the abstract base on `Channel` with no-op `AudioChain` / `MidiChain` / `VideoChain` / `FileChain` subclasses. M5 introduces `ChannelStrip<SignalType::Audio>` with real gain/pan DSP; the same class is wired in as the concrete implementation behind `Channel::processing` so InputMixer channels apply real input-side gain/pan before tape writes (not just OutputMixer channels). Mechanically: either `ChannelStrip<SignalType::Audio>` inherits `ProcessingChain` and supersedes `AudioChain` (rename — preferred), or `AudioChain` is rewritten to delegate to a held `ChannelStrip<SignalType::Audio>` (composition — fallback if inheritance forces an unwanted vtable layout on OutputMixer paths). Test addition to M5 Session 1: assert InputMixer channels apply gain/pan on the input side before enqueueing to TapeWriter. Added by `docs/superpowers/specs/2026-05-18-m3-design.md` Plan Amendment §3.

**Dependencies.** M4.

**Files touched.**

- `engine/include/ida/OutputMixer.h`, `engine/src/OutputMixer.cpp` (modified: real bodies)
- `engine/include/ida/ChannelStrip.h`, `engine/src/ChannelStrip.cpp` (new; SignalType-parameterized template)
- `engine/include/ida/Bus.h`, `engine/src/Bus.cpp` (new)
- `engine/include/ida/AudioCallback.h`, `engine/src/AudioCallback.cpp` (modified: OutputMixer slot real)
- `engine/include/ida/RenderPipeline.h`, `engine/src/RenderPipeline.cpp` (modified: emit ActiveReads keyed by OutputChannelId)
- `tests/OutputMixerTests.cpp` (modified: real assertions)
- `tests/ChannelStripTests.cpp`, `tests/BusTests.cpp` (new)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** `EffectChain` (existing), `RenderPipeline` (extended to emit per-OutputChannel reads).

**Tests landing in this milestone.**

- `OutputMixerTests`: channel gain, pan, mute, solo; send-to-bus; bus FX; master; channel-count scaling
- `ChannelStripTests`: per-modality strip variants instantiate; setters work
- `BusTests`: bus add/remove; effect chain attachment; send routing

**Sessions 1-3 broken out.**

- Session 1: `ChannelStrip<SignalType>` template + Audio specialization with gain/pan; OutputMixer registers channels; tests; commit.
- Session 2: `Bus` + send/return; master bus implicit; tests; commit.
- Session 3: Wire `render_buffer` audio-thread body; integration test through full I/O path; RT-budget assertions; commit and push.

**Verification.**

```bash
ctest --test-dir build -R "OutputMixer|ChannelStrip|Bus"
bash bash/autotest.sh
# Manual: launch app, route a Constituent through a channel + bus + master, play, verify audible output.
```

**Risks & open decisions.**

- Bus count limit: V3 §7 open question. Decision: dynamic, capped at 64 buses per session (enough for any realistic session; tape memory budget for bus channel-strips is ~few MB at most). Revisit if a session exceeds.
- EQ / dynamics DSP: stubs in M5 (gain + pan are real; EQ/dynamics declared but apply identity processing). Real DSP implementations are a separate design effort — flagged as a deferred sub-milestone the user can schedule independently.

**Execution mode.** `orchestrator+subagents` (Performance Benchmarker for the 32-channel/8-bus RT-budget test; Backend Architect for the channel→bus→master DAG audit).

---

### M6 — NotificationBus engine↔UI truthfulness channel

**Goal.** Build the V5 §8.6 NotificationBus once; route every engine→UI truthfulness signal through it (failure events per V5 §17.9, accessibility cues per V4 §16.10, capacity warnings, partition events). Categories prevent priority inversion; SPSC per category keeps the audio-thread post wait-free; UI drain runs on the message thread.

**Acceptance criteria.**

- `engine/include/ida/NotificationBus.h` + impl exists. `post(NotificationLevel, Category, Message)` callable from any thread including audio thread; allocation-free, wait-free on the audio-thread path (uses pre-allocated per-category SPSC ring).
- `Category` enum covers V5 §8.6 list: `DiskPressure, CpuPressure, RamPressure, DeviceEvent, PluginEvent, ClockEvent, NetworkEvent, StateRepair, TapeRotation`.
- `NotificationLevel` enum: `Info, Degradation, Warning, Error`.
- `drain()` non-blocking; returns a `std::vector<Notification>` for the UI thread to render.
- UI integration: a new minimal notification surface in `MainComponent` (text list under the Preparation tab; M22 redesigns the surface).
- Tests assert: post from audio thread is wait-free; drain returns posted items in posted order per category; per-category ordering preserved across concurrent post + drain.

**Dependencies.** M1 (audio thread exists to post from), M5 (UI integration goes alongside the Output Mixer's status surfaces).

**Files touched.**

- `engine/include/ida/NotificationBus.h`, `engine/src/NotificationBus.cpp` (new)
- `app/MainComponent.h`, `app/MainComponent.cpp` (modified: drain on Timer tick; render under Preparation tab)
- `tests/NotificationBusTests.cpp` (new)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** `LockFreeSpscQueue` — one per category.

**Tests landing in this milestone.**

- `NotificationBusTests`: post-from-audio-thread wait-free assertion (allocation audit); per-category FIFO; concurrent producer + consumer correctness; level filtering for UI subscription

**Sessions 1-3 broken out.**

- Session 1: `NotificationBus.{h,cpp}` + per-category SPSC; unit tests; commit.
- Session 2: Wire into existing emitters (M1's device events, M3's tape-rotation events, M5's overload events); UI drain in `MainComponent::timerCallback`; commit.
- Session 3: Minimal Preparation-tab notification list; smoke test; commit and push.

**Verification.**

```bash
ctest --test-dir build -R NotificationBus
bash bash/autotest.sh
# Manual: launch app, trigger an overload (large processing load), confirm notification appears.
```

**Risks & open decisions.**

- Notification capacity per ring: pick 256 entries per category as a starting cap; overflow drops the oldest entry with a counter visible in diagnostics. Revisit if any category overruns in real use.

**Execution mode.** `orchestrator+subagents`.

---

## Part C — Plug-in hosting

### M7 — Out-of-process plug-in hosting framework

**Goal.** Implement V7 §9.1 in full: plug-in host process binary, shared-memory SPSC IPC rings, watchdog, supervisor process, GUI host process with platform-specific window embedding, parameter marshalling. **No in-process fallback.** This is the largest single piece of architectural work in the plan.

**Acceptance criteria.**

- A standalone `ida_plugin_host` executable target builds, takes a plug-in UID and a shared-memory segment name as arguments, loads the plug-in into its own address space, pumps buffers through it.
- Engine-side `OutOfProcessPluginInstance` class manages the lifecycle of one host process per plug-in instance: spawn, attach shared memory, send/receive buffers, observe missed deadlines, request restart.
- Shared-memory transport: two SPSC rings per instance (engine→host, host→engine); pre-allocated at session start; LMC-timestamps in each message header.
- Engine-side watchdog: per-buffer time budget per plug-in; on miss, bypass node substitutes (dry signal flows); supervisor escalates persistent misses.
- Supervisor: non-realtime thread observes miss reports; transient → log; persistent → terminate + restart host process; repeated → mark channel permanently bypassed + emit NotificationBus event.
- GUI host process: plug-in editor windows render in their own process and are embedded into the main UI via `HWND`/`NSView`/`X11` reparenting (macOS first; Windows/Linux per the M23+ platform-completion roadmap).
- Parameter changes: written into the input ring as messages alongside audio, in LMC-timestamp order; host applies at correct sample boundaries.
- Latency cost measured: shared-memory copy round-trip < 10 µs on Apple Silicon dev machine.
- Tests assert: spawn-load-bypass cycle on a synthetic test plug-in; watchdog substitutes bypass on intentional timeout; supervisor restart works; GUI embedding works on macOS.

**Dependencies.** M5 (OutputMixer has channels to route plug-in output into), M6 (NotificationBus carries plug-in events).

**Files touched.**

- `host/include/ida/OutOfProcessPluginInstance.h`, `host/src/OutOfProcessPluginInstance.cpp` (new)
- `host/include/ida/PluginHostProcess.h`, `host/src/PluginHostProcess.cpp` (new — IPC primitives shared between engine and host binary)
- `host/include/ida/PluginWatchdog.h`, `host/src/PluginWatchdog.cpp` (new)
- `host/include/ida/PluginSupervisor.h`, `host/src/PluginSupervisor.cpp` (new)
- `host/include/ida/PluginGuiEmbedding.h`, `host/src/PluginGuiEmbedding.cpp` (new; platform-conditional bodies)
- `host_process/main.cpp` (new — the `ida_plugin_host` executable's entry point)
- `host_process/CMakeLists.txt` (new — separate executable target)
- `CMakeLists.txt` (modified: add `host_process` subdirectory)
- `engine/include/ida/AudioCallback.h`, `engine/src/AudioCallback.cpp` (modified: pump plug-in IPC rings inline)
- `tests/OutOfProcessPluginTests.cpp`, `tests/PluginWatchdogTests.cpp`, `tests/PluginSupervisorTests.cpp` (new)
- `tests/fixtures/SyntheticTestPlugin.cpp` (new — minimal CLAP plug-in for tests)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** `LockFreeSpscQueue` for the in-process side of IPC envelope handling; `juce::KnownPluginList` (from existing `PluginScanner.cpp`) as the plug-in discovery source for the supervisor.

**Tests landing in this milestone.**

- `OutOfProcessPluginTests`: spawn + load synthetic plug-in + identity pass-through; clean shutdown
- `PluginWatchdogTests`: deadline miss → bypass; bypass node returns dry signal
- `PluginSupervisorTests`: persistent miss → host process killed + restarted; repeated → channel permanent bypass + notification posted
- Synthetic test plug-in (`SyntheticTestPlugin.cpp`): identity, deliberate-timeout, deliberate-crash variants for behavior testing

**Sessions 1-3 broken out.**

- Session 1: `ida_plugin_host` executable target; loads a synthetic plug-in; identity pass-through over `stdin`/`stdout` (the simplest transport before shared memory); commit.
- Session 2: Replace `stdin`/`stdout` with POSIX shared-memory + SPSC rings; LMC timestamps in headers; round-trip latency measurement; commit.
- Session 3: Engine-side `OutOfProcessPluginInstance` wires through `OutputMixer`'s plug-in slot on a Constituent's `EffectChain`; integration test plays audio through the synthetic plug-in; commit.

Sessions 4-N (estimated): watchdog (Session 4); supervisor (Session 5-6); GUI embedding macOS (Session 7-8); platform conditional structure for Windows/Linux GUI (Session 9 — bodies stubbed until M23+); polish + tests + RT-budget validation (Session 10-12).

**Verification.**

```bash
cmake --build build --target ida_plugin_host
ctest --test-dir build -R "OutOfProcessPlugin|PluginWatchdog|PluginSupervisor"
bash bash/autotest.sh
# Manual: launch app, add a synthetic test plug-in to a Constituent's effect chain,
# play audio, intentionally trigger a timeout, verify dry signal continues + notification appears.
```

**Risks & open decisions.**

- Plug-in GUI embedding is platform-specific and historically painful (NSView reparenting on macOS in particular). Budget extra time. Fall back to "plug-in GUI in its own top-level window" if embedding stalls — operator UX cost is small.
- Sandbox entitlements on macOS for the `ida_plugin_host` binary: shared memory IPC requires careful entitlement design under macOS's app sandbox (`com.apple.security.app-sandbox` interactions with `com.apple.security.cs.allow-shared-memory`). The signed CI workflow (`ci-macos-signed.yml`) needs updating to sign the host binary too.
- Wet-capture pre-allocation: budget for non-deterministic plug-ins' wet captures (V5 §15.6) is committed in M8; M7 only allocates the input/output rings.

**Execution mode.** `orchestrator+subagents` (Backend Architect for the IPC ring shape; Security Engineer for sandboxing review; Performance Benchmarker for round-trip latency).

---

### M8 — Plug-in determinism + failure semantics + CLAP as first format

**Goal.** Implement V5 §15.6's determinism contract, the three user-selectable archival strategies (determinism / wet capture / version pinning + state hashing), the Constituent broken/invalid states from V5 §17, LMC calibration corruption recovery, tape rotation on disk-full. CLAP is the first plug-in format hosted (per V6 §15.6 priority — cleanest API, exercises the determinism contract without legacy compromises). VST3 follows in M20; AU in M21.

**Acceptance criteria.**

- `host/include/ida/PluginDeterminismFlag.h` defines `enum class ArchivalMode { DeterminismContract, WetCapture, VersionPinning }`. Default for new sessions: `VersionPinning` (per V5 §8.3 default disposition).
- CLAP host extension in `ida_plugin_host`: loads `.clap` bundles via the CLAP API; queries `clap_plugin->is_deterministic` (or equivalent host extension if CLAP exposes one; otherwise treat as non-deterministic by default).
- `WetCapture` mode: tape writer adds a `<channelId>.wet.tape` alongside the dry tape; stored in SAF's `tapes/` subdir (interop with M11).
- `VersionPinning` mode: plug-in version, settings hash (SHA-256 of state blob), oversampling rate, declared internal state hash stored in the session manifest; on reopen, mismatch warns operator via NotificationBus.
- Constituent state machine extended: `Valid | Broken | Invalid`. `Broken` = references missing tape segment; `Invalid` = anchor/bounds contradict parent. Rendering treats both as silence; identity preserved.
- LMC calibration corruption: on session load, validate calibration table checksum; on failure, trigger fresh loopback calibration via existing `AudioDeviceCalibration`; emit NotificationBus event during recalibration.
- Tape rotation on disk-full: reachability scan from Constituent graph identifies tape segments referenced by no active Constituent; reclaims oldest unreferenced segments first; runs on a non-realtime thread.
- Tests cover all three archival modes; broken/invalid state behavior; calibration recovery; reachability scan.

**Dependencies.** M7 (out-of-process framework exists to host plug-ins through), M3 (tape writer exists to capture wet output through), M6 (NotificationBus surfaces failures).

**Files touched.**

- `host/include/ida/ArchivalMode.h`, `host/include/ida/PluginDeterminismFlag.h` (new)
- `host/src/OutOfProcessPluginInstance.cpp` (modified: archival mode handling)
- `host_process/main.cpp` (modified: CLAP loader)
- `host_process/CMakeLists.txt` (modified: link CLAP host SDK)
- `external/clap/` (new submodule or vendored — pick after format-priority sub-decision)
- `engine/include/ida/WetCaptureWriter.h`, `engine/src/WetCaptureWriter.cpp` (new)
- `core/include/ida/Constituent.h`, `core/src/Constituent.cpp` (modified: state enum + transitions)
- `engine/src/AudioDeviceCalibration.cpp` (modified: corruption detection + auto-recovery)
- `engine/include/ida/TapeReachabilityScan.h`, `engine/src/TapeReachabilityScan.cpp` (new)
- `persistence/include/ida/TapeStore.h`, `persistence/src/TapeStore.cpp` (modified: `reclaimUnreferenced(span<TapeId>)` API)
- `tests/ArchivalModeTests.cpp`, `tests/ConstituentStateTests.cpp`, `tests/CalibrationRecoveryTests.cpp`, `tests/TapeReachabilityTests.cpp` (new)

**Existing utilities to reuse.** `Constituent` (extended in place), `AudioDeviceCalibration` (extended for recovery), `TapeStore`, `NotificationBus`.

**Tests landing in this milestone.**

- `ArchivalModeTests`: round-trip across each mode; mismatch on reopen surfaces warning
- `ConstituentStateTests`: broken/invalid transitions; silence on render; identity preserved
- `CalibrationRecoveryTests`: corrupt checksum → auto-recovery
- `TapeReachabilityTests`: scan correctly identifies unreferenced segments

**Sessions 1-3 broken out.**

- Session 1: ArchivalMode enum + VersionPinning hashing (no CLAP yet — test with synthetic plug-in); commit.
- Session 2: CLAP loader in `host_process/main.cpp`; integration test with a known free CLAP plug-in; commit.
- Session 3: Constituent state machine + broken/invalid render-as-silence; tests; commit and push.

Sessions 4-N: WetCapture writer (Session 4-5); calibration recovery (Session 6); tape reachability scan (Session 7-8); end-to-end integration (Session 9-10).

**Verification.**

```bash
ctest --test-dir build -R "ArchivalMode|ConstituentState|CalibrationRecovery|TapeReachability"
bash bash/autotest.sh
# Manual: load a CLAP plug-in (e.g., Surge XT CLAP build), record audio through it, save session,
# upgrade the plug-in version, reopen, verify warning appears.
```

**Risks & open decisions.**

- Wet-capture storage budget: V5 §13 (open question 13) — decision: master bus output wet-captured by default when ANY non-deterministic plug-in is in the chain; per-channel wet capture opt-in via per-instance ArchivalMode. UI for this lands in M22.
- CLAP host SDK source: vendor from official CLAP repo (`free-audio/clap`) as a submodule under `external/clap/`; license is MIT.
- Trust model for `isDeterministic` declarations (V5 §14 open question): on first load of any plug-in version, render the same buffer twice and compare; mismatch demotes the plug-in to non-deterministic regardless of its declaration. One-time cost per plug-in version; result cached in user-config dir.

**Execution mode.** `orchestrator+subagents`.

---

## Part D — Modality completion

### M9 — MIDI 2.0 / UMP end-to-end

**Goal.** Implement V3 Step 9 (MIDI portion) + V4 MIDI 2.0 commitments + V7 §6.12 full specification. UMP-shaped from the first commit. MIDI-CI capability negotiation at device attach; MPE channel allocation at the input membrane; per-note expression on parallel parameter tapes; downcast at the output membrane; SMF and UMP-JSONL export readiness (export UX lands in M13).

**Acceptance criteria.**

- `engine/include/ida/UmpEvent.h` defines a discriminated union covering 32-bit Channel Voice Message (MIDI 1.0 upcast), 64-bit Channel Voice Message (MIDI 2.0), 128-bit System Exclusive 8, 32-bit Utility Message.
- `engine/include/ida/MidiInput.h` + impl wraps `juce::MidiInput`; upcast happens at the wrapper (decision per V4 open question 7: at the input wrapper, not the channel strip — keeps the device-dependent code concentrated and the channel-strip code modality-clean).
- `engine/include/ida/MidiChannelStrip.h` implements `ChannelStrip<SignalType::Midi>` specialization: transpose, velocity curve, channel filter, event remap.
- `engine/include/ida/MidiDeviceWrapper.h` performs MIDI-CI Discovery on attach; flags device 1.0-only or 2.0-capable per-direction.
- MPE channel allocation: incoming MPE messages allocated per MPE lower/upper zone model; allocation persisted as tape metadata.
- Per-note expression: stored on parallel parameter tapes time-aligned with the note tape; NOT interleaved with notes; first-class automatable.
- Output membrane downcast: per-destination policy (1.0 destination → downcast 16-bit velocity to 7-bit, drop per-note attributes); UMP 2.0 destination → pass-through.
- Controller learn UX hook: `MidiLearnSession` class lets UI capture next inbound CC + bind to a parameter; bindings persist in session.
- Tests cover UMP round-trip, upcast at wrapper, MPE allocation, parameter-tape parallel storage, downcast on output.

**Dependencies.** M5 (OutputMixer + ChannelStrip exist), M6 (NotificationBus for device events).

**Files touched.**

- `engine/include/ida/UmpEvent.h` (new)
- `engine/include/ida/MidiInput.h`, `engine/src/MidiInput.cpp` (new)
- `engine/include/ida/MidiDeviceWrapper.h`, `engine/src/MidiDeviceWrapper.cpp` (new)
- `engine/include/ida/MidiChannelStrip.h`, `engine/src/MidiChannelStrip.cpp` (new — `ChannelStrip<SignalType::Midi>` specialization)
- `engine/include/ida/MpeAllocator.h`, `engine/src/MpeAllocator.cpp` (new)
- `engine/include/ida/MidiLearnSession.h`, `engine/src/MidiLearnSession.cpp` (new)
- `core/include/ida/ParameterAutomation.h` (modified: per-note expression parameter shape)
- `engine/src/InputMixer.cpp` (modified: MIDI inputs supported)
- `engine/src/OutputMixer.cpp` (modified: MIDI outputs supported; downcast policy)
- `tests/UmpEventTests.cpp`, `tests/MidiInputTests.cpp`, `tests/MidiDeviceWrapperTests.cpp`, `tests/MidiChannelStripTests.cpp`, `tests/MpeAllocatorTests.cpp`, `tests/MidiLearnSessionTests.cpp` (new)
- `tests/CMakeLists.txt` (modified)

**Existing utilities to reuse.** `Tape<UmpEvent>` (template specialization), `ChannelStrip` template (specialized), `LockFreeSpscQueue` (MIDI input queue), `NotificationBus` (device-attach events).

**Tests landing in this milestone.**

- `UmpEventTests`: discriminated-union round-trip
- `MidiInputTests`: MIDI 1.0 message upcast to UMP
- `MidiDeviceWrapperTests`: MIDI-CI Discovery + capability flagging
- `MpeAllocatorTests`: per MPE spec allocation
- `MidiChannelStripTests`: transpose, velocity curve, channel filter, event remap
- `MidiLearnSessionTests`: capture-and-bind UX hook

**Sessions 1-3 broken out.**

- Session 1: `UmpEvent.h` + tests; `MidiInput` wrapper + upcast tests; commit.
- Session 2: `MidiDeviceWrapper` with MIDI-CI Discovery (real protocol via `juce::MidiInput::getAvailableDevices` enriched); commit.
- Session 3: `MidiChannelStrip` specialization + wired through InputMixer; commit and push.

Sessions 4-N: MPE allocator (Session 4); per-note expression on parameter tapes (Session 5-6); output membrane downcast (Session 7); MidiLearnSession (Session 8); end-to-end integration tests (Session 9-10).

**Verification.**

```bash
ctest --test-dir build -R "Ump|Midi|Mpe"
bash bash/autotest.sh
# Manual: attach a MIDI 1.0 keyboard, verify input flows; attach a MIDI 2.0 / MPE controller,
# verify per-note expression captured on parameter tape; route through to a MIDI 1.0 output,
# verify downcast.
```

**Risks & open decisions.**

- MIDI-CI library: implement against the spec directly (M2-103-UM); no widely-available open-source MIDI-CI library exists yet. Scope to Discovery + Profile Configuration in M9; Property Exchange deferred.
- Per-note expression UX (V4 open question 10): MidiLearnSession surfaces a `bindMode` field — `PerChannelCc | PerNoteExpression` — operator-selected per binding. Default `PerChannelCc` (matches operator's pre-MPE muscle memory).

**Execution mode.** `orchestrator+subagents`.

---

### M10 — Mix snapshots

**Goal.** Implement V3 §2.5 / Step 8: a `MixSnapshot` Constituent subtype carrying complete input + output mixer state, anchored to conceptual time, with transition types (Cut / LinearFade / Curve — curves are stubbed; Cut + LinearFade are real).

**Acceptance criteria.**

- `core/include/ida/MixSnapshot.h` extends `Constituent` (via the existing copy-on-write pattern); carries `InputMixerState input_state`, `OutputMixerState output_state`, `TransitionType { Cut, LinearFade, Curve }`, `Duration transition_duration`.
- `InputMixerState` and `OutputMixerState` capture the full per-channel + per-bus + per-strip configuration snapshot.
- `RenderPipeline::activeReadsAt` recognizes MixSnapshot Constituents and emits a "snapshot active" marker that `AudioCallback` consumes to call `OutputMixer::recall_snapshot(snapshotId, transition, duration)`.
- `OutputMixer::capture_snapshot(name)` returns a `SnapshotId` capturing current state; `OutputMixer::recall_snapshot(snapshotId, transition, duration)` applies a snapshot with the requested transition.
- Cut transition is instantaneous; LinearFade interpolates parameter values linearly over duration; Curve is a stub returning Cut behavior with a `TODO: M10.b` log line (curve representation is a separate sub-design — flagged for a future session).
- Tests assert: capture-then-recall yields byte-identical state; LinearFade midpoint = arithmetic mean of endpoints; MixSnapshot Constituents serialize through SAF (M11 dependency — M10's serialization lands as a SAF format addition).

**Dependencies.** M5 (OutputMixer state to capture), M9 (MIDI strip state included in snapshots).

**Files touched.**

- `core/include/ida/MixSnapshot.h`, `core/src/MixSnapshot.cpp` (new)
- `engine/include/ida/InputMixerState.h`, `engine/include/ida/OutputMixerState.h` (new — capturable structs)
- `engine/src/OutputMixer.cpp` (modified: capture_snapshot, recall_snapshot, transition interpolation)
- `engine/src/RenderPipeline.cpp` (modified: emit snapshot markers)
- `engine/src/AudioCallback.cpp` (modified: consume snapshot markers)
- `tests/MixSnapshotTests.cpp` (new)

**Existing utilities to reuse.** `Constituent` (extended via subtype), `RenderPipeline` (extended), `OutputMixer` (extended).

**Tests landing in this milestone.**

- `MixSnapshotTests`: capture / recall / Cut / LinearFade midpoint / state round-trip

**Sessions 1-3 broken out.**

- Session 1: `InputMixerState` + `OutputMixerState` structs; `capture_snapshot` and `recall_snapshot` real bodies for Cut transition; tests; commit.
- Session 2: `MixSnapshot` Constituent subtype + RenderPipeline emission; integration test; commit.
- Session 3: LinearFade interpolation; tests; commit and push.

**Verification.**

```bash
ctest --test-dir build -R MixSnapshot
bash bash/autotest.sh
```

**Risks & open decisions.**

- Per V3 open question 5 ("per-snapshot routing recall"): direct-layer routing state IS included in the snapshot (V3 implies but does not commit; this plan commits to inclusion — eliminates the surprise of routing surviving a snapshot recall).
- Curve representation deferred to a follow-up sub-design session.

**Execution mode.** `orchestrator+subagents`.

---

### M11 — IDA Archive Format (clean break from JSON SessionFormat)

**Goal.** Implement V7 §15.7 in full: `.saf` ZIP container; `manifest.json` with SemVer; reader version-check policy; `tapes/` subdir with tier-appropriate codecs; `constituents.json` flat-array-keyed-by-ID (refactor from current nested tree); `plugins.json` with state migration; `permissions.json`; atomic save via tmp+rename. Delete the existing JSON SessionFormat after SAF round-trip is proven.

**Acceptance criteria.**

- `persistence/include/ida/SafContainer.h` + impl: `.saf` ZIP wrapper using `juce::ZipFile`; compression level 0 (tapes are already compressed).
- `persistence/include/ida/SafManifest.h` + impl: `manifest.json` schema (SemVer `format-version`, engine version, session UUID, creation + modification timestamps in LMC + UTC, capability tier, tape index, constituent index, top-level SHA-256).
- Reader SemVer check is FIRST action after opening: same MAJOR (else refuse), MINOR ≤ reader's MINOR (else open + warn + ignore unknown fields), PATCH ignored.
- `persistence/include/ida/SafTapeFormat.h`: FLAC for audio above Survival; WAV for Lavish if user prefers; JSONL for MIDI (one UMP event per line); ProRes 422 LT / MJPEG / HEVC-I for video by tier; JSONL for parameter/control/system tapes.
- `persistence/include/ida/SafConstituentSerializer.h`: flat-array-keyed-by-ID (refactor from `SessionFormat.cpp:560-595`'s nested-tree-with-refs); each entry has boundaries, anchor, tempo map, effect chain refs, repetition rules, metadata, child IDs.
- `persistence/include/ida/SafPluginSerializer.h`: `plugins.json` records UID, version, state hash, state blob, determinism flag, wet-capture pointer.
- `persistence/include/ida/SafPermissions.h`: `permissions.json` initialized to owner-writable + ensemble-readable defaults (M16 extends with peer-specific entries).
- Atomic save: write to `session.saf.tmp`, fsync, atomic rename to `session.saf`. POSIX `rename`; Windows `MoveFileEx` (M23+).
- Plug-in state migration UI: four-outcome dispatcher (exact match / newer version / older version / missing) per V7 §15.7.
- Forward compatibility test: deliberately-broken "future MINOR version" file opens with warning, ignores unknown fields.
- Tier-aware flush intervals committed to V7 §17.8 spec values: Lavish per-buffer (~1-3 ms), Comfortable 50 ms, Tight 200 ms, Survival 1000 ms; performer override 1 ms-5000 ms.
- Existing `persistence/src/SessionFormat.cpp` and `tests/SessionFormatTests.cpp` deleted at the end of the milestone (clean break per decision #4).
- New `tests/SafContainerTests.cpp`, `SafManifestTests.cpp`, `SafConstituentSerializerTests.cpp`, `SafPluginSerializerTests.cpp`, `SafRoundTripTests.cpp` land.

**Dependencies.** M3 (TapeWriter exists), M8 (plug-in determinism + state hashing exists), M10 (MixSnapshots serialize through here).

**Files touched.**

- `persistence/include/ida/SafContainer.h`, `persistence/src/SafContainer.cpp` (new)
- `persistence/include/ida/SafManifest.h`, `persistence/src/SafManifest.cpp` (new)
- `persistence/include/ida/SafTapeFormat.h`, `persistence/src/SafTapeFormat.cpp` (new)
- `persistence/include/ida/SafConstituentSerializer.h`, `persistence/src/SafConstituentSerializer.cpp` (new)
- `persistence/include/ida/SafPluginSerializer.h`, `persistence/src/SafPluginSerializer.cpp` (new)
- `persistence/include/ida/SafPermissions.h`, `persistence/src/SafPermissions.cpp` (new)
- `persistence/include/ida/SessionFormat.h`, `persistence/src/SessionFormat.cpp` (DELETED at end of milestone)
- `tests/SessionFormatTests.cpp` (DELETED)
- `tests/SafContainerTests.cpp`, `SafManifestTests.cpp`, `SafConstituentSerializerTests.cpp`, `SafPluginSerializerTests.cpp`, `SafRoundTripTests.cpp` (new)
- `app/CapabilityTier.cpp` (modified: expose flush-interval tunables per tier)
- `app/MainComponent.cpp` (modified: Save/Load buttons wire to SAF, not SessionFormat)
- `bash/smoke-persistence.sh` (modified: round-trip a SAF file instead of JSON)
- `external/miniz/` or use `juce::ZipFile` (decide first session — `juce::ZipFile` keeps deps minimal)

**Existing utilities to reuse.** `TapeStore` (contents live in SAF's `tapes/` subdir), `CapabilityTier` (drives format selection), `juce::ZipFile` (container), `juce::JSON` (manifest + index files), `Constituent` (serialized), Constituent's existing structural-sharing logic (preserved by flat-array-with-IDs serialization).

**Tests landing in this milestone.**

- `SafContainerTests`: ZIP wrap/unwrap; compression 0; directory-tree round-trip
- `SafManifestTests`: SemVer parsing; reader policy (same MAJOR / MINOR ≤ reader / PATCH ignored); forward-compat test with future MINOR file
- `SafConstituentSerializerTests`: flat-array-with-IDs round-trip preserves structural sharing
- `SafPluginSerializerTests`: four-outcome migration dispatch
- `SafRoundTripTests`: end-to-end session save → load → render → byte-identical to original

**Sessions 1-3 broken out.**

- Session 1: `SafContainer` ZIP wrap/unwrap; tests; commit.
- Session 2: `SafManifest` SemVer + reader policy; tests including the future-MINOR forward-compat test; commit.
- Session 3: `SafConstituentSerializer` flat-array refactor; round-trip test; commit and push.

Sessions 4-N: tape format selection per tier (Session 4); plug-in serializer + migration UI (Session 5-7); permissions file (Session 8); atomic save (Session 9); flush-interval tunables (Session 10); deletion of old SessionFormat + bash/smoke-persistence.sh migration (Session 11); end-to-end (Session 12).

**Verification.**

```bash
ctest --test-dir build -R Saf
bash bash/autotest.sh
APP_BUNDLE="build-xcode/app/IDA_artefacts/Release/IDA.app" \
  bash bash/smoke-persistence.sh                # SAF round-trip
grep -r "SessionFormat\|sessionformat" .         # expect zero hits after milestone close
```

**Risks & open decisions.**

- FLAC encoder dependency: use `libFLAC` (BSD-3, mature). Vendor under `external/flac/`.
- ProRes encoder for video: macOS has `VideoToolbox`-based encoder; cross-platform alternative is `ffmpeg` libs. Decision: `VideoToolbox` on macOS (matches platform-first order), `ffmpeg` libs added as a cross-platform option when M23 / Windows / Linux work begins.
- HEVC-I encoder: also `VideoToolbox` on macOS.
- Container library choice (`juce::ZipFile` vs `miniz` vs `libzip`): pick `juce::ZipFile` for the first implementation (zero new dep); if performance issues surface, swap to `miniz`.
- **M3-era partial-tape forward compatibility.** M3 Session 3 ships a placeholder JSONL parameter-delta format (`{"t": <lmcRational>, "param": "<name>", "value": <number>}` per line) for NonDestructive tapes. M11's canonical format must either read M3-era `.tape.partial` files losslessly or ship a one-time migration shim. Verified by adding an M3-era partial as a fixture to `SafRoundTripTests`. Added by `docs/superpowers/specs/2026-05-18-m3-design.md` Plan Amendment §2.

**Execution mode.** `orchestrator+subagents` (Database Optimizer for the constituents.json flat-array schema design; Compliance Auditor for the SemVer policy enforcement audit).

---

### M12 — Video tier-aware rendering

**Goal.** Implement V7 §11.8 / §9.4: three render strategies per capability tier — nearest-frame (Tight, Survival default), frame-blending (Comfortable default), motion-compensated interpolation (Lavish default). Performer may override per-Constituent. GPU compute pipeline as a separate queue from audio; audio and video independently disciplined against the LMC.

**Acceptance criteria.**

- `video/include/ida/VideoRenderer.h` + impl exposes `RenderStrategy { NearestFrame, FrameBlending, MotionCompensated }` enum.
- Strategy selected at session start from `CapabilityTier`; per-Constituent override field on `Constituent` (uses existing metadata channel).
- `VideoRenderer::renderFrame(LmcTime target, VideoTapeRef tape) → Frame` implementation:
  - NearestFrame: single timestamp lookup; deterministic
  - FrameBlending: GPU shader (Metal on macOS) per-pixel alpha blend of two bracketing frames
  - MotionCompensated: OpenCV `DISOpticalFlow` (cross-platform; vendor-specific upgrades for later milestones)
- Render runs on a dedicated GPU queue, not the audio thread.
- Tests cover each strategy; FrameBlending midpoint = arithmetic mean; MotionCompensated produces non-trivial intermediate frame (not just nearest).
- Tier override surfaces in NotificationBus when GPU budget exceeded (Lavish strategy falls back to FrameBlending automatically).

**Dependencies.** M5 (OutputMixer routes video out), M11 (SAF stores video tapes; format selection per tier).

**Files touched.**

- `video/include/ida/VideoRenderer.h`, `video/src/VideoRenderer.cpp` (new)
- `video/include/ida/RenderStrategy.h` (new)
- `video/include/ida/FrameBlendShader.h`, `video/src/FrameBlendShader.metal` (new — Metal shader for macOS)
- `video/include/ida/MotionCompensator.h`, `video/src/MotionCompensator.cpp` (new — OpenCV DISOpticalFlow wrapper)
- `video/src/VideoTape.cpp` (modified: integration with VideoRenderer)
- `core/include/ida/Constituent.h` (modified: per-Constituent render-strategy override field)
- `tests/VideoRendererTests.cpp`, `MotionCompensatorTests.cpp` (new)
- `external/opencv/` (new — vendored or system-installed)
- `CMakeLists.txt` (modified: link OpenCV, Metal)

**Existing utilities to reuse.** `Tape<VideoFrame>` (existing), `FrameMembrane` (existing — extends rather than rewrites), `VideoPreview` (UI already in place).

**Tests landing in this milestone.**

- `VideoRendererTests`: per-strategy correctness
- `MotionCompensatorTests`: optical-flow output validity on synthetic test footage

**Sessions 1-3 broken out.**

- Session 1: `VideoRenderer` + NearestFrame strategy + tests; commit.
- Session 2: FrameBlending Metal shader (macOS); tests; commit.
- Session 3: MotionCompensated via OpenCV; tests; commit and push.

**Verification.**

```bash
ctest --test-dir build -R "VideoRenderer|MotionCompensator"
bash bash/autotest.sh
# Manual: import a video file, scrub through it under each tier, verify visual quality.
```

**Risks & open decisions.**

- OpenCV is large. If footprint is a concern, the `DISOpticalFlow` module can be extracted standalone. Defer decision until first measurement.
- Metal-only at this milestone; Windows D3D / Linux Vulkan equivalents land with M23+ platform work.

**Execution mode.** `orchestrator+subagents` (Technical Artist or visionOS Spatial Engineer for the Metal shader; AI Engineer for the optical-flow integration).

---

### M13 — File I/O: audio readers/writers + SMF + UMP-JSONL + video files + export ergonomics

**Goal.** Implement V3 Step 9 (file I/O portion) + V6 §6.11 export. Audio file formats: WAV, FLAC, AIFF, MP3 read; WAV / FLAC / AIFF write. MIDI: SMF Type 0 + Type 1 read/write; UMP-JSONL read/write for full-fidelity archive. Video: read via VideoToolbox (macOS) / ffmpeg; write via M12's per-tier codec selection. Export UI: stems vs full-mix selector, bus selection, destination chooser.

**Acceptance criteria.**

- File readers/writers for each format above. Audio readers feed into `InputMixer` as `FileInputChannelStrip`s (`ChannelStrip<SignalType::File>` specialization).
- SMF reader produces a sequence of UMP events (upcast at read time); SMF writer downcasts UMP to 1.0 at write time.
- UMP-JSONL reader/writer: one UMP event per line, LMC + conceptual timestamps on each event.
- Export UI: a new `Export...` menu item under Preparation tab. Modal dialog with: format selector, stems vs full-mix radio, per-bus checkboxes for stem export, destination path picker.
- Export runs on a non-realtime thread; progress surfaces via NotificationBus.
- Tests cover round-trip for each format.

**Dependencies.** M9 (UMP shape exists), M11 (SAF stores files in `tapes/` subdir), M12 (video read/write codec selection aligned).

**Files touched.**

- `persistence/include/ida/AudioFileReader.h`, `persistence/src/AudioFileReader.cpp` (new — wraps `juce::AudioFormatReader` family)
- `persistence/include/ida/AudioFileWriter.h`, `persistence/src/AudioFileWriter.cpp` (new)
- `persistence/include/ida/SmfReader.h`, `persistence/src/SmfReader.cpp` (new)
- `persistence/include/ida/SmfWriter.h`, `persistence/src/SmfWriter.cpp` (new)
- `persistence/include/ida/UmpJsonlReader.h`, `persistence/src/UmpJsonlReader.cpp` (new)
- `persistence/include/ida/UmpJsonlWriter.h`, `persistence/src/UmpJsonlWriter.cpp` (new)
- `persistence/include/ida/VideoFileReader.h`, `persistence/src/VideoFileReader.cpp` (new)
- `persistence/include/ida/VideoFileWriter.h`, `persistence/src/VideoFileWriter.cpp` (new)
- `engine/include/ida/FileInputChannelStrip.h`, `engine/src/FileInputChannelStrip.cpp` (new — `ChannelStrip<SignalType::File>`)
- `engine/include/ida/ExportEngine.h`, `engine/src/ExportEngine.cpp` (new)
- `app/MainComponent.cpp` (modified: Export... menu + modal)
- `tests/AudioFileTests.cpp`, `SmfTests.cpp`, `UmpJsonlTests.cpp`, `VideoFileTests.cpp`, `ExportEngineTests.cpp` (new)

**Existing utilities to reuse.** `juce::AudioFormatManager` + readers/writers (WAV, FLAC, AIFF builtin; MP3 read via JUCE's `MP3AudioFormat` or LAME for write — MP3 write deferred per V6 §6.11 scope), `juce::MidiFile` (SMF base; UMP upcast wrapper on top), `juce::AudioFormatReaderSource` (for FileInputChannelStrip), VideoToolbox / ffmpeg (M12).

**Tests landing in this milestone.**

- `AudioFileTests`: round-trip each format
- `SmfTests`: Type 0 + Type 1 round-trip with UMP upcast / downcast
- `UmpJsonlTests`: line-by-line round-trip with timestamp preservation
- `VideoFileTests`: codec round-trip
- `ExportEngineTests`: stems vs full-mix; bus selection; progress reporting

**Sessions 1-3 broken out.** Per file format. Ralph drives this milestone:

- Session 1 (operator-launched ralph PRD): list every format above as a TODO line in the PRD; ralph iterates "implement next format reader, writer, round-trip test, commit" until `passes: true`.
- Session 2-N: as ralph iterates, the orchestrator monitors and intervenes only on architectural questions (e.g., where does the Export modal live in MainComponent's tab structure).

**Verification.**

```bash
ctest --test-dir build -R "AudioFile|Smf|UmpJsonl|VideoFile|ExportEngine"
bash bash/autotest.sh
# Manual: import a WAV, FLAC, SMF, ProRes; export a session as stems; verify each round-trip.
```

**Risks & open decisions.**

- MP3 read uses JUCE's built-in decoder if license-clean for the use case; MP3 write deferred.
- Export-destination defaults: per V6 open question 16 — project folder by default, persisted choice across sessions; surfaced in Settings (M22).

**Execution mode.** `orchestrator+subagents, ralph inner loop after PRD`.

---

## Part E — Auto-routing & time discipline

### M14 — Automatic direct-routing inference

**Goal.** V3 Step 10: context-tracking logic that watches arm states, playback overlaps, utility signals; auto-configures `DirectLayer` routes; user-override surfaces.

**Acceptance criteria.**

- `engine/include/ida/DirectRoutingInference.h` + impl: pure function from `(ChannelArmStates, PlaybackOverlaps, UtilitySignals) → set<DirectLayerRoute>`.
- Deterministic; fast (< 100 µs per call on the dev machine); traceable (returns reasoning alongside routes for debugging).
- Wired into `AudioCallback` at the start of each audio block; reads context, updates DirectLayer routes if changed.
- UI surfaces auto-derived routes with override toggles in the Preparation tab.
- Tests cover the inference function with hand-constructed contexts.

**Dependencies.** M4 (DirectLayer exists), M6 (NotificationBus for route-change events).

**Files touched.**

- `engine/include/ida/DirectRoutingInference.h`, `engine/src/DirectRoutingInference.cpp` (new)
- `engine/include/ida/AudioCallback.h`, `engine/src/AudioCallback.cpp` (modified: inference call at block start)
- `app/MainComponent.cpp` (modified: Preparation-tab override toggles)
- `tests/DirectRoutingInferenceTests.cpp` (new)

**Existing utilities to reuse.** `DirectLayer` (route registry), `NotificationBus`.

**Tests landing in this milestone.**

- `DirectRoutingInferenceTests`: hand-constructed contexts → expected route sets; trace fidelity; determinism

**Sessions 1-3 broken out.**

- Session 1: design the inference rules with the user (sub-design session — defer if it expands beyond a single-page rule table per `feedback-defer-big-design-to-own-session`); commit rules to a `docs/DIRECT_ROUTING_RULES.md` doc.
- Session 2: implement pure function + tests; commit.
- Session 3: wire into AudioCallback + UI overrides; commit and push.

**Verification.**

```bash
ctest --test-dir build -R DirectRoutingInference
bash bash/autotest.sh
```

**Risks & open decisions.** The rule set itself is a design question; the docs/DIRECT_ROUTING_RULES.md authorship is a small spec sub-session. Defer to its own brainstorm pass if rules expand beyond a single page.

**Execution mode.** `orchestrator+subagents`.

---

### M15 — LMC discipline tiers: GPS / PTP / NTP / Ableton Link

**Goal.** Implement the remaining four LMC discipline-source tiers (currently only `LocalMonotonic` is wired). GPS via PPS device input; PTP via the platform PTP daemon; NTP via the system clock; Ableton Link via the official Link library.

**Acceptance criteria.**

- `engine/include/ida/LmcSource.h` defines the source interface.
- Four concrete implementations: `LmcGpsSource`, `LmcPtpSource`, `LmcNtpSource`, `LmcAbletonLinkSource`.
- `Lmc::selectSource(DisciplineTier)` chooses the active source; fallback chain per V2's hierarchy GPS → PTP → NTP → Link → local.
- Source switch is glitch-free (rate slewing per existing slewing infrastructure).
- Tests for each source (mock GPS, mock PTP, NTP system test, Link via the library's loopback mode).

**Dependencies.** M1 (Lmc wired into audio thread).

**Files touched.**

- `engine/include/ida/LmcSource.h` (new)
- `engine/include/ida/LmcGpsSource.h`, `engine/src/LmcGpsSource.cpp` (new)
- `engine/include/ida/LmcPtpSource.h`, `engine/src/LmcPtpSource.cpp` (new)
- `engine/include/ida/LmcNtpSource.h`, `engine/src/LmcNtpSource.cpp` (new)
- `engine/include/ida/LmcAbletonLinkSource.h`, `engine/src/LmcAbletonLinkSource.cpp` (new)
- `engine/src/Lmc.cpp` (modified: source selection + fallback)
- `external/ableton-link/` (new submodule)
- `tests/LmcSourceTests.cpp` (new)

**Existing utilities to reuse.** Existing `Lmc` slewing infrastructure (extends rather than rewrites); `MonotonicClock`.

**Sessions 1-3 broken out.**

- Session 1: `LmcSource` interface + `LmcNtpSource` (easiest); tests; commit.
- Session 2: `LmcAbletonLinkSource` (Link library wraps cleanly); tests; commit.
- Session 3: `LmcPtpSource` against macOS PTP daemon; tests; commit and push.

Sessions 4+: `LmcGpsSource` (requires PPS device or simulator); fallback chain integration tests.

**Verification.**

```bash
ctest --test-dir build -R Lmc
bash bash/autotest.sh
# Manual: launch app with Ableton Live in Link mode; verify IDA locks to Link clock.
```

**Risks & open decisions.**

- GPS testing: defer hardware testing to a separate field-test session; in code, validate via PPS simulator.

**Execution mode.** `orchestrator+subagents`.

---

## Part F — Ensemble

### M16 — Ensemble consistency: vector clocks + causal holding queue + partition forking + anchor authority + split replication + permissions

**Goal.** V7 §9.3 in full. The current `net/SessionMerge` uses a single `editTimestamp` — that's the V2 baseline. M16 refactors to vector clocks + causal holding queue + partition forking with anchor-node authority + split replication (metadata wholesale / media on demand) + permissions model.

**Acceptance criteria.**

- `net/include/ida/VectorClock.h` + impl: `std::array<uint64_t, MAX_PEERS>` with happens-before / concurrent comparison operators.
- `net/include/ida/CausalHoldingQueue.h` + impl: incoming messages buffered until causal predecessors arrive; graduation when predecessors present; configurable timeout (default 5s → escalate to performer via NotificationBus).
- `net/include/ida/PartitionForkResolver.h` + impl: rejoin → vector-clock summary exchange → causal-order replay → unambiguous-merge vs semantic-conflict outcome → conflict-slot marked forked, both candidates surfaced.
- `net/include/ida/AnchorAuthority.h` + impl: anchor flag in `permissions.json` (M11 SAF integration); anchor designation changeable mid-session by anchor consent; without anchor, causal-time last-writer-wins.
- `net/include/ida/SplitReplication.h` + impl: metadata channel (full Constituent graph; always carried) and media channel (tape data; pulled on first playback); cache per session.
- `net/include/ida/PermissionsModel.h` + impl: owner-writable, ensemble-readable, explicit grant for write-sharing; revocations immediate + symmetric (revoked peer's local replica invalidated); observer-node and ensemble-writable-namespace as opt-in modes.
- Refactor `net/src/SessionMerge.cpp` to use vector clocks instead of `editTimestamp`.
- Network transport: at least one concrete transport (TCP socket via `juce::SocketDataInputStream` or similar) wires the vector-clock messages between peers.
- Fork-resolution UI surfaces in Preparation tab.
- Tests for vector-clock semantics, holding queue, partition + rejoin scenarios, anchor designation, split replication, permission enforcement.

**Dependencies.** M11 (SAF stores permissions.json), M6 (NotificationBus for partition / fork events).

**Files touched.**

- `net/include/ida/VectorClock.h`, `net/src/VectorClock.cpp` (new)
- `net/include/ida/CausalHoldingQueue.h`, `net/src/CausalHoldingQueue.cpp` (new)
- `net/include/ida/PartitionForkResolver.h`, `net/src/PartitionForkResolver.cpp` (new)
- `net/include/ida/AnchorAuthority.h`, `net/src/AnchorAuthority.cpp` (new)
- `net/include/ida/SplitReplication.h`, `net/src/SplitReplication.cpp` (new)
- `net/include/ida/PermissionsModel.h`, `net/src/PermissionsModel.cpp` (new)
- `net/include/ida/EnsembleTransport.h`, `net/src/EnsembleTransport.cpp` (new — TCP first; QUIC later if needed)
- `net/include/ida/SessionMerge.h`, `net/src/SessionMerge.cpp` (modified: vector clocks)
- `persistence/include/ida/SafPermissions.h`, `persistence/src/SafPermissions.cpp` (modified: extend with peer entries)
- `app/MainComponent.cpp` (modified: fork-resolution UI in Preparation tab)
- `tests/VectorClockTests.cpp`, `CausalHoldingQueueTests.cpp`, `PartitionForkTests.cpp`, `AnchorAuthorityTests.cpp`, `SplitReplicationTests.cpp`, `PermissionsModelTests.cpp` (new)
- `tests/SessionMergeTests.cpp` (modified)

**Existing utilities to reuse.** `LmcElection` (existing Marzullo + tier dominance + anchor override — composes cleanly with VectorClock); `SessionMerge` data shape (extended).

**Tests landing in this milestone.**

- All listed above; partition + rejoin scenario tests with deliberate network kill / restore

**Sessions 1-3 broken out.**

- Session 1: `VectorClock` primitive + tests; commit.
- Session 2: `CausalHoldingQueue` + tests; commit.
- Session 3: Refactor `SessionMerge` to use vector clocks; tests; commit and push.

Sessions 4-N: PartitionForkResolver (Session 4-6); AnchorAuthority + permissions model (Session 7-8); SplitReplication (Session 9-10); EnsembleTransport TCP (Session 11-12); fork-resolution UI (Session 13); end-to-end partition test (Session 14).

**Verification.**

```bash
ctest --test-dir build -R "VectorClock|CausalHoldingQueue|PartitionFork|AnchorAuthority|SplitReplication|PermissionsModel|SessionMerge"
bash bash/autotest.sh
# Manual: run two IDA instances on the same machine, induce a partition (firewall rules),
# edit on both sides, restore, verify fork-resolution UI surfaces the conflict.
```

**Risks & open decisions.**

- `MAX_PEERS`: cap at 16 for the initial implementation (matches transition guide §9.3 example); raise if practice demands.
- Transport choice: TCP first; QUIC later if latency or packet-loss handling become issues.

**Execution mode.** `orchestrator+subagents` (Backend Architect for CRDT design review; Code Reviewer for vector-clock implementation correctness).

---

### M17 — Ensemble security: libsodium + Noise Protocol + X25519

**Goal.** V6 §14.10 implementation per Section 7 item 17: libsodium for primitives, Noise Protocol Framework for the handshake, ephemeral X25519 for session keys. Air-gapped solo default, consent for shared Constituents, per-session identity.

**Acceptance criteria.**

- `net/include/ida/EnsembleCrypto.h` + impl wraps libsodium primitives.
- `net/include/ida/NoiseHandshake.h` + impl drives the Noise handshake.
- `net/include/ida/SessionIdentity.h` + impl issues per-session identity (ephemeral X25519 key pair).
- `EnsembleTransport` (M16) integrates encryption: every message E2E encrypted; key exchange via NoiseHandshake at peer connect.
- Air-gapped solo default: ensemble networking off by default; opt-in via Settings (M22).
- Consent for shared Constituents: per-Constituent permissions check before any data leaves the local machine.
- Tests for crypto primitives (against libsodium test vectors); Noise handshake; per-session identity rotation; air-gapped default.

**Dependencies.** M16 (transport exists to encrypt).

**Files touched.**

- `net/include/ida/EnsembleCrypto.h`, `net/src/EnsembleCrypto.cpp` (new)
- `net/include/ida/NoiseHandshake.h`, `net/src/NoiseHandshake.cpp` (new)
- `net/include/ida/SessionIdentity.h`, `net/src/SessionIdentity.cpp` (new)
- `net/src/EnsembleTransport.cpp` (modified: encrypt every message)
- `app/MainComponent.cpp` (modified: ensemble opt-in toggle under Settings)
- `external/libsodium/` (new — vendored)
- `external/noise-c/` or hand-rolled per the Noise spec (decide first session)
- `tests/EnsembleCryptoTests.cpp`, `NoiseHandshakeTests.cpp`, `SessionIdentityTests.cpp` (new)

**Existing utilities to reuse.** None new; libsodium is the substrate.

**Tests landing in this milestone.**

- `EnsembleCryptoTests` against libsodium test vectors
- `NoiseHandshakeTests` per the Noise spec test vectors
- `SessionIdentityTests` for key rotation

**Sessions 1-3 broken out.**

- Session 1: vendor libsodium; `EnsembleCrypto` wrapper + tests; commit.
- Session 2: `NoiseHandshake` driver + tests; commit.
- Session 3: `SessionIdentity` + EnsembleTransport encryption wiring; tests; commit and push.

**Verification.**

```bash
ctest --test-dir build -R "EnsembleCrypto|NoiseHandshake|SessionIdentity"
bash bash/autotest.sh
```

**Risks & open decisions.**

- libsodium build for iOS (M23): use the upstream iOS-supported build script.
- Noise Protocol implementation: pick `noise-c` if available; otherwise hand-roll per spec (the protocol is small enough that hand-rolling is reasonable for one expected pattern — `XX` is the default).

**Execution mode.** `orchestrator+subagents` (Security Engineer leads design review).

---

## Part G — Quality bars

### M18 — Inclusive-design surfaces

**Goal.** V4 §16.10 + V6 hearing-impaired symmetry: one-handed operation; switchable footswitch layouts (abstraction lets layouts swap without rebinding actions); color-blind modes (every UI color carries a non-color fallback — shape, pattern, position); screen-reader access to preparation state; large-print HUD; audible confirmation; hearing-impaired symmetry (visual + haptic equivalents for every audible cue).

**Acceptance criteria.**

- `ui/include/ida/FootswitchLayout.h` + impl: layouts swap without rebinding actions (binding is action → layout-slot, not action → physical-button-id).
- `ui/include/ida/ColorTokens.h` + impl: a token system where every UI color carries a non-color fallback. Audited via a test that every color use site references a token, not a literal.
- `ui/include/ida/PreparationStateTree.h` + impl: screen-reader-friendly DOM-shaped representation of preparation state.
- `ui/include/ida/LargePrintHud.h` + impl: large-print HUD overlay toggled via a Settings switch.
- `ui/include/ida/AudibleConfirmation.h` + impl: discrete confirmation sounds for state changes (mirrors visible cues for eyes-free use).
- `ui/include/ida/HapticConfirmation.h` + impl: haptic equivalent for hearing-impaired operators (USB game controller rumble channel; iOS uses Core Haptics on M23).
- All JUCE components in `app/` and `ui/` adopt `juce::AccessibilityHandler` per JUCE 8's accessibility model.
- One-handed operation: every multi-button gesture has a single-button alternative.
- Tests audit color-token coverage; screen-reader DOM completeness; footswitch-layout swap; haptic + audible parity.

**Dependencies.** M6 (NotificationBus surfaces feed the accessibility cues).

**Files touched.**

- `ui/include/ida/FootswitchLayout.h`, `ui/src/FootswitchLayout.cpp` (new)
- `ui/include/ida/ColorTokens.h`, `ui/src/ColorTokens.cpp` (new)
- `ui/include/ida/PreparationStateTree.h`, `ui/src/PreparationStateTree.cpp` (new)
- `ui/include/ida/LargePrintHud.h`, `ui/src/LargePrintHud.cpp` (new)
- `ui/include/ida/AudibleConfirmation.h`, `ui/src/AudibleConfirmation.cpp` (new)
- `ui/include/ida/HapticConfirmation.h`, `ui/src/HapticConfirmation.cpp` (new)
- Every existing component file under `app/` and `ui/` (modified: accessibility hooks, color tokens replacing literals)
- `tests/ColorTokenAuditTests.cpp`, `FootswitchLayoutTests.cpp`, `PreparationStateTreeTests.cpp`, `AudibleHapticParityTests.cpp` (new)

**Existing utilities to reuse.** `juce::AccessibilityHandler` (JUCE 8 builtin); existing view classes (extended).

**Sessions 1-3 broken out.**

- Session 1: `ColorTokens` system + color-literal audit + replacement in `TimelineView.cpp` (the worst offender with hex literals); commit.
- Session 2: `FootswitchLayout` abstraction + tests; commit.
- Session 3: `juce::AccessibilityHandler` adoption pass across `app/` and `ui/`; commit and push.

Sessions 4-N: PreparationStateTree (Session 4-5); LargePrintHud (Session 6); AudibleConfirmation + HapticConfirmation (Session 7-8); parity tests (Session 9).

**Verification.**

```bash
ctest --test-dir build -R "ColorTokenAudit|FootswitchLayout|PreparationStateTree|AudibleHapticParity"
bash bash/autotest.sh
# Manual: launch app with macOS VoiceOver active; navigate via screen reader.
# Manual: toggle Large-Print HUD; verify legibility.
```

**Risks & open decisions.**

- Haptic on macOS standalone: limited to game controller rumble; iOS Core Haptics richer (lands with M23).

**Execution mode.** `orchestrator+subagents` (Accessibility Auditor leads).

---

### M19 — Validation test harness: drift, micro-timing, polymetric, ensemble latency, blind listening, archival fidelity

**Goal.** V4 §15.5 six concrete success criteria as executable tests. The drift test is the easiest start (tone generator + loopback recorder + phase comparison) and exercises the full LMC → membrane → tape → membrane chain.

**Acceptance criteria.**

- `tests/validation/DriftTest.cpp`: tone generator + loopback + phase comparison; assert drift < threshold over a 10-minute run
- `tests/validation/MicroTimingPreservationTest.cpp`: capture a precise micro-timing pattern, render, compare; assert sample-accurate
- `tests/validation/PolymetricCoexistenceTest.cpp`: 5-against-7 polymeter through capture + render + playback; assert independent meter preserved
- `tests/validation/EnsembleLatencyCompensationTest.cpp`: two-peer scenario with deliberate network latency; assert LMC compensation keeps audio aligned
- `tests/validation/BlindListeningFidelityTest.cpp`: programmatic blind A/B between original and captured-then-rendered (uses null test — subtraction yields silence within noise floor)
- `tests/validation/ArchivalFidelityTest.cpp`: save → reopen → render → byte-compare against pre-save render; assert exact match for deterministic plug-in chains, within wet-capture tolerance for non-deterministic

**Dependencies.** M11 (SAF for archival test), M16 (ensemble for latency-compensation test), most other milestones (each test exercises an end-to-end path).

**Files touched.**

- `tests/validation/` directory (new)
- All six test files listed above (new)
- `tests/CMakeLists.txt` (modified: register `SiriusValidationTests` target separate from `IdaTests`)
- `bash/validation.sh` (new — runs validation suite separately, since these are slower than unit tests)

**Existing utilities to reuse.** All existing engine primitives; loopback driver from M1 calibration code.

**Sessions 1-3 broken out.** Per validation test. Ralph drives this milestone:

- Session 1 (operator-launched ralph PRD): list all six tests as TODO lines; ralph iterates "build next test, run, pass, commit" until `passes: true`.

**Verification.**

```bash
bash bash/validation.sh                    # full validation suite, slower than unit tests
ctest --test-dir build -L validation       # individual validation tests
```

**Risks & open decisions.**

- BlindListeningFidelityTest needs a "noise floor" tolerance. Pick -120 dBFS as the initial threshold; tune if false negatives surface.

**Execution mode.** `orchestrator+subagents, ralph inner loop after PRD`.

---

## Part H — Plug-in format expansion

### M20 — VST3 host (after CLAP is solid)

**Goal.** Extend `ida_plugin_host` to load `.vst3` bundles. VST3 SDK from Steinberg.

**Acceptance criteria.**

- `ida_plugin_host` loads `.vst3` bundles via the VST3 SDK.
- Determinism handling: VST3 has no `isDeterministic()` equivalent → default non-deterministic (per V5 §8.3 default disposition for unknown).
- Tests round-trip a known free VST3 plug-in.

**Dependencies.** M8 (CLAP solid, archival modes in place).

**Files touched.**

- `host_process/main.cpp` (modified: VST3 loader)
- `host_process/CMakeLists.txt` (modified: link VST3 SDK)
- `external/vst3sdk/` (new submodule)
- `tests/Vst3IntegrationTests.cpp` (new)

**Existing utilities to reuse.** Out-of-process framework from M7; ArchivalMode from M8.

**Sessions 1-3.** Session 1: vendor VST3 SDK + load test; Session 2: integration through `OutOfProcessPluginInstance`; Session 3: tests + push.

**Verification.**

```bash
ctest --test-dir build -R Vst3
bash bash/autotest.sh
```

**Execution mode.** `orchestrator+subagents`.

---

### M21 — AU host (prep for iOS in M23)

**Goal.** Extend `ida_plugin_host` to load Audio Unit components (AU v3 prefered; AU v2 fallback for legacy AU). Required for iOS where AU is the only allowed format.

**Acceptance criteria.**

- `ida_plugin_host` loads AU components via macOS `AudioUnit.framework`.
- AU v3 (App Extension model) supported alongside AU v2 in-process model. Note: AU v3 already uses macOS extension sandboxing — composes naturally with the out-of-process model.
- Tests round-trip a known free AU plug-in.

**Dependencies.** M20 (VST3 solid; AU is the third format).

**Files touched.**

- `host_process/main.cpp` (modified: AU loader; platform-conditional macOS)
- `host_process/CMakeLists.txt` (modified: link `AudioUnit.framework`, `AudioToolbox.framework`)
- `tests/AuIntegrationTests.cpp` (new)

**Sessions 1-3.** Session 1: AU v2 loader; Session 2: AU v3 extension loader; Session 3: tests + push.

**Execution mode.** `orchestrator+subagents`.

---

## Part I — Operator UX

### M22 — Hide-internals UI vocabulary cleanup

**Goal.** Honor `feedback-hide-internals-from-musician`: operator-facing surfaces show phrases and loops only; no tapes, no data-model vocabulary in default flow; advanced surfaces are opt-in. Scheduled late so the underlying engine model has stopped moving and the cleanup doesn't get redone twice.

**Acceptance criteria.**

- `MainComponent.cpp` operator-facing strings audit: 48 current "tape" hits resolved (renamed to "phrase" / "loop" / removed from operator surface entirely).
- Per-tape arm rows in `TimelineView.cpp` reframed as per-phrase arm rows; underlying data shape preserved (still `TapeId`-keyed for the data model, but the rendered string is the phrase name).
- Diagnostics line under Preparation tab strips "tape #N" wording.
- An "Advanced" toggle under Settings opt-in reveals the existing data-model vocabulary for power users and debugging.
- Capture banner copy reviewed for vocabulary compliance.
- `Arm` button copy preserved per `feedback-arm-disarm-is-required`.
- New `tests/UiVocabularyAuditTests.cpp` audits every operator-facing string against a "no internal vocabulary" allow-list.
- Operator-facing arm/disarm gesture wired to `InputMixer::finalizeChannel(ChannelId)` so committed tapes transition from `.tape.partial` → `<sha256>.tape` automatically at the natural end of a take. Until M22 lands, finalization is test-driven only (via direct calls from the M3 test suite). Added by `docs/superpowers/specs/2026-05-18-m3-design.md` Plan Amendment §1.

**Dependencies.** All previous milestones (engine model must be settled).

**Files touched.**

- `app/MainComponent.cpp` (modified: extensive)
- `ui/src/TimelineView.cpp`, `ui/src/PerformanceView.cpp`, `ui/src/PreparationView.cpp` (modified)
- `app/SettingsComponent.h`, `app/SettingsComponent.cpp` (new — "Advanced" toggle)
- `tests/UiVocabularyAuditTests.cpp` (new)

**Existing utilities to reuse.** Existing view classes; ColorTokens from M18 (already in place).

**Sessions 1-3.** Ralph drives:

- Session 1 (operator-launched ralph PRD): list every "tape" string in operator-facing surfaces; ralph iterates "judge next string, rename or move to Advanced, commit" until `passes: true`.

**Verification.**

```bash
ctest --test-dir build -R UiVocabularyAudit
bash bash/autotest.sh
# Manual: launch app, switch to Performance tab, verify no "tape" wording visible.
# Manual: toggle Advanced under Settings, verify tape vocabulary reappears for power use.
```

**Execution mode.** `orchestrator+subagents, ralph inner loop after PRD`.

---

## Part J — iOS

### M23 — iOS AUv3 port

**Goal.** Port IDA to iOS as an AUv3 plug-in per the platform memory (`feedback-mac-first-linux-windows-last`: "iOS hosts AUv3 only"). Reuse all engine + persistence + net code; iOS-specific work is the AUv3 wrapper, audio device handling, UI adaptation, and signing.

**Acceptance criteria.**

- iOS Release build (per `feedback-clean-builds` and the CLAUDE.md iOS-release-builds rule).
- AUv3 wrapper exposes the IDA engine as an Audio Unit; host controls view presentation per the AUv3 layout rule (no full-screen forcing).
- `APPLICATION_EXTENSION_API_ONLY=YES` enforced; no `[UIApplication sharedApplication]` paths reachable from shared code (guard with `#if !TARGET_OS_IOS` or extension-safe abstractions).
- Audio device handling via iOS AVAudioSession.
- Touch UI adaptation in `app/MainComponent`; existing accessibility hooks (M18) extend with iOS-specific VoiceOver wiring.
- Plug-in hosting: AU only (per platform memory); reuse M21's AU host code with iOS conditional compilation.
- Code signing: Apple Developer Team `RR5DY39W4Q` (per `project-apple-developer-team-id`); existing CMake `set_target_properties` block from OTTO copied per memory guidance.
- Notarization (notarytool keychain profile `sirius-notary` per `reference-apple-id`).
- CI: extend `.github/workflows/ci-macos-signed.yml` or fork to `ci-ios-signed.yml`.

**Dependencies.** M22 (UI vocabulary stable before iOS adapts it), M21 (AU host code in place).

**Files touched.**

- `app/Main.ios.cpp` (new — iOS entry point) or AUv3 extension target structure
- `app/IDA.ios.entitlements` (new)
- `app/Info.ios.plist` (new)
- `CMakeLists.txt` (modified: iOS target)
- `cmake/iOSToolchain.cmake` (new or modified)
- `.github/workflows/ci-ios-signed.yml` (new)
- Every shared header audit: extension-safe (no `UIApplication.sharedApplication`)

**Existing utilities to reuse.** All engine + core + persistence + net code.

**Sessions 1-3.** Session 1: CMake iOS toolchain + smallest possible AUv3 target that loads; Session 2: extension-safety audit + guards; Session 3: AVAudioSession wiring + first iOS Release build; commit and push. Sessions 4-N: full UI port, signing, notarization, CI.

**Verification.**

```bash
# iOS Release build (per CLAUDE.md iOS rules — never Debug)
xcodebuild -project build-ios/IDA.xcodeproj -configuration Release -sdk iphoneos
# Run on hardware device (simulator may pass while hardware fails per past lessons)
```

**Execution mode.** `orchestrator+subagents` (Mobile App Builder leads).

---

## Part K — Docs final pass

### M24 — White paper ↔ user guide ↔ marketing site ↔ inline doc-comments final alignment

**Goal.** Synchronize every documentation surface against V7 and against the actually-shipped behavior. Per `project-user-guide-alongside-whitepaper`: white paper = why; user guide = how; both stay in sync.

**Acceptance criteria.**

- `docs/IDA_Whitepaper_V8.md` (V7) reviewed for any behavior that drifted during implementation; sections updated to match reality (rare — V7 is the spec; implementation aligns to it, not vice versa).
- `docs/IDA User Guide.md` rewritten / extended to cover every feature shipped through M23.
- Marketing site (`website/`) content audited against V7; updates per `408f63c` precedent.
- Every public header in `core/include`, `engine/include`, `host/include`, `persistence/include`, `net/include`, `ui/include`, `app/` has up-to-date one-line doc comments on each class and one-paragraph block comments on each non-trivial method.
- `README.md` updated.
- `CLAUDE.md` (project-level) audited and updated.

**Dependencies.** All previous milestones complete (cannot finalize docs while features still moving).

**Files touched.**

- `docs/IDA_Whitepaper_V8.md` (audit; minimal edits if any)
- `docs/IDA User Guide.md` (extensive)
- `website/` (audit)
- Every public header (one-line + one-paragraph doc comments)
- `README.md`, project-root `CLAUDE.md`

**Sessions 1-3.** Ralph drives:

- Session 1 (operator-launched ralph PRD): list every doc surface above as a TODO line; ralph iterates "audit / update next doc, commit" until `passes: true`.

**Verification.**

- Operator review pass — docs are the last surface where automation can't fully judge correctness.

**Execution mode.** `orchestrator+subagents, ralph inner loop after PRD`.

---

## Cross-cutting concerns (sections, not milestones)

### RT-safety invariants (the permanent contract)

Established in M1; enforced thereafter. Every PR touching audio-thread code must self-certify against this list (the doc lives at `docs/RT_SAFETY_CONTRACT.md` after M1):

- No allocation (no `new`, `malloc`, `std::vector::push_back` without pre-reserve, `std::string` construction with non-trivial size, `make_unique`, `make_shared`, `std::function` construction).
- No lock acquisition (no `std::mutex::lock`, `std::lock_guard`, `std::scoped_lock`, `std::unique_lock`, OS-level mutex, `juce::CriticalSection::enter`, `ScopedLock`). Replace with lock-free SPSC/MPSC queues or atomic snapshot pointers.
- No synchronous I/O (no file reads/writes, network calls, plug-in license checks, GUI calls). File writes belong on a dedicated writer thread fed by a lock-free queue.
- No unbounded loops. Worst-case iteration bound documented and fits in the buffer's time budget.
- Graph reads via atomic-snapshot pointer (Constituent graph is read from a single atomically-swapped pointer; the audio thread sees a pre-computed flattened schedule).
- Plug-in watchdog per buffer (M7 enforces).

### TDD per milestone

Every milestone enumerates the tests landing **with** it. CLAUDE.md rule #9: tests verify intent. Per CLAUDE.md commit discipline, finished milestones commit; commit messages follow `feat: M<N> — <short title>`, `fix: M<N> — <short title>`, etc.

### Commit discipline

Per CLAUDE.md and `feedback-claude-commits-and-pushes-master`:

- Single-line commit messages (compatible with `bash/bu.sh` Dropbox zip naming).
- `<type>: M<N> — <short title>` format for milestone work.
- Stage explicit files; never `-A` when sensitive paths are nearby.
- Commit when a milestone session ends with state worth preserving; never mid-thought; never silent uncommitted state across sessions.
- Push to `origin/master` is authorized and expected after every commit.
- `bash/bu.sh` runs locally per its own contract; orthogonal to push.

### Platform completion order

**macOS standalone → iOS AUv3 (M23) → Windows → Linux.** Strict. Per `feedback-mac-first-linux-windows-last`. Windows + Linux ports are not in this plan's scope (separate plans after M24). Within macOS work, every milestone fully completes macOS-side before any non-macOS code lands.

### Documentation hygiene

Per `project-user-guide-alongside-whitepaper`: white paper and user guide are updated **alongside each milestone**, not after. The doc surface for each milestone is part of its acceptance criteria when applicable.

### Memory rule honoring

Every milestone cross-checked against the `feedback-*` and `project-*` memories at the time it's scheduled. Specific references:

- `feedback-clean-builds`: `rm -rf build && bash bash/autotest.sh` before any GUI-test step in any milestone.
- `feedback-arm-disarm-is-required`: preserve through all UI work, especially M22.
- `feedback-can-launch-app`: Claude launches `.app` for testing; interactive gestures still need operator.
- `feedback-short-responses`: chat replies stay tight; only the artifact (this plan) is comprehensive.
- `feedback-defer-big-design-to-own-session`: if any milestone surfaces a sub-design that needs its own brainstorm (e.g., M10 curves, M14 routing rules), defer it cleanly with a `todo.md` entry and continue current path.
- `feedback-esc-while-typing-is-not-abort`: if a tool call silently rejects, wait for next message before re-planning.
- `project-apple-developer-team-id`: `RR5DY39W4Q` used in all signing-related milestones (M11 + M23).
- `reference-apple-id`: `itunes@larryseyer.com` for notarization; `sirius-notary` keychain profile.
- `project-sirius-branding-and-otto`: shared L&F submodule alignment with OTTO maintained through M22 UI work.

---

## Coverage matrix — every V3-V7 transition-guide item → milestone → files

| Transition-guide reference | Milestone | Primary files |
|---|---|---|
| §1 — Unchanged from V2 (Constituent, LMC, polymetric, repetition, capability tiers) | (carry through; no rework) | `core/include/ida/*`, `engine/include/ida/Lmc.h`, `engine/include/ida/RenderPipeline.h` |
| §2.1 — Input Mixer | M2, M3 | `engine/include/ida/InputMixer.h` |
| §2.2 — Output Mixer | M2, M5 | `engine/include/ida/OutputMixer.h` |
| §2.3 — Direct Layer | M4 (manual), M14 (inference) | `engine/include/ida/DirectLayer.h`, `engine/include/ida/DirectRoutingInference.h` |
| §2.4 — SignalType / MIDI / Video / File | M2 (SignalType), M9 (MIDI), M12 (Video), M13 (File) | `core/include/ida/SignalType.h`, `engine/include/ida/UmpEvent.h`, `video/`, `persistence/include/ida/AudioFileReader.h` |
| §2.5 — Mix snapshots | M10 | `core/include/ida/MixSnapshot.h` |
| §3.1 — Scope revision (mixing in scope) | M5 | `engine/src/OutputMixer.cpp` |
| §3.2 — Membrane → Mixer rename | M2 | `engine/include/ida/LatencyTiming.h` |
| §3.3 — Tape topology channel-driven | M3 | `engine/include/ida/Channel.h`, `engine/include/ida/TapeWriter.h` |
| §3.4 — Effects on Constituent + bus | M5 | `engine/include/ida/Bus.h` + existing `core/include/ida/EffectChain.h` |
| §4 — Migration order Steps 1-10 | M2-M14 | (covers V3 migration) |
| §7 #1 — RT-safety of channel-strip processing | M1, M5 | `docs/RT_SAFETY_CONTRACT.md` |
| §7 #2 — Bus topology limits | M5 | `engine/src/Bus.cpp` (cap at 64) |
| §7 #3 — Mix snapshot transitions | M10 | `engine/src/OutputMixer.cpp` (Cut, LinearFade; Curve deferred) |
| §7 #4 — File output formats | M13 | `persistence/include/ida/AudioFileWriter.h`, `SmfWriter.h`, etc. |
| §7 #5 — Per-snapshot routing recall | M10 | (committed: direct-layer routing IS included) |
| §7 #6 — JUCE 8 mapping | (decided inline per milestone) | various |
| §7 #7 — MIDI 1.0 → UMP upcast location | M9 | `engine/include/ida/MidiInput.h` (at the wrapper) |
| §7 #8 — Inclusive design surfaces | M18 | `ui/include/ida/FootswitchLayout.h`, `ColorTokens.h`, etc. |
| §7 #9 — Validation test harness | M19 | `tests/validation/` |
| §7 #10 — MIDI 2.0 controller learn UX | M9 | `engine/include/ida/MidiLearnSession.h` |
| §7 #11 — Plug-in sandboxing (CLOSED in V7) | M7 | `host/include/ida/OutOfProcessPluginInstance.h` |
| §7 #12 — Tape flush per tier (CLOSED in V7) | M3, M11 | `engine/src/TapeWriter.cpp`, `app/CapabilityTier.cpp` |
| §7 #13 — Wet capture budget | M8 | `engine/include/ida/WetCaptureWriter.h` |
| §7 #14 — Plug-in determinism trust model | M8 | `host/src/OutOfProcessPluginInstance.cpp` (first-load differential render) |
| §7 #15 — Notification categories | M6 | `engine/include/ida/NotificationBus.h` |
| §7 #16 — Export ergonomics | M13 | `engine/include/ida/ExportEngine.h`, `app/MainComponent.cpp` |
| §7 #17 — Ensemble security implementation | M17 | `net/include/ida/EnsembleCrypto.h` |
| §7 #18 — Plug-in format priorities | M8 (CLAP), M20 (VST3), M21 (AU) | `host_process/main.cpp` |
| §8.1 — Realtime execution audit | M1 | `docs/RT_SAFETY_CONTRACT.md` |
| §8.2 — Failure semantics | M8 | `core/include/ida/Constituent.h` (state machine), `engine/src/AudioDeviceCalibration.cpp`, `engine/include/ida/TapeReachabilityScan.h` |
| §8.3 — Plug-in determinism | M8 | `host/include/ida/ArchivalMode.h` |
| §8.6 — Notification channel | M6 | `engine/include/ida/NotificationBus.h` |
| §9.1 — Out-of-process plug-in hosting | M7 | `host_process/`, `host/include/ida/OutOfProcessPluginInstance.h` |
| §9.2 — IDA Archive Format | M11 | `persistence/include/ida/Saf*.h` |
| §9.3 — Ensemble consistency | M16 | `net/include/ida/VectorClock.h`, `CausalHoldingQueue.h`, etc. |
| §9.4 — Video tier-aware rendering | M12 | `video/include/ida/VideoRenderer.h` |
| §9.5 — MIDI 2.0 integration details | M9 | `engine/include/ida/UmpEvent.h`, `MidiDeviceWrapper.h`, etc. |
| §9.6 — Flush intervals (spec values) | M11 | `app/CapabilityTier.cpp` |
| §9.6 — Reference-vs-containment | (already correct) | `core/include/ida/Constituent.h` carries `TapeReference` not tape ownership |
| §9.6 — §18.3 UX research questions | (parked; not blockers) | see "Parked decisions" below |

---

## Parked decisions (not blockers for shipping)

These are V7 §18.3 items that don't gate any milestone but need to surface to the operator at the right time:

- Control surface ergonomics (post-M23, once iOS hardware in hand)
- Phrase-relationship UX (post-M22, after vocabulary settles)
- Structured improvisation interfaces (post-M22)
- AI assistance role (cross-cutting; revisit at M24)
- Capability-tier auto-detection heuristics (currently demo-hardcoded in `app/MainComponent.cpp:43-58`; revisit post-M11 when CapabilityTier becomes the central tunable source)
- Real-world flush-interval validation (M11 ships V7-spec values; field testing post-M11)
- M10 transition Curve representation (deferred sub-design)
- M14 direct-routing rule table (deferred sub-design)
- M5 EQ + dynamics DSP implementations (deferred sub-design; M5 ships gain + pan only)

Each parked decision will get a `todo.md` entry as it's defrosted for an operator-led sub-session.

---

## Session start checklist (paste into the first new chat)

> Before touching any code, this chat must:
>
> 1. Read `/Users/larryseyer/IDA/continue.md` (the prior session's handoff).
> 2. Read `/Users/larryseyer/IDA/docs/IDA_Whitepaper_V8.md` (V7 white paper).
> 3. Read `/Users/larryseyer/IDA/docs/sirius-looper-v2-to-v7-transition.md` (V2→V7 bridge).
> 4. Read this plan end-to-end.
> 5. Copy this plan's Parts 0-3 + coverage matrix to `docs/superpowers/specs/2026-05-17-v7-alignment-design.md` and Parts 4+ to `docs/superpowers/plans/2026-05-17-v7-alignment-plan.md`. Single commit: `docs: V7 alignment design + plan (M0 setup)`. Push.
> 6. Open M1's block; brainstorm any open questions; start Session 1.
>
> Memory rules auto-load. User-level CLAUDE.md auto-loads. Do not skip steps 1-5; the dependency graph is real and the docs are the spine.

---

*End of V7 alignment roadmap. Total milestones: 24. Total parts: 11 (A-K). Estimated end-to-end calendar: 4-8 months at the operator's working pace, depending on parallelism the orchestrator finds within and across milestones.*
