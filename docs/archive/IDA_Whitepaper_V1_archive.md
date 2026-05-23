> **Historical archive.** This document was written when the product was named
> "Sirius Looper". The product was renamed to **IDA — Idea Development
> Arranger** on 2026-05-23 as part of the AutomagicArt brand structure (IDA
> is the looping environment counterpart to OTTO). References to "Sirius
> Looper" below reflect the contemporaneous name and are preserved as-is.
> See `IDA_Naming_Decision.md` for the full rationale.

---

# The Sirius Looper

### A Reference Architecture for Time-Domain Audio/Video Looping

*Looping as the Capture and Repetition of Musical Ideas*

---

**Status:** Draft for review
**Version:** 1.0
**License:** TBD (intended for permissive public release)

---

## Abstract

This paper proposes a reference architecture for audio/video loopers that rejects assumptions built into nearly every existing looper, and proposes a foundational reframe that follows from rejecting them.

The rejected assumptions are: that audio sample clocks are a valid timing reference; that audio and video clocks can be synchronized to each other; that "record" is a fundamental operation; that the looper must protect the user from operating it; that time is a number; and that a loop is the unit of musical thought.

The reframe is built on three principles that hold together:

1. **A phrase is a musical utterance.** Phrases — not loops — are the unit of musical thought. A loop is a *mechanism* that a phrase may employ. A phrase may contain multiple loops, in multiple time domains, alongside non-looped material. Phrases have roles, intent, grammar, and internal time that may differ from the music surrounding them.

2. **Time is a concept, not a number.** The engine manipulates time symbolically — positions, structures, relationships — and renders to numerical time only at the membrane between digital and physical reality. This makes polymetric and polytemporal coexistence trivial, eliminates accumulated rounding error, and makes the system exact by construction rather than by representation precision.

3. **A loop is an idea, and ideas are worth repeating.** Every architectural decision exists to serve the capture, repetition, and arrangement of musical ideas without the friction that has always come with that capture.

What follows is built around a **Logical Master Clock (LMC)** treated as the only honest external timebase at the membrane, an **always-running tape per input** treated as the source of truth, a unified **Constituent hierarchy** (tape → loop → phrase → section → song → set) in which each level operates in its own conceptual time domain, and a user interface that trusts the musician absolutely.

The result is a looper that can do things existing loopers cannot — including capturing polymetric phrases, supporting structural improvisation through role-fillable phrases, and reproducing micro-timing and feel exactly — while also doing the things existing loopers already do, without the friction that has always come with them.

---

## Table of Contents

- **Part I:** First Principles
- **Part II:** The Lies of Digital Time
- **Part III:** Time as Concept
- **Part IV:** The Logical Master Clock
- **Part V:** The Engine
- **Part VI:** The Tape
- **Part VII:** The Constituent Hierarchy
- **Part VIII:** Phrases
- **Part IX:** Polymetric and Polytemporal Coexistence
- **Part X:** Repetition
- **Part XI:** Arrangement and Narrative
- **Part XII:** The Ensemble
- **Part XIII:** Fidelity and Resources
- **Part XIV:** The Performer's Instrument
- **Part XV:** What This Architecture Enables
- **Appendix A:** Glossary
- **Appendix B:** Decision Log

---

# Part I — First Principles

Before any architecture, six principles. Everything that follows is a consequence of these.

### 1.1 A loop is an idea.

The loop is not a recording. It is not a buffer. It is not a time interval. Those are implementation details of the thing the loop *is*: **a musical idea, captured in a form that allows it to be heard again.**

This framing is not poetic indulgence. It is the only framing that produces a looper that serves musicians rather than confounds them. When the loop is treated as an idea, every design question becomes tractable: how do we capture ideas without losing them? How do we let ideas repeat in ways that feel musically alive? How do we let ideas combine into larger ideas? How do we let ideas develop? These are the questions a looper answers.

### 1.2 Ideas are worth repeating.

Repetition is not a feature of the looper. Repetition is the *point* of the looper. The performer captures an idea precisely because they want to hear it again. The architecture must therefore make repetition *musical* — alive, variable, responsive — rather than merely mechanical. The instant repetition becomes mechanical is the instant the idea dies.

### 1.3 A phrase is a musical utterance; a loop is one mechanism it may use.

A loop and a phrase are not the same thing. **A phrase is a complete musical utterance** — with its own internal shape, entrance, body, and resolution — containing whatever combination of looped and non-looped material the music demands. A loop is a *mechanism* by which content within a phrase may repeat. Phrases have grammar, roles, and intent that loops do not. A phrase may contain several loops which may or may not be related, alongside one-time content, alongside silence as content.

This distinction reorganizes the entire architecture: the loop drops back to being a tool, and **the phrase becomes the unit of musical thought.** Arrangement happens at the phrase level and above; mechanism happens at the loop level and below.

### 1.4 Time is a concept, not a number.

The engine does not measure time. It manipulates time symbolically — positions, structures, relationships, hierarchies of nested time domains. Numerical time exists only at the membrane between digital and physical reality, where sound must actually emerge from a speaker. **Inside the engine, everything is symbolic; only at the membrane does the symbolic become numerical.**

This principle dissolves problems that have plagued digital audio for decades: accumulated rounding error, the impossibility of reconciling audio and video clocks, the difficulty of representing polymetric music, the loss of micro-timing under quantization. None of these are problems in conceptual time. They are problems that arise *only* when conceptual time is forced into a single numerical grid.

### 1.5 The looper trusts the user.

The user is a musician. They know what they want, when they want it, and how they want it. The looper's job is not to constrain, not to guide, not to second-guess, not to protect them from their own choices. **The looper's job is to anticipate what the user is likely to want at any given moment, present that affordance, and then disappear.**

Many design decisions in existing loopers can be traced to a quiet assumption that the user might make a mistake. That assumption is itself the failure. A looper built on trust will be vastly more powerful and vastly more transparent than one built on caution.

### 1.6 The looper's responsibility ends at the membrane.

The looper captures, repeats, arranges. It does *not* mix, route, EQ, dynamically process, or master. Those are downstream concerns with mature dedicated tools. A looper that respects its scope becomes the best possible front end to whatever downstream tools the user prefers.

The single exception: **arrangement is creation, not processing.** The order in which phrases play, the sections they form, the songs they compose into, the transitions between them — these are part of the musical idea, not separate from it. The looper is responsible for arrangement at every level of the Constituent hierarchy.

---

# Part II — The Lies of Digital Time

The most important sentence in this paper is one most audio engineers have never said out loud: ***there is no true time on any computer, only the best disciplined approximation.*** Every other architectural decision in this document depends on internalizing that statement.

### 2.1 The first lie: sample clocks

When an audio interface reports that it is sampling at 48 kHz, what it actually means is that an onboard crystal oscillator, divided down by a clock chip, is producing strobes that *approximately* arrive at 48,000 per second. The actual rate is determined by the crystal's manufacturing tolerance (typically ±10 to ±50 parts per million), its temperature, its age, the voltage on its supply rail, and acoustic shock to the housing. A "48 kHz" interface in a cold studio at the start of a session and the same interface in a warm room two hours later are running at different rates.

Two audio interfaces nominally clocked at 48 kHz, recording the same source, will produce recordings whose lengths differ by milliseconds over a single song and seconds over a single set. There is no engineering trick that eliminates this. It is the nature of the substrate.

### 2.2 The second lie: frame clocks

Video clocks are worse. The nominal frame rates of professional video — 23.976, 24, 25, 29.97, 30, 50, 59.94, 60 — exist for reasons of broadcast compatibility, not for reasons of mathematical alignment with anything else. The 0.1% pulldown that produces 29.97 from 30 was a compromise for color NTSC's subcarrier spacing in 1953, and we are still living with it.

Worse still: video frame timing is constrained by display vertical sync, which is itself determined by the panel's onboard clock, which is itself a crystal with all the imprecision of any other crystal. Frames are not delivered to the display "at 60 Hz" — they are delivered approximately every 16.67 milliseconds, with jitter that varies by GPU driver, OS scheduler, and load. Even "frame-accurate" video is, at the substrate level, approximately accurate.

### 2.3 The third lie: standards cannot be synchronized

It is mathematically impossible for 48,000 samples per second to be exactly synchronized to 29.97 frames per second. The numbers do not share a useful common divisor. Whatever apparent synchronization you observe is the product of one or both of those clocks being silently warped to make them appear to agree.

The industry has built a vast technical infrastructure on the premise that these standards are real. They are not. They are conventions, and the conventions do not mathematically close.

### 2.4 The only honest answer

If sample clocks lie and frame clocks lie and the standards cannot be reconciled, then the only honest architectural response is to refuse to use any of them as a master timing reference. **The master must be something else — something that does not pretend to know anything about audio samples or video frames, and against which both can be measured and corrected.**

That something — at the *membrane*, where physical reality begins — is **absolute time**: a monotonic, high-resolution time reference that exists *above* the audio and video clocks and treats both as merely the imperfect substrates they actually are. Inside the engine, however, the answer is deeper still, and is the subject of the next part.

---

# Part III — Time as Concept

The earlier draft of this paper treated absolute time as the master timebase throughout. That treatment was incomplete. **Absolute time is the master at the membrane. Inside the engine, time is a concept — symbolic, hierarchical, exact by construction.**

This part establishes the principle, its consequences, and the architectural pattern that follows from it.

### 3.1 The principle

> **Time is a concept the system manipulates symbolically. Numerical time is a rendering, not a representation. The engine thinks in musical and structural relationships; the membrane renders those relationships against absolute time at the moment of audible production.**

This is the same architectural pattern as *time-domain at the core, sample-domain at the membrane* — restated one level higher. Just as the engine renders symbolic time intervals to samples at the membrane, the engine now renders **conceptual positions** to **numerical times** at the membrane. The numerical world only exists where physics demands it.

### 3.2 What conceptual time means in practice

A loop's boundary is not "47 milliseconds and 832 microseconds." It is a *position* — described in whatever conceptual terms the loop's context calls for:

- "The downbeat of bar 1 of phrase A, in phrase A's local meter and tempo"
- "The third quintuplet of beat 4 of loop X"
- "The anchor where phrase B aligns to section C's boundary"

These are **symbolic positions in conceptual time**. They are exact because they are *defined* exactly, not because they are measured exactly. The exactness comes from the symbolic structure, not from precision of representation.

### 3.3 What this dissolves

Three categories of problem dissolve once conceptual time is accepted as the engine's substrate.

**The PPQ problem.** Existing systems must choose a tick resolution that accommodates every musical subdivision they want to support. 480 PPQ disallows septuplets. 3840 PPQ does too. To support 11-tuplets cleanly, the LCM gets very large. *In conceptual time, this problem does not exist.* A 7-tuplet is "7 in the space of 4" — a structural relationship, not a number on a grid. There is no shared grid that must accommodate every subdivision simultaneously.

**The polymetric reconciliation problem.** A 4/4 loop and a 7/8 loop in the same phrase cannot share a sample-level grid without one of them being approximated. *In conceptual time, they don't need to.* Each lives in its own conceptual time domain. They meet only at phrase boundaries, conceptually, and the rendering at the membrane resolves both to absolute time independently.

**The accumulated-rounding problem.** Any system that stores time as floating-point or fixed-grid integers accumulates error over thousands of operations. A loop in its hundredth cycle drifts from a loop in its first cycle. *In conceptual time, there is nothing to round.* The conceptual position of "beat 3 of loop X, cycle 100" is identical to "beat 3 of loop X, cycle 1" — same position, same definition, different rendering moment.

### 3.4 The hierarchy of time domains

Conceptual time is not flat. It is a *tree*, with each level potentially having its own structure:

- **Session time** — the outermost domain, anchored to LMC absolute time
- **Song time** — may have its own tempo map, independent of session
- **Section time** — may have its own structure within the song
- **Phrase time** — may have its own meter and tempo within the section
- **Loop time** — may have its own meter within the phrase
- **Cycle time** — the position within a single playback of a loop

Each level inherits its parent's time domain by default but may declare its own. The conceptual position of any event is its position within its immediate parent, *recursively unrolled* up the tree at rendering time to produce the absolute time at the membrane.

### 3.5 Conceptual time is more exact than numerical time

There is one thing worth saying explicitly because it could be misunderstood: **conceptual time is not vague time.** It is *more* exact than numerical time, not less. A musical position described as "the third sixteenth of bar 4 of phrase A in its local 7/8 meter" is *unambiguous* — there is exactly one moment it refers to, given the surrounding context. There is no rounding, no drift, no accumulated error.

This is the same kind of move that mathematics makes when it deals with irrational numbers symbolically (π, e, √2) rather than approximating them. Numerical representation introduces error; symbolic representation is exact. The engine works in the symbolic domain. The membrane renders to the numerical domain at the latest possible moment.

### 3.6 The rendering pipeline

When a sample must be produced for the audio interface at the next callback, the engine performs the following at the membrane:

1. Identify which conceptual positions are active at the upcoming render window
2. Unroll each position's hierarchical context (session → song → section → phrase → loop)
3. Apply the relevant tempo maps at each level to convert conceptual position to absolute LMC time
4. Apply device calibration to convert LMC time to device sample index
5. Render the sample

This pipeline is executed only at the membrane, only when needed, and only with the precision the membrane requires. Above this layer, the engine knows nothing of samples, frames, or absolute time.

### 3.7 Editing is symbolic

A consequence worth noting: **editing a loop boundary does not change a number from 47.832 to 47.913. It changes a *reference* from "the downbeat of bar 3" to "the second eighth of bar 3."** Edits are operations on conceptual structures, not on numbers. This makes editing exact, deterministic, and free of representation artifacts.

### 3.8 Axiom for the paper

> **Conceptual at the core, numerical at the membrane. The engine manipulates time symbolically; numerical precision is a property of the membrane, not the engine. Nothing observable depends on greater precision than the membrane offers.**

---

# Part IV — The Logical Master Clock

With conceptual time as the engine's internal substrate, the LMC's role is now precisely defined: **the LMC is the discipline mechanism for the membrane.** It is not the engine's internal time. It is the absolute-time reference against which numerical time is rendered at the moment a sample or frame must physically exist.

### 4.1 Definition

The LMC is a software construct above all hardware clocks. It is exposed to the rendering layer (and only the rendering layer) as a continuously-advancing absolute-time reference. Every hardware clock — audio device clocks, video device clocks, the host CPU clock itself — is treated as a *source* against which the LMC is disciplined, never as the LMC's master.

### 4.2 The discipline hierarchy

At session start, the LMC selects its discipline source from the best available option in a strict hierarchy:

| Tier | Source | Typical precision | Use case |
|---|---|---|---|
| 1 | GPS-disciplined / atomic | sub-microsecond | Broadcast, scientific rigs |
| 2 | PTP / IEEE 1588 grandmaster | microseconds | Pro studios with Dante/AES67 |
| 3 | NTP stratum 1–2 | milliseconds | Networked workstations |
| 4 | Ableton Link peers | peer-consensus | Live ensembles |
| 5 | Local CPU monotonic | drift-bounded against itself | Solo, offline |

When nothing external is reachable, local CPU monotonic *is* the LMC — self-consistent within a session, which is sufficient for solo work.

### 4.3 Calibration tables

Every hardware clock at the membrane carries a calibration record against the LMC:

```
ClockCalibration {
    source_id           // which hardware clock
    rate_factor         // measured rate ratio (e.g., 0.999987)
    offset              // measured constant offset (in LMC time units)
    last_updated        // when this calibration was measured
    confidence_interval // statistical uncertainty
}
```

These tables are refreshed continuously by background measurement. The rendering layer reads them to convert from LMC time to device-native time. They are the only place in the system where audio sample indices and video frame indices live.

### 4.4 Behavior under reference loss

When the LMC's discipline source becomes unavailable mid-session, the LMC continues smoothly using local CPU monotonic at the last known disciplined rate. There is no step, no glitch, no audible discontinuity. When the reference returns, the LMC re-engages via *gradual rate slewing* over seconds or minutes — never an instantaneous re-lock.

### 4.5 Archival fidelity

The LMC's discipline history is recorded as session metadata. Years later, the LMC's relationship to UTC at the moment of any recorded event is reconstructable to the precision the discipline source allowed. This makes recordings made under this architecture **archivally valid** at a level most current systems cannot match.

### 4.6 The LMC's relationship to conceptual time

The LMC and conceptual time meet at exactly one place: **the rendering pipeline at the membrane.** The conceptual position of a musical event is unrolled through its hierarchical context, the result is mapped to LMC absolute time via the session's outermost tempo map, and the LMC time is then mapped to device-native time via the calibration tables. Three transformations, applied in order, at the latest possible moment.

### 4.7 Axiom for the paper

> **There is no true time on any computer — only the best disciplined approximation. The LMC is that approximation, lives at the membrane, and is the only thing in the system that ever produces a number representing absolute time.**

---

# Part V — The Engine

With conceptual time and the LMC established, the engine architecture follows.

### 5.1 Conceptual at the core, numerical at the membrane

The looper engine does not deal in samples, frames, or absolute time. It deals in **conceptual positions** within nested time domains. Samples and frames exist exactly twice in the engine's signal path: at the input membrane (capture, timestamping) and at the output membrane (presentation, rendering). Between those two points, the engine knows only concept.

This is the fundamental architectural inversion of this looper. Most loopers count samples and bolt time-correction layers on top. This one *thinks in music* and emits samples at the edges.

### 5.2 The two membranes

The engine has exactly two membrane layers — one inbound, one outbound.

**The inbound membrane** receives sample buffers from audio devices and frame buffers from video devices. For each sample or frame, it:

1. Reads the device's reported capture timestamp and applies the device calibration to convert to LMC time
2. Records the data on its source tape (Part VI) with its LMC timestamp
3. The tape entry is now in the engine's hands, addressable conceptually

**The outbound membrane** receives a request to produce a sample buffer for an audio device or frame buffer for a video device. For each output, it:

1. Determines the LMC time window the buffer corresponds to
2. Queries the active Constituent hierarchy for what should sound during that window
3. Unrolls each active constituent's conceptual position through its hierarchical context to produce LMC times
4. Reads the appropriate tape data via the conceptual-to-LMC mapping
5. Mixes, applies effects, and produces the output buffer

In between, the engine operates entirely in conceptual time.

### 5.3 Continuous async sample-rate conversion at the membranes

The membranes are where conceptual time meets the physical reality of an audio interface running at "48 kHz" that is actually running at some slightly different rate. Bridging this is the job of **continuous async sample-rate conversion (ASRC)** at each membrane.

The ASRC's quality determines the looper's audible fidelity. Cheap ASRC produces audible chirping artifacts on sustained content; transparent ASRC (libsoxr's VHQ preset is the industry reference) is computationally expensive but inaudible. Budget approximately 3–5% of one CPU core per channel for VHQ-quality continuous resampling.

Video has an equivalent membrane: frame-rate conversion between the camera's actual capture rate and whatever rate is needed for storage, processing, or display. The simplest approach is nearest-frame selection; more ambitious implementations use motion-compensated frame interpolation. Implementation choice; the engine architecture above the membrane is identical regardless.

### 5.4 Tempo maps as conceptual transformations

A tempo map is no longer "a function from absolute time to musical position." It is **a transformation between two conceptual time domains** — typically between a parent's time and a child's time. Each level of the hierarchy may have its own tempo map relating its time to its parent's.

Tempo maps are stored as sequences of breakpoints expressed in their own conceptual terms ("at this position in the parent, this position in the child"). They become numerical only when the rendering pipeline unrolls a position through them.

For piecewise-constant tempo, the transformation is linear in each segment. For tempo ramps, it is piecewise quadratic. The implementation is straightforward; what matters architecturally is that **the tempo map is conceptual, not numerical**, and is only numerically evaluated at the membrane.

### 5.5 Latency compensation as architectural

When the inbound membrane receives a sample buffer, the samples were not captured *now*. The sample at index N in a buffer of size B was captured at:

```
LMC_capture_time = LMC_callback_time − latency_input − (B − N − 1) / device_rate
```

The membrane applies this transformation before the sample becomes a tape entry. From the engine's perspective, **every tape entry carries its true conceptual time of capture**, not its arrival time.

Outbound, the mirror transformation tells the membrane when each sample in an output buffer will actually be heard:

```
LMC_present_time = LMC_callback_time + latency_output + N / device_rate
```

The rendering pipeline aims at this future time, not the present.

Device latencies are typically reliable on CoreAudio and ASIO, less reliable on WASAPI, and effectively absent on USB Class-Compliant on some platforms. To handle this generally, the system performs a **one-time loopback calibration per device**: output a known click, capture it back, measure the round-trip in samples, store the result. Subsequent sessions on the same device read the stored calibration.

---

# Part VI — The Tape

This is the architectural inversion at the heart of the system's storage model.

### 6.1 Everything is always recorded

The looper is not "armed" or "recording" or "in standby." From the moment the user opens a session, **every input is being captured to a tape, continuously, until the session ends.** The tapes run whether the user has any plans to use them or not. They run during silence. They run during conversation. They run during the act of thinking.

This is not an exotic recording mode. It is the default — the only — mode. Storage is cheap; ideas are precious. The architecture refuses to lose ideas.

### 6.2 All inputs are tapes

The tape abstraction is not specific to audio. **Every signal in the system is a tape**, sharing one uniform event format:

```
TapeEvent {
    conceptual_timestamp    // when, in the source's conceptual time
    lmc_timestamp           // when, in LMC absolute time (the membrane's record)
    tape_id                 // which input
    payload                 // the data itself
}
```

Each tape event carries *both* a conceptual timestamp and an LMC timestamp. The LMC timestamp is the truth about when the event arrived at the membrane; the conceptual timestamp is the truth about what musical moment that arrival represents. They are equivalent for inbound capture (the capture moment defines both) but diverge for synthesized tape data (parameter automation, control events generated within the engine), which has only conceptual timestamps until rendered.

Tape sources include:

- Audio inputs (one tape per channel; dense PCM payload)
- Video inputs (one tape per camera or capture device; frame payloads)
- MIDI inputs (one tape per port; event payloads)
- Control surface events (footswitches, knobs, pads, faders)
- Parameter automation (one tape per automatable parameter)
- Transport events (session start, tempo changes, section markers)
- System events (clock discipline changes, device hot-plugs)

All of these are *the same kind of thing*. They all live on tapes. They are all queryable by conceptual or LMC time. They are all the source of truth for "what happened."

### 6.3 Tapes are append-only and immutable

Tapes are never modified. They are written once, at the moment of capture, and never edited. **Every edit in the system happens elsewhere** — in Constituents (Part VII), in overlay layers, in effect chains — never on the tape itself.

This is the architectural foundation that makes the entire system tractable. It enables trivial undo, trivial multi-take, trivial collaboration, trivial archival, and trivial reproducibility.

### 6.4 The retroactive ring

Because tapes are always running, the user never has to "start recording before the moment they want to capture." The moment is already on the tape. The user marks a boundary on a tape that has been running for some time, and pulls the in-point backward to grab the pickup note that preceded the mark.

The **retroactive ring** is the in-memory portion of recent tape data, held against the possibility that the user will reach backward in time to capture something. Its depth depends on the capability tier (Part XIII): seconds for tight resource budgets, minutes for comfortable ones, hours for lavish.

> **The looper records the past. Because every event has been timestamped on the way in, the "record button" is a time bookmark, not a capture trigger.**

### 6.5 Tape format strategy

Three reasonable strategies for tape storage:

- **Uncompressed PCM** (CAF/WAV per channel, ProRes/DNxHR per video stream): instant random access, maximum quality, highest storage cost
- **Lossless compressed** (FLAC for audio, intra-frame codec for video): roughly half the storage cost, with decompression overhead at read time
- **Tiered**: uncompressed in the retroactive ring (RAM), lossless on disk during the live session, optional offline archival compression on session close

The tier system (Part XIII) chooses among these at startup based on measured hardware capability.

### 6.6 Tapes are local

Tape data lives on the machine that captured it, always. The network carries only coordination metadata. This is non-negotiable and is justified fully in Part XII.

---

# Part VII — The Constituent Hierarchy

If the tape is the source of truth, the Constituent is the thing the system actually plays.

### 7.1 The unifying abstraction

Every musical object in the system is a **Constituent**. A loop is a Constituent. A phrase is a Constituent. A section is a Constituent. A song is a Constituent. A set is a Constituent. They share a common structure:

```
Constituent {
    id                          // identity, persistent across edits
    conceptual_in               // start position in parent's time
    conceptual_out              // end position in parent's time
    local_tempo_map             // optional; inherits from parent if absent
    local_meter                 // optional; inherits if absent
    anchor_to_parent            // how this aligns to its parent (see below)
    effect_chain                // signal processing (loops and below)
    repetition_rules            // how this plays back (loops and phrases)
    metadata                    // role, intent, name, etc.
    children                    // for containers: nested Constituents
}
```

The Constituent does not store audio data. It contains *references* — either to tape slices (at the loop level) or to other Constituents (at higher levels). All data is on tapes; everything else is structure.

### 7.2 The hierarchy

The conventional musical hierarchy maps onto the Constituent system as:

```
Tape (immutable source of truth)
   ↓
Loop or non-looped tape slice (mechanism layer)
   ↓
Phrase (musical utterance; the unit of musical thought)
   ↓
Section (structural grouping of phrases)
   ↓
Song (narrative composed of sections)
   ↓
Set (sequence of songs)
   ↓
Performance arrangement (the outermost Constituent)
```

This is the *typical* organization. The data model does not enforce it strictly: any Constituent may contain any other Constituent as long as the time-domain relationships are coherent. A bed that persists through several phrases may live directly at the section level, alongside its phrases, not inside any of them.

### 7.3 Constituents are immutable; edits are copy-on-write

Editing a Constituent — trimming its boundaries, changing its effects, modifying its repetition rules, replacing its children — does not modify it. It produces a **new Constituent** with the modifications, sharing the same source references where applicable. The old version is preserved in the edit history.

This makes undo trivial. It makes branching alternatives trivial. It makes "show me what this would sound like with these changes" trivial. The cost is minimal: each Constituent is metadata only.

### 7.4 Loops within the hierarchy

A loop is the Constituent type that directly references tape data. Its specifics:

- `children` is empty (or contains overlays; see effects below)
- `conceptual_in` and `conceptual_out` reference positions on its source tape(s)
- `repetition_rules` describe how it plays back (the five dimensions of Part X)
- `effect_chain` describes processing applied to its output

A loop is the *mechanism* — the thing that actually causes tape data to become sound. Everything above it in the hierarchy is structure that organizes when and how loops are heard.

### 7.5 Non-looped tape slices are first-class

Inside a phrase, content may be looped or not looped. A non-looped tape slice is structurally similar to a loop with `repetition_rules.cardinality = Once`, but is named differently because the intent is different. A loop is "this is meant to repeat." A non-looped slice is "this is meant to be heard once, in this moment."

The distinction matters for the user interface (Part XIV) — a performer should be able to capture content with an inferred intent (loop vs. one-shot) without having to declare it explicitly. The data model accommodates both as first-class.

### 7.6 Identity persists across content revision

Every Constituent has an identity that persists even as its content changes. A phrase named "the verse" remains "the verse" through fifteen revisions of its content. **Identity is what makes the phrase the same phrase across edits.** Versioning happens at the content level; identity persists at the structural level.

This is closer to how a screenwriter thinks about a scene than how a sound engineer thinks about a recording — and it is the right model for music-making.

### 7.7 What this enables

- **Save and load are trivial.** The Constituent graph serializes to kilobytes of INI or JSON. Tapes are heavy files addressed by ID.
- **Effects are applied per-Constituent and are replaceable.** Changing the reverb on phrase 7 changes only phrase 7.
- **Effect parameters are themselves automatable.** Parameter automation is data on a tape. Automation curves are Constituents over parameter tapes. The recursion holds at every level.
- **Render is deterministic.** The same Constituent tree applied to the same tapes produces the same output, forever.

---

# Part VIII — Phrases

The phrase is the unit of musical thought. It deserves its own treatment.

### 8.1 What a phrase is

A phrase is a **complete musical utterance with its own identity, internal arc, and relationship to surrounding utterances.** It is the unit at which a musician *thinks* about music. When a performer says "let me try that part again," the part is a phrase. When they say "I want a fill here," the fill is a phrase. When they say "the verse is too long," the verse is a phrase.

Phrases are not defined by their content alone. They are defined by their *role* in a larger musical structure. Two completely different sets of notes can both be "the chorus" — they share a role and a structural position, while differing in content.

### 8.2 What a phrase contains

A phrase contains zero or more child Constituents — typically loops and non-looped tape slices, but possibly other phrases (a phrase may contain sub-phrases). The children may be in any time relationship to the phrase: simultaneous, sequential, overlapping. They may be in the phrase's local time domain, or in their own local time domains nested inside it.

Critically, a phrase may contain:

- **Multiple loops in different time domains.** A 4/4 drum loop and a 7/8 ostinato in the same phrase (Part IX).
- **Non-looped one-shot material.** A spoken line, a sampled hit, a vocal phrase delivered once.
- **Silence as content.** A rest, a held breath, a deliberate absence. The phrase's interior may contain regions where nothing sounds; the silence is part of the phrase's structure.
- **Rubato or unmetered passages.** Content not aligned to any metric grid (Part IX).

### 8.3 Phrase metadata

A phrase carries metadata beyond what loops carry:

```
PhraseMetadata {
    role                   // verse, chorus, response, fill, statement, etc.
    intent                 // free-text description of the phrase's musical purpose
    entrance_character     // how the phrase begins (pickup, downbeat, etc.)
    exit_character         // how the phrase ends (resolution, hand-off, etc.)
    grammatical_links      // relationships to other phrases (Part 8.5)
    is_role_fillable       // whether this phrase can substitute for others of the same role
}
```

This metadata is not exotic. It is what musicians already think about when they think about phrases. The architecture exposes it as first-class data.

### 8.4 Roles make phrases interchangeable

When a phrase carries a role (e.g., "verse," "response," "fill"), it becomes potentially interchangeable with other phrases carrying the same role. The arrangement layer above the phrase can specify:

- A specific phrase to play here
- A *role* to fill here, leaving the specific phrase to be chosen at performance time

This is what enables **structured improvisation**. The song's structure is fixed (verse, chorus, verse, chorus, bridge, chorus); the specific phrases that fill each role can be selected by the performer in the moment. Two performances of the same song share their structure but diverge in their content.

### 8.5 Grammatical relationships between phrases

Phrases relate to each other in ways that sequence-and-overlap cannot capture. The architecture supports these relationships as first-class metadata:

- **Call and response**: phrase A poses a question that phrase B answers. The pair is linked.
- **Statement and variation**: phrase B is a variation of phrase A. The relationship is functional.
- **Theme and development**: phrase A is a theme; later phrases develop it.
- **Tension and release**: phrase A builds; phrase B resolves.
- **Punctuation**: phrase A closes a section; phrase B opens one.

These relationships are stored as `grammatical_links` in the phrase metadata. They do not directly affect playback (playback is governed by repetition rules and arrangement); they affect *display, analysis, and composition*. A performer looking at a phrase can see what it answers, what answers it, what it varies, what varies it. This is information the musician's mind is already carrying; the architecture surfaces it.

### 8.6 The phrase's internal time

A phrase has its own conceptual time, which may have its own meter and tempo independent of its parent. This is detailed in Part IX. The crucial point: **a phrase's interior is a complete time domain.** It is not just a slice of the song's time.

### 8.7 Entrance and exit are part of the phrase

A phrase has a specific entrance character (how it begins) and exit character (how it ends). These are part of the phrase's identity, not separate concerns:

- A phrase that begins with a pickup carries its pickup as part of the phrase
- A phrase that resolves with a tail carries its tail as part of the phrase
- A phrase that hands off to the next phrase encodes the handoff in its exit

The entrance and exit characters interact with arrangement: a phrase that ends with a hand-off implies the existence of a next phrase to receive the hand-off. The arrangement layer respects these characters when sequencing phrases.

---

# Part IX — Polymetric and Polytemporal Coexistence

The Constituent hierarchy admits something almost no music software has ever admitted: **multiple time domains coexisting within a single phrase.**

### 9.1 The commitment

A 4/4 drum loop and a 7/8 ostinato and a free-rubato vocal line may live inside one phrase, in three different time domains, sharing only the phrase's outer boundaries. This is not an edge case. It is real music — Steve Reich, Frank Zappa, Meshuggah, African and Indian classical traditions, contemporary jazz, electronic music.

Most music software pretends this music does not exist or treats it as a workaround. This architecture puts it at the foundation.

### 9.2 Time domains as a tree

The conceptual time tree (Part III) makes polymetric coexistence trivial. Each level — and each Constituent within a level — may declare its own meter and its own tempo relationship to its parent:

- **The session's tempo map** governs the outermost time domain
- **A song's tempo map** may differ from the session's
- **A phrase's tempo map** may differ from the song's
- **A loop's tempo map** may differ from the phrase's

Each level inherits its parent's time domain by default but may override. The override is local to the Constituent and does not affect its siblings.

### 9.3 The polyrhythmic case

When a 4/4 drum loop and a 7/8 ostinato play simultaneously inside the same phrase, they share absolute time (via the LMC at the membrane) but do not share musical time. Each has its own tempo map. The musical relationship between them is *expressible* as a ratio:

```
drum_loop:    4/4 at 120 BPM  →  one bar at the phrase's tempo
ostinato:     7/8 at 120 BPM  →  shorter bars, same beat
              or 7/8 at 105 BPM →  matched bar length, slower beat
```

The performer chooses the relationship. Either way, the engine renders both correctly because the conceptual descriptions are exact and the rendering reconciles them at the membrane.

The mathematical consequence: in conceptual time, the relationship is exact by definition. A 4-against-7 polyrhythm holds across thousands of cycles because **the math is in symbolic structure, not in numerical approximation.** The phrase that began in alignment ends in alignment.

### 9.4 The polytemporal case

When a vocal phrase floats freely over a metric backdrop, the vocal is in *unmetered local time* — it has timing but no grid. Its tempo map is flat (no beats, no bars); its content is anchored directly in absolute time at the phrase level. The vocal still lives inside the phrase, but its conceptual time domain is "raw absolute time" rather than a metric structure.

This handles rubato passages, ad-libs, expressive timing, and drone-like material as natural cases — not exceptions. The architecture supports them by default.

### 9.5 Meeting at boundaries

Constituents in different time domains inside the same parent must *meet* somewhere. The natural meeting place is the parent's boundaries: a phrase begins and ends at song-time positions, regardless of what its interior does.

Whatever the constituents are doing inside, they must be at musically sensible positions at the phrase's start and end. A polyrhythmic phrase that ends mid-cycle for one of its constituents is usually a mistake or a deliberate effect; it is not the default. The default behavior is **align at boundaries; do whatever you want internally.**

For constituents that don't naturally complete by the phrase's boundary, the system applies termination rules (Part X): hard cut, complete current cycle, fade, hand off. The phrase boundary is itself a termination trigger for its interior.

### 9.6 Meter is a property, not a constraint

In existing DAWs, the session has a time signature, and everything in the session is in that time signature unless you go to elaborate lengths to declare otherwise. In this architecture, **meter is a property of each Constituent, declared locally, inherited from parent only by default.** The session has no global meter. The song has no required meter. The phrase has no required meter. Each loop carries its own.

This matches how music actually works. A jazz tune in 3/4 may have a bridge in 4/4. A prog metal song may shift meters every bar. A drone piece has no meter. A classical sonata may have a metric movement followed by a free cadenza. The architecture accommodates all of these without effort.

### 9.7 Micro-timing is preserved exactly

Because conceptual time is exact and only renders to numerical time at the membrane, **micro-timing — swing, drummer feel, deliberate behind-the-beat or ahead-of-the-beat playing — is preserved exactly.** It is not quantized away. It is not smoothed. The "feel" that makes a recording feel human is preserved at the substrate, because the substrate is the performer's actual timing, captured at the membrane and stored conceptually as captured.

The architecture does not need a special "micro-timing mode" or a "groove template." Micro-timing is just *what was actually played*, conceptually preserved, numerically rendered when needed. This was free all along; it just took the conceptual-time framing to see it.

---

# Part X — Repetition

A phrase contains loops; a loop is an idea; ideas are worth repeating. Repetition is what makes a loop music rather than mere capture.

### 10.1 The five dimensions of repetition

Every loop's playback behavior is described by five orthogonal axes. Defaults handle the common cases; combinations of non-default values produce the interesting music.

#### Trigger — what causes the idea to play?

- **Free-running**: starts at capture, plays continuously
- **On-demand**: dormant until triggered (footswitch, pad, MIDI note, another loop event)
- **Conditional**: triggered by another event (every N bars, after loop X ends, on a specific musical position)
- **Probabilistic**: chance-based per cycle, or selection from a set of alternates
- **Sequenced**: position in an explicit timeline

#### Cardinality — how many times does it repeat?

- **Once** (one-shot — a hit, a fill, a moment)
- **N times** (fixed count)
- **Until-condition** (until another loop starts, until silenced, until next downbeat)
- **Forever**

#### Phase relationship — when does each repetition start?

- **Free**: at trigger time, no quantization
- **Quantized to grid**: snaps to the next bar/beat/division per the local tempo map
- **Synchronized to another Constituent**: starts at X's `conceptual_in`, or X's midpoint, or N bars after X
- **Phase-locked**: maintains a fixed musical offset from a reference

#### Mutation — does the idea change as it repeats?

- **Identical**: the same data, every time
- **Varied automatically**: pitch, time, filter, reverse, randomized parameter ranges per cycle
- **Layered**: each repetition adds another overdub (the "build a drone" pattern)
- **Decaying**: each repetition softer/darker/shorter than the last
- **Evolving by rule**: granular position drift, rhythmic permutation, Markov choice

#### Termination — how does the repetition end?

- **Hard cut**: instantly silent on stop
- **Complete current cycle**: finish what's playing, then stop
- **Fade over N bars**
- **Continue until natural end** (the content's own arc resolves)
- **Hand off**: ending triggers another Constituent's beginning

### 10.2 Repetition is regenerator feedback

The architectural significance of repetition rules is not that they let the system play things back. It is that they let the system *participate in a cybernetic feedback loop with the performer.* The performer captures an idea; the system offers to repeat it; the performer's continued engagement signals "yes, again"; the system continues. The instant the performer's engagement withdraws, the system gracefully retires the idea.

**Repetition rules are therefore not commands but hypotheses about what the performer wants next.** Default rules embody the system's best guess for the most common case. Performer action — adjusting, accepting, ignoring, overriding — is the feedback signal. The system honors that signal instantly, without resistance, without confirmation dialogs.

### 10.3 Mutation as preservation of engagement

Mutation deserves explicit treatment because it is the dimension most commonly misunderstood. The purpose of mutation is *not* to make playback more interesting algorithmically. It is to **sustain the engagement the performer already has**.

Pure repetition is psychologically satisfying for some number of cycles, then becomes monotonous. The system's job — when mutation is enabled — is to introduce variation *just before* the engagement would have begun to wane. Done well, the performer does not notice the variation; they only notice that they still love the loop after the twentieth cycle.

**The art of mutation is in its invisibility.** Variation that draws attention to itself has failed. Variation that keeps the music alive without ever announcing itself has succeeded.

### 10.4 Termination matches attention decay

Hard-cut termination is *command-mode termination*: "stop now." It is appropriate for specific musical contexts but is the wrong default for most music. The default termination behaviors should match how listener attention actually decays:

- For background beds: fade
- For musical sections: complete the bar
- For phrases with natural arcs: continue until natural end
- For arrangements: hand off to the next section

The performer can override at any time, by any of the five axes. But the defaults serve the music, not the engineer's instinct for precision control.

---

# Part XI — Arrangement and Narrative

Arrangement is composition in real time. The looper is responsible for arrangement at every level.

### 11.1 Arrangement happens at the phrase level and above

The phrase is the unit of arrangement. A song is not "an arrangement of loops" — it is **an arrangement of phrases**, each of which may internally contain loops. This corrects a confusion present in many existing loopers, which treat loops as the arrangement unit and have no concept of phrase.

This means arrangement gestures operate on phrases:

- "Play this phrase here" — placing a phrase at a song-time position
- "Repeat this phrase" — replaying the phrase with its own repetition rules
- "Hand off from this phrase to that phrase" — chaining via exit/entrance characters
- "Fill this role with whichever phrase the performer chooses" — role-based slots

Loops still have repetition rules at the mechanism level, but those govern *how the loop plays within its phrase*. Arrangement is a phrase-level concern.

### 11.2 The recursive structure

The same arrangement principles apply at every level above the phrase:

- A **section** is an arrangement of phrases
- A **song** is an arrangement of sections (or directly of phrases, for short forms)
- A **set** is an arrangement of songs
- A **performance** is an arrangement of sets

Each level uses the same Constituent data model. The same gestures that arrange phrases within a section arrange sections within a song. **The performer who learns one level has learned all of them.**

### 11.3 Arrangement primitives

The following arrangement operations are not new features — they are Constituent operations applied at the phrase level and above:

- **Sequencing**: explicit order with handoff between exits and entrances
- **Layering**: simultaneous Constituents with overlapping `conceptual_in`/`conceptual_out`
- **Sections and scenes**: named groupings with their own repetition rules
- **Transitions**: governed by termination rules of outgoing + trigger rules of incoming
- **Conditional branches**: alternate continuations via conditional triggers
- **Role-based slots**: positions that select a Constituent by role at play time
- **Returns and recapitulations**: re-invoking earlier material, optionally with variation
- **Set lists**: the outermost arrangement

### 11.4 The song as narrative

A song is not a sequence. It is a *narrative built out of phrases*, with dramatic structure, intentional shape, rising and falling action. The song layer represents:

- **The arc**: how energy/intensity/density rises and falls
- **The structure**: verse/chorus/bridge/etc.
- **The trajectory**: where the song is going and how it gets there
- **The variability**: which phrases are fixed, which are improvised, which are role-filled at performance time

A song is a kind of **musical script**: structurally complete but performatively variable. Two performances of the same song share its narrative shape and role structure while diverging in the specific phrases that fill the roles.

### 11.5 The set list is the outermost Constituent

A live performance — an entire concert — is, in this architecture, just a Constituent. Its children are songs. Its `conceptual_in` is the moment the performer steps onstage; its `conceptual_out` is the moment they step off. The same engine that plays back a four-bar drum groove plays back the entire evening.

This has practical consequences:

- Save and load of an entire concert is a single Constituent graph file
- Restarting the same set on a different night loads the same outer Constituent
- Cross-concert continuity (motifs returning, arrangements evolving across performances) is achievable because all data lives in the same model
- Archive of a complete career of performances is a tractable storage problem

---

# Part XII — The Ensemble

The single-machine architecture extends gracefully to ensembles under one strict commitment.

### 12.1 The commitment: recording and playback are always local

**Tape data never traverses the network as primary signal.** Audio captured on Node A lives on Node A. Audio played back on Node B is sourced from Node B's local tapes only. The network carries no live audio between nodes; it carries only coordination.

The reason is physical: every transit between digital and physical reality (the membrane) must occur exactly once per node, and it must occur on the machine that the audio interface is plugged into. Audio that has traversed a network as a live stream is no longer disciplinable by the receiving node's LMC — its timing has already been perturbed by network jitter, packet loss recovery, and buffer scheduling, none of which the receiving node can correct after the fact.

**Network-streamed audio is, from the receiver's perspective, exactly as flawed as a poorly-clocked audio interface, with the additional disadvantage that the flaws are non-stationary and unpredictable.**

### 12.2 What does cross the network

The network footprint of a distributed ensemble is small:

- **LMC discipline messages** — clock comparison and slewing data
- **Constituent graph updates** — phrases/loops/sections created, edited, deleted
- **Control events** — transport, footswitch, parameter changes
- **Optional low-bitrate monitoring previews** — for cross-node awareness, explicitly separate from the recording signal path

That is the entire network surface.

### 12.3 Distributed LMC election

When an ensemble forms, the participating nodes elect an LMC master via a quality-weighted protocol. Naive median voting is insufficient because nodes have asymmetric error profiles. The correct algorithm is **Marzullo interval intersection**:

1. Each node advertises its LMC time as a *confidence interval* `[t_min, t_max]`
2. The algorithm finds the largest set of mutually-intersecting intervals
3. Nodes outside this consensus set ("falsetickers") are discarded
4. The LMC is computed from the consensus, weighted by interval width

Within the consensus, **tier dominance** governs: one GPS-disciplined node beats any number of NTP-disciplined nodes. Within a tier, the narrowest-interval source wins.

### 12.4 Master/slave model with overrides

The highest-tier node becomes **LMC Master**; others become **LMC Slaves** and discipline against it. Two explicit overrides:

- **Anchor node**: the user designates one machine as authoritative regardless of tier. Musical authority outranks technical authority.
- **Manual master selection**: the user picks the master explicitly.

Election occurs at session start and on topology changes. It does not run continuously. **Stability beats optimality.**

### 12.5 Local LMC and ensemble LMC

Each node maintains *two* LMC views:

- **Local LMC**: drift-free against itself, used for stamping local tape events
- **Ensemble LMC**: calibrated against the master, used for coordinating with other nodes

The two are related by a continuously-updated calibration record. A node that disconnects from the ensemble continues recording with fully valid local timestamps. When it reconnects, no data is rewritten — the calibration record is updated.

### 12.6 CRDT-compatible session state

Tapes are append-only. Constituents are immutable, copy-on-write. **These properties make the entire session state a natural Conflict-Free Replicated Data Type (CRDT).** Two nodes that edit the session concurrently during a network partition will merge cleanly on rejoin:

- Tapes: union of all tape events (no conflicts possible)
- Constituents: union of all versions (immutability eliminates conflict)
- Active selection: last-writer-wins with timestamps

Reconciliation requires no human conflict resolution. **The data model makes conflicts structurally impossible.**

### 12.7 Graceful degradation

Worst-case ensemble failure — a node loses network entirely, mid-performance — degrades gracefully to **a high-quality solo recording**, not a session loss. The disconnected node continues recording locally with valid timestamps and valid playback of its own Constituents.

> **Every musician's machine holds a complete, full-fidelity record of their contribution to the session, regardless of network behavior.**

### 12.8 The category distinction

> **The network is not a substrate for audio. It is a substrate for coordination. Audio is local. Time is shared. Mixing the two — sending live audio over the network and treating it as part of the production signal path — is a category error that this architecture refuses to make.**

---

# Part XIII — Fidelity and Resources

The system runs on hardware of widely varying capability. It must size itself to that hardware honestly.

### 13.1 Startup-time tier selection

At session launch, the system performs a one-time capability assessment and selects a **fidelity tier** for the session. The tier is locked for the duration of the session.

The assessment measures CPU (core count, vector capability, DSP throughput benchmark), RAM (total and available), storage (write-speed test), audio device (buffer size, latency, channels), power source (battery vs. mains), and thermal posture.

### 13.2 The four tiers

| Tier | Description | Tape format | ASRC quality | Effect strategy | Ring depth |
|---|---|---|---|---|---|
| **Lavish** | High-end workstation | Uncompressed PCM | VHQ | All live | Minutes |
| **Comfortable** | Modern laptop on AC | FLAC | VHQ | All live | Tens of seconds |
| **Tight** | Older or battery | FLAC | HQ | Mixed live + cached | Seconds |
| **Survival** | Marginal hardware | FLAC | MQ | Aggressive caching | Minimal |

The user is informed of the selected tier and may override (with a warning if they request more than the hardware will reliably deliver).

### 13.3 The unbreakable rules

Regardless of tier:

1. **Audio output never glitches.** All other concerns subordinate to it.
2. **Tape data integrity is never compromised.** Data already captured is never lost or corrupted.
3. **Degradation is announced, not silent.**
4. **Runtime overload protection drops video frames, UI updates, and analyzer work — never audio.**

### 13.4 The division of concerns

The performer authors musical intent. The system chooses fidelity. **The performer never thinks about render quality, storage format, or processing strategy** — those are the system's problems, solved at startup against the hardware it actually finds itself running on.

---

# Part XIV — The Performer's Instrument

Everything above this point has been architecture. What follows is what the performer actually touches.

### 14.1 The trust principle

The user is a competent musician operating a trusted instrument. The looper's job is to anticipate well, present the right affordance at the right moment, honor the user's gesture instantly, and otherwise disappear.

Many design decisions in existing loopers can be traced to a quiet assumption that the user might make a mistake. **That assumption is itself the design failure.** A looper built on trust is more powerful and more transparent than one built on caution.

### 14.2 Inspiration is fragile

The single most precious resource in music-making is the moment of inspiration. It is also the most easily destroyed. **Every UI decision in the looper answers to one question: does this preserve or destroy inspiration?**

The inspiration-killers in existing loopers, named explicitly:

- **Mode confusion** ("Am I in record mode or play mode?")
- **Pre-declaration** (choose length, quantization, routing before capture)
- **Punishment for imprecise timing** ("You started too early; retake.")
- **Visual noise demanding attention** (animations, meters, pulses pulling the eye to the screen)
- **High-precision targeting requirements** (tiny buttons, fine menus)
- **Reversibility that costs more than the original action**

The architecture refuses all six. The tape-as-always-recording model eliminates pre-declaration. The Constituent model eliminates mode confusion in the creative state. The retroactive ring eliminates timing punishment. Glanceable visualization eliminates demanding visual noise. Coarse controls eliminate precision targeting. Undo is the most accessible operation.

### 14.3 The system anticipates; the user decides

The system's intelligence lives in *anticipation*, not in *interpretation*. Anticipation means:

- The most likely next action is the most prominent affordance
- Default values are musically excellent for the common case
- Context shapes the affordances offered
- The system's guesses are *offers*, not *commitments*

The user is the final authority. The system's role is to make the right thing trivially available, then yield.

### 14.4 Two cognitive states, one continuous instrument

The performer operates in two cognitive states at different times:

- **Performance state**: creative, reflexive, fast. Eyes on the music, not on the screen. Calculation impossible.
- **Preparation state**: deliberative, analytical, fine-grained. Eyes on the screen. Time available for thought.

A trusted instrument serves both states without making the user choose a "mode." The transition between states is fluid; the system reads context (the user is touching detailed controls? They're in preparation. They just hit a footswitch? They're performing). **The instrument is one. The presentation adapts.**

### 14.5 Glanceable, not readable

Loop state in performance is communicated by *shape, color, position, and motion* — not by text. The performer registers state in peripheral vision while their primary attention is on the music.

Glanceable: which loops are playing, where in their cycle, how many repetitions remain, which loop has focus, whether the system is currently capturing.

Not glanceable (and should not demand attention): loop names, parameter values, storage usage, network status (visible only if degraded), tier information.

The performer reads in preparation, glances in performance.

### 14.6 Coarse and decisive

Performance controls are large, unambiguous, reflexive. **Same gesture, same outcome, always.** Preparation controls are fine, precise, detailed — available when the user wants them, invisible when they don't.

### 14.7 Undo is sacred

Undo is the permission slip for experimentation. A performer who knows they can undo will take more creative risks. Undo must therefore be:

- At least as accessible as any creative action
- Instantaneous in effect
- Multi-level, with depth limited only by storage
- Visible (the user knows what will be undone)
- Reversible (redo is equally accessible)

The Constituent-as-immutable architecture makes deep undo trivial.

### 14.8 Latency budget for trust

- **< 10ms**: action feels like one's own (proprioceptive integration)
- **10–30ms**: action feels like consequence (causal coupling)
- **30–100ms**: action feels like response
- **> 100ms**: action feels like waiting

Performance gestures must respond in the < 30ms range. The LMC and conceptual-time architecture give us this for audio. Visual and tactile responses must honor the same budget.

### 14.9 Eyes-free is the goal

The highest UI achievement for a performance looper is **eyes-free operation**: the performer never needs to look at the screen during a performance. Every gesture they need is reachable by trained reflex; every confirmation they need arrives through hearing. **A looper that requires the performer to look at it has failed at its highest job.** The screen is for preparation. The performance is for the music.

---

# Part XV — What This Architecture Enables

### 15.1 Capabilities unique to this architecture

The following follow directly from the architectural commitments above. None is available in any commercial looper as of this writing.

- **Retroactive capture.** The tape was running; in-points move backward.
- **Lossless multi-take.** Every attempt at every idea is preserved.
- **Imprecise capture without penalty.** Boundaries are refined in preparation.
- **Bit-exact archival reproducibility.** Same Constituent graph + same tapes = same output, forever.
- **Graceful ensemble degradation to solo recording.**
- **Cross-session continuity** of motifs and arrangements.
- **Effect parameter automation as native Constituent content.**
- **Recursive arrangement.** Sets, songs, sections, phrases, loops — same data model.
- **Polymetric coexistence.** Multiple meters in one phrase, no compromise.
- **Polytemporal coexistence.** Metric and unmetered material side by side.
- **Micro-timing preserved exactly.** Swing, feel, pocket — all captured at substrate precision.
- **Role-based phrase substitution.** Structured improvisation as a first-class capability.
- **Grammatical phrase relationships.** Call/response, theme/variation as data, not interpretation.
- **Drift-free synchronization at any timescale.** Hundredth cycle is as tight as the first.

### 15.2 What this architecture gives up

Honesty about trade-offs:

- **Initial complexity.** The conceptual model is more sophisticated than "press record, press play." First-time users need an onramp.
- **Storage footprint.** Continuous capture is more storage-intensive than capture-on-demand. Modern storage makes this affordable, but it is real.
- **Implementation effort.** Building the conceptual time engine, the Constituent hierarchy, the LMC discipline, the continuous ASRC, and the phrase-level UX is significantly more work than building a traditional looper. The payoff is in capability and longevity.
- **No real-time AI assistance** (today). Inference latencies do not support sub-30ms creative response. The architecture is AI-compatible, but the creative authority is the performer.

### 15.3 Open questions

The architecture as specified is internally consistent and complete enough to implement. The following questions remain genuinely open:

- **Optimal hardware control surface.** Footswitches, pads, knobs, touch — the right combination is likely use-case-specific.
- **Cross-platform consistency of the membrane.** CoreAudio, ASIO, WASAPI, JACK, Android, browser, embedded — implementations vary.
- **Video implementation maturity.** Audio looping in this architecture is well-understood. Video looping at performance latencies is less explored.
- **Phrase-relationship UX.** How grammatical relationships between phrases are surfaced for editing and visualization is an open UX problem.
- **Structured improvisation interfaces.** Role-fillable phrase slots are a powerful capability, but the UX for them at performance time is novel and untested.

### 15.4 Closing

This paper does not prescribe an implementation. It prescribes a way of thinking about loopers, grounded in respect for the performer, honesty about digital time, and trust in the user.

The hope is that other developers — those building loopers as commercial products, as open-source tools, as research prototypes — find in this paper a coherent reference point. Adopt the architecture, dispute it, refine it, replace it. The goal is not orthodoxy. The goal is better loopers.

**A phrase is a musical utterance. A loop is an idea. Ideas are worth repeating. Time is a concept, not a number. Trust the user. Build accordingly.**

---

# Appendix A — Glossary

**Absolute time** — A monotonic, high-resolution time reference at the membrane, against which numerical time is rendered from conceptual time.

**Anchor domain** — Whether a Constituent's boundaries are stored in absolute time or in some level of musical (conceptual) time.

**Anchor to parent** — How a Constituent aligns to its parent: at start, at end, at both, locked throughout, or free.

**ASRC (Async Sample Rate Conversion)** — Continuous resampling between the engine's canonical internal rate and the device's actual measured rate. Lives at the membrane.

**Capability tier** — One of four operating profiles selected at session startup based on hardware capability. Governs format, effect strategy, and resource budgets.

**Cardinality** — One of the five repetition dimensions. How many times a loop plays before stopping.

**Conceptual time** — The engine's internal time domain. Symbolic, hierarchical, exact by construction. Becomes numerical only at the membrane.

**Constituent** — The unifying abstraction for all musical objects: tape slices, loops, phrases, sections, songs, sets. All share a common structure (id, conceptual boundaries, local tempo map, anchor, repetition rules, metadata, children).

**CRDT (Conflict-Free Replicated Data Type)** — A data structure that replicates across nodes and merges without manual conflict resolution. The append-only-tape + immutable-Constituent architecture is naturally CRDT-compatible.

**Ensemble LMC** — The shared logical master clock across a distributed ensemble. Calibrated against the elected master node.

**Grammatical relationship** — A linked relationship between phrases (call/response, statement/variation, theme/development, tension/release, punctuation) stored as phrase metadata.

**Hierarchy of time domains** — The tree of nested conceptual time domains: session → song → section → phrase → loop → cycle. Each level may have its own tempo map and meter.

**Identity** — The persistent ID of a Constituent, surviving all content revisions. "The verse" remains the verse across edits.

**Inspiration** — The fragile creative state during which the performer captures musical ideas. Designed for above all other considerations.

**Intent** — A phrase's musical purpose, captured as metadata. Survives content revisions because identity is separate from content.

**LMC (Logical Master Clock)** — The software construct above all hardware clocks. The membrane's absolute-time reference. Disciplined by the best available source.

**Local LMC** — A node's own monotonic time reference, used for stamping local tape events.

**Loop** — A Constituent that directly references tape data. The mechanism by which content within a phrase may repeat. *Not* the unit of musical thought.

**Marzullo's algorithm** — The interval-intersection algorithm used to elect the LMC master across a distributed ensemble.

**Membrane** — The boundary between digital time and physical reality. The only place in the system where conceptual time becomes numerical time. Exists exactly once per node per direction (inbound and outbound).

**Mutation** — One of the five repetition dimensions. How a loop's content varies as it repeats. The art is in its invisibility.

**Phrase** — A complete musical utterance with its own identity, internal time domain, role, intent, entrance/exit characters, and relationship to other phrases. The unit of musical thought. May contain loops, non-looped tape slices, sub-phrases, silence.

**Polymetric** — Multiple meters coexisting in the same parent Constituent (e.g., 4/4 and 7/8 in the same phrase).

**Polytemporal** — Multiple time domains (metric and unmetered) coexisting in the same parent.

**Repetition rules** — The five-axis description of how a Constituent plays back: trigger, cardinality, phase, mutation, termination.

**Retroactive ring** — The in-memory portion of recent tape data, available for retroactive capture.

**Role** — A phrase's structural function (verse, chorus, response, fill, etc.). Makes phrases interchangeable by role for structured improvisation.

**Tape** — An append-only, immutable stream of timestamped events from a single input source. The source of truth.

**Tempo map** — A transformation between two conceptual time domains, parameterized by tempo and time signature events at boundaries.

**Tier dominance** — In distributed LMC election, a higher-quality discipline source beats any number of lower-quality sources.

**Trigger** — One of the five repetition dimensions. What event causes a Constituent to begin playing.

---

# Appendix B — Decision Log

The following decisions were made during the design process. They appear in roughly the order made, with brief justification. This log is offered for readers who want to understand *why* the architecture is what it is.

1. **Absolute time is the master timebase at the membrane**; audio and video are both flawed substrates rendered against it.
2. **A loop conceptually is a position interval over tape data**, not a sample count or frame count.
3. **Time-domain at the core, sample-domain at the membrane.** Continuous ASRC bridges the two.
4. **Latency compensation is architectural.** Every tape event carries its true capture time; one-time loopback calibration per device.
5. **Everything is always recorded.** The tape, not the loop, is the source of truth.
6. **All inputs are tapes** — audio, video, MIDI, control, parameter automation, system events — sharing one uniform event format.
7. **Tapes are append-only and immutable.** All editing happens elsewhere.
8. **Tapes are local; they never traverse the network** as primary data.
9. **System sizes itself once at startup**, locks a capability tier for the session.
10. **Capability tiers** (Lavish → Comfortable → Tight → Survival) govern format, effect strategy, ring depth, ASRC quality.
11. **Unbreakable rules**: audio never glitches; tape integrity is sacred; degradation is announced.
12. **The Logical Master Clock (LMC)** is a software construct above all hardware clocks.
13. **Discipline hierarchy**: GPS → PTP → NTP → Link → local CPU monotonic.
14. **Distributed LMC election** uses Marzullo interval-intersection, not median voting.
15. **Anchor node override**: musical authority outranks technical tier.
16. **Local LMC and ensemble LMC are distinct domains.**
17. **Mixing and monitoring are always local.** Audio that has crossed the network is no longer disciplinable.
18. **CRDT-compatible session state.**
19. **Graceful ensemble degradation to solo recording.**
20. **A loop is an idea; ideas are worth repeating.**
21. **Repetition is regenerator feedback** between performer and system.
22. **Five repetition dimensions**: trigger, cardinality, phase, mutation, termination.
23. **Mutation exists to sustain engagement, not to add complexity.** Its art is invisibility.
24. **Termination matches attention decay.**
25. **The looper trusts the user.**
26. **The looper's scope ends at the membrane.** Mixing, routing, effects topology are downstream.
27. **Arrangement is creation, not processing.** The looper handles arrangement natively.
28. **Inspiration is fragile** and is the design target.
29. **Defaults are sacred.**
30. **Reversibility replaces precision.**
31. **Latency budget for trust**: <30ms for consequence; <10ms for one's own.
32. **Two cognitive states, one continuous instrument.**
33. **Glanceable, not readable** during performance.
34. **Coarse decisive controls** for performance; **fine precise controls** for preparation.
35. **Undo is the most accessible operation.**
36. **Eyes-free operation is the highest live-performance UI goal.**

— *Subsequent decisions adding the phrase, conceptual-time, and polymetric layers:*

37. **A phrase is a musical utterance; a loop is a mechanism it may use.** Phrase is the unit of musical thought.
38. **A phrase may contain multiple loops, non-looped tape slices, and silence as content.**
39. **Loops within a phrase may live in different time domains** (polymetric and polytemporal coexistence).
40. **The Constituent is the unifying abstraction.** Tape slices, loops, phrases, sections, songs, sets all share one structure.
41. **The Constituent hierarchy** is recommended (loop → phrase → section → song → set) but not strictly enforced.
42. **Identity persists across content revision.** A phrase remains "the verse" through edits.
43. **Time is a concept, not a number.** The engine manipulates time symbolically; numerical time exists only at the membrane.
44. **The PPQ problem dissolves under conceptual time.** No shared grid is needed for tuplets, polyrhythm, or micro-timing.
45. **The hierarchy of time domains** — session, song, section, phrase, loop, cycle — each may have its own meter and tempo.
46. **Constituents in different time domains meet at parent boundaries**, not internally.
47. **Meter is a property of each Constituent**, not a global session constraint.
48. **Micro-timing is preserved exactly** because conceptual time has no quantization grid.
49. **The LMC's role is precisely defined**: it disciplines the membrane, not the engine's internal time.
50. **Editing is symbolic.** Operations on time are operations on conceptual structures, not on numbers.
51. **Phrases carry role and intent metadata** that persists across content edits.
52. **Phrases are interchangeable by role**, enabling structured improvisation.
53. **Grammatical relationships between phrases** (call/response, theme/variation, etc.) are first-class metadata.
54. **A song is a narrative, not a sequence.** Structurally complete, performatively variable.
55. **Arrangement happens at the phrase level and above**, not at the loop level.
56. **The set list is the outermost Constituent.** An entire concert is a single Constituent graph.

---

*End of Sirius Looper Whitepaper. Comments, criticism, and contributions welcome.*
