# Tape Subsystem Slice 2 — Multi-Tape Routing Engine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generalize the input mixer's routing graph from one implicit `Tape` terminal to N tape terminals keyed by `TapeId`, sum every node routed to the same tape, and deliver each tape's summed stereo block through an injectable capture-sink seam.

**Architecture:** Three cohesive engine layers, bottom-up. (1) `MixerGraph` gains *dynamic* terminal add/remove (it already supports multiple terminals at construction and orders all terminals last — only the dynamic mutators and the "set at construction" constraint are missing). (2) `InputMixer` keeps a `TapeId → terminal-node` registry (primary = `TapeId{1}`, matching `TapePool`'s default primary), adds `setChannelMainOutToTape(ChannelId, TapeId)` / `setBusMainOutToTape(BusId, TapeId)` (the no-arg overloads keep targeting the primary, behavior-preserving), and per-`TapeId` query helpers. (3) `renderInputGraph` accumulates all tape-routed nodes (channels *and* buses) into per-tape stereo mix buffers, then delivers each touched tape via a new `ITapeSink` interface — replacing the current per-node enqueue-to-`TapeWriter`. The single `TapeWriter` stays bound to the legacy `processBuffer` path; slice 3 wires real per-tape sinks behind `ITapeSink` in `MainComponent`.

**Tech Stack:** C++17, JUCE-free engine public headers, Catch2 tests, CMake/Ninja. RT-safety contract `docs/RT_SAFETY_CONTRACT.md` (audio-thread paths: `noexcept`, no alloc/lock/I/O/throw).

---

## Design decisions (locked — do not re-litigate during execution)

- **`MixerGraph` stays a pure topology layer — it does NOT learn about `TapeId`.** Multiple tape terminals are just multiple `Terminal` nodes of kind `MixerTerminal::Tape`. `InputMixer` owns the `TapeId → MixerNodeId` mapping. This preserves the graph's existing "topology layer above the DSP registries" boundary (`MixerGraph.h:36`).
- **The delivery seam is an `ITapeSink` interface, not the `TapeWriter`.** Slice 3's spec language is "one capture **sink** per pooled tape." Slice 2 sums per tape and calls `ITapeSink::deliverTapeBlock(TapeId, L, R, n)`; slice 3 implements that interface over real per-tape `TapeWriter`s in `MainComponent`. The render path's old `tapeWriter_` enqueue is removed (it can't express summing); `processBuffer` keeps `tapeWriter_` untouched.
- **`kMaxTapes = 64`** — a generous RT ceiling mirroring `kMaxInputBuses`. The pool is conceptually unbounded; the engine pre-allocates this many per-tape mix buffers. `addTape` past the cap fails loud (`jassertfalse`, return `false`), mirroring `addBus`.
- **Primary tape == `TapeId{1}`.** `TapePool`'s default-constructed primary is `TapeId{1}` (slice 1). `InputMixer`'s ctor-seeded primary tape terminal is mapped to `TapeId{1}` so the no-arg `setChannelMainOutToTape(ChannelId)` and the import path stay behavior-preserving.
- **Scope:** engine apparatus only. NO `MainComponent`/UI wiring (slice 4), NO real per-tape recording (slice 3), NO routing-graph persistence of tape assignments (routing-spec slice 5). `TapePool` itself is not constructed or owned by `InputMixer` — the eventual caller (slice 4) keeps the graph's tape terminals in sync with the pool via `addTape`/`removeTape`.

---

## File Structure

- **Create** `engine/include/ida/ITapeSink.h` — the per-tape capture-sink interface (audio-thread delivery contract). JUCE-free.
- **Modify** `engine/include/ida/MixerGraph.h` — declare `addTerminal` / `removeTerminal`; relax the "set at construction" comment.
- **Modify** `engine/src/MixerGraph.cpp` — implement `addTerminal` / `removeTerminal`.
- **Modify** `engine/include/ida/InputMixer.h` — include `TapeId.h` + `ITapeSink.h`; add the tape-terminal registry, the multi-tape main-out API, per-tape query helpers, `setTapeSink`, and per-tape mix-buffer scratch.
- **Modify** `engine/src/InputMixer.cpp` — implement all of the above; rewrite `renderInputGraph`'s tape delivery to sum-per-tape-then-sink; remove the now-dead `enqueueToTape`.
- **Modify** `tests/MixerGraphTests.cpp` — dynamic-terminal cases.
- **Modify** `tests/InputMixerTests.cpp` — multi-tape routing + the recording-fake-sink delivery cases; migrate the four existing `renderInputGraph` tape cases off `TapeWriter` onto the fake sink.

No CMake changes — every modified/created file is already inside a target (`ITapeSink.h` is a header consumed by the `engine` target via `InputMixer.h`; no new `.cpp`).

---

### Task 1: MixerGraph dynamic terminal add/remove

**Files:**
- Modify: `engine/include/ida/MixerGraph.h` (the terminal-kind comment at line 17; new declarations after `addNode`/`removeNode` block, ~line 75)
- Modify: `engine/src/MixerGraph.cpp` (new methods)
- Test: `tests/MixerGraphTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/MixerGraphTests.cpp` (before the final `}` if any namespace closes — these are free `TEST_CASE`s, place them at end of file):

```cpp
TEST_CASE ("MixerGraph::addTerminal mints a new terminal node usable as a main-out dest",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);          // primary tape terminal
    const auto primaryTape = g.terminalNode();
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);

    CHECK (tape2.isValid());
    CHECK (tape2 != primaryTape);
    CHECK (g.kindOf (tape2) == MixerNodeKind::Terminal);
    CHECK (g.contains (tape2));

    const auto ch = g.addNode (MixerNodeKind::Channel);
    CHECK (g.mainOutOf (ch) == primaryTape);     // default still the primary
    REQUIRE (g.setMainOut (ch, tape2));          // a second tape is a valid dest
    CHECK (g.mainOutOf (ch) == tape2);
}

TEST_CASE ("MixerGraph evaluationOrder keeps ALL terminals last with >1 tape terminal",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);
    const auto chA = g.addNode (MixerNodeKind::Channel);
    const auto chB = g.addNode (MixerNodeKind::Channel);
    REQUIRE (g.setMainOut (chB, tape2));

    const auto& order = g.evaluationOrder();
    // Both terminals must appear, and every non-terminal node must precede them.
    const auto idx = [&order] (MixerNodeId id) {
        for (std::size_t i = 0; i < order.size(); ++i) if (order[i] == id) return (int) i;
        return -1;
    };
    REQUIRE (idx (g.terminalNode()) >= 0);
    REQUIRE (idx (tape2) >= 0);
    CHECK (idx (chA) < idx (g.terminalNode()));
    CHECK (idx (chA) < idx (tape2));
    CHECK (idx (chB) < idx (tape2));
}

TEST_CASE ("MixerGraph::removeTerminal reassigns orphaned main-outs to the primary and refuses the primary",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);
    const auto primaryTape = g.terminalNode();
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);
    const auto ch = g.addNode (MixerNodeKind::Channel);
    REQUIRE (g.setMainOut (ch, tape2));

    SECTION ("removing a non-primary terminal succeeds and orphans fall back to primary")
    {
        CHECK (g.removeTerminal (tape2));
        CHECK_FALSE (g.contains (tape2));
        CHECK (g.mainOutOf (ch) == primaryTape);
    }
    SECTION ("removing the primary terminal is refused")
    {
        CHECK_FALSE (g.removeTerminal (primaryTape));
        CHECK (g.contains (primaryTape));
    }
    SECTION ("removing an unknown / non-terminal id is refused")
    {
        CHECK_FALSE (g.removeTerminal (ch));            // a registered node, not a terminal
        CHECK_FALSE (g.removeTerminal (MixerNodeId {})); // invalid
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target IdaTests 2>&1 | head -30`
Expected: COMPILE FAILURE — `addTerminal` / `removeTerminal` are not members of `MixerGraph`.

- [ ] **Step 3: Declare the new API in `MixerGraph.h`**

Change the comment at line 17 from:

```cpp
/// Which terminal this graph drives — set at construction, never changes.
enum class MixerTerminal { Tape, HardwareOutput };
```

to:

```cpp
/// A terminal kind. The primary terminal is fixed at construction; additional
/// terminals (e.g. one Tape terminal per pooled tape on the input side) may be
/// added and removed dynamically via addTerminal/removeTerminal.
enum class MixerTerminal { Tape, HardwareOutput };
```

Add these declarations immediately after the `int nodeCount() const noexcept;` line (currently line 75):

```cpp
    /// Mints an additional terminal node of the given kind and returns its id.
    /// Terminals are sinks: no main-out, always last in evaluationOrder(). Used
    /// by the input mixer to add a Tape terminal per pooled tape. Recomputes the
    /// evaluation order. Message-thread only.
    MixerNodeId addTerminal (MixerTerminal kind);

    /// Removes a terminal node. Returns false (no change) if the id is unknown,
    /// is not a terminal, or is the PRIMARY terminal (terminals_.front() — the
    /// removeNode fallback must always exist). Any node whose main-out pointed at
    /// the removed terminal falls back to the primary terminal. Recomputes the
    /// evaluation order. Message-thread only.
    bool removeTerminal (MixerNodeId node);
```

- [ ] **Step 4: Implement in `MixerGraph.cpp`**

Add after `removeNode` (currently ends at line 96):

```cpp
MixerNodeId MixerGraph::addTerminal (MixerTerminal kind)
{
    const MixerNodeId id { nextId_++ };
    terminals_.push_back (TerminalNode { id, kind });
    recomputeOrder();
    return id;
}

bool MixerGraph::removeTerminal (MixerNodeId node)
{
    if (! node.isValid() || ! isTerminal (node)) return false;
    if (node == terminals_.front().id)           return false; // primary is permanent

    terminals_.erase (std::remove_if (terminals_.begin(), terminals_.end(),
                                      [node] (const TerminalNode& t) { return t.id == node; }),
                      terminals_.end());

    // Orphaned main-outs fall back to the primary terminal (same policy as removeNode).
    for (auto& n : nodes_)
        if (n.mainOut == node) n.mainOut = terminals_.front().id;

    recomputeOrder();
    return true;
}
```

> Note: `terminals_` is `reserve()`d only to the construction count, so `addTerminal` may reallocate it. That is fine — `addTerminal`/`removeTerminal` are message-thread only and the audio thread reads `order_` (a separate, kMaxNodes-reserved vector), never `terminals_`. No RT-safety concern.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][terminal]"`
Expected: PASS (3 cases). Then `./build/tests/IdaTests "[mixer-graph]"` — all prior mixer-graph cases still PASS (no regression).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/MixerGraph.h engine/src/MixerGraph.cpp tests/MixerGraphTests.cpp
git commit -m "feat: MixerGraph dynamic terminal add/remove (tape subsystem slice 2)"
```

---

### Task 2: InputMixer tape-terminal registry + multi-tape main-out API

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/InputMixerTests.cpp` (free `TEST_CASE`s at end of file). They use only the routing/query API — no audio render yet:

```cpp
TEST_CASE ("InputMixer: a freshly constructed mixer has exactly the primary tape (TapeId 1)",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::TapeId;
    InputMixer mixer;
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.hasTape (TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (TapeId { 2 }));
}

TEST_CASE ("InputMixer: addTape registers a routable terminal; removeTape unregisters it; primary is permanent",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::TapeId;
    InputMixer mixer;

    CHECK (mixer.addTape (TapeId { 2 }));
    CHECK (mixer.tapeCount() == 2);
    CHECK (mixer.hasTape (TapeId { 2 }));
    CHECK_FALSE (mixer.addTape (TapeId { 2 }));   // duplicate refused

    CHECK (mixer.removeTape (TapeId { 2 }));
    CHECK_FALSE (mixer.hasTape (TapeId { 2 }));
    CHECK_FALSE (mixer.removeTape (TapeId { 99 })); // unknown refused
    CHECK_FALSE (mixer.removeTape (TapeId { 1 }));  // primary refused
    CHECK (mixer.tapeCount() == 1);
}

TEST_CASE ("InputMixer: a channel routes to a chosen tape; the no-arg overload targets the primary",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::InputId; using ida::SignalType; using ida::TapeId;
    InputMixer mixer;
    REQUIRE (mixer.addTape (TapeId { 2 }));
    const auto ch = mixer.addChannel (InputId { 1 }, SignalType::Audio);

    REQUIRE (mixer.setChannelMainOutToTape (ch, TapeId { 2 }));
    CHECK (mixer.channelMainOut (ch) == InputMixer::MainOutDest::Tape);
    CHECK (mixer.channelMainOutIsTape (ch, TapeId { 2 }));
    CHECK_FALSE (mixer.channelMainOutIsTape (ch, TapeId { 1 }));

    REQUIRE (mixer.setChannelMainOutToTape (ch));   // no-arg → primary
    CHECK (mixer.channelMainOutIsTape (ch, TapeId { 1 }));

    CHECK_FALSE (mixer.setChannelMainOutToTape (ch, TapeId { 99 })); // unknown tape refused
}

TEST_CASE ("InputMixer: a bus routes to a chosen tape via setBusMainOutToTape(BusId, TapeId)",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::BusId; using ida::BusConfig; using ida::TapeId;
    InputMixer mixer;
    REQUIRE (mixer.addTape (TapeId { 2 }));
    const auto bus = mixer.addBus (BusConfig { 2, "Sub", ida::BusKind::Bus });

    REQUIRE (mixer.setBusMainOutToTape (bus, TapeId { 2 }));
    CHECK (mixer.busMainOut (bus) == InputMixer::MainOutDest::Tape);
    CHECK (mixer.busMainOutIsTape (bus));                 // routed to *a* tape
    CHECK (mixer.busMainOutIsTape (bus, TapeId { 2 }));
    CHECK_FALSE (mixer.busMainOutIsTape (bus, TapeId { 1 }));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target IdaTests 2>&1 | head -30`
Expected: COMPILE FAILURE — `tapeCount` / `hasTape` / `addTape` / `removeTape` / the new overloads / `channelMainOutIsTape` are not members.

- [ ] **Step 3: Declare the new API in `InputMixer.h`**

Add the includes (top of file, with the other `sirius/` includes near line 1-11):

```cpp
#include "ida/TapeId.h"
```

Add `kMaxTapes` beside the other caps (after line 42 `static constexpr int kMaxInputBuses = 64;`):

```cpp
    static constexpr int kMaxTapes         = 64;
```

Add the tape-pool-routing API. Place after the existing main-out block (after `busMainOut` at line 65) — and ADD per-tape overloads/queries; keep the existing no-arg signatures:

```cpp
    // Multi-tape terminal registry (tape subsystem slice 2) -----------------
    /// Registers a Tape terminal for a pooled tape. The eventual owner (slice 4)
    /// keeps this in sync with the project TapePool. Returns false on a duplicate
    /// id or when kMaxTapes is exceeded. Message-thread only.
    bool addTape (TapeId);
    /// Unregisters a Tape terminal. Returns false for an unknown id or the
    /// primary tape (TapeId{1} — the permanent default). Nodes routed to the
    /// removed tape fall back to the primary tape. Message-thread only.
    bool removeTape (TapeId);
    int  tapeCount() const noexcept;
    bool hasTape (TapeId) const noexcept;

    /// Routes a node's main-out to a specific pooled tape. Returns false if the
    /// node or the tape is unknown, or the edge is invalid. The no-arg overloads
    /// (declared above) target the PRIMARY tape, behavior-preserving.
    bool setChannelMainOutToTape (ChannelId, TapeId);
    bool setBusMainOutToTape (BusId, TapeId);

    /// True iff the node's main-out targets this specific pooled tape.
    bool channelMainOutIsTape (ChannelId, TapeId) const noexcept;
    bool busMainOutIsTape (BusId, TapeId) const noexcept;
```

> `setChannelMainOutToTape (ChannelId)`, `setBusMainOutToTape (BusId)`, and `busMainOutIsTape (BusId)` already exist (lines 60, 63, 51). Keep them; they now mean "the primary tape" / "any tape" respectively (see Step 5 semantics).

In the private section, add the tape-terminal registry (after the `nextBusId_` line, ~line 201):

```cpp
    struct TapeTerminal { std::int64_t tapeId; MixerNodeId node; };
    std::vector<TapeTerminal> tapeTerminals_; // [0] = primary (TapeId 1); >= 1
```

Add private helpers (after `MixerNodeId nodeForChannel (ChannelId) const noexcept;`, ~line 204):

```cpp
    MixerNodeId tapeNodeFor (TapeId) const noexcept;        // invalid id if absent
    int         tapeSlotForNode (MixerNodeId) const noexcept; // -1 if not a tape terminal
```

- [ ] **Step 4: Seed the primary tape in the ctor (`InputMixer.cpp`)**

In the constructor (currently lines 29-38), after `buses_.reserve (...)`/`busNodeIds_.reserve (...)` and BEFORE the `addFxReturn` calls, seed the primary tape terminal mapping to the graph's ctor-created primary Tape terminal:

```cpp
    tapeTerminals_.reserve (static_cast<std::size_t> (kMaxTapes));
    tapeTerminals_.push_back ({ 1, graph_.terminalNode (MixerTerminal::Tape) });
```

- [ ] **Step 5: Implement the registry + routing in `InputMixer.cpp`**

Add the helpers near `nodeForBus`/`nodeForChannel` (~line 346-488):

```cpp
MixerNodeId InputMixer::tapeNodeFor (TapeId id) const noexcept
{
    for (const auto& t : tapeTerminals_)
        if (t.tapeId == id.value()) return t.node;
    return MixerNodeId {};
}

int InputMixer::tapeSlotForNode (MixerNodeId node) const noexcept
{
    for (std::size_t i = 0; i < tapeTerminals_.size(); ++i)
        if (tapeTerminals_[i].node == node) return static_cast<int> (i);
    return -1;
}

bool InputMixer::hasTape (TapeId id) const noexcept { return tapeNodeFor (id).isValid(); }
int  InputMixer::tapeCount() const noexcept { return static_cast<int> (tapeTerminals_.size()); }

bool InputMixer::addTape (TapeId id)
{
    if (hasTape (id)) return false;
    if (tapeTerminals_.size() >= static_cast<std::size_t> (kMaxTapes))
    {
        jassertfalse; // fail loud — silently dropping a tape terminal corrupts routing
        return false;
    }
    tapeTerminals_.push_back ({ id.value(), graph_.addTerminal (MixerTerminal::Tape) });
    return true;
}

bool InputMixer::removeTape (TapeId id)
{
    if (id == TapeId { 1 }) return false;            // primary is permanent
    const MixerNodeId node = tapeNodeFor (id);
    if (! node.isValid()) return false;
    if (! graph_.removeTerminal (node)) return false; // graph also refuses the primary
    tapeTerminals_.erase (std::remove_if (tapeTerminals_.begin(), tapeTerminals_.end(),
                                          [id] (const TapeTerminal& t)
                                          { return t.tapeId == id.value(); }),
                          tapeTerminals_.end());
    return true;
}
```

Add the per-tape main-out setters / queries near the existing one-line main-out cluster (~line 497-512):

```cpp
bool InputMixer::setChannelMainOutToTape (ChannelId ch, TapeId tape)
{ return graph_.setMainOut (nodeForChannel (ch), tapeNodeFor (tape)); }
bool InputMixer::setBusMainOutToTape (BusId bus, TapeId tape)
{ return graph_.setMainOut (nodeForBus (bus), tapeNodeFor (tape)); }

bool InputMixer::channelMainOutIsTape (ChannelId ch, TapeId tape) const noexcept
{
    const MixerNodeId node = tapeNodeFor (tape);
    return node.isValid() && graph_.mainOutOf (nodeForChannel (ch)) == node;
}
bool InputMixer::busMainOutIsTape (BusId bus, TapeId tape) const noexcept
{
    const MixerNodeId node = tapeNodeFor (tape);
    return node.isValid() && graph_.mainOutOf (nodeForBus (bus)) == node;
}
```

Generalize the existing `busMainOutIsTape (BusId)` (currently lines 353-358) from "primary tape only" to "any tape terminal", and generalize `classifyMainOut` (lines 490-495) to treat any tape terminal as `Tape`:

```cpp
bool InputMixer::busMainOutIsTape (BusId id) const noexcept
{
    const MixerNodeId node = nodeForBus (id);
    return node.isValid() && tapeSlotForNode (graph_.mainOutOf (node)) >= 0;
}
```

```cpp
InputMixer::MainOutDest InputMixer::classifyMainOut (MixerNodeId dest) const noexcept
{
    if (tapeSlotForNode (dest) >= 0)                                 return MainOutDest::Tape;
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput)) return MainOutDest::HardwareOutput;
    return MainOutDest::Bus;
}
```

> The no-arg `setChannelMainOutToTape (ChannelId)` (line 501-502) and `setBusMainOutToTape (BusId)` (line 507-508) already route to `graph_.terminalNode (MixerTerminal::Tape)` — which is `terminals_.front()`, i.e. the primary. `tapeTerminals_[0]` maps `TapeId{1}` to that same node, so the no-arg overloads and `channelMainOutIsTape (ch, TapeId{1})` agree. Leave the no-arg bodies unchanged.

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-mixer][multi-tape]"`
Expected: PASS (4 cases). Then `./build/tests/IdaTests "[input-mixer]"` and `"[sessionformat][mixer]"` — no regression (the export/import + persistence suites still pass; `mainOutSnapshot` still classifies the primary tape as `Terminal/Tape` because it compares against `terminalNode (MixerTerminal::Tape)` = primary).

> ⚠ Carry-forward note for slice 5: `mainOutSnapshot` (line 156-181) records a tape main-out as `MixerTerminalKind::Tape` without the specific `TapeId`. A node routed to a *non-primary* tape currently round-trips through persistence as the primary tape. Recording the `TapeId` is routing-spec slice 5's job (out of scope here). Add a `todo.md` entry for it in this task's commit.

- [ ] **Step 7: Add the carry-forward to `todo.md` and commit**

Append to `todo.md`:

```
### 2026-05-21 - Tape subsystem slice 2 carry-forward
- Files: engine/src/InputMixer.cpp (mainOutSnapshot), core/include/ida/MixerGraphState.h
- What was deferred: persisting WHICH tape a node's main-out targets. mainOutSnapshot
  records only MixerTerminalKind::Tape (no TapeId); a non-primary tape route round-trips
  as the primary on load.
- Why deferred: per-node tape-terminal persistence is routing-spec slice 5, out of slice-2 scope.
- What's needed to finish: add a tapeId field to MixerMainOut (Terminal kind) + serialize it;
  applyChannelMainOut/applyBusMainOut route to setChannelMainOutToTape(id, tapeId).
```

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp todo.md
git commit -m "feat: InputMixer multi-tape terminal registry + per-tape routing API (tape subsystem slice 2)"
```

---

### Task 3: Per-tape summing + capture-sink delivery in renderInputGraph

**Files:**
- Create: `engine/include/ida/ITapeSink.h`
- Modify: `engine/include/ida/InputMixer.h` (include, `setTapeSink`, sink member, per-tape scratch)
- Modify: `engine/src/InputMixer.cpp` (rewrite tape delivery in `renderInputGraph`; remove `enqueueToTape`)
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Create the sink interface**

`engine/include/ida/ITapeSink.h`:

```cpp
#pragma once

#include "ida/TapeId.h"

namespace ida
{

/// Per-tape capture-sink seam (tape subsystem slice 2). The input mixer sums
/// every node routed to a given tape and delivers ONE stereo block per tape per
/// audio block. Slice 3 implements this over real per-tape TapeWriters in
/// MainComponent; tests implement a recording fake.
///
/// RT-safety: deliverTapeBlock is called on the audio thread. Implementations
/// MUST be noexcept and allocation/lock/I/O-free (docs/RT_SAFETY_CONTRACT.md).
class ITapeSink
{
public:
    virtual ~ITapeSink() = default;

    /// `left`/`right` are `numSamples` non-interleaved post-mix samples for the
    /// pooled tape `tape`. Called at most once per tape per block, only for tapes
    /// that received signal.
    virtual void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                   int numSamples) noexcept = 0;
};

} // namespace ida
```

- [ ] **Step 2: Write the failing tests**

Add to the top of `tests/InputMixerTests.cpp` (with the other includes):

```cpp
#include "ida/ITapeSink.h"
```

Add a recording fake + the delivery cases (free `TEST_CASE`s at end of file). The fake sums into per-tape accumulators so summing is observable:

```cpp
namespace
{
    struct RecordingTapeSink : ida::ITapeSink
    {
        struct Block { std::int64_t tapeId; std::vector<float> left, right; };
        std::vector<Block> blocks;

        void deliverTapeBlock (ida::TapeId tape, const float* l, const float* r,
                               int n) noexcept override
        {
            // Test-only: vector ops here are NOT on a real audio thread.
            blocks.push_back ({ tape.value(),
                                std::vector<float> (l, l + n),
                                std::vector<float> (r, r + n) });
        }

        const Block* find (std::int64_t tapeId) const
        {
            for (const auto& b : blocks) if (b.tapeId == tapeId) return &b;
            return nullptr;
        }
    };

    // Builds a mixer with one stereo channel sourcing device ch 0/1, returns its id.
    ida::ChannelId addStereoChannel (ida::InputMixer& mixer, int leftDev, int rightDev)
    {
        using ida::InputId; using ida::SignalType;
        const auto ch = mixer.addChannel (InputId { 1 }, SignalType::Audio);
        mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
        mixer.setChannelInputSource (ch, leftDev, rightDev, true);
        return ch;
    }
}

TEST_CASE ("renderInputGraph: a tape-routed channel is delivered to that tape via the sink",
           "[input-mixer][multi-tape][render]")
{
    using ida::InputMixer; using ida::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto ch = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (ch)); // primary tape (1)

    constexpr int n = 8;
    std::vector<float> l (n, 0.5f), r (n, 0.25f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);                 // delivered to the primary tape
    REQUIRE (b->left.size() == (std::size_t) n);
    CHECK (b->left[0] == Catch::Approx (0.5f));   // stereo invariant: L and R preserved
    CHECK (b->right[0] == Catch::Approx (0.25f));
    CHECK (sink.blocks.size() == 1);        // only the one tape received
}

TEST_CASE ("renderInputGraph: two channels on one tape SUM into a single delivery",
           "[input-mixer][multi-tape][render]")
{
    using ida::InputMixer;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto chA = addStereoChannel (mixer, 0, 1);
    const auto chB = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (chA)); // both → primary tape
    REQUIRE (mixer.setChannelMainOutToTape (chB));

    constexpr int n = 4;
    std::vector<float> l (n, 0.3f), r (n, 0.1f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    REQUIRE (sink.blocks.size() == 1);       // ONE summed delivery, not two
    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    CHECK (b->left[0]  == Catch::Approx (0.6f)); // 0.3 + 0.3 summed
    CHECK (b->right[0] == Catch::Approx (0.2f)); // 0.1 + 0.1 summed
}

TEST_CASE ("renderInputGraph: channels on distinct tapes record in parallel",
           "[input-mixer][multi-tape][render]")
{
    using ida::InputMixer; using ida::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);
    REQUIRE (mixer.addTape (TapeId { 2 }));

    const auto chA = addStereoChannel (mixer, 0, 1);
    const auto chB = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (chA, TapeId { 1 }));
    REQUIRE (mixer.setChannelMainOutToTape (chB, TapeId { 2 }));

    constexpr int n = 4;
    std::vector<float> l (n, 0.4f), r (n, 0.4f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    CHECK (sink.blocks.size() == 2);
    REQUIRE (sink.find (1) != nullptr);
    REQUIRE (sink.find (2) != nullptr);
    CHECK (sink.find (1)->left[0] == Catch::Approx (0.4f)); // not summed across tapes
    CHECK (sink.find (2)->left[0] == Catch::Approx (0.4f));
}

TEST_CASE ("renderInputGraph: channel -> bus -> tape delivers the bus output to the chosen tape",
           "[input-mixer][multi-tape][render]")
{
    using ida::InputMixer; using ida::BusId; using ida::BusConfig; using ida::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto bus = mixer.addBus (BusConfig { 2, "Sub", ida::BusKind::Bus });
    const auto ch  = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToBus (ch, bus));
    REQUIRE (mixer.setBusMainOutToTape (bus)); // bus → primary tape

    constexpr int n = 4;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);                  // the bus's output reached the tape
    CHECK (b->left[0] == Catch::Approx (0.5f));
}

TEST_CASE ("renderInputGraph: with no sink bound, tape-routed signal is dropped without crashing",
           "[input-mixer][multi-tape][render]")
{
    using ida::InputMixer;
    InputMixer mixer; // no setTapeSink
    const auto ch = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (ch));

    constexpr int n = 4;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n); // must not crash
    SUCCEED();
}
```

> `Catch::Approx` requires `#include <catch2/catch_approx.hpp>` if not already present — check the top of `InputMixerTests.cpp`; the file already uses Catch2, and other tests in the suite use `Approx`, so the include is already there. If a build error says `Approx` is undefined, add `#include <catch2/catch_approx.hpp>`.

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build build --target IdaTests 2>&1 | head -30`
Expected: COMPILE FAILURE — `setTapeSink` is not a member of `InputMixer`.

- [ ] **Step 4: Declare the sink + per-tape scratch in `InputMixer.h`**

Add the include (with the other `sirius/` includes):

```cpp
#include "ida/ITapeSink.h"
```

Add the setter beside the other injected collaborators (after `setNotificationBus`, ~line 106):

```cpp
    /// Injects the per-tape capture sink (tape subsystem slice 2). Set-once on
    /// the message thread before the audio device starts; non-owning. When unset,
    /// renderInputGraph drops tape-routed signal (no capture). Slice 3 binds a
    /// real per-tape sink in MainComponent.
    void setTapeSink (ITapeSink* sink) noexcept;
```

Add the member beside the other injected pointers (after `notificationBus_`, ~line 221):

```cpp
    ITapeSink* tapeSink_ { nullptr };
```

Replace the `enqueueToTape` declaration (line 214) with nothing (it is being removed — see Step 6), and add the per-tape mix-buffer scratch declaration beside `scratchLeft_`/`scratchRight_` (after line 240):

```cpp
    // Per-tape summing scratch for renderInputGraph — kMaxTapes rows of
    // kMaxScratchSamples, pre-allocated in the constructor. Each pooled tape's
    // slot accumulates every node routed to it; the touched slots are delivered
    // once per block via tapeSink_. Indexed by tape-terminal slot
    // (tapeTerminals_ order). RT-safe: never resized after construction.
    std::vector<std::vector<float>> tapeMixLeft_;
    std::vector<std::vector<float>> tapeMixRight_;
    std::vector<char>               tapeTouched_; // per-slot "received signal" flag
```

Keep the `accumulateIntoBus` declaration. Add one private helper declaration near it:

```cpp
    void accumulateIntoTape (int slot, const float* left, const float* right,
                             float level, int numSamples) noexcept;
```

- [ ] **Step 5: Pre-allocate the per-tape scratch in the ctor (`InputMixer.cpp`)**

In the constructor initializer list (lines 30-32), add the per-tape buffers. Change:

```cpp
InputMixer::InputMixer()
    : processingScratch_ (kMaxScratchSamples, 0.0f),
      scratchLeft_ (kMaxScratchSamples, 0.0f),
      scratchRight_ (kMaxScratchSamples, 0.0f)
{
```

to:

```cpp
InputMixer::InputMixer()
    : processingScratch_ (kMaxScratchSamples, 0.0f),
      scratchLeft_ (kMaxScratchSamples, 0.0f),
      scratchRight_ (kMaxScratchSamples, 0.0f),
      tapeMixLeft_  (static_cast<std::size_t> (kMaxTapes), std::vector<float> (kMaxScratchSamples, 0.0f)),
      tapeMixRight_ (static_cast<std::size_t> (kMaxTapes), std::vector<float> (kMaxScratchSamples, 0.0f)),
      tapeTouched_  (static_cast<std::size_t> (kMaxTapes), 0)
{
```

Add the setter near the other set-once setters (after line 45):

```cpp
void InputMixer::setTapeSink (ITapeSink* sink) noexcept { tapeSink_ = sink; }
```

- [ ] **Step 6: Rewrite tape delivery in `renderInputGraph`; remove `enqueueToTape`**

Add `accumulateIntoTape` near `accumulateIntoBus` (after line 563):

```cpp
void InputMixer::accumulateIntoTape (int slot, const float* left, const float* right,
                                     float level, int numSamples) noexcept
{
    if (slot < 0 || slot >= static_cast<int> (tapeMixLeft_.size())) return;
    float* tl = tapeMixLeft_[static_cast<std::size_t> (slot)].data();
    float* tr = tapeMixRight_[static_cast<std::size_t> (slot)].data();
    for (int s = 0; s < numSamples; ++s) { tl[s] += left[s] * level; tr[s] += right[s] * level; }
    tapeTouched_[static_cast<std::size_t> (slot)] = 1;
}
```

Rewrite `renderInputGraph` (lines 565-667). The structure stays the same; only the tape branches change (accumulate instead of enqueue) and a final delivery step is added. Replace the whole function body with:

```cpp
void InputMixer::renderInputGraph (const float* const* deviceIn, int numDeviceChannels,
                                   float* const* directOut, int numDirectOutChannels,
                                   int numSamples) noexcept
{
    if (deviceIn == nullptr || numDeviceChannels <= 0 || numSamples <= 0) return;
    const int n = std::min (numSamples, static_cast<int> (kMaxScratchSamples));

    const MixerNodeId hwNode = graph_.terminalNode (MixerTerminal::HardwareOutput);

    // Zero only the active tape slots; clear their touched flags.
    const std::size_t tapeSlots = tapeTerminals_.size();
    for (std::size_t i = 0; i < tapeSlots; ++i)
    {
        std::memset (tapeMixLeft_[i].data(),  0, static_cast<std::size_t> (n) * sizeof (float));
        std::memset (tapeMixRight_[i].data(), 0, static_cast<std::size_t> (n) * sizeof (float));
        tapeTouched_[i] = 0;
    }

    // ── Steps 1–2: per channel, gather → strip → route main-out + sends ──
    for (const auto& [chValue, source] : channelSources_)
    {
        auto chIt = channels_.find (chValue);
        if (chIt == channels_.end()) continue;
        const auto& channel = chIt->second;
        if (channel.signalType != SignalType::Audio || channel.processing == nullptr) continue;

        const int leftCh  = source.left;
        const int rightCh = source.stereo ? source.right : source.left;
        if (leftCh  < 0 || leftCh  >= numDeviceChannels) continue;
        if (rightCh < 0 || rightCh >= numDeviceChannels) continue;
        if (deviceIn[leftCh] == nullptr || deviceIn[rightCh] == nullptr) continue;

        const auto byteCount = static_cast<std::size_t> (n) * sizeof (float);
        std::memcpy (scratchLeft_.data(),  deviceIn[leftCh],  byteCount);
        std::memcpy (scratchRight_.data(), deviceIn[rightCh], byteCount);

        float* stereo[2] { scratchLeft_.data(), scratchRight_.data() };
        auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (channel.processing.get());
        strip->process (stereo, 2, n); // also updates peak/LUFS meters

        const MixerNodeId chNode = nodeForChannel (channel.id);
        const MixerNodeId dest   = graph_.mainOutOf (chNode);
        const int tapeSlot       = tapeSlotForNode (dest);

        if (tapeSlot >= 0)
        {
            if (channel.tapeMode != TapeMode::NoTape)
                accumulateIntoTape (tapeSlot, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }
        else if (dest == hwNode)
        {
            for (int c = 0; c < std::min (numDirectOutChannels, 2); ++c)
            {
                float* o = directOut[c];
                if (o == nullptr) continue;
                const float* src = (c == 0) ? scratchLeft_.data() : scratchRight_.data();
                for (int s = 0; s < n; ++s) o[s] += src[s];
            }
        }
        else // a bus
        {
            accumulateIntoBus (dest, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }

        for (const auto& e : graph_.sendEdges())
        {
            if (e.source != chNode) continue;
            accumulateIntoBus (e.fxReturn, scratchLeft_.data(), scratchRight_.data(), e.level, n);
        }
    }

    // ── Step 3: process each bus / FX return into its main-out destination ──
    for (const MixerNodeId nodeId : graph_.evaluationOrder())
    {
        std::size_t busIdx = busNodeIds_.size();
        for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
            if (busNodeIds_[i] == nodeId) { busIdx = i; break; }
        if (busIdx >= busNodeIds_.size()) continue; // channel or terminal node

        const Bus& bus        = buses_[busIdx];
        const MixerNodeId dest = graph_.mainOutOf (nodeId);
        const int tapeSlot     = tapeSlotForNode (dest);

        if (tapeSlot >= 0)
        {
            std::memset (scratchLeft_.data(),  0, static_cast<std::size_t> (n) * sizeof (float));
            std::memset (scratchRight_.data(), 0, static_cast<std::size_t> (n) * sizeof (float));
            float* sc[2] { scratchLeft_.data(), scratchRight_.data() };
            bus.process (sc, 2, n);
            accumulateIntoTape (tapeSlot, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }
        else if (dest == hwNode)
        {
            float* dp[2] { (numDirectOutChannels > 0 ? directOut[0] : nullptr),
                           (numDirectOutChannels > 1 ? directOut[1] : nullptr) };
            bus.process (dp, std::min (numDirectOutChannels, 2), n);
        }
        else // another bus
        {
            for (std::size_t di = 0; di < busNodeIds_.size(); ++di)
            {
                if (busNodeIds_[di] != dest) continue;
                float* dp[2] { buses_[di].mixBufferChannel (0), buses_[di].mixBufferChannel (1) };
                bus.process (dp, 2, n);
                break;
            }
        }
    }

    // ── Step 4: deliver each tape that received signal, summed, once. ──
    if (tapeSink_ != nullptr)
        for (std::size_t i = 0; i < tapeSlots; ++i)
            if (tapeTouched_[i])
                tapeSink_->deliverTapeBlock (TapeId { tapeTerminals_[i].tapeId },
                                             tapeMixLeft_[i].data(),
                                             tapeMixRight_[i].data(), n);
}
```

Delete the `enqueueToTape` definition (lines 527-549) entirely — it is now unused (only `renderInputGraph` called it; `processBuffer` builds its message inline).

- [ ] **Step 7: Run the new tests + the noexcept static_assert + full input-mixer suite**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-mixer][multi-tape][render]"`
Expected: PASS (6 cases). The existing `renderInputGraph must be noexcept` static_assert (InputMixerTests.cpp:36) still compiles — `accumulateIntoTape` and `deliverTapeBlock` are both `noexcept`.

> ⚠ Compile guard: `enqueueToTape` removal must not leave a dangling declaration — confirm it was removed from BOTH `InputMixer.h` (Step 4) and `InputMixer.cpp` (Step 6). `grep -rn enqueueToTape engine/ tests/` must return zero hits.

- [ ] **Step 8: Migrate the four pre-existing renderInputGraph tape cases off TapeWriter**

The pre-slice-2 cases at `tests/InputMixerTests.cpp` lines ~599, 634, 673, 708 assert tape delivery via a real `TapeWriter` + `flushChannel`. The render path no longer writes to `TapeWriter`, so these must move to the fake sink. Rewrite them:

`"renderInputGraph: default-graph tape-routed channel enqueues its processed block"` (~599) → assert via `RecordingTapeSink`: the channel's processed block is delivered to tape 1. Replace the `TapeWriter writer (...)` + `mixer.setTapeWriter (&writer)` + `flushChannel`/`partial` assertions with:

```cpp
    RecordingTapeSink sink; mixer.setTapeSink (&sink);
    // ... existing channel setup + renderInputGraph call ...
    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    CHECK (b->left.size() == (std::size_t) n);
```

`"renderInputGraph: a channel routed to the hardware output sums into direct-out, not tape"` (~634) → keep the direct-out assertions; replace the `CHECK_FALSE (partial.existsAsFile())` (and the `TapeWriter`) with a `RecordingTapeSink sink; mixer.setTapeSink (&sink);` and `CHECK (sink.blocks.empty()); // routed to hardware output, not tape`.

`"renderInputGraph: channel -> bus -> tape delivers the bus output to tape"` (~673) → this is now covered by the new Task-3 test of the same intent; if it duplicates, DELETE the old one (DRY). If it asserts anything the new test does not (e.g. a specific block size), fold that assertion into the new test instead. Do not keep two tests covering the identical path.

`"renderInputGraph: a channel send reaches an FX return, which delivers to direct-out"` (~708) → this case routes the channel to tape (`setChannelMainOutToTape (ch)`) only as a side condition; its real assertion is on direct-out from the FX-return send. It uses no `TapeWriter`/sink for the tape leg, so leave its assertions intact — just confirm it still passes (no sink bound means the tape leg is a harmless drop).

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-routing][render]" "[input-mixer]"`
Expected: PASS — all render cases green, no `TapeWriter`-backed tape assertions remain on the render path.

- [ ] **Step 9: Commit**

```bash
git add engine/include/ida/ITapeSink.h engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: per-tape summing + ITapeSink delivery in renderInputGraph (tape subsystem slice 2)"
```

---

### Task 4: Full-suite verification + handoff

**Files:** none (verification + docs)

- [ ] **Step 1: Clean rebuild**

Per project rule (CLAUDE.md), a clean rebuild catches stale CMake config:

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
```

Expected: builds clean, no warnings (engine is `-Werror`).

- [ ] **Step 2: Run the full suite**

Run: `ctest --test-dir build`
Expected: all pass except the one documented Not-Run sentinel (`MainComponentPluginEditorTests_NOT_BUILT`, run separately by `bash/test-s7.sh`). Record the new count (baseline before slice 2 was 540/541; this slice adds ~13 cases: 3 `[mixer-graph][terminal]` + 4 `[input-mixer][multi-tape]` + 6 `[input-mixer][multi-tape][render]`, minus any deleted duplicate from Task 3 Step 8).

- [ ] **Step 3: Deferral grep**

Run: `grep -rnE "TODO|FIXME|XXX|stub|placeholder" engine/include/ida/ITapeSink.h engine/include/ida/MixerGraph.h engine/src/MixerGraph.cpp engine/include/ida/InputMixer.h engine/src/InputMixer.cpp`
Expected: zero hits (or every hit accounted for in `todo.md`).

- [ ] **Step 4: Push (authorized per memory)**

```bash
git push origin master
```

- [ ] **Step 5: Update `continue.md`**

Refresh the RESUME HERE block: slice 2 SHIPPED (list the 3 commits + ctest count + clean-rebuild green), next = **slice 3 (capture-to-disk wiring — "real recording")** with its first moves: implement `ITapeSink` over real per-tape `TapeWriter`s owned by `MainComponent`, one sink per pooled `TapePool` tape, writing per-tape partials and finalizing into `TapeStore` under `<IDA>/tapes`, establishing the `TapeId → content` manifest seam. Note the slice-5 carry-forward (persisting which tape a node targets) recorded in `todo.md`.

---

## Self-Review

**Spec coverage** (against `2026-05-21-tape-subsystem-design.md` slice 2, lines 96-111):
- "Generalize MixerGraph from one implicit Tape terminal to N Tape terminals" → Task 1 (`addTerminal`/`removeTerminal`) + Task 2 (`addTape`/`removeTape` registry).
- "HardwareOutput stays singleton" → unchanged; `addTape` only ever adds `MixerTerminal::Tape`.
- "output mixer's single-terminal graph is untouched and re-proven behavior-preserving" → Task 2 Step 6 runs the OutputMixer + persistence suites; OutputMixer is never modified.
- "Add/remove a tape terminal keyed by TapeId" → Task 2 `addTape`/`removeTape`.
- "assign a node's main-out to a chosen tape terminal" → Task 2 `setChannelMainOutToTape(ch,tape)` / `setBusMainOutToTape(bus,tape)`.
- "enforce acyclic routing and topological evaluation across multiple tape terminals" → reuses the existing `MixerGraph` acyclic+Kahn machinery (terminals are sinks with no out-edges, so they can never close a cycle); Task 1 test "evaluationOrder keeps ALL terminals last with >1 tape terminal" proves the topo order.
- "deliver each tape terminal's summed input to that tape" → Task 3 (`accumulateIntoTape` + `ITapeSink::deliverTapeBlock`).
- "the existing no-arg overloads target the pool's primary tape, kept behavior-preserving" → Task 2 Step 5 note; the primary-tape seeding in the ctor.
- Test coverage list ("per-tape delivery, summing, parallel distinct tapes, cycle rejection, topo order with >1 tape terminal, stereo invariant, traversal RT-safety static-asserts, single-tape default equivalence"): per-tape delivery + summing + parallel = Task 3 cases; cycle rejection = reuse + Task 1 (terminals can't cycle); topo order = Task 1; stereo invariant = Task 3 case 1 (L≠R preserved); RT-safety static-assert = the existing InputMixerTests.cpp:36 assert still holds (verified Task 3 Step 7); single-tape default equivalence = Task 3 case 1 routes to the lone primary tape and the migrated case at ~599.
- "Routing-graph persistence extends to record per-node tape-terminal assignments by TapeId" → explicitly OUT of slice 2 (routing-spec slice 5); carry-forward recorded in `todo.md` (Task 2 Step 7).

**Placeholder scan:** no TBD/TODO/"handle edge cases"/"similar to" in any step; every code step shows complete code.

**Type consistency:** `addTerminal`/`removeTerminal` (Task 1) ↔ `addTape`/`removeTape` (Task 2) call them with matching signatures; `tapeSlotForNode`/`tapeNodeFor` (Task 2) used by `classifyMainOut`, `busMainOutIsTape`, and `renderInputGraph` (Task 3) with the declared signatures; `accumulateIntoTape(int slot, …)` declared (Task 3 Step 4) and defined (Task 3 Step 6) identically; `ITapeSink::deliverTapeBlock(TapeId, const float*, const float*, int)` matches the fake (Task 3 Step 2) and the call site (Task 3 Step 6); `tapeTerminals_` element type `{ std::int64_t tapeId; MixerNodeId node; }` used consistently (compared via `t.tapeId == id.value()`).
