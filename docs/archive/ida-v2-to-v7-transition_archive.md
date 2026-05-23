> **Historical archive.** This document was written when the product was named
> "Sirius Looper". The product was renamed to **IDA — Idea Development
> Arranger** on 2026-05-23 as part of the AutomagicArt brand structure (IDA
> is the looping environment counterpart to OTTO). References to "Sirius
> Looper" below reflect the contemporaneous name and are preserved as-is.
> See `IDA_Naming_Decision.md` for the full rationale.

---

# Sirius Looper: V2 → V7 Transition Guide

**Audience:** Claude Code and the developer working on the Sirius Looper codebase

**Purpose:** Migrate code already built against the V2 white paper to align with the current architecture (V7), without unnecessary rework. Distinguishes what's unchanged, what's added, and what's revised across V3 through V7.

**Companion to:** `Sirius_Looper.md` (the current architecture; previous filename was `Sirius_Looper_Whitepaper_V6.md`)

---

## TL;DR

The architectural baseline is **V3**: V3 added the input mixer, output mixer, direct layer, modality-agnostic signal handling, two-layer routing, and user-defined tape topology. V4–V7 did not change the foundational architecture; they added clarifications, implementation requirements, and the commitments that close every previously-deferred item.

V3 is **additive and clarifying** relative to V2, not foundational. Roughly 80% of V2's architecture is unchanged. Do not start over. Existing code for the LMC, conceptual time, tapes, Constituents, phrases, polymetric coexistence, repetition, arrangement, ensemble model, and capability tiers stays.

> **The layers of this guide.** This is a V2→V7 migration with five distinguishable layers:
>
> - **V2→V3 architectural migration** (Sections 1–6): the core of the guide. Rename membrane → mixer, add the direct layer, refactor tape topology to be channel-driven, introduce SignalType, add the channel/input routing layers. Unchanged from earlier versions.
> - **V4 clarifications** (inline callouts and items 7–10 in Section 7): MIDI 2.0 / UMP-shaped tape events, inclusive design surfaces, validation test harness, MIDI controller learn UX.
> - **V5 implementation requirements** (Section 8 and items 11–15 in Section 7): realtime execution audit, failure semantics implementation, plug-in determinism. These are *real implementation work*, not architectural changes.
> - **V6 polish** (small additions noted inline; items 16–18 in Section 7): export to standard formats, ensemble security defaults, hearing-impaired symmetry in inclusive design, plug-in format scope (CLAP/VST3/AU). No new substantive architectural commitments.
> - **V7 closures** (Section 9 and the closed items in Section 7): every previously-deferred architectural decision is now committed. Out-of-process plug-in hosting is mandated (not "decide per-platform"); the Sirius Archive Format is fully specified; ensemble consistency, partition behavior, conflict resolution, and permissions are committed; video behavior under retroactive edits is committed; tape flush intervals are concrete values per tier; MIDI 2.0 integration details are specified. **Section 9 is the largest new block of implementation work since V5.**
>
> If you're working from V3, you can ignore the V4–V7 callouts initially but you will need Section 9 before shipping. If you're working from V7, treat all five layers as part of the guide. The architectural shape is unchanged across versions.

What V3 adds (the architectural migration):
1. **Input mixer + output mixer** as first-class architectural objects (the signal path is now `input mixer → tape → output mixer`)
2. **Modality-agnostic mixers** — both mixers handle live audio, live MIDI, live video, and file I/O as first-class signal types
3. **Two-layer routing** — input-layer decisions (per source) and channel-layer decisions (per channel)
4. **User-defined tape topology** — number of tapes is determined by channel configuration
5. **The direct layer** — parallel signal path bypassing tape for sub-millisecond monitoring
6. **One scope revision** — mixing is in scope (revises V2 decision #26)

Everything else is unchanged.

---

## Section 1: What's unchanged from V2

If you've built any of the following against V2, **do not modify it**. The V3 paper preserves these architectures exactly:

### Core time and clock subsystems
- The Logical Master Clock (LMC) and discipline hierarchy (GPS → PTP → NTP → Link → local)
- Conceptual time as the engine's internal substrate
- The tempo map as a transformation between conceptual time domains
- LMC calibration tables and rate slewing under reference loss
- Marzullo interval-intersection for distributed LMC election
- Local LMC vs. ensemble LMC distinction

### Tape architecture
- Tapes are append-only, immutable, always-running
- All inputs are tapes, sharing a uniform `(time, source, payload)` event format
- Retroactive ring buffer for retroactive capture
- Tape format strategy (uncompressed / FLAC / tiered per capability tier)
- Tapes are local; they never traverse the network as primary data

### Constituent hierarchy
- Constituents as the unifying abstraction (tape slice → loop → phrase → section → song → set)
- Constituents are immutable; edits are copy-on-write
- "Loops all the way up" recursion
- Identity persists across content revision
- Phrases as the unit of musical thought
- Role and intent metadata on phrases
- Grammatical relationships between phrases

### Polymetric/polytemporal
- Hierarchy of time domains, each level may have its own meter and tempo
- Constituents in different time domains meet at parent boundaries
- Meter as a property of each Constituent
- Micro-timing preserved exactly

### Repetition
- Five-axis repetition rules (trigger, cardinality, phase, mutation, termination)
- Mutation as preservation of engagement
- Termination matching attention decay

### Ensemble
- Distributed LMC election
- Master/slave model with anchor-node override
- CRDT-compatible session state
- Graceful degradation to solo recording

### Capability tiers
- Four-tier startup selection (Lavish / Comfortable / Tight / Survival)
- Unbreakable rules (audio never glitches, tape integrity sacred)

### UI principles
- Inspiration is fragile
- Trust the user; anticipate, don't constrain
- Glanceable not readable; coarse and decisive
- Undo as the most accessible operation
- Eyes-free operation as the highest live-performance goal

**Action for existing code:** If you have working implementations of any of the above, they're correct as-is. No refactor needed.

---

## Section 2: What's new in V3 (subsystems to add)

### 2.1 The Input Mixer

A new top-level subsystem that sits between the audio/MIDI/video device layer and the tape layer. Replaces what was previously implicit "membrane plumbing" with an explicit, full creative mixer.

**Core responsibilities:**
- Accept signals from physical inputs (audio, MIDI, video) and file inputs (audio files, MIDI files, video files)
- Provide per-input configuration (input layer)
- Provide per-channel configuration (channel layer)
- Apply channel processing chains
- Route channels to tape (one tape, two tapes for non-destructive, or no tape)
- Route channels to the direct layer
- Route channels to internal buses

**Interface sketch:**

```
class InputMixer {
    // Input-layer registry
    void register_input(InputId, InputDescriptor)
    void set_input_raw_direct(InputId, bool enabled)
    void set_input_enabled(InputId, bool)
    void set_input_defaults(InputId, ChannelDefaults)
    
    // Channel registry
    ChannelId add_channel(InputId source, SignalType, ChannelConfig)
    void remove_channel(ChannelId)
    void set_channel_processing(ChannelId, ProcessingChain)
    void set_channel_tape_mode(ChannelId, TapeMode)
        // TapeMode: CommitToTape | NonDestructive | NoTape
    void set_channel_direct_routing(ChannelId, DirectRouting)
    void set_channel_destinations(ChannelId, [TapeId | BusId])
    
    // Bus registry
    BusId add_bus(BusConfig)
    void route_channel_to_bus(ChannelId, BusId, SendLevel)
    
    // Audio-thread interface (real-time safe)
    void process_buffer(const InputBuffers&, OutputDestinations&)
}
```

**Per-signal-type channel strip variants:**
- `AudioChannelStrip` — gain, EQ, dynamics, sends
- `MidiChannelStrip` — transpose, velocity curve, channel filter, event remap
- `VideoChannelStrip` — color, scaling, format conversion
- `FileInputChannelStrip` — transport, rate control, loop region

### 2.2 The Output Mixer

Symmetric to the input mixer. Sits between Constituent rendering and the audio/MIDI/video output layer and file rendering.

**Core responsibilities:**
- Receive rendered output from active Constituents
- Receive direct-layer signals bypassing the tape
- Provide per-channel processing and routing
- Route to physical outputs, file destinations, MIDI ports, video destinations
- Manage session-level effect buses
- Master bus processing
- Mix snapshot recall and interpolation

**Interface sketch:**

```
class OutputMixer {
    // Channel registry (typically auto-created from active Constituents)
    ChannelId add_channel(ConstituentRef | DirectLayerRef, SignalType)
    void set_channel_strip(ChannelId, ChannelStripConfig)
    
    // Bus and send/return
    BusId add_bus(BusConfig, EffectChain)
    void route_channel_to_bus(ChannelId, BusId, SendLevel)
    
    // Output routing
    void route_channel_to_output(ChannelId, OutputDestination)
        // OutputDestination: PhysicalOutput | FileOutput | MidiPort | VideoOut
    
    // Mix snapshots
    SnapshotId capture_snapshot(name)
    void recall_snapshot(SnapshotId, TransitionType, duration)
    
    // Audio-thread interface (real-time safe)
    void render_buffer(const ConstituentRenders&, const DirectInputs&, OutputBuffers&)
}
```

### 2.3 The Direct Layer

A parallel signal path from input mixer to output mixer that bypasses the tape entirely.

**Key properties:**
- Carries signal, NOT time-anchored data
- Not part of the conceptual time architecture
- Not part of the Constituent hierarchy
- Per-channel routing decisions (raw direct from input layer; processed direct from channel layer)
- Automatic routing inference based on context

**Interface sketch:**

```
class DirectLayer {
    // Routing (typically configured automatically)
    void add_raw_route(InputId, OutputChannelId)
    void add_processed_route(ChannelId, OutputChannelId)
    void remove_route(RouteId)
    
    // Automatic inference
    void update_context(ChannelArmStates, PlaybackOverlaps, UtilitySignals)
    
    // Audio-thread interface (real-time safe)
    void route_buffers(const RawInputBuffers&, const ProcessedChannelBuffers&,
                       OutputBuffers&)
}
```

The automatic inference function deserves its own design pass — it should be deterministic, fast, and trace-able for debugging.

### 2.4 Modality support

If V2 code is audio-only, V3 expects MIDI, video, and file I/O as first-class. This may require:

- A `SignalType` enum (or similar) used throughout the mixer/tape/Constituent code
- MIDI tape format (event-based, sparse)
- Video tape format (frame-based, intra-only codec like ProRes Proxy or motion JPEG)
- File reader infrastructure (audio file formats, MIDI files, video files)
- File writer infrastructure for export

If V2 was already audio-focused with awareness of the abstraction, the addition is incremental. If V2 hard-coded "audio" everywhere, this is a wider refactor.

> **V4 note — design MIDI for UMP from day one.** V4 makes explicit (Part VI; decisions 70–71) that the tape format and Constituent model are MIDI 2.0-native. **V7 makes this fully specified** in §6.12 of the architecture: UMP storage on tape, MIDI 1.0 upcast at the membrane, MIDI-CI capability negotiation at device attach, MPE channel allocation per the MPE spec, per-note expression stored on parallel parameter tapes (not interleaved with note events), downcast policy per destination, and SMF vs UMP-JSONL export rules. The implication for new code: do **not** define your MIDI tape event as a 3-byte (status, data1, data2) struct and plan to widen it later. Make the tape event payload UMP-shaped from the start — a discriminated union that can carry a 32-bit Channel Voice Message, a 64-bit Channel Voice Message (MIDI 2.0), a 128-bit System Exclusive 8 packet, or a 32-bit Utility Message. MIDI 1.0 input at the membrane is upcast to UMP and stored canonically. Per-note expression (per-note pitch bend, per-note pressure, MPE-style channel allocation, 32-bit controllers) is then naturally a first-class automatable parameter, not a special case. Going UMP-first costs almost nothing if done early and is painful to retrofit.

### 2.5 Mix snapshots

A new Constituent subtype that carries complete input + output mixer state and anchors it to conceptual time.

**Interface sketch:**

```
struct MixSnapshot : Constituent {
    InputMixerState input_state
    OutputMixerState output_state
    TransitionType transition  // Cut | LinearFade | Curve
    Duration transition_duration
}
```

Plays into existing Constituent architecture; activation logic hooks into the playback scheduler the same way other Constituents do.

---

## Section 3: What's revised in V3 (light refactor)

### 3.1 Scope (V2 decision #26 → revised by V3 decision #57)

V2 said: "The looper's scope ends at the membrane. Mixing, routing, effects topology are downstream."

V3 says: "The looper is a complete production environment from physical input to physical output. Mixing is in scope, on both sides of the tape."

**Impact on code:** If you stubbed-out mixing as "out of scope," replace those stubs with the input/output mixer subsystems described above. If you built nothing in this area expecting external tools, the new mixer subsystems fill that gap.

### 3.2 "Membrane" language

V2 called the boundaries between digital and physical reality "membranes." V3 still uses "membrane" as a conceptual term but now the *concrete software objects* at those boundaries are called **mixers** (input mixer, output mixer). The membrane is a conceptual layer; the mixer is the implementation.

**Impact on code:** Class names like `InputMembrane` or `OutboundMembrane` should be renamed to `InputMixer` / `OutputMixer`. The functional behavior is the same; the name and the scope of responsibility expand to include mixing.

### 3.3 Tape topology

V2 talked about "tapes per input." V3 clarifies that **tape topology is determined by channel configuration**, not input configuration. One input may produce zero, one, two, or many tapes depending on how channels are configured.

**Impact on code:** If your tape-creation logic was "one tape per registered input," refactor to "one tape per channel that has tape routing enabled, plus a parallel parameter tape if the channel is in non-destructive mode." The capture loop changes from input-driven to channel-driven.

### 3.4 Effects placement

V2 said effects live on Constituents. V3 says effects live in two places: on Constituents (local effects, part of the idea) and on session-level buses in the output mixer (bus effects, part of the production).

**Impact on code:** If your effect chain is exclusively per-Constituent, add bus-level effect chains in the output mixer. Constituents need a way to declare bus sends in addition to their local effect chain.

---

## Section 4: Recommended migration order

Build in this order to minimize rework:

### Step 1: Rename and re-scope existing membrane code
- If you have `InputMembrane` / `OutputMembrane` (or equivalent) classes, rename to `InputMixer` / `OutputMixer`
- Document the expanded responsibility but don't add new features yet
- Verify all existing tests still pass

### Step 2: Introduce SignalType modality
- Add `SignalType` enum (Audio, MIDI, Video, File) wherever signals are tracked
- Default existing code to `Audio` everywhere
- This prepares the codebase for MIDI/video/file without breaking anything

### Step 3: Refactor tape creation to be channel-driven
- Introduce a `Channel` concept inside the input mixer (if not already present)
- Move tape routing decisions from input-level to channel-level
- Each channel declares its tape mode (CommitToTape / NonDestructive / NoTape)
- Tape allocation follows channel configuration

### Step 4: Add input-layer routing decisions
- Add per-input `raw_direct_monitor` flag (default false)
- Add per-input `enabled` flag
- Add per-input `defaults` that channels inherit

### Step 5: Add channel-layer processing chains
- Add per-channel processing chain (typed by SignalType)
- Implement commit-to-tape vs non-destructive logic
- Implement processed-direct routing flag

### Step 6: Build the direct layer subsystem
- Standalone subsystem with raw and processed routes
- Hook into the audio thread between input mixer and output mixer
- Initial implementation can be all-manual routing; automatic inference comes later

### Step 7: Output mixer expansion
- Add per-channel channel strips in the output mixer
- Add bus/send/return architecture
- Add direct-layer channel handling

### Step 8: Mix snapshots
- New Constituent subtype
- Capture and recall logic
- Transition interpolation

### Step 9: MIDI, then video, then file I/O
- Add MIDI as a SignalType, implement MIDI channel strips, MIDI tape format
- Then video
- Then file I/O for import and export
- Each is roughly the same shape of work: signal-type-specific channel strip, tape format, device interface

### Step 10: Automatic direct routing inference
- Add the context-tracking logic that watches arm states, playback overlaps, etc.
- Hook into the direct layer to auto-configure routes
- Add user-override surfaces

---

## Section 5: Code-level inventory questions

To plan the actual work, the developer should answer the following about the current V2 codebase. Claude Code can help inventory these:

1. **Is there a class explicitly named `InputMembrane`, `OutputMembrane`, or equivalent?** If yes → rename to mixer; if no → identify the code that handles inbound/outbound buffer flow and create explicit mixer classes around it.

2. **Where is tape creation triggered?** Look for tape allocation code; assess whether it's input-driven or already channel-driven.

3. **Is there a Channel concept in the current code?** If yes → use it for V3's channel layer; if no → introduce it.

4. **Are effect chains attached to Constituents only, or also to a mixer layer?** If only Constituents → add bus-level effect chain in the output mixer.

5. **Is MIDI handling already in place?** If yes → check whether it shares architecture with audio (good) or is segregated (refactor needed).

6. **Is there a SignalType abstraction already?** If yes → use it; if no → add it as the first V3 step.

7. **How is monitoring currently handled?** Look for any existing "monitor" or "passthrough" code; this is the seed of the direct layer.

8. **What does the audio thread's processing loop look like?** The mixer + tape + direct layer all need to slot into this loop in a real-time-safe way. The current shape determines whether the new subsystems can be added incrementally or require restructuring.

---

## Section 6: What Claude Code should do first

When Claude Code receives this transition guide along with the current architecture (`Sirius_Looper.md`), the recommended first actions are:

1. **Read the architecture in full**, with particular attention to: Parts V, VI, VII for the mixer + direct layer material; §5.6 for realtime execution guarantees (including the out-of-process plug-in commitment); §6.12 for MIDI 2.0 integration; §11.8 for video behavior under retroactive edits; §14.6 and §14.9 for the ensemble consistency and conflict model; §15.5 and §15.6 for validation and plug-in determinism; §15.7 for the Sirius Archive Format; §16.10 for inclusive design; Part XVII for the failure model (including §17.8 with concrete flush intervals); Appendix C for the worked example; and the five Mermaid diagrams scattered through Parts III, VI, IX, XVII, and Appendix C for architectural orientation.
2. **Run a code inventory** against the questions in Section 5 above
3. **Propose a concrete migration plan** specific to the current codebase — what files to touch, what to add, what to rename
4. **Surface any architectural conflicts** between V2 code and the V3+ architecture that aren't covered in this transition guide
5. **Begin migration in the order described in Section 4**, with the developer reviewing each step before moving to the next

Do not start over. Do not refactor anything not on the migration path. The goal is to preserve all working V2 code and add V3+ capability incrementally. V4, V5, and V6 add no new *architecture*; V5 adds real implementation work (Section 8) that should be planned alongside the V2→V3 migration; **V7 adds substantial implementation work (Section 9)** that closes every previously-deferred architectural decision and must be planned for before shipping.

---

## Section 7: Open questions for the developer

These are decisions the V3 paper leaves open and that will need to be made during implementation:

1. **Real-time safety of the channel-strip processing.** Each channel strip will run on the audio thread. The processing chain needs to be lock-free, with parameter changes applied via atomic swap or message-passing. How is this currently handled in V2 code?

2. **Bus topology limits.** How many buses does the output mixer support? Static or dynamic? Memory budget implications.

3. **Mix snapshot transition implementation.** Linear interpolation is simple; curve-based transitions (S-curves, custom shapes) need a curve representation. Worth implementing curves now or starting with cut + linear-fade only?

4. **File output formats.** Which audio formats (WAV, FLAC, AIFF, MP3)? Video formats (ProRes, H.264, NDI)? MIDI files (Type 0, Type 1)?

5. **Per-snapshot routing recall.** Should mix snapshots include direct-layer routing state, or only mixer parameters? V3 implies the former but doesn't commit.

6. **JUCE 8 mapping.** Concretely: which JUCE classes map to which mixer concepts? `AudioProcessorGraph` for routing? Custom node types for channel strips? This is implementation strategy worth deciding early.

— *V4 additions:*

7. **MIDI 1.0 → UMP upcast at the membrane.** When a MIDI 1.0 device sends a 3-byte status/data/data message, where exactly is it converted to a UMP packet? Inside the MIDI device driver wrapper, or inside the input mixer's MIDI channel strip? The earlier the upcast, the cleaner the rest of the pipeline — but the device wrapper is the most platform-dependent code, so concentrating the upcast there has portability implications.

8. **Inclusive design surfaces in the UI layer.** V4 §16.10 lists six accessibility capabilities (one-handed operation, switchable footswitch layouts, color-blind modes, screen-reader access to preparation state, large-print HUD, audible confirmation). These need UI-layer hooks that the rest of the system can drive. Concretely: a footswitch-layout abstraction that lets layouts be swapped without rebinding actions; a color-token system where every UI color carries a non-color fallback (shape, pattern, position); a preparation-state DOM/tree that's screen-reader-friendly. Worth designing these surfaces before building any UI, even if only one configuration is initially implemented.

9. **Validation test harness.** V4 §15.5 specifies six concrete success criteria — drift, micro-timing preservation, polymetric coexistence, ensemble latency compensation, blind listening fidelity, archival fidelity. The test harness for these should be built alongside the features they validate, not after. The drift test is the easiest to start with (tone generator + loopback recorder + phase comparison) and exercises the full LMC → membrane → tape → membrane chain.

10. **MIDI 2.0 controller learn UX.** When a user moves a controller, the system needs to bind it to a parameter. With per-note expression, the binding may need to distinguish per-channel CCs from per-note expression streams. This is a UX problem that doesn't have an obvious right answer; worth prototyping early.

— *V5 additions:*

11. **Plug-in sandboxing strategy.** ~~Per V5 §17.4, plug-in faults must not block the audio thread. The architectural commitment is clear; the implementation strategy is open and platform-dependent.~~ **CLOSED in V7.** §5.6, §15.6, and §17.4 commit to out-of-process hosting via lock-free shared-memory SPSC ring buffers timestamped against the LMC, with a non-realtime supervisor that detects failure and restarts the plug-in host. No in-process-with-watchdog fallback; no per-platform strategy split. See Section 9.1 below for the implementation specifics.

12. **Tape flush interval per capability tier.** ~~Per V5 §17.8, capability tiers govern how often tape data is fsynced to disk. The exact intervals are reasonable defaults but should be validated against representative workloads.~~ **CLOSED in V7.** The intervals are committed in §17.8: Lavish per-buffer (~1–3 ms), Comfortable 50 ms, Tight 200 ms, Survival 1000 ms. Performer override is bounded 1 ms–5000 ms. Validation against representative workloads remains useful but the defaults are no longer "reasonable guesses" — they are the spec.

13. **Wet capture storage budget.** Per V5 §15.6 strategy 2, sessions can store wet-rendered output of non-deterministic plug-ins for absolute archival fidelity. Storage cost scales with session length × channel count × bit depth. A budget model — "wet capture is enabled for the master bus output only by default; per-channel wet capture is opt-in" — is sensible but not yet committed.

14. **Plug-in determinism declaration trust model.** Per V5 §15.6, plug-ins declare `isDeterministic()`. The host trusts the declaration. But what if a plug-in lies? Worth deciding whether to validate via differential rendering (render the same input twice and compare) on first load. This is a one-time cost per plug-in version; worth it for archival sessions.

15. **Notification channel categories and routing.** Per Section 8.6 above, the engine→UI notification bus needs categories and a routing policy. Which categories deserve audible alerts during performance? Which are visible-only? Defaults should be conservative (only critical-path failures audible during performance; preparation HUD shows everything).

— *V6 additions:*

16. **Export ergonomics.** Per V6 §6.11, export targets are WAV/FLAC/AIFF for audio, Type 0/1 SMF for MIDI, intra-frame codecs for video. The architectural decision is settled; the UX is open. How does the performer select stems vs. full-mix? How do they specify which buses get rendered? Where does the rendered file go (system default, project folder, custom)? Worth deciding before building any export UI.

17. **Ensemble security defaults — implementation choices.** V6 §14.10 commits to end-to-end encryption, air-gapped solo as default, consent for shared Constituents, per-session identity. The crypto library and key-exchange protocol are implementation choices not specified. A reasonable starting point: libsodium for primitives, Noise Protocol Framework for the handshake, ephemeral X25519 for session keys. Document the choice so it can be audited.

18. **Plug-in format priorities per platform.** V6 §15.6 names CLAP, VST3, and AU as the target formats. The implementation effort per format varies (CLAP is simplest; VST3 is most prevalent; AU is iOS-required). On a fresh implementation, build CLAP first (cleanest API, exercises the determinism contract and watchdog without legacy compromises), then VST3 (broadest plug-in availability), then AU (Mac/iOS reach). AAX and LV2 are open per the paper.

---

## Section 8: V5 implementation guidance

V5 adds no new architecture, but it adds three substantive areas of *implementation work* that V3 and V4 did not address explicitly. Plan for these alongside the V2→V3 migration, not after it. The order below is rough priority for a fresh implementation; an existing V3-aligned codebase may take them in any order.

### 8.1 Realtime execution audit (V5 §5.6) — start here

The most important V5 addition for existing code. V5 §5.6 promotes the audio-thread contract from "implementation detail" to "architectural commitment." Audit the existing audio thread against these rules:

- **No allocation on the audio thread.** Search the audio-path code for `new`, `malloc`, `std::vector::push_back` (when not pre-reserved), `std::string` construction with non-trivial size, `make_unique`, `make_shared`, `std::function` construction. Each is a refactor candidate.
- **No lock acquisition on the audio thread.** Search for `std::mutex::lock`, `std::lock_guard`, `std::scoped_lock`, `std::unique_lock`, OS-level mutex calls, `juce::CriticalSection::enter` (or `ScopedLock`). Replace with lock-free queues (single-producer single-consumer for tape append; multi-producer single-consumer for parameter changes) or atomic snapshot pointers.
- **No synchronous I/O on the audio thread.** Search for file reads/writes, network calls, plug-in license checks, GUI calls. File writes belong on a dedicated writer thread fed by a lock-free queue.
- **No unbounded loops.** Every loop on the audio thread must have a worst-case iteration bound that fits in the buffer's time budget.
- **Graph reads via atomic snapshot.** The Constituent graph must be read from a single atomically-swapped pointer, not traversed live. The audio thread sees a flattened pre-computed schedule of active tape reads for the current buffer.
- **Plug-in watchdog.** Every plug-in invocation needs a per-buffer time budget. Exceed it → bypass and notify (see §8.2).

This audit can be done before any other V5 work, and the refactors it suggests are likely already overdue in any non-trivial audio codebase.

### 8.2 Failure semantics implementation (V5 Part XVII)

V5 Part XVII defines what happens when reality intrudes. The implementation surface is non-trivial:

**Tape format with self-delimiting checksummed records.** Each tape event on disk needs a length prefix and a checksum. On session open, the tape is scanned from the last known checkpoint; trailing partial/corrupt records are truncated. Existing tape code that writes raw event sequences needs a thin wrapper.

**Atomic file-replace for session manifest.** Every session save writes the manifest to `manifest.tmp`, fsyncs, then renames to `manifest`. Either the old or the new manifest is present at all times; never a partial write. This is platform-specific (POSIX `rename` is atomic on same filesystem; Windows needs `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`).

**Constituent "broken" / "invalid" state.** Constituents need a state machine beyond exists/deleted. Add `valid` / `broken` / `invalid` states. Broken means "references a tape segment that's gone"; invalid means "anchor or bounds contradict the parent." Rendering treats both as silence; identity is preserved so the performer can repair.

**Plug-in lifecycle handling.** Three failure modes:
- *Crash* (host process dies) → engine observes broken IPC channel, substitutes bypass node, captures parameter automation against bypass, supervisor restarts the plug-in host in the background, notifies the performer
- *Hang* (exceeds watchdog timeout) → engine substitutes bypass for the timed-out buffer; if persistent, supervisor terminates and restarts the host; the timed-out buffer becomes silence in that channel, not glitch
- *Version mismatch on load* → present performer with options (accept divergence, use wet-capture if available, bypass, substitute, refuse)

V7 commits to out-of-process hosting (§5.6, §15.6) — see Section 9.1 below for the full IPC mechanism and supervisor architecture. Existing plug-in hosting code that runs plug-ins in the engine's address space is the largest single piece of V7 implementation work; plan for it explicitly.

**LMC calibration table corruption recovery.** Detect on load; on detection, perform fresh loopback calibration. Session may be unavailable during recalibration (a few seconds per device); tape data is unaffected.

**Tape rotation on disk-full.** When disk fills, the oldest tape segments referenced by no active Constituent are reclaimed first. This requires a reachability scan from the Constituent graph to tape segments; the scan runs on a non-realtime thread.

**The truthfulness surface.** Every degradation surfaces — visibly during preparation, audibly via discreet cues during performance. The UI layer needs an architectural notification channel that the engine writes to and the UI reads. This connects to the inclusive-design accessibility surfaces in §8.6.

### 8.3 Plug-in determinism (V5 §15.6)

V5 splits archival fidelity into *symbolic* (unconditional) and *re-render* (DSP-dependent). The implementation surface:

**Plug-in API flag.** Add `bool isDeterministic() const` to the plug-in interface. Plug-in authors declare; the host trusts (with the version-pinning safety net below). For VST3/AU plug-ins that don't expose this, treat as non-deterministic by default and let the user override.

**Three handling strategies, user-selectable per plug-in instance:**

1. *Determinism contract.* Non-deterministic plug-ins are bypassed or refused at load. Effect chain composition is constrained to deterministic-only.
2. *Wet capture.* The session stores the wet-rendered audio of every non-deterministic plug-in as a parallel parameter tape. On reopen, the wet capture replaces re-rendering. Storage cost is significant (a 10-minute reverb tail at 24-bit 48 kHz is ~85 MB per channel); fidelity is absolute.
3. *Version pinning + state hashing.* The session stores plug-in version, settings hash, oversampling rate, and any declared internal state as hashes. On reopen, hash mismatch warns the performer.

**Default disposition.** Always store the dry signal and parameter tape — symbolic fidelity is never optional. For non-deterministic plug-ins, the user chooses among the three strategies above; the default in a new session is *version pinning + state hashing* (low storage cost, warns on mismatch). The performer can switch any instance to *wet capture* for archival sessions.

**UI surface.** A per-plug-in-instance "archival mode" selector. Three radio buttons (Determinism / Wet capture / Version pin). Should be visible but not intrusive — performers rarely think about this, but archivists do.

### 8.4 What V5 does NOT add to the migration

To be clear about scope:

- §1.7 "Why this is still a looper" is positional; nothing to implement
- Appendix C is a worked example for human understanding; nothing to implement
- The mathematical tightening in §3.3 is wording-only

(V5's §14.9 originally deferred ensemble specifics; **V7 closes all of those** and turns them into substantive implementation work — see Section 9.)

### 8.5 Recommended order alongside the V2→V3 migration

If you're starting fresh (V3-aligned codebase being built from this guide), interleave V5 work like this:

- **Before Step 1** (rename + re-scope): Do the §8.1 realtime audit. If V2 code violates the audio-thread contract, that's the first refactor.
- **At Step 3** (tape rotation refactor): While you're in the tape code, implement the checksummed self-delimiting record format from §8.2.
- **At Step 5** (channel processing chains): Add the plug-in determinism flag and watchdog wrappers from §8.2 and §8.3.
- **At Step 7** (output mixer expansion): Build the truthfulness notification channel that the engine writes to and the UI reads. This serves both failure announcements (§8.2) and the inclusive-design accessibility hooks (§8.6).
- **After Step 8** (mix snapshots): Add the session manifest atomic-replace pattern and the recovery scanner from §8.2.
- **At Step 10** (automatic direct routing): Add the LMC calibration corruption-detection and recovery from §8.2.

### 8.6 Notification channel — a small architectural addition

Both V4's inclusive-design surfaces (Section 7, item 8 above) and V5's truthfulness principle (§17.9) want the same thing: a structured channel by which the engine reports state-changes the UI may want to surface. Build this once, use it for both.

Shape:

```
class NotificationBus {
    // Engine side (any thread, including audio thread — lock-free queue)
    void post(NotificationLevel, Category, Message);

    // UI side (UI thread)
    std::vector<Notification> drain();  // non-blocking
}

enum NotificationLevel { Info, Degradation, Warning, Error };
enum Category {
    DiskPressure, CpuPressure, RamPressure,
    DeviceEvent, PluginEvent, ClockEvent,
    NetworkEvent, StateRepair, TapeRotation
};
```

Audio thread posts via a lock-free SPSC queue per category (the post is wait-free; the categories prevent priority inversion across notification types). UI drains and presents in the preparation HUD (visible) and the eyes-free vocabulary (audible cue or haptic) per the inclusive-design rules.

This is small, but it's the architectural anchor for everything the system needs to *tell the truth* about. Worth building early.

---

## Section 9: V7 implementation guidance

V7 closed every architectural item that was previously deferred. None of those closures revises V2 or V3; they specify decisions that were implicit or sketched in earlier versions. **For a codebase aligned with V3+V4+V5+V6, Section 9 is where the remaining work lives** — and it is non-trivial work. Plan accordingly.

The order below is rough priority for an implementation already past the V2→V3 migration and the V5 audit. An existing V6-aligned codebase can take these in any order, but **9.1 (out-of-process plug-in hosting) is the most disruptive change** and is best scheduled when the plug-in path is otherwise quiet.

### 9.1 Out-of-process plug-in hosting (§5.6, §15.6, §17.4) — the largest piece

V5 said "plug-ins are isolated" without specifying *how*. V7 commits to out-of-process: every plug-in instance runs in its own operating-system host process, communicating with the engine over lock-free shared-memory ring buffers. **No in-process fallback.** This is the architectural commitment that makes "audio never glitches on plug-in failure" load-bearing rather than aspirational.

**The pieces to build:**

- **The plug-in host process binary.** A small executable that takes a plug-in UID and a shared-memory segment name as arguments, loads the plug-in into its own address space, and pumps buffers through it. It does only one job; it is not the engine. Existing in-process hosting code is the rough template, but it must be lifted into its own executable.
- **Shared-memory IPC.** Single-producer single-consumer (SPSC) lock-free ring in a POSIX shared-memory segment (or Windows `CreateFileMapping` equivalent), pre-allocated at session start for the session's audio buffer size plus header. Two rings per plug-in instance: engine→host (input audio/MIDI + parameter messages) and host→engine (output audio/MIDI). Both carry LMC timestamps in the header.
- **Watchdog.** Engine-side timer that bounds each plug-in's per-buffer execution. On miss, engine substitutes a bypass node for that buffer (dry signal flows); the supervisor decides whether to restart the host.
- **Supervisor process.** Non-realtime thread (or separate process) that observes the audio-thread's miss reports. Transient misses → log and continue. Persistent misses → terminate and restart the plug-in host. Repeated failures of the same plug-in → mark the channel permanently bypassed and notify the performer.
- **GUI host process.** Plug-in GUIs also run out-of-process, embedded into the main UI's window via platform-specific window embedding (HWND parent on Windows, NSView on macOS, X11 reparenting on Linux). GUI hangs and crashes are isolated from the audio path AND from the main UI.
- **Parameter marshalling.** Parameter changes from the engine to the plug-in are written into the input ring as messages alongside audio buffers, in LMC-timestamp order. The plug-in host applies them at the correct sample boundaries within the buffer.

**Latency cost.** One shared-memory copy per buffer, each direction. On modern systems this is sub-microsecond per buffer — invisible compared to the audio buffer itself. The performer should not notice the architecture change in normal operation.

**Existing in-process plug-in code is not lost.** The processing logic that runs the plug-in's `process()` callback moves into the host process binary largely intact. What changes is the *transport*: instead of calling `plugin->process(buffers)` directly, the engine writes buffers into a shared-memory ring and the host process reads them out. The plug-in itself is unaware of the change.

**JUCE 8 mapping.** JUCE's `AudioPluginInstance` and `AudioProcessor` survive intact. What changes is the hosting: instead of `juce::AudioProcessorGraph` running plug-ins in the engine process, the graph becomes a coordinator that routes audio/MIDI through shared-memory rings to child processes. JUCE doesn't ship a ready-made out-of-process host (as of JUCE 8), so this is custom work. The Tracktion Engine's process-isolation work and the Bitwig out-of-process model are good references.

**Migration order.** Build the host process binary first as a standalone tool that can be tested with a single plug-in. Then build the shared-memory transport. Then integrate into the engine's audio path, behind a feature flag, so the existing in-process path can be kept as a debug fallback during the transition. Once stable, remove the in-process path entirely (the architecture commits to out-of-process; an in-process fallback would dilute the guarantee).

### 9.2 Sirius Archive Format implementation (§15.7)

V7 specifies the session file format completely. The implementation surface:

**Container.** A directory tree wrapped in an uncompressed ZIP container with `.saf` extension. Use a standard ZIP library (miniz, libzip, JUCE's `ZipFile`); compression level 0 (uncompressed) for fast read/write since tape data is already compressed (FLAC, ProRes, etc.). Read access is by directory path within the container; write access goes to a temp directory and rebuilds the container on save.

**Manifest.** `manifest.json` at the container root. Contains format-version (SemVer), engine version at save time, session ID (UUID), creation and last-modification timestamps in both LMC and UTC, the capability tier, the tape and constituent indices, and a top-level checksum (SHA-256) over the rest of the archive.

**SemVer reader policy.** The reader uses three checks: same MAJOR (required, else refuse); MINOR ≤ reader's MINOR (else open with warning and ignore unknown fields); PATCH ignored (forward-compatible within MINOR). Implement the version check as the *first* thing the reader does after opening the container.

**Tape storage.** Per §15.7: FLAC for audio above Survival; WAV for Lavish if user prefers; JSONL for MIDI (one UMP event per line with LMC and conceptual timestamps); ProRes 422 LT / MJPEG / HEVC-I for video by tier; JSONL for parameter, control, and system event tapes. Each tape has a header object identifying its modality, source, format, and first/last LMC timestamps.

**Constituent graph storage.** `constituents.json` as a flat array keyed by ID. Each entry has its boundaries, anchor, tempo map, effect chain references, repetition rules, metadata, and child IDs. Tape references in loops point to tape IDs (paths in the `tapes/` subdirectory).

**Plug-in state migration.** `plugins.json` records UID, version, state hash, state blob, determinism flag, and wet-capture pointer per instance. On reopen, the four-outcome migration policy from §15.7 (exact match / newer version / older version / missing) is presented to the performer. Build the migration UI alongside the format reader.

**Atomic save.** Write the new container to `session.saf.tmp`, fsync, then atomic rename to `session.saf`. The atomic-rename pattern is the same as the §17.8 manifest update — same primitives (POSIX `rename`; Windows `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`).

**Forward compatibility test.** Build the format reader with a deliberately-broken "future MINOR version" file as a test case; the reader must open it, ignore unknown fields, and warn the performer. This test prevents future MINOR changes from breaking the architecture's promise.

**Estimated effort.** A working SAF reader+writer is one to two weeks of focused work for a familiar developer. The tape-format-specific encoders/decoders (FLAC, ProRes, JSONL) are mostly off-the-shelf libraries. The plug-in state migration UI is the longest single piece.

### 9.3 Ensemble consistency implementation (§14.6, §14.9)

V5 deferred everything ensemble-specific past the CRDT-compatibility commitment. V7 closes it all. The substantive new work:

**Vector clocks.** Per-peer monotonic counter, attached to every coordination message. A 16-peer ensemble carries 16 64-bit counters per message (128 bytes), comfortably under any reasonable network budget. Implement vector clocks as a `std::array<uint64_t, MAX_PEERS>` or equivalent, with comparison operators for the "happens-before" and "concurrent" relations.

**Causal consistency.** Receivers buffer incoming messages whose causal predecessors have not yet arrived, applying them only when their predecessors are in place. Implement as a holding queue keyed by vector-clock dependency; messages graduate to "applied" when all predecessors have arrived. Tunable timeout for missing predecessors (default: 5 seconds → escalate to performer).

**Partition forking with optimistic auto-merge.** When the network partitions, each side continues with its local vector clock. On rejoin, peers exchange vector-clock summaries; each peer applies the other's events in causal order. Two cases:
- *Unambiguous merge*: disjoint Constituent edits → apply all silently
- *Semantic conflict*: same arrangement slot was assigned different Constituents on each side → both Constituents survive (immutability guarantees this), but the slot is marked **forked**, with both candidates surfaced to the performer

Build the fork-resolution UI alongside the partition-handling code. Forks are uncommon in practice but they MUST surface honestly, not be auto-resolved silently.

**Anchor node authority.** Per §14.4, a designated anchor node wins disputes. Implement as a session-level flag stored in `permissions.json` (SAF container). Anchor designation is changeable mid-session by anchor consent. Without an anchor, ambiguous arrangement-slot conflicts fall to causal-time last-writer-wins (the peer whose vector clock indicates the more recent edit in its own causal history wins).

**Split replication.** Per §14.9: Constituent graph metadata replicates wholesale (kilobytes; every peer has it always); tape media replicates on demand (gigabytes; fetched per playback). Implement as two separate channels: a "metadata channel" that always carries the full Constituent graph, and a "media channel" that pulls tape data on first playback. Cache fetched tape data locally for the session.

**Permissions model.** Per §14.9: owner-writable, ensemble-readable, explicit grant for write-sharing. Store permissions in `permissions.json`. Implement permission checks at every edit operation; revocations are immediate and symmetric (revoked peer's local replica is invalidated). Observer-node and ensemble-writable-namespace cases are special-purpose modes; build them as opt-in flags.

**Estimated effort.** Two to four weeks. The vector-clock primitives are small; the holding queue is small; the partition recovery and fork-resolution UI are the longest pieces. Test with deliberate partition injection (kill the network between two peers, edit on both sides, rejoin) early and often.

### 9.4 Video tier-aware rendering (§11.8)

V7 commits to a three-strategy menu for video under retroactive boundary shifts and tempo warping. Implementation surface:

- **Nearest-frame selection.** The default for Tight and Survival tiers. At each video membrane callback, pick the frame whose LMC capture timestamp is closest to the target LMC time. Cheap (a single timestamp lookup) and deterministic.
- **Frame-blending.** The default for Comfortable. Blend the two frames bracketing the target LMC time, with ratio determined by temporal distance. Implement as a GPU shader (per-pixel alpha blend); cost is well within integrated-graphics budgets.
- **Motion-compensated interpolation.** The default for Lavish. Compute optical flow between bracketing frames and synthesize an intermediate frame. Use a GPU-accelerated optical flow library (OpenCV's `DISOpticalFlow` is reasonable; vendor-specific libraries like NVIDIA Optical Flow SDK are faster). Cost: 5–20 ms per frame pair at 1080p on a discrete GPU.

The strategy is selected at session start from the capability tier; the performer may override per-Constituent. Build the GPU compute pipeline as a separate queue from the audio path — audio and video are independently disciplined against the LMC and must not share a thread of execution.

**Estimated effort.** One to three weeks depending on how much GPU pipeline infrastructure already exists. Nearest-frame alone is a couple of days; the full menu with optical flow is the upper bound.

### 9.5 MIDI 2.0 integration details (§6.12)

V4 said the tape format is UMP-shaped; V7 specifies the rest. Implementation surface:

- **MIDI-CI capability negotiation at device attach.** Send a MIDI-CI Discovery message on each newly-attached MIDI device; read the device's reply; flag the device as 2.0-capable or 1.0-only, per-direction. Use a standard MIDI-CI library or implement against the M2-103-UM specification. The flag governs the output-membrane downcast policy.
- **MPE channel allocation at the membrane.** For incoming MPE messages, allocate channels per the MPE lower/upper zone model and preserve the allocation as tape metadata. For UMP 2.0 sources with native per-note expression, no MPE allocation needed.
- **Per-note expression on parallel parameter tapes.** Per-note pitch bend, per-note pressure, per-note volume, and per-note attribute streams are stored on parallel parameter tapes time-aligned with the note tape — not as control changes interleaved with notes. This makes them automatable per-Constituent and mutable per repetition cycle.
- **Downcast at the output membrane.** When routing to a MIDI 1.0 destination, downcast UMP 2.0 → MIDI 1.0 with explicit precision-loss (16-bit velocity → 7-bit; per-note attributes dropped or aggregated). Per-destination policy; performer may override.
- **SMF export and UMP-JSONL archive.** Per §6.11, MIDI export to Standard MIDI File downcasts to MIDI 1.0. For full-fidelity export, UMP JSONL is the long-term archive format (same as on-tape format).
- **Controller learn UX.** Per §7.10, listen for any MIDI 1.0 or 2.0 message including MIDI-CI Property Exchange and bind to a parameter target. Bindings persist in the session.

**Estimated effort.** One to two weeks if the tape format is already UMP-shaped (V4 work). Longer if the MIDI input path is still 3-byte-message-shaped — but that retrofit was warned against in the V4 note in §2.4, so hopefully it's already done.

### 9.6 The rest of V7

A few smaller commitments worth noting but not large enough to merit their own subsection:

- **§17.8 concrete flush intervals.** Use the spec values (Lavish per-buffer, Comfortable 50 ms, Tight 200 ms, Survival 1000 ms). If your tape writer was tier-aware-with-placeholder-values, update the placeholders.
- **§9.1 reference-vs-containment clarification.** Code-level: a `Loop` Constituent should hold a `TapeSliceRef` (pointer or ID), not own a tape object. If your data model has loops owning tape data, refactor to references; tapes are session-global. This is likely already correct from V2 (the Constituent model has always been reference-based) but worth confirming.
- **§18.3 open questions trimmed.** The architecturally-deferred items are closed; what remains in §18.3 are UX-research questions (control surface ergonomics, phrase-relationship UX, structured improvisation interfaces, AI assistance, capability-tier detection heuristics) and one empirical validation question (real-world flush-interval performance). These are not blockers for shipping; they are work for the implementation companion and for field testing.

### 9.7 Recommended order alongside V3/V4/V5 work

If you're starting fresh (V3+V4+V5-aligned codebase being built from this guide), interleave V7 work like this:

- **At Step 5** (channel processing chains) or earlier: design the plug-in hosting layer for §9.1 out-of-process from day one. Retrofitting later is expensive.
- **At Step 8** (mix snapshots): the snapshot recall infrastructure is the same shape as the §9.3 vector-clock-applied edit replay. Build the primitives once.
- **At Step 9** (MIDI, then video, then file I/O): implement §9.5 (MIDI 2.0 details) and §9.4 (video tier-aware rendering) as part of each modality's bring-up.
- **Before first save/load works**: implement §9.2 (Sirius Archive Format) — the session file format is the durability contract and should be in place before there's anything worth saving.
- **Before any ensemble use**: implement §9.3 (ensemble consistency). The CRDT primitives can be in place from early on; the partition/fork handling and the anchor-node UI are the longer pieces.

If you're past Step 10 already, Section 9 is mostly net-new work. Take 9.1 first (largest impact, largest disruption), then 9.2 and 9.3 in parallel, then 9.4 and 9.5 as they become relevant.

---

*End of transition guide. Use alongside `Sirius_Looper.md`.*
