# Multi-terminal MixerGraph (Routing Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generalize `MixerGraph` from one implicit terminal to a set of typed terminal sinks (`Tape`, `HardwareOutput`), so the input side (Phase 3) can route to both tape and hardware output, while keeping `OutputMixer` behavior-identical.

**Architecture:** Replace the single `terminal_`/`terminalId_` pair with a `std::vector<TerminalNode>` (one Terminal node per kind; `terminals_[0]` = the primary / zero-config default destination). Add a `std::initializer_list` constructor and a kind-keyed `terminalNode(kind)` accessor. Keep the single-`MixerTerminal` constructor (delegates) and the no-arg `terminalNode()`/`terminal()` (return the primary) so every existing call site and test works unchanged. All cycle/order/validation logic already terminates correctly at sinks; only the "is this id the terminal?" check generalizes from one id to an `isTerminal()` scan.

**Tech Stack:** C++17, JUCE-free `engine` core, Catch2 tests, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md` (Phase 2 section, lines 248–255). The spec is **locked** — do not re-brainstorm.

---

## File Structure

- `engine/include/sirius/MixerGraph.h` — modify: enum rename, terminal-set members, new ctor + accessor, `isTerminal` helper decl.
- `engine/src/MixerGraph.cpp` — modify: ctors, and generalize every `terminalId_`/`terminal_` use to the terminal set.
- `engine/include/sirius/OutputMixer.h` — modify: line 184 enum spelling only.
- `tests/MixerGraphTests.cpp` — modify: `Output` → `HardwareOutput` rename; add multi-terminal cases.

`OutputMixer.cpp` lines 58 & 313 use the no-arg `terminalNode()` and need **no** change — it still resolves to the sole terminal. The `[output-mixer]` suite is the regression-equivalence proof.

---

## Task 1: Rename `MixerTerminal::Output` → `HardwareOutput`

Mechanical, behavior-preserving rename to match the spec's vocabulary. Isolated from the logic change so the diff is reviewable.

**Files:**
- Modify: `engine/include/sirius/MixerGraph.h:16`
- Modify: `engine/include/sirius/OutputMixer.h:184`
- Modify: `tests/MixerGraphTests.cpp` (8 occurrences of `MixerTerminal::Output`)

- [ ] **Step 1: Rename the enumerator**

In `engine/include/sirius/MixerGraph.h:16` change:

```cpp
enum class MixerTerminal { Tape, Output };
```
to:
```cpp
enum class MixerTerminal { Tape, HardwareOutput };
```

- [ ] **Step 2: Update the two consumers**

In `engine/include/sirius/OutputMixer.h:184`:
```cpp
    MixerGraph                graph_ { MixerTerminal::HardwareOutput };
```

In `tests/MixerGraphTests.cpp`, replace every `MixerTerminal::Output` with `MixerTerminal::HardwareOutput` (lines 49, 72, 87, 121, 142, 189 — 6 occurrences; line 38 stays `MixerTerminal::Tape`). Verify none remain:

Run: `grep -rn "MixerTerminal::Output" engine tests | grep -v HardwareOutput`
Expected: no output.

- [ ] **Step 3: Build and run the affected suites**

Run:
```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release   # if build/ absent
cmake --build build --target SiriusTests
./build/tests/SiriusTests "[mixer-graph]" "[output-mixer]"
```
Expected: PASS, all cases green (rename is behavior-preserving — the single-terminal path is unchanged).

- [ ] **Step 4: Commit**

```bash
git add engine/include/sirius/MixerGraph.h engine/include/sirius/OutputMixer.h tests/MixerGraphTests.cpp
git commit -m "refactor: rename MixerTerminal::Output -> HardwareOutput (spec vocabulary)"
```

---

## Task 2: Generalize `MixerGraph` to a typed terminal set

**Files:**
- Modify: `engine/include/sirius/MixerGraph.h`
- Modify: `engine/src/MixerGraph.cpp`
- Test: `tests/MixerGraphTests.cpp`

- [ ] **Step 1: Write the failing multi-terminal tests**

Append to `tests/MixerGraphTests.cpp`:

```cpp
TEST_CASE ("MixerGraph supports multiple typed terminals",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });

    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);

    CHECK (tape.isValid());
    CHECK (hw.isValid());
    CHECK (tape != hw);
    CHECK (g.kindOf (tape) == MixerNodeKind::Terminal);
    CHECK (g.kindOf (hw)   == MixerNodeKind::Terminal);
    CHECK (g.contains (tape));
    CHECK (g.contains (hw));

    // Primary (no-arg) accessors return the first-listed terminal.
    CHECK (g.terminalNode() == tape);
    CHECK (g.terminal()     == MixerTerminal::Tape);

    // A node defaults its main-out to the primary terminal (tape = capture).
    const auto ch = g.addNode (MixerNodeKind::Channel);
    CHECK (g.mainOutOf (ch) == tape);

    // An absent terminal kind resolves to invalid on a single-terminal graph.
    MixerGraph single (MixerTerminal::HardwareOutput);
    CHECK_FALSE (single.terminalNode (MixerTerminal::Tape).isValid());
    CHECK (single.terminalNode (MixerTerminal::HardwareOutput) == single.terminalNode());
}

TEST_CASE ("MixerGraph routes distinct nodes to distinct terminals",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);

    REQUIRE (g.setMainOut (busA, tape));
    REQUIRE (g.setMainOut (busB, hw));
    CHECK (g.mainOutOf (busA) == tape);
    CHECK (g.mainOutOf (busB) == hw);
}

TEST_CASE ("MixerGraph removeNode falls a dangling main-out back to the primary terminal",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);
    REQUIRE (g.setMainOut (busA, busB)); // busA -> busB

    g.removeNode (busB);
    CHECK (g.mainOutOf (busA) == tape); // fell back to the primary terminal
}

TEST_CASE ("MixerGraph evaluation order: every node precedes all terminals",
           "[mixer-graph][multi-terminal][evaluation-order]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    REQUIRE (g.setMainOut (ch,   busA));
    REQUIRE (g.setMainOut (busA, hw)); // busA -> hardware output

    const auto& order = g.evaluationOrder();
    REQUIRE (order.size() == 4); // ch, busA, tape, hw

    const int chPos = posOf (order, ch);
    const int buPos = posOf (order, busA);
    CHECK (chPos < buPos);
    CHECK (buPos < posOf (order, tape));
    CHECK (buPos < posOf (order, hw));
    // Both terminals occupy the final positions (no node after a terminal).
    CHECK (posOf (order, tape) >= chPos);
    CHECK (posOf (order, hw)   >= chPos);
    const int lastNonTerminal = (chPos > buPos) ? chPos : buPos;
    CHECK (posOf (order, tape) > lastNonTerminal);
    CHECK (posOf (order, hw)   > lastNonTerminal);
}
```

- [ ] **Step 2: Verify the new tests fail to compile**

Run: `cmake --build build --target SiriusTests`
Expected: FAIL — `MixerGraph` has no `std::initializer_list` constructor and no `terminalNode(MixerTerminal)` overload.

- [ ] **Step 3: Update the header**

In `engine/include/sirius/MixerGraph.h`, add the include near the top (after `<cstdint>`):

```cpp
#include <initializer_list>
```

Change the constructor declaration block (currently `explicit MixerGraph (MixerTerminal terminal);`) to:

```cpp
    /// Single-terminal graph (Output Mixer): one implicit terminal of the given
    /// kind. Delegates to the list constructor.
    explicit MixerGraph (MixerTerminal terminal);

    /// Multi-terminal graph (Input Mixer): one implicit Terminal node per kind,
    /// in order. terminals[0] is the PRIMARY — the zero-config default main-out
    /// destination and the removeNode fallback. Precondition: non-empty.
    explicit MixerGraph (std::initializer_list<MixerTerminal> terminals);
```

Change the two inline accessors:

```cpp
    MixerTerminal terminal()     const noexcept { return terminals_.front().kind; }
    MixerNodeId   terminalNode() const noexcept { return terminals_.front().id; }

    /// The Terminal node id for a given kind, or an invalid id if this graph has
    /// no terminal of that kind.
    MixerNodeId   terminalNode (MixerTerminal kind) const noexcept;
```

In the `private:` section, replace:

```cpp
    MixerTerminal            terminal_;
    MixerNodeId              terminalId_;
```
with:
```cpp
    struct TerminalNode { MixerNodeId id; MixerTerminal kind; };

    bool isTerminal (MixerNodeId id) const noexcept;

    std::vector<TerminalNode> terminals_; // [0] = primary; >= 1 by construction
```

(Leave `nodes_`, `sends_`, `order_`, `nextId_` as-is. `find()`, `reaches()`, `recomputeOrder()` decls unchanged.)

- [ ] **Step 4: Update the .cpp**

In `engine/src/MixerGraph.cpp`, replace the constructor (lines 8–16) with:

```cpp
MixerGraph::MixerGraph (std::initializer_list<MixerTerminal> terminals)
{
    nodes_.reserve (kMaxNodes);
    sends_.reserve (kMaxNodes);
    order_.reserve (static_cast<std::size_t> (kMaxNodes) + terminals.size());
    terminals_.reserve (terminals.size());
    for (const MixerTerminal kind : terminals)
        terminals_.push_back (TerminalNode { MixerNodeId { nextId_++ }, kind });
    recomputeOrder();
}

MixerGraph::MixerGraph (MixerTerminal terminal)
    : MixerGraph (std::initializer_list<MixerTerminal> { terminal })
{
}

bool MixerGraph::isTerminal (MixerNodeId id) const noexcept
{
    for (const auto& t : terminals_)
        if (t.id == id) return true;
    return false;
}

MixerNodeId MixerGraph::terminalNode (MixerTerminal kind) const noexcept
{
    for (const auto& t : terminals_)
        if (t.kind == kind) return t.id;
    return MixerNodeId {};
}
```

In `contains` — replace `node == terminalId_` with `isTerminal (node)`:
```cpp
bool MixerGraph::contains (MixerNodeId node) const noexcept
{
    return isTerminal (node) || find (node) != nullptr;
}
```

In `kindOf` — replace `if (node == terminalId_)` with `if (isTerminal (node))`:
```cpp
MixerNodeKind MixerGraph::kindOf (MixerNodeId node) const noexcept
{
    if (isTerminal (node)) return MixerNodeKind::Terminal;
    if (const Node* n = find (node)) return n->kind;
    return MixerNodeKind::Terminal; // unknown ids are treated as the sink
}
```

In `addNode` — the default main-out becomes the primary terminal:
```cpp
    nodes_.push_back (Node { id, kind, terminals_.front().id }); // default: primary terminal
```

In `removeNode` — the early-out and the fallback both use the terminal set:
```cpp
    if (isTerminal (node)) return;
```
and:
```cpp
    // Any node whose main-out pointed at the removed node falls back to the primary terminal.
    for (auto& n : nodes_)
        if (n.mainOut == node) n.mainOut = terminals_.front().id;
```

In `setMainOut` — destination validity scans the terminal set:
```cpp
    const MixerNodeKind destKind = kindOf (dest);
    const bool destValid = isTerminal (dest) || (destKind == MixerNodeKind::Bus);
    if (! destValid) return false; // only a Bus or a Terminal is a valid destination
```

In `recomputeOrder` — push every terminal id into `ids` instead of the single `terminalId_`. Replace:
```cpp
    ids.push_back (terminalId_);
```
with:
```cpp
    for (const auto& t : terminals_) ids.push_back (t.id);
```
And update the `ids.reserve` line just above it:
```cpp
    ids.reserve (nodes_.size() + terminals_.size());
```

No change is needed in `reaches` (terminals are not in `nodes_`, so `find()` returns null and they have no outgoing edges — DFS terminates correctly) or in the `find()` overloads.

- [ ] **Step 5: Run the full mixer-graph + output-mixer suites**

Run:
```bash
cmake --build build --target SiriusTests
./build/tests/SiriusTests "[mixer-graph]" "[output-mixer]"
```
Expected: PASS — the 4 new `[multi-terminal]` cases plus all pre-existing `[mixer-graph]` and `[output-mixer]` cases (OutputMixer behavior unchanged: it still constructs a single `HardwareOutput` terminal).

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir build`
Expected: baseline **478/479** — the only non-pass is `MainComponentPluginEditorTests_NOT_BUILT` (documented; built/run separately by `bash/test-s7.sh`).

- [ ] **Step 7: Commit**

```bash
git add engine/include/sirius/MixerGraph.h engine/src/MixerGraph.cpp tests/MixerGraphTests.cpp
git commit -m "feat: MixerGraph supports a typed terminal set (Tape + HardwareOutput)"
```

---

## Verification (end-to-end)

1. Clean configure not required (engine-only, no GUI bundle): `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`.
2. `cmake --build build --target SiriusTests` — compiles clean under `-Werror`.
3. `./build/tests/SiriusTests "[mixer-graph]"` — all single- and multi-terminal cases pass.
4. `./build/tests/SiriusTests "[output-mixer]"` — regression-equivalence; OutputMixer behavior identical to pre-Phase-2.
5. `ctest --test-dir build` — 478/479 (the 1 = `MainComponentPluginEditorTests_NOT_BUILT`).
6. No operator eyes-on / `rm -rf build` needed — engine logic only, no GUI surface touched.

## Close-out (mandatory per operator instruction)

Phase 2 is NOT complete until:
- Both commits pushed to `origin/master` (authorized; no PR, no force-push).
- `continue.md` updated: commits shipped, ctest count, and **Phase 3 (input-side routing apparatus)** as next with its first moves (InputMixer gains its own `MixerGraph({Tape, HardwareOutput})`, own buses/FX-returns/sends, RT traversal mirroring OutputMixer; absorbs channel→tape main-out).
