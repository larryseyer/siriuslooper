# Render path + durable tape store — design (Tier 0)

Status: approved design, ready for implementation planning.

## Context

IDA's Output Mixer is built (phrase channels, gain/pan/mute/meter, buses,
routing) but **its phrase channels are silent**: `RenderPipeline::activeReadsAt`
has no callers outside tests, there is no tape *reader* (the tape subsystem is
write-only), and `OutputMixer::setChannelAudioSource` is called only for OTTO
strips — phrase channels' source pointers stay `nullptr`. Every per-phrase
capability (level, pan, FX, sends, metering) therefore processes silence. This
is the load-bearing gap: until promoted phrase audio flows through the output
mixer, the "operator-testable mixer" goal is unmet on the output side, and the
render-to-parts/timeline/song end-goal cannot begin.

This slice closes that gap: the **playback-resolution path** that feeds phrase
channels from tape. Naming follows the whitepaper §6.11 caution — "render" is
reserved for the future materialize/bounce operation; this is the
playback-resolution (render-query) path.

Driven by the whitepaper: §3.6 (rendering pipeline), §5.2 + §6.6 ("everything
the output mixer renders is sourced from tape"), §8.1–8.5 (the tape), §15.2
(tiers), §17.8 (power loss and durability).

## Goal / success criteria

A phrase pill plays back through its Output-Mixer strip and its per-phrase meter
moves, with the playhead driven by OTTO's transport. **Audio only this sprint**;
the tape container is media-agnostic so MIDI/video/other media plug in next
sprint without reworking durability.

Done when (headless-verifiable): with a known loop placed at session conceptual
time mapped to LMC time T, advancing the playhead to T produces non-silent
samples on that phrase channel's source buffer, and crash-recovery + concurrent
read tests pass. Operator-verifiable: hit play, hear the pill, watch its meter.

## Resolved decisions

| Decision | Choice | Rationale |
|---|---|---|
| Playhead source | **OTTO transport** (positionInSeconds/beats + isPlaying) | Whitepaper §5.7 makes OTTO the transport/tempo-map source; IDA's LMC is pure time. Architecturally final, no throwaway. |
| Reader architecture | **Worker-driven streaming reader from disk** | §8.5 on-disk lossless tape = source of truth; FLAC/decode is not RT-safe → prefetch on a worker, hand the audio thread samples lock-free. RAM-only reader rejected (can't play committed/loaded content; not whitepaper-true). |
| Tape on-disk format | **Uniform self-delimiting checksummed record container, media-agnostic, per-type payload codecs** | §17.8 literal ("a sequence of self-delimiting records, each with a checksum"); §8.2 ("every signal is a tape"). FLAC is audio-only, so it can only be the *audio payload codec*, never the container. |
| First codec | **Audio** (FLAC block; PCM at Lavish) | The keystone is an audio phrase. Other media are the next sprint. |
| Slice split | **T0a (storage hardening) then T0b (reader + wiring)** | The reader reads from the on-disk container; the container must exist, be durable, and be concurrently readable first. |

## Architecture overview

```
Physical/file inputs → INPUT MIXER → [TAPE STORE] → OUTPUT MIXER → outputs
                                          ▲                ▲
                              T0a: durable record    T0b: playback step
                              container (write+read)  fills phrase scratch
                                                      from TapeReader, driven
                                                      by OTTO transport playhead
```

The write side (input mixer → tape) exists; T0a hardens its on-disk format and
adds the read foundation. T0b adds resolution + reader + the audio-callback
playback step that sources phrase channels.

---

## Phase T0a — Durable, media-agnostic tape store

### Record container (on disk)

The on-disk tape is an **append-only stream of self-delimiting records**. Each
record:

```
[ u32 length | u64 seq | u16 type-tag | conceptual-ts | lmc-ts
  | u32 checksum(header+payload) | payload bytes ]
```

- Append-only, immutable (§8.3): no record already on disk is ever mutated.
- `conceptual-ts` + `lmc-ts` per §8.2 (both timestamps on every record).
- `checksum` covers header + payload so a torn trailing record is detectable.

### Payload type registry (forward-thinking core)

A `type-tag` + a codec registry decouple the medium from the durability layer.
Tags reserved for audio, video, MIDI, control/automation/transport/system, and
**arbitrary future media** (text, HTML, docs — "media-agnostic looping"). A new
medium = a new tag + a codec implementing encode(payload)→bytes /
decode(bytes)→payload; **the container, flush, crash-recovery, index, and reader
machinery never change.**

**This sprint implements only the audio codec:** payload = one FLAC-compressed
block (PCM at the Lavish tier). Each record's payload is **independently
decodable** so the reader can random-access by record without decoding the whole
file.

### Flush cadence (durability)

Tier-driven per §17.8, operator-overridable (1–5000 ms):

| Tier | Flush interval | Worst-case power-loss |
|---|---|---|
| Lavish | per buffer (~1–3 ms) | one audio buffer |
| Comfortable | 50 ms | ≤50 ms |
| Tight | 200 ms | ≤200 ms |
| Survival | 1000 ms | ≤1 s |

The worker flushes/fsyncs completed records at the tier interval. Bit-exact
recovery is guaranteed up to the last fully-flushed record.

### Crash recovery

On reopen: scan records from the start (or last checkpoint), validate each
checksum, and **truncate at the first bad or partial trailing record**. The
recovered prefix is intact. An honest truncation report surfaces per §17.9 (no
silent degradation). Session manifest is written via atomic temp→fsync→rename so
the index is never half-written (§17.8).

### Concurrent random-access read

A per-tape **record index** (seq / time → file offset), maintained as records
are flushed, lets a reader read *finalized* records while the writer is still
appending. The live edge (records not yet flushed) is not yet readable — the lag
is one flush window (sub-second), invisible for looping playback of
already-running loops.

### RAM invariant

The retroactive ring stays volatile capture-reach-back scratch, **never a
playback source**. Only the bounded, tier-capped ring is resident; nothing
accumulates a whole tape in RAM, so arbitrarily large/long media never exhausts
memory. Disk is the sole playback source.

### Reuse + interfaces

- Reuse the existing audio-thread → SPSC queue → worker write architecture
  (`FlacTapeSink`). Generalize `ITapeSink` so delivery carries a typed payload
  (audio block today; other payloads later). The current monolithic-FLAC writer
  becomes the **audio codec** inside the record container.
- New components (engine/audio + persistence): the record writer (worker-owned,
  append + flush + index), the record reader (random-access by index, codec
  dispatch), the codec registry + audio codec, the crash-recovery scanner.

### T0a testing (headless TDD)

- Record round-trip: write N audio records → read back bit-identical payloads.
- Random access: read record K directly via the index without decoding 0..K-1.
- Crash recovery: append, corrupt/truncate the trailing record's bytes → reopen
  recovers the valid prefix and reports the truncation; checksum mismatch is
  caught.
- Concurrent read-during-write: reader reads flushed records while the writer
  appends new ones; reader never sees a partial record.
- Flush cadence: records become readable within the tier's flush window.

---

## Phase T0b — Playback-resolution path

### OTTO transport playhead

Add an RT-safe accessor on `OttoHost` exposing the current transport position
(`positionInSeconds`; beats available too) + `isPlaying`, published as an atomic
inside `renderBlock` after OTTO's conductor advances. This is the playhead.
Mapping: the session's `RenderPipeline` `sessionToLmc` TempoMap derives from
OTTO's tempo (§5.7); the playhead's LMC time = OTTO's position mapped through it.
**v1 uses OTTO's `positionInSeconds` directly as the LMC query time** (identity
calibration — the real loopback calibration is deferred, see Honest v1 limits).

### Resolution snapshot (worker / control thread)

`RenderPipeline::activeReadsAt(playheadLmc)` allocates and walks the Constituent
tree — **not** audio-thread safe. Run it off the audio thread at a control
cadence and publish a **lock-free snapshot** of active reads: for each sounding
loop, (tape id, tape-position, phrase channel, cycle). The snapshot is the
audio/worker boundary; the audio thread never walks the tree.

### TapeReader (worker prefetch)

Per active phrase channel, decode tape records from disk (via T0a's reader)
**ahead of the playhead** into a lock-free PCM ring. The worker computes exact
sample boundaries (loop spans, cycle arithmetic) when filling the ring, so the
audio thread consumes sequentially and playback stays sample-accurate.

### Audio-callback playback step

A new step in `AudioCallback::audioDeviceIOCallbackWithContext`, **between OTTO
render (Step 2b) and OutputMixer render (Step 3)**:

1. Read the playhead (atomic) and the active-reads snapshot (lock-free).
2. For each active read, pull decoded samples from that phrase channel's ring
   into the channel's **stable per-phrase scratch buffer**.
3. Channels with no active read leave their scratch zeroed (architectural
   silence).

The OutputMixer phrase channel's audio source points at that scratch buffer
(set once via `setChannelAudioSource`, exactly how the MON path already works).
The output side — ChannelStrip FX, sends, routing, metering — is already built;
the render path's job ends at filling the scratch.

No allocation, decode, I/O, locking, or tree-walk on the audio thread: only an
atomic read, a lock-free ring pull, and a memcpy.

### Honest v1 limits

- FreeRunning leaf loops only — `RenderPipeline`'s M3 reality; other triggers
  are correctly dormant (they await subsystems not in this slice).
- Identity device calibration — the real loopback calibration engine is
  separately deferred; v1 maps LMC seconds → sample index as
  `round(seconds * sampleRate)`.

### T0b testing (headless TDD)

- Transport accessor publishes monotonically while playing, holds while stopped.
- Resolution snapshot: a known tree at playhead T yields the expected active
  reads; off-thread, no audio-thread allocation.
- Reader random-access correctness against T0a records.
- End-to-end: place a known loop, advance the playhead to T → the phrase
  channel's scratch is non-silent and matches the recorded samples; advance past
  the loop → silence.
- RT-safety: the playback step allocates zero and locks zero (alloc-counting /
  inspection per RT_SAFETY_CONTRACT.md).

---

## RT-safety contract

Audio thread (playback step + OttoHost accessor): atomic reads, lock-free ring
pulls, memcpy into pre-allocated scratch — no `new`/`malloc`/container growth, no
locks, no I/O, no decode, no tree-walk, `noexcept`. All allocation, FLAC decode,
file I/O, and `activeReadsAt` tree-walks live on worker/message threads. Buffers
(scratch, rings) are sized once on the message thread in prepare.

## Explicitly deferred (next sprint — not this one)

- MIDI payload codec + MIDI playback through the output mixer.
- Video payload codec + video playback/display.
- Arbitrary-media codecs (text, HTML, docs) — the registry is ready; codecs are
  per-medium follow-ons.
- File-input transport-sync variants.
- The render-to-parts / timeline / finished-song arrangement engine (§6.11
  future direction).
- Real loopback device calibration (§3.6 step 4 beyond identity).

## Open questions for the plan stage

- Exact record-container byte layout + endianness + version field (pin in T0a's
  plan; choose a small explicit binary header).
- Whether the per-tape index is a sidecar file or reconstructed by scan on
  open (sidecar is faster to open; scan is simpler and crash-trivial — likely
  scan first, sidecar as an optimization).
- Resolution control cadence (per audio block via a control message vs a fixed
  ms timer) and prefetch lookahead depth (tie to tier ring depth, §15.2).
- Whether `FlacTapeSink` is refactored in place into the container's audio codec
  or replaced by a new writer that absorbs its SPSC/worker pattern.

## Verification

- `ctest` baseline stays green (828/829) plus the new T0a/T0b suites.
- `rm -rf build` clean rebuild before any operator GUI handoff.
- Operator: load a session with a captured loop, hit play (OTTO transport), hear
  the phrase through its output strip, see its per-phrase meter move.
