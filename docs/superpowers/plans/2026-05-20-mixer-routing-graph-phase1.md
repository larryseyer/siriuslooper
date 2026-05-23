# Mixer Routing Graph — Phase 1: Engine Routing-Graph Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pure, JUCE-free, fully-tested `MixerGraph` routing model (nodes,
main-out, sends, acyclic enforcement, topological evaluation order) plus a
`BusKind` discriminator, with `OutputMixer` refactored to drive its bus evaluation
order from `MixerGraph` (behavior-preserving; enables bus→bus subgroups).

**Architecture:** New engine module `engine/include/ida/MixerGraph.h` +
`engine/src/MixerGraph.cpp`, JUCE-free (no graph utility exists anywhere — written
fresh). `BusKind` lands on `BusConfig` in `Bus.h`. `OutputMixer` constructs/owns a
`MixerGraph(MixerTerminal::Output)`, mirrors channel/bus registration into graph
nodes, and walks `evaluationOrder()` in `renderBuffer` Step 3 so a bus may route its
main-out into another bus or the master.

**Tech Stack:** C++20, Catch2, CMake/Ninja. Engine public headers JUCE-free
(`juce_core` is PRIVATE).

---

## Source-of-truth references

- Spec: `docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md` (Phase 1 =
  "Engine routing-graph core (shared substrate)").
- `engine/include/ida/OutputMixer.h` — `addChannel`/`addBus`/`routeChannelToBus`/
  `sendLevelFor`/`renderBuffer`; `kMaxOutputChannels=32`, `kMaxBuses=64`; master at
  `BusId{0}`; `std::vector<Bus> buses_`; dense `sendMatrix_`.
- `engine/src/OutputMixer.cpp` — `renderBuffer` 4-step traversal; Step 3 (~lines
  247-318) currently sends **every** non-master bus into master.
- `engine/include/ida/Bus.h` — `BusConfig` (2-field POD); `Bus` stereo summing node.
- `tests/CMakeLists.txt` — `IdaTests` source list (append `MixerGraphTests.cpp`).
- `engine/CMakeLists.txt` — `IdaEngine` source list (append `src/MixerGraph.cpp`).
- RT-safety contract: `docs/RT_SAFETY_CONTRACT.md` §6. Graph mutation is bracketed by
  the mixer's `removeAudioCallback`/`addAudioCallback` (the `rebuildInputStrips()`
  pattern), so `MixerGraph` needs no internal atomic snapshot.

## Scope nuance (carry forward to Phase 2)

`MixerGraph` models send **topology** in Phase 1; the send-level **summing DSP**
stays in `OutputMixer`'s existing `sendMatrix_`. Full DSP reconciliation (sends
driving FX-return summing on both mixers) lands in Phase 2 when the input side needs
it. The stereo invariant is untouched — `MixerGraph` carries no audio; `Bus` is
already stereo-only (`kMaxBusChannelsHard=2`).

---

## Task 1: `BusKind` discriminator on `BusConfig`

**Files:**
- Modify: `engine/include/ida/Bus.h` (add `enum class BusKind` before `BusConfig`; add field)
- Test: `tests/BusTests.cpp` (append one TEST_CASE)

- [ ] **Step 1: Write the failing test** (append to `tests/BusTests.cpp`)

```cpp
TEST_CASE ("BusConfig defaults to BusKind::Bus and carries FxReturn through a Bus",
           "[bus][bus-kind]")
{
    using ida::Bus;
    using ida::BusConfig;
    using ida::BusId;
    using ida::BusKind;

    SECTION ("default kind is Bus")
    {
        const BusConfig cfg;
        CHECK (cfg.kind == BusKind::Bus);
    }

    SECTION ("FxReturn kind round-trips through Bus::config()")
    {
        Bus bus (BusId { 7 }, BusConfig { 2, "Reverb", BusKind::FxReturn });
        CHECK (bus.config().kind == BusKind::FxReturn);
        CHECK (bus.config().channelCount == 2);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: COMPILE FAIL — `BusKind` undeclared, `BusConfig` has no `kind` member.

- [ ] **Step 3: Add the enum + field** (`engine/include/ida/Bus.h`, immediately before `struct BusConfig`)

```cpp
/// Distinguishes a summing node that takes channel/bus main-outs (Bus) from one
/// that takes sends only (FxReturn). Structurally identical nodes; differ in how
/// signal arrives and typical contents (Bus: comp/EQ; FxReturn: RVB/DLY).
enum class BusKind { Bus, FxReturn };

struct BusConfig
{
    int         channelCount { 2 };
    std::string name;
    BusKind     kind { BusKind::Bus };
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[bus-kind]"`
Expected: PASS (2 sections).

- [ ] **Step 5: Confirm no regression**

Run: `./build/tests/IdaTests "[bus]"`
Expected: all existing `[bus]` cases still pass (the new field is defaulted, so
existing `BusConfig{2,"Master"}` aggregate-inits leave `kind == Bus`).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/Bus.h tests/BusTests.cpp
git commit -m "feat: BusKind discriminator on BusConfig"
```

---

## Task 2: `MixerGraph` types + node registry

**Files:**
- Create: `engine/include/ida/MixerGraph.h`
- Create: `engine/src/MixerGraph.cpp`
- Create: `tests/MixerGraphTests.cpp`
- Modify: `engine/CMakeLists.txt` (append `src/MixerGraph.cpp` to `IdaEngine`)
- Modify: `tests/CMakeLists.txt` (append `MixerGraphTests.cpp` to `IdaTests`)

- [ ] **Step 1: Create the header** `engine/include/ida/MixerGraph.h`

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace sirius
{

/// Graph-layer node kind (distinct from BusConfig::BusKind, which is the DSP
/// registry's view). A Channel is a source (input strip or phrase strip); Bus
/// and FxReturn are summing nodes; Terminal is the single implicit sink
/// (tape on the input side, output·master on the output side).
enum class MixerNodeKind { Channel, Bus, FxReturn, Terminal };

/// Which terminal this graph drives — set at construction, never changes.
enum class MixerTerminal { Tape, Output };

/// Strong id for routing-graph nodes. 0 == invalid. Distinct from BusId /
/// OutputChannelId: the graph is a topology layer above the DSP registries.
class MixerNodeId
{
public:
    constexpr MixerNodeId() noexcept = default;
    explicit constexpr MixerNodeId (std::int64_t v) noexcept : value_ (v) {}
    std::int64_t value() const noexcept { return value_; }
    bool isValid() const noexcept { return value_ != 0; }
    bool operator== (const MixerNodeId& o) const noexcept { return value_ == o.value_; }
    bool operator!= (const MixerNodeId& o) const noexcept { return value_ != o.value_; }
private:
    std::int64_t value_ { 0 };
};

/// Pure routing-topology model shared by both mixers. Owns the node registry,
/// each node's single main-out destination, the send edges, acyclic enforcement,
/// and a topologically-sorted evaluation order recomputed on every successful
/// mutation.
///
/// Threading: ALL mutators are message-thread only. The owning mixer brackets
/// mutations with removeAudioCallback/addAudioCallback (the rebuildInputStrips
/// pattern), so the audio thread never reads evaluationOrder() mid-mutation —
/// no atomic snapshot needed here. evaluationOrder() is the only audio-thread
/// read surface and is const noexcept.
class MixerGraph
{
public:
    /// Generous ceiling: 32 channels + 64 buses + headroom. Pre-reserved so the
    /// node/edge/order vectors never reallocate during normal use.
    static constexpr int kMaxNodes = 256;

    explicit MixerGraph (MixerTerminal terminal);

    MixerTerminal terminal()     const noexcept { return terminal_; }
    MixerNodeId   terminalNode() const noexcept { return terminalId_; }

    /// Registers a Channel / Bus / FxReturn node. Its main-out defaults to the
    /// terminal (zero-config). Passing Terminal returns an invalid id (the
    /// terminal is implicit and created at construction).
    MixerNodeId addNode (MixerNodeKind kind);
    void        removeNode (MixerNodeId node);
    bool        contains (MixerNodeId node) const noexcept;
    MixerNodeKind kindOf (MixerNodeId node) const noexcept; // Terminal for the terminal/unknown
    int         nodeCount() const noexcept; // excludes the implicit terminal

    /// Assigns node's single main-out. dest must be a Bus or the Terminal.
    /// Returns false (no change) if node is unknown/Terminal, dest is an invalid
    /// kind, or the edge would create a cycle.
    bool        setMainOut (MixerNodeId node, MixerNodeId dest);
    MixerNodeId mainOutOf (MixerNodeId node) const noexcept;

    /// Leveled send from a Channel/Bus into an FxReturn. level clamped [0,1];
    /// level<=0 removes the edge. Returns false if fxReturn is not an FxReturn,
    /// source is an FxReturn (v1: no FX-return sends), source==fxReturn, either
    /// is unknown, or the edge would create a cycle.
    bool        setSend (MixerNodeId source, MixerNodeId fxReturn, float level);
    float       sendLevel (MixerNodeId source, MixerNodeId fxReturn) const noexcept;

    /// Message-thread predicate for the UI destination picker: would assigning
    /// node's main-out to dest close a cycle? (true iff dest already reaches node.)
    bool        wouldMainOutCycle (MixerNodeId node, MixerNodeId dest) const noexcept;

    /// Audio-thread read: topologically sorted (sources before destinations,
    /// terminal last). Recomputed on every successful mutation.
    const std::vector<MixerNodeId>& evaluationOrder() const noexcept { return order_; }

private:
    struct Node { MixerNodeId id; MixerNodeKind kind; MixerNodeId mainOut; };
    struct SendEdge { MixerNodeId source; MixerNodeId fxReturn; float level; };

    const Node* find (MixerNodeId id) const noexcept;
    Node*       find (MixerNodeId id) noexcept;
    bool        reaches (MixerNodeId from, MixerNodeId target) const noexcept;
    void        recomputeOrder();

    MixerTerminal            terminal_;
    MixerNodeId              terminalId_;
    std::vector<Node>        nodes_;   // excludes the terminal (handled separately)
    std::vector<SendEdge>    sends_;
    std::vector<MixerNodeId> order_;
    std::int64_t             nextId_ { 1 };
};

} // namespace sirius
```

- [ ] **Step 2: Create the implementation** `engine/src/MixerGraph.cpp` (full body — every method, no stubs)

```cpp
#include "sirius/MixerGraph.h"

#include <algorithm>

namespace sirius
{

MixerGraph::MixerGraph (MixerTerminal terminal)
    : terminal_ (terminal)
{
    nodes_.reserve (kMaxNodes);
    sends_.reserve (kMaxNodes);
    order_.reserve (kMaxNodes + 1);
    terminalId_ = MixerNodeId { nextId_++ }; // the implicit terminal owns id 1
    recomputeOrder();
}

const MixerGraph::Node* MixerGraph::find (MixerNodeId id) const noexcept
{
    for (const auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

MixerGraph::Node* MixerGraph::find (MixerNodeId id) noexcept
{
    for (auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

int MixerGraph::nodeCount() const noexcept { return static_cast<int> (nodes_.size()); }

bool MixerGraph::contains (MixerNodeId node) const noexcept
{
    return node == terminalId_ || find (node) != nullptr;
}

MixerNodeKind MixerGraph::kindOf (MixerNodeId node) const noexcept
{
    if (node == terminalId_) return MixerNodeKind::Terminal;
    if (const Node* n = find (node)) return n->kind;
    return MixerNodeKind::Terminal; // unknown ids are treated as the sink
}

MixerNodeId MixerGraph::addNode (MixerNodeKind kind)
{
    if (kind == MixerNodeKind::Terminal) return MixerNodeId {}; // implicit only
    if (static_cast<int> (nodes_.size()) >= kMaxNodes) return MixerNodeId {};

    const MixerNodeId id { nextId_++ };
    nodes_.push_back (Node { id, kind, terminalId_ }); // main-out defaults to terminal
    recomputeOrder();
    return id;
}

void MixerGraph::removeNode (MixerNodeId node)
{
    if (node == terminalId_) return;

    nodes_.erase (std::remove_if (nodes_.begin(), nodes_.end(),
                                  [node] (const Node& n) { return n.id == node; }),
                  nodes_.end());

    // Drop any send edges touching the removed node.
    sends_.erase (std::remove_if (sends_.begin(), sends_.end(),
                                  [node] (const SendEdge& e)
                                  { return e.source == node || e.fxReturn == node; }),
                  sends_.end());

    // Any node whose main-out pointed at the removed node falls back to terminal.
    for (auto& n : nodes_)
        if (n.mainOut == node) n.mainOut = terminalId_;

    recomputeOrder();
}

MixerNodeId MixerGraph::mainOutOf (MixerNodeId node) const noexcept
{
    if (const Node* n = find (node)) return n->mainOut;
    return MixerNodeId {};
}

bool MixerGraph::reaches (MixerNodeId from, MixerNodeId target) const noexcept
{
    // DFS over outgoing edges (main-out + sends). Bounded by node count; the
    // graph is small (<= kMaxNodes) and acyclic by construction, so the explicit
    // visited set guards only against the transient pre-validation state.
    std::vector<MixerNodeId> stack;
    std::vector<MixerNodeId> visited;
    stack.push_back (from);
    while (! stack.empty())
    {
        const MixerNodeId cur = stack.back();
        stack.pop_back();
        if (cur == target) return true;
        if (std::find (visited.begin(), visited.end(), cur) != visited.end()) continue;
        visited.push_back (cur);

        if (const Node* n = find (cur))
        {
            if (n->mainOut.isValid()) stack.push_back (n->mainOut);
            for (const auto& e : sends_)
                if (e.source == cur) stack.push_back (e.fxReturn);
        }
    }
    return false;
}

bool MixerGraph::wouldMainOutCycle (MixerNodeId node, MixerNodeId dest) const noexcept
{
    // Adding node -> dest closes a cycle iff dest can already reach node.
    return reaches (dest, node);
}

bool MixerGraph::setMainOut (MixerNodeId node, MixerNodeId dest)
{
    Node* n = find (node);
    if (n == nullptr) return false; // unknown / terminal cannot have a main-out

    const MixerNodeKind destKind = kindOf (dest);
    const bool destValid = (dest == terminalId_) || (destKind == MixerNodeKind::Bus);
    if (! destValid) return false; // only a Bus or the Terminal is a valid destination

    if (wouldMainOutCycle (node, dest)) return false;

    n->mainOut = dest;
    recomputeOrder();
    return true;
}

float MixerGraph::sendLevel (MixerNodeId source, MixerNodeId fxReturn) const noexcept
{
    for (const auto& e : sends_)
        if (e.source == source && e.fxReturn == fxReturn) return e.level;
    return 0.0f;
}

bool MixerGraph::setSend (MixerNodeId source, MixerNodeId fxReturn, float level)
{
    if (source == fxReturn) return false;
    const Node* src = find (source);
    if (src == nullptr || src->kind == MixerNodeKind::FxReturn) return false; // v1: no FX-return sends
    if (kindOf (fxReturn) != MixerNodeKind::FxReturn) return false;

    // A send is an edge source -> fxReturn; reject if fxReturn already reaches source.
    if (reaches (fxReturn, source)) return false;

    const float clamped = std::clamp (level, 0.0f, 1.0f);

    auto it = std::find_if (sends_.begin(), sends_.end(),
                            [source, fxReturn] (const SendEdge& e)
                            { return e.source == source && e.fxReturn == fxReturn; });

    if (clamped <= 0.0f)
    {
        if (it != sends_.end()) { sends_.erase (it); recomputeOrder(); }
        return true;
    }

    if (it != sends_.end()) it->level = clamped;
    else                    sends_.push_back (SendEdge { source, fxReturn, clamped });
    recomputeOrder();
    return true;
}

void MixerGraph::recomputeOrder()
{
    // Kahn's algorithm. Nodes: all registered nodes + the implicit terminal.
    // Edges: each node's main-out (node -> mainOut) and each send (source -> fxReturn).
    order_.clear();

    std::vector<MixerNodeId> ids;
    ids.reserve (nodes_.size() + 1);
    for (const auto& n : nodes_) ids.push_back (n.id);
    ids.push_back (terminalId_);

    // in-degree per node id (parallel to ids)
    std::vector<int> indeg (ids.size(), 0);
    const auto indexOf = [&ids] (MixerNodeId id) -> int
    {
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id) return static_cast<int> (i);
        return -1;
    };

    const auto addEdge = [&] (MixerNodeId from, MixerNodeId to)
    {
        (void) from;
        const int ti = indexOf (to);
        if (ti >= 0) ++indeg[static_cast<std::size_t> (ti)];
    };

    for (const auto& n : nodes_)
        if (n.mainOut.isValid()) addEdge (n.id, n.mainOut);
    for (const auto& e : sends_)
        addEdge (e.source, e.fxReturn);

    std::vector<MixerNodeId> queue;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (indeg[i] == 0) queue.push_back (ids[i]);

    while (! queue.empty())
    {
        const MixerNodeId cur = queue.front();
        queue.erase (queue.begin());
        order_.push_back (cur);

        // Decrement in-degree of every node cur points at.
        const auto relax = [&] (MixerNodeId to)
        {
            const int ti = indexOf (to);
            if (ti >= 0 && --indeg[static_cast<std::size_t> (ti)] == 0)
                queue.push_back (to);
        };

        if (const Node* n = find (cur))
        {
            if (n->mainOut.isValid()) relax (n->mainOut);
            for (const auto& e : sends_)
                if (e.source == cur) relax (e.fxReturn);
        }
    }
    // The graph is acyclic by construction (setMainOut/setSend reject cycles), so
    // order_ contains every id. The terminal, having no outgoing edge, sorts last.
}

} // namespace sirius
```

- [ ] **Step 3: Register in CMake**

In `engine/CMakeLists.txt`, append to the `IdaEngine` source list (after
`src/ConstituentValidator.cpp`):
```cmake
    src/MixerGraph.cpp)
```
In `tests/CMakeLists.txt`, append to the `IdaTests` source list (after
`StatefulSynthFixtureTests.cpp`):
```cmake
    MixerGraphTests.cpp)
```

- [ ] **Step 4: Create `tests/MixerGraphTests.cpp` with the node-registry tests**

```cpp
#include "sirius/MixerGraph.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using ida::MixerGraph;
using ida::MixerNodeId;
using ida::MixerNodeKind;
using ida::MixerTerminal;

TEST_CASE ("MixerNodeId validity + equality", "[mixer-graph][node-registry]")
{
    CHECK_FALSE (MixerNodeId {}.isValid());
    CHECK (MixerNodeId { 1 }.isValid());
    CHECK (MixerNodeId { 3 } == MixerNodeId { 3 });
    CHECK (MixerNodeId { 3 } != MixerNodeId { 4 });
}

TEST_CASE ("MixerGraph constructs with an implicit terminal and no nodes",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::Tape);
    CHECK (g.terminal() == MixerTerminal::Tape);
    CHECK (g.terminalNode().isValid());
    CHECK (g.kindOf (g.terminalNode()) == MixerNodeKind::Terminal);
    CHECK (g.nodeCount() == 0);
    CHECK (g.contains (g.terminalNode()));
}

TEST_CASE ("MixerGraph::addNode registers distinct nodes; Terminal is rejected",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::Output);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto bus = g.addNode (MixerNodeKind::Bus);
    const auto fx  = g.addNode (MixerNodeKind::FxReturn);

    CHECK (ch.isValid());
    CHECK (bus.isValid());
    CHECK (fx.isValid());
    CHECK (ch != bus);
    CHECK (bus != fx);
    CHECK (g.kindOf (ch)  == MixerNodeKind::Channel);
    CHECK (g.kindOf (bus) == MixerNodeKind::Bus);
    CHECK (g.kindOf (fx)  == MixerNodeKind::FxReturn);
    CHECK (g.nodeCount() == 3);

    const auto rejected = g.addNode (MixerNodeKind::Terminal);
    CHECK_FALSE (rejected.isValid());
    CHECK (g.nodeCount() == 3);
}

TEST_CASE ("MixerGraph::removeNode drops the node, its main-out, and its sends",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::Output);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto fx  = g.addNode (MixerNodeKind::FxReturn);
    CHECK (g.setSend (ch, fx, 0.5f));

    g.removeNode (fx);
    CHECK_FALSE (g.contains (fx));
    CHECK (g.nodeCount() == 1);
    CHECK (g.sendLevel (ch, fx) == 0.0f); // edge gone with the node
    CHECK (g.mainOutOf (ch) == g.terminalNode()); // unaffected
}
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][node-registry]"`
Expected: PASS (4 cases).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/MixerGraph.h engine/src/MixerGraph.cpp \
        engine/CMakeLists.txt tests/CMakeLists.txt tests/MixerGraphTests.cpp
git commit -m "feat: MixerGraph node registry + terminal"
```

> NOTE: `MixerGraph.cpp` in Step 2 already contains the full bodies for Tasks
> 3-6. Those tasks are therefore **test-only** (the green is already implemented):
> write the test, run it, confirm PASS, commit. This is intentional — the
> implementation is one cohesive pure module that is easier to reason about whole
> than to dribble out method-by-method. If a test fails, fix the corresponding
> method in `MixerGraph.cpp` before committing.

---

## Task 3: main-out assignment + kind validation (test-only)

**Files:** Test: `tests/MixerGraphTests.cpp` (append)

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE ("MixerGraph main-out defaults to terminal and validates destination kind",
           "[mixer-graph][main-out]")
{
    MixerGraph g (MixerTerminal::Output);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto fx   = g.addNode (MixerNodeKind::FxReturn);

    SECTION ("new node main-out defaults to terminal")
    {
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("main-out to a Bus succeeds")
    {
        CHECK (g.setMainOut (ch, busA));
        CHECK (g.mainOutOf (ch) == busA);
    }
    SECTION ("main-out to a Channel is rejected and leaves main-out unchanged")
    {
        const auto ch2 = g.addNode (MixerNodeKind::Channel);
        CHECK_FALSE (g.setMainOut (ch, ch2));
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("main-out to an FxReturn is rejected (FX returns are send-fed)")
    {
        CHECK_FALSE (g.setMainOut (ch, fx));
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("the terminal cannot be given a main-out")
    {
        CHECK_FALSE (g.setMainOut (g.terminalNode(), busA));
    }
}
```

- [ ] **Step 2: Run**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][main-out]"`
Expected: PASS (all sections). If any fail, fix `setMainOut`/`mainOutOf` in `MixerGraph.cpp`.

- [ ] **Step 3: Commit**

```bash
git add tests/MixerGraphTests.cpp
git commit -m "test: MixerGraph main-out assignment + kind validation"
```

---

## Task 4: cycle detection (test-only)

**Files:** Test: `tests/MixerGraphTests.cpp` (append)

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE ("MixerGraph rejects main-out assignments that would create a cycle",
           "[mixer-graph][cycle]")
{
    MixerGraph g (MixerTerminal::Output);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);

    SECTION ("a valid subgroup A->B succeeds")
    {
        CHECK (g.setMainOut (busA, busB));
        CHECK (g.mainOutOf (busA) == busB);
    }
    SECTION ("closing the loop B->A is rejected and leaves B unchanged")
    {
        REQUIRE (g.setMainOut (busA, busB));
        CHECK (g.wouldMainOutCycle (busB, busA));
        CHECK_FALSE (g.setMainOut (busB, busA));
        CHECK (g.mainOutOf (busB) == g.terminalNode());
    }
}
```

- [ ] **Step 2: Run**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][cycle]"`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/MixerGraphTests.cpp
git commit -m "test: MixerGraph acyclic main-out enforcement"
```

---

## Task 5: sends (test-only)

**Files:** Test: `tests/MixerGraphTests.cpp` (append)

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE ("MixerGraph sends target FX returns only, clamp, and reject cycles",
           "[mixer-graph][sends]")
{
    MixerGraph g (MixerTerminal::Output);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto fxA  = g.addNode (MixerNodeKind::FxReturn);
    const auto fxB  = g.addNode (MixerNodeKind::FxReturn);

    SECTION ("channel -> FX return succeeds and stores the level")
    {
        CHECK (g.setSend (ch, fxA, 0.5f));
        CHECK (g.sendLevel (ch, fxA) == 0.5f);
    }
    SECTION ("levels clamp to [0,1]")
    {
        CHECK (g.setSend (ch, fxA, 2.0f));
        CHECK (g.sendLevel (ch, fxA) == 1.0f);
        CHECK (g.setSend (ch, fxA, -1.0f)); // <=0 removes the edge
        CHECK (g.sendLevel (ch, fxA) == 0.0f);
    }
    SECTION ("send to a Bus is rejected (not an FX return)")
    {
        CHECK_FALSE (g.setSend (ch, busA, 0.5f));
    }
    SECTION ("FX-return-sourced sends are rejected (v1: no FX-return sends)")
    {
        CHECK_FALSE (g.setSend (fxA, fxB, 0.5f));
    }
    SECTION ("a send that would close a cycle is rejected")
    {
        // fxA -> busA via main-out, then busA -> fxA via send would loop.
        REQUIRE (g.setMainOut (fxA, busA));
        CHECK_FALSE (g.setSend (busA, fxA, 0.5f));
    }
}
```

- [ ] **Step 2: Run**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][sends]"`
Expected: PASS. Fix `setSend`/`sendLevel` in `MixerGraph.cpp` on any failure.

- [ ] **Step 3: Commit**

```bash
git add tests/MixerGraphTests.cpp
git commit -m "test: MixerGraph send edges, clamping, FX-return constraint, cycle rejection"
```

---

## Task 6: topological `evaluationOrder` + RT-safety static_assert (test-only)

**Files:** Test: `tests/MixerGraphTests.cpp` (append)

- [ ] **Step 1: Add the file-scope RT-safety static_assert** (top of `tests/MixerGraphTests.cpp`, after includes)

```cpp
// Compile-time invariant — evaluationOrder() is the only audio-thread read
// surface and MUST be noexcept (RT-safety contract §6).
static_assert (noexcept (std::declval<const MixerGraph&>().evaluationOrder()),
               "MixerGraph::evaluationOrder must be noexcept (RT-safety contract §6)");
```

- [ ] **Step 2: Write the ordering tests** (helper finds a node's index in the order)

```cpp
namespace
{
    int posOf (const std::vector<MixerNodeId>& order, MixerNodeId id)
    {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == id) return static_cast<int> (i);
        return -1;
    }
}

TEST_CASE ("MixerGraph evaluation order: sources before destinations, terminal last",
           "[mixer-graph][evaluation-order]")
{
    MixerGraph g (MixerTerminal::Output);

    SECTION ("default graph — all channels before the terminal")
    {
        const auto c1 = g.addNode (MixerNodeKind::Channel);
        const auto c2 = g.addNode (MixerNodeKind::Channel);
        const auto c3 = g.addNode (MixerNodeKind::Channel);
        const auto& order = g.evaluationOrder();
        const int t = posOf (order, g.terminalNode());
        REQUIRE (t >= 0);
        CHECK (posOf (order, c1) < t);
        CHECK (posOf (order, c2) < t);
        CHECK (posOf (order, c3) < t);
        CHECK (t == static_cast<int> (order.size()) - 1); // terminal last
    }

    SECTION ("subgroup chain — chan before busA before busB before terminal")
    {
        const auto ch   = g.addNode (MixerNodeKind::Channel);
        const auto busA = g.addNode (MixerNodeKind::Bus);
        const auto busB = g.addNode (MixerNodeKind::Bus);
        REQUIRE (g.setMainOut (ch,   busA));
        REQUIRE (g.setMainOut (busA, busB));
        REQUIRE (g.setMainOut (busB, g.terminalNode()));
        const auto& order = g.evaluationOrder();
        CHECK (posOf (order, ch)   < posOf (order, busA));
        CHECK (posOf (order, busA) < posOf (order, busB));
        CHECK (posOf (order, busB) < posOf (order, g.terminalNode()));
    }

    SECTION ("send fan-in — both sources before the FX return before the terminal")
    {
        const auto ch   = g.addNode (MixerNodeKind::Channel);
        const auto busA = g.addNode (MixerNodeKind::Bus);
        const auto fx   = g.addNode (MixerNodeKind::FxReturn);
        REQUIRE (g.setSend (ch,   fx, 0.4f));
        REQUIRE (g.setSend (busA, fx, 0.6f));
        const auto& order = g.evaluationOrder();
        CHECK (posOf (order, ch)   < posOf (order, fx));
        CHECK (posOf (order, busA) < posOf (order, fx));
        CHECK (posOf (order, fx)   < posOf (order, g.terminalNode()));
    }
}
```

- [ ] **Step 3: Run**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph][evaluation-order]"`
Expected: PASS. Add `#include <utility>` and `#include <vector>` to the test file if
the compiler flags `std::declval`/`std::vector`.

- [ ] **Step 4: Commit**

```bash
git add tests/MixerGraphTests.cpp
git commit -m "test: MixerGraph topological evaluation order + RT-safety static_assert"
```

---

## Task 7: OutputMixer drives bus evaluation order from MixerGraph

**Files:**
- Modify: `engine/include/ida/OutputMixer.h`
- Modify: `engine/src/OutputMixer.cpp`
- Test: `tests/OutputMixerTests.cpp` (append one TEST_CASE)

**Behavior-preservation contract:** the entire existing `[output-mixer]` suite must
stay green. The default graph (every channel + aux bus → master, master → terminal)
reproduces the current "channels → all buses → master → outputs" behavior exactly.

- [ ] **Step 1: Add the failing subgroup test** (append to `tests/OutputMixerTests.cpp`)

```cpp
TEST_CASE ("OutputMixer bus->bus subgroup routes through the parent bus",
           "[output-mixer][subgroup]")
{
    using ida::BusConfig;
    using ida::OutputMixer;
    using ida::SignalType;

    OutputMixer mixer;
    const auto ch   = mixer.addChannel (SignalType::Audio);
    const auto busA = mixer.addBus (BusConfig { 2, "A" });
    const auto busB = mixer.addBus (BusConfig { 2, "B" });

    // Route the channel's send fully into busA, and subgroup busA -> busB.
    mixer.routeChannelToBus (ch, busA, 1.0f);
    REQUIRE (mixer.routeBusToBus (busA, busB)); // new thin accessor over MixerGraph

    // One sample of input on channel index 0 (ch.value()-1 == 0).
    std::array<float, 4> in;  in.fill (0.5f);
    std::array<float, 4> out; out.fill (0.0f);
    const float* inPtrs[1]  = { in.data() };
    float*       outPtrs[2] = { out.data(), nullptr };

    mixer.renderBuffer (inPtrs, 1, outPtrs, 1,
                        static_cast<int> (in.size()));

    // The signal flowed ch -> busA -> busB -> master -> output (unity throughout),
    // so the output carries the channel signal (no double-count, no drop).
    for (float v : out) CHECK (v == Catch::Approx (0.5f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: COMPILE FAIL — `OutputMixer::routeBusToBus` does not exist yet.

- [ ] **Step 3: Add `MixerGraph` to `OutputMixer.h`**

Add include + the accessor + members:
```cpp
#include "sirius/MixerGraph.h"   // with the other sirius/ includes
```
Public, after `routeChannelToBus`:
```cpp
    /// Routes a bus's main-out into another bus (subgroup) via the routing
    /// graph. Returns false if either id is unknown or the assignment would
    /// create a cycle (delegates to MixerGraph::setMainOut). Default main-out
    /// for an aux bus is the master.
    bool routeBusToBus (BusId from, BusId to);
```
Private members (alongside `channels_`/`buses_`):
```cpp
    MixerGraph                graph_ { MixerTerminal::Output };
    std::vector<MixerNodeId>  channelNodeIds_; // parallel to channels_
    std::vector<MixerNodeId>  busNodeIds_;     // parallel to buses_ (index 0 = master)
```

- [ ] **Step 4: Wire registration + the accessor in `OutputMixer.cpp`**

In the constructor, after the master `Bus` is emplaced, register the master node
(its main-out is the terminal):
```cpp
    busNodeIds_.push_back (graph_.addNode (MixerNodeKind::Bus)); // master node
    graph_.setMainOut (busNodeIds_.front(), graph_.terminalNode());
```
In `addChannel`, after pushing the `ChannelEntry`:
```cpp
    channelNodeIds_.push_back (graph_.addNode (MixerNodeKind::Channel));
```
In `addBus`, after emplacing the `Bus`:
```cpp
    const auto kind = (config.kind == BusKind::FxReturn) ? MixerNodeKind::FxReturn
                                                         : MixerNodeKind::Bus;
    const auto node = graph_.addNode (kind);
    busNodeIds_.push_back (node);
    graph_.setMainOut (node, busNodeIds_.front()); // aux bus -> master by default
```
Add the accessor (maps `BusId` → graph node via `busNodeIds_` index = `BusId.value()`):
```cpp
bool OutputMixer::routeBusToBus (BusId from, BusId to)
{
    const auto fi = static_cast<std::size_t> (from.value());
    const auto ti = static_cast<std::size_t> (to.value());
    if (fi >= busNodeIds_.size() || ti >= busNodeIds_.size()) return false;
    return graph_.setMainOut (busNodeIds_[fi], busNodeIds_[ti]);
}
```

- [ ] **Step 5: Refactor `renderBuffer` Step 3 to walk the graph order**

Replace the current "for each non-master bus → master" loop (`OutputMixer.cpp`
~lines 247-318) with a walk of `graph_.evaluationOrder()`. For each entry that maps
to a non-master, non-channel bus, process it into its **main-out destination's**
mix buffer:
- Resolve the destination `BusId` from `graph_.mainOutOf(node)`: if it is the master
  node or the terminal, the destination is the master bus's mixBuffer; otherwise it
  is the destination bus's mixBuffer (look up via `busNodeIds_`).
- Call `bus.process(destPtrs, 2, clampedSamples)` where `destPtrs` point at the
  destination bus's `mixBufferChannel(0/1)` (the `process` path already handles the
  effect chain vs inline branch and zeroes its own mix buffer).
- Process buses strictly in `evaluationOrder()` so a subgroup bus is fully processed
  into its parent before the parent is processed. Master (Step 4) is unchanged.

Keep Step 1 (channel strips) and Step 2 (channel send-matrix into bus mix buffers)
exactly as they are — the send-level DSP still flows through `sendMatrix_`. The graph
governs **bus processing order + bus main-out destination only** in Phase 1.

- [ ] **Step 6: Build + run the subgroup test and the full output-mixer suite**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[output-mixer]"`
Expected: PASS — every pre-existing `[output-mixer]` case AND the new
`[output-mixer][subgroup]` case. If a pre-existing render test regresses, the Step 5
refactor changed default behavior — revisit the destination resolution so aux buses
still sum into master at unity.

- [ ] **Step 7: Commit**

```bash
git add engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp tests/OutputMixerTests.cpp
git commit -m "feat: OutputMixer drives bus evaluation order from MixerGraph"
```

---

## Task 8: self-review, clean rebuild, push, handoff

- [ ] **Step 1: Clean rebuild + full suite**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
ctest --test-dir build
```
Expected: prior baseline + the new `[mixer-graph]`, `[bus-kind]`, and
`[output-mixer][subgroup]` cases all pass. The single documented non-pass
(`MainComponentPluginEditorTests_NOT_BUILT`, run separately by `bash/test-s7.sh`)
remains and is not a regression.

- [ ] **Step 2: Standalone module run + placeholder grep**

```bash
./build/tests/IdaTests "[mixer-graph]"
grep -rnE "TODO|FIXME|XXX|stub|placeholder" engine/include/ida/MixerGraph.h \
     engine/src/MixerGraph.cpp engine/include/ida/OutputMixer.h \
     engine/src/OutputMixer.cpp engine/include/ida/Bus.h
```
Expected: `[mixer-graph]` all green; grep returns zero hits (or each accounted for in `todo.md`).

- [ ] **Step 3: Push + refresh continue.md**

```bash
git push origin master
```
Update `continue.md`: Phase 1 shipped; next = Phase 2 (input-side routing apparatus —
add the shared substrate to `InputMixer`, terminal=tape, channel main-out→bus/tape,
sends→FX returns; absorbs tape-output routing). Note the Phase 1 scope nuance carried
forward (send-level summing DSP reconciliation happens in Phase 2).

---

## Self-review against the spec (run before declaring Phase 1 done)

- **Bus kind** → Task 1. ✓
- **Main-out vs sends split** → `MixerGraph` models them as distinct edge kinds (Tasks 3, 5). ✓
- **Acyclic enforcement** → `reaches`/`wouldMainOutCycle`/rejection (Task 4 + send cycle in Task 5). ✓
- **Topological evaluation** → Kahn's `recomputeOrder` (Task 6). ✓
- **Build on OutputMixer + factor shared MixerGraph** → Tasks 2 + 7. ✓
- **Send summing** → topology modeled here; level summing stays in `sendMatrix_`,
  full reconciliation deferred to Phase 2 (documented scope nuance). ✓ (partial by design)
- **Stereo invariant** → untouched (`MixerGraph` carries no audio; `Bus` stereo-only). ✓
- **RT-safety static_asserts** → Task 6 file-scope assert + the bracket contract documented. ✓
