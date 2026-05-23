# Tape Subsystem Design — project tape pool + multi-tape routing

## Context

Phase 6 of the mixer routing graph (`2026-05-20-mixer-routing-graph-design.md`)
calls for a blank-area creation gesture whose menu reads **Add bus / Add FX
return / Add tape**, and a per-channel destination picker offering **bus / tape /
hardware output**. The engine supports the bus and FX-return halves (`addBus`,
`addFxReturn`, the bus-controls slice). It does **not** support the tape half:
`MixerGraph` holds a fixed terminal set decided at construction
(`MixerGraph.h:17` — "set at construction, never changes"), with a single `Tape`
terminal, and `InputMixer::setChannelMainOutToTape(ChannelId)` targets *the* tape
with no way to name *which* tape. There is no enumerable pool of tapes, no
tape-creation path, and production capture (`TapeWriter`) is not wired into
`MainComponent` at all (see the audit note at `MainComponent.cpp:1101`).

This document designs the tape subsystem those features need. It is grounded in:

- **Whitepaper §5.2** (line 349): "an input channel chooses per channel where its
  processed signal goes: to a **bus**, to a **tape**, or **direct to a hardware
  output**… Tape routing is itself flexible: **many hardware inputs may sum into a
  single tape, or each may take its own.**" The output mixer "routes only to
  outputs and buses, never to tape."
- **Whitepaper §6.2** (line 493): channel-layer destinations are plural — "tapes,
  buses, outputs."
- **`docs/design/mixer-design.md`**: "route single or combined inputs to a
  user-selected number of tapes… The operator chooses **how many tapes and which
  input(s) land on each**" (41–43); "**Tape count lives in Settings**… that pool
  of tapes" and "**Counts are independent**" (71–77); node main-out → "a bus or
  the terminal (tape on input…)" (112).
- **Operator decisions this session:** each project keeps a **list/tab of tapes**;
  a **minimum of ONE tape** is required, with **no maximum**; the user can create
  a new tape or route to an existing one; channels may go to buses first (not only
  directly to tapes); buses route into individual tapes.

## The model

A project has exactly **one tape pool** — an ordered list of tapes, **≥1 and
unbounded**. The same pool is surfaced in three views:

1. **Tapes tab/list** — manage the pool: create, rename, remove (with the ≥1
   floor enforced).
2. **Timeline / preparation pane** — arm and focus per tape (this view already
   exists: `armedTapeIds_`, `toggleArm`, `setFocused`, `CaptureSession`).
3. **Input Mixer** — route a node's main-out into a chosen tape.

There is **one** tape concept, not three parallel ones; the three surfaces read
and mutate the same pool.

**Routing.** Any node — an input channel **or** a bus — assigns its single
main-out to one of: a **bus**, a **specific tape** in the pool, or the **hardware
output** terminal (direct monitoring through the channel's processing). Many
nodes routed to the same tape **sum** into it; nodes routed to distinct tapes
record in **parallel**. Channels are not required to route directly to a tape —
`channel → bus → tape` is a first-class path. The output mixer never routes to a
tape (whitepaper §5.2); this subsystem's multi-tape terminal generalization
applies to the **input** mixer's graph only — the output mixer keeps its single
`HardwareOutput` terminal, behavior-preserving.

**Counts are independent.** Input-channel count, tape count, bus count, and phrase
count are unrelated: 16 inputs may share 1 tape; 4 inputs may each take their own.

## Relationship to existing types

- **`TapeId`** (`core/include/ida/TapeId.h`) — the existing `int64` value type
  identifying a tape. Reused unchanged as the pool's key.
- **`InputDescriptor`** (`core/include/ida/InputDescriptor.h`) — input-source
  metadata that carries a `tapeId` back-reference. An input descriptor *points at*
  a tape; it is **not** the pool. The pool is the authoritative list of tapes that
  exist; an input's `tapeId` names its default capture target within that pool.
  This subsystem does **not** repurpose `InputDescriptor` as the pool — the pool is
  a new, explicit list (counts are independent; you can have more tapes than
  inputs, or route many inputs to one tape).
- **`Tape<Payload>`** (`core/include/ida/Tape.h`) — the heavy, immutable
  event-stream data type. The pool stores **light metadata** (id + name), never
  the heavy stream, honoring the §7.2 data-layer / structure-layer split exactly
  as `InputDescriptor` does.
- **`MixerGraph`** (`engine/include/ida/MixerGraph.h`) — currently one
  implicit `Tape` terminal on the input side. Slice 2 generalizes the input
  mixer's instance to N `Tape` terminals.

## Decomposition

Four slices, each independently buildable, reviewable, and shippable, each its
own implementation plan and (per the operator's workflow) its own chat, with
`continue.md` updated between. Engine/model slices are headless TDD; the UI slice
is operator-verified.

### Slice 1 — Tape pool model + persistence (headless TDD) — **planned first**

The data foundation everything else reads. A pure, JUCE-free pool type in `core`
plus its `SessionFormat` serializer in `persistence`, mirroring the established
`MixerGraphState` (core type) + `serializeMixerGraphState` (persistence)
layering. **Detailed below; this is what the first implementation plan covers.**

### Slice 2 — Multi-tape routing engine (headless TDD)

Generalize the **input mixer's** `MixerGraph` from one implicit `Tape` terminal to
**N `Tape` terminals**, one per pooled tape (`HardwareOutput` stays singleton;
the output mixer's single-terminal graph is untouched and re-proven
behavior-preserving by its existing suite). Add/remove a tape terminal keyed by
`TapeId`; assign a node's main-out to a chosen tape terminal; enforce acyclic
routing and topological evaluation across the multiple tape terminals; deliver
each tape terminal's summed input to that tape. `InputMixer` API:
`setChannelMainOutToTape(ChannelId, TapeId)` and `setBusMainOutToTape(BusId,
TapeId)` (the existing no-arg overloads target the pool's primary tape, kept
behavior-preserving). Cover: per-tape delivery, summing of multiple nodes into one
tape, parallel distinct tapes, cycle rejection, topo order with >1 tape terminal,
stereo invariant, traversal RT-safety static-asserts, single-tape default
equivalence. Routing-graph **persistence** (slice 5 of the routing spec) extends
to record per-node tape-terminal assignments by `TapeId`.

### Slice 3 — Capture-to-disk wiring (headless TDD + operator eyes-on) — **"real recording"; explicitly in the path**

Wire the production capture path so routing a node to tape X **actually records**
into tape X. Construct and own a per-tape capture path in `MainComponent` (absent
today) implementing `ITapeSink`: one append-only **FLAC** stream per pooled tape,
written to `<IDA>/tapes/<tapeId>.flac` and **flushed continuously** (whitepaper
§8.5 "lossless on disk during the live session", §17.8 continuous flush). This is
the slice that turns the routing model from apparatus into behavior. Sequenced
**immediately** behind slices 1–2 (operator constraint: the plan must put real
recording in the path, not strand the routing model).

**On-disk format model (decided 2026-05-21 — whitepaper §8.5/§8.3/§17.8):**
- **RAM** capture is uncompressed PCM (the retroactive ring, §8.4) — instant
  reach-back, never compressed.
- **Disk, live** is append-only **FLAC** per `TapeId`. The audio thread enqueues
  raw float PCM to the SPSC queue (unchanged, RT-safe); the **worker/flush thread
  FLAC-encodes** — zero audio-thread cost. FLAC's per-frame CRC + sync codes are
  exactly §17.8's "self-delimiting checksummed records", so a crash truncates only
  the partial trailing frame. FLAC is **required** (not an optimization) because of
  storage limits on constrained / mobile (iOS) targets — an always-running PCM tape
  is ~0.5–1.4 GB/hr/tape.
- **A tape is immutable and part of the project from the instant bytes flow.**
  There is **no DAW-style "finalize / seal the take" event.** Arm/disarm and mark
  in/out create Constituents; they do not stop the tape (§8.4).
- **SHA-256 content-addressing + the `TapeId → contentHash` manifest are a
  session-close archival step (§8.5 "optional offline archival compression on
  session close") and are DEFERRED out of slice 3.** A content hash can only be
  computed once the file stops growing, and the always-running tape only stops at
  session close. Live tapes are identified on disk by `TapeId` (filename); the
  content-addressed `TapeStore` is the archival form, paired with the deferred
  M8 S7–8 tape-rotation/reachability work. (NOTE: the existing
  `InputMixer::finalizeChannel` → `TapeStore::store` per-channel path predates this
  decision and is NOT the slice-3 live path.)
- Archival render target must be **CAF or RF64/WAV64**, never classic WAV (4 GB
  cap) — only relevant at the deferred export stage.

### Slice 4 — Tapes UI (operator-verified)

The **Tapes tab/list** (create / rename / remove with the ≥1 floor) + the Input
Mixer per-node **destination picker** targeting a chosen tape + the blank-area
**"Add tape / use existing"** creation gesture (extends the existing
right-click/500 ms long-press plumbing in `InputMixerPane`). After this, the
original **Phase 6** (bus/FX-return strips) resumes with the tape model beneath
its destination picker.

---

## Slice 1 detail — Tape pool model + persistence

### Component: `TapeDescriptor` (core, header-only)

`core/include/ida/TapeDescriptor.h` — light, value-typed pool entry, parallel
to `InputDescriptor`. JUCE-free.

```cpp
#pragma once

#include "ida/TapeId.h"

#include <string>

namespace ida
{

/// One entry in the project tape pool: light metadata naming a tape that exists
/// as a capture destination. Parallel to InputDescriptor — honors the §7.2
/// data-layer / structure-layer split: the heavy Tape<Payload> stream does not
/// know about descriptors; this points at a tape by id and gives it a name.
struct TapeDescriptor
{
    TapeId      id;
    std::string name;

    bool operator== (const TapeDescriptor& other) const noexcept
    {
        return id == other.id && name == other.name;
    }
};

} // namespace ida
```

### Component: `TapePool` (core, header + cpp)

`core/include/ida/TapePool.h` + `core/src/TapePool.cpp` — owns the ordered
list, enforces the ≥1 invariant, allocates ids monotonically. Pure C++, JUCE-free,
fully headless-testable. Message-thread only (no audio-thread access in slice 1).

Invariants:
- **≥1 always.** A default-constructed pool holds exactly one tape
  (`TapeId{1}`, name `"Tape 1"`). `remove` refuses (returns `false`, no change)
  when it would drop the count to 0.
- **Unbounded max.** No upper cap.
- **Monotonic ids.** `nextId_` never rewinds; removing a tape does not free its id
  for reuse. Imported pools seed `nextId_` to one past the maximum present id.
- **Stable order.** `add` appends; `remove` erases in place; iteration order is
  insertion order. The first entry is the **primary** tape (the zero-arg
  `setChannelMainOutToTape` target in slice 2).

API:

```cpp
#pragma once

#include "ida/TapeDescriptor.h"
#include "ida/TapeId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ida
{

/// The project's pool of tapes — an ordered list, minimum one, unbounded
/// maximum. The single source of truth for "which tapes exist"; the timeline,
/// the Tapes tab, and the input mixer's routing all read it. Message-thread only.
class TapePool
{
public:
    /// Seeds exactly one tape (TapeId{1}, "Tape 1") so the ≥1 invariant holds
    /// from construction.
    TapePool();

    /// Constructs from an explicit list (used by deserialization). The list must
    /// be non-empty; ids must be unique. nextId_ is seeded past the max id.
    /// Throws std::invalid_argument on an empty list or duplicate ids.
    explicit TapePool (std::vector<TapeDescriptor> tapes);

    /// Appends a new tape with the given name and a freshly allocated id;
    /// returns the new id.
    TapeId add (std::string name);

    /// Removes the tape with the given id. Returns false (no change) if the id
    /// is unknown OR if removing it would leave the pool empty (the ≥1 floor).
    bool remove (TapeId id);

    /// Renames the tape with the given id. Returns false if the id is unknown.
    bool rename (TapeId id, std::string name);

    int                          count() const noexcept;
    const TapeDescriptor*        find (TapeId id) const noexcept; // nullptr if absent
    const TapeDescriptor&        at (int index) const;            // throws std::out_of_range
    const std::vector<TapeDescriptor>& tapes() const noexcept;

    /// The primary tape — the first entry; always valid (≥1 invariant).
    TapeId primary() const noexcept;

private:
    std::vector<TapeDescriptor> tapes_;
    std::int64_t                nextId_ { 1 };
};

} // namespace ida
```

### Component: persistence serializer (persistence)

Extend `persistence/include/ida/SessionFormat.h` + `.cpp` with a tape-pool
round-trip, mirroring the `serializeMixerGraphState` / `deserializeInputMixerGraphState`
pattern (a standalone JSON document, `juce::var`-based, reusing the file's
anonymous-namespace helpers).

```cpp
/// Serializes the tape pool to a JSON document. Round-trips exactly through
/// deserializeTapePool.
juce::String serializeTapePool (const TapePool& pool);

/// Reconstructs a tape pool from serializeTapePool's output. Throws
/// std::runtime_error on a malformed document. An empty/absent tape array is
/// rejected as malformed (the ≥1 invariant is a load-time contract too).
TapePool deserializeTapePool (const juce::String& json);
```

JSON shape (one object, `tapes` array of `{ id, name }`):

```json
{ "tapes": [ { "id": 1, "name": "Tape 1" }, { "id": 2, "name": "Drums" } ] }
```

Forward-compat: a caller loading a pre-tape-pool session (no tape-pool document on
disk) constructs a default `TapePool()` (single primary tape) rather than calling
`deserializeTapePool` — the same convention slice 5 of the routing spec uses for
missing graph sections. `deserializeTapePool` itself is strict: a present but
empty `tapes` array throws (the on-disk contract mirrors the in-memory ≥1
invariant).

### Testing strategy (slice 1)

New `tests/TapePoolTests.cpp` (`[tape-pool]`) and additions to the persistence
test target (`[sessionformat][tape-pool]`):

- Default construction holds exactly one tape (`TapeId{1}`, `"Tape 1"`);
  `primary()` returns it.
- `add` appends, returns a fresh monotonic id, grows `count()`, preserves order.
- `remove` of a non-primary tape succeeds and shrinks the pool.
- `remove` of the **last remaining** tape returns `false` and leaves the pool
  unchanged (the ≥1 floor — a falsifiable test: assert `count()==1` after the
  refused removal).
- `remove` of an unknown id returns `false`.
- After removing a tape, `add` allocates an id past the removed one (ids never
  reused).
- `rename` changes the name; unknown id returns `false`.
- The explicit-list ctor rejects an empty list and duplicate ids
  (`std::invalid_argument`).
- `serializeTapePool` → `deserializeTapePool` round-trips a multi-tape pool to an
  equal pool (compare `tapes()` element-wise via `TapeDescriptor::operator==`,
  and that `add` after import allocates past the max imported id).
- `deserializeTapePool` of a present-but-empty `tapes` array throws; of malformed
  JSON throws.

### Scope boundaries (slice 1)

- **In scope:** the `TapeDescriptor` value type, the `TapePool` class with the ≥1
  invariant and monotonic ids, and the `SessionFormat` round-trip.
- **Out of scope (later slices):** any `MixerGraph` change (slice 2); any
  `TapeWriter`/capture wiring (slice 3); any UI (slice 4); wiring the pool into
  `MainComponent`'s live state or the timeline (slice 4 surfaces it — slice 1
  ships the tested type + serializer, not its production wiring, exactly as the
  routing-graph phases shipped engine/persistence apparatus ahead of the UI).
```
