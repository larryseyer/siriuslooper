# Input-Side Routing Apparatus (Routing Phase 3) Implementation Plan

> **HISTORICAL — superseded by minimal-defaults rule (2026-05-22):** the RVB+DLY ctor-seed described below was removed. Plan body preserved as the as-implemented record at that time.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `InputMixer` its own routing apparatus — a multi-terminal `MixerGraph` (Tape + HardwareOutput), buses, FX returns (incl. default RVB + DLY), graph-stored sends, per-node main-out assignment, and an RT-safe topological render traversal — mirroring `OutputMixer` but with two terminal sinks.

**Architecture:** `InputMixer` gains the same graph/bus members `OutputMixer` has, instantiated as `MixerGraph({ Tape, HardwareOutput })` (Tape = primary). Channels and buses each carry one **main-out** (→ bus / tape terminal / hardware-output terminal) and any number of leveled **sends** (→ FX return). Unlike `OutputMixer` (whose channel→bus summing flows through a dense `sendMatrix_`), the input side uses the graph's own `setSend`/`sendLevel` storage as the single source of truth for both topology AND summing level — this **reconciles the send-summing DSP** the spec calls for and avoids a dense matrix keyed by the churning `nextChannelId_`. A new `renderInputGraph(...)` traversal gathers device inputs → per-channel `ChannelStrip` → routes each node's main-out to its destination and its sends into FX returns → walks `evaluationOrder()` to process buses/FX-returns into their destinations. Tape-terminal delivery enqueues a `TapeWriteMessage` (stereo interleaved); hardware-output-terminal delivery accumulates into the supplied direct-out buffers.

**Scope (operator-confirmed):** Engine apparatus, TDD, proven by tests. **No app rewire** — `AudioCallback`/`MainComponent` keep using today's `processBuffer`/`processDeviceInputs` paths. The new `renderInputGraph` is the substrate the Input Mixer UI phases (P6/P7) will wire to production; that migration is tracked in `todo.md`. `OutputMixer` is **untouched** (the two mixers are totally separate consoles — they reuse the generic `MixerGraph`/`Bus` *types*, never shared instances).

**Tech Stack:** C++17, JUCE-free `engine`, Catch2, CMake + Ninja, `-Werror`.

**Spec:** `docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md` (Phase 3 section lines 257–269; node model lines 27–96; engine-mapping item 4 lines 143–156). **Locked — do not re-brainstorm.**

---

## Key references (read before starting)

- `engine/include/ida/OutputMixer.h` + `engine/src/OutputMixer.cpp` — the template: parallel `channelNodeIds_`/`busNodeIds_` vectors, `addBus`, `routeBusToBus`, the 4-step `renderBuffer`. **Mirror its structure; do not edit it.**
- `engine/include/ida/Bus.h` — `Bus(BusId, BusConfig)`, `BusConfig{ channelCount, name, BusKind kind }`, `BusKind { Bus, FxReturn }`, `process(float* const* output, int numChannels, int numSamples) const noexcept` (accumulates into output, zeros its own mix buffer), `mixBufferChannel(int)`, `kMaxBusMixSamples = 8192`, `kMaxBusChannelsHard = 2`.
- `engine/include/ida/MixerGraph.h` — multi-terminal graph (Phase 2): `MixerGraph(std::initializer_list<MixerTerminal>)`, `terminalNode(MixerTerminal)`, `addNode(MixerNodeKind)` (defaults main-out to primary terminal), `setMainOut`, `setSend`, `sendLevel`, `evaluationOrder()`, `mainOutOf`, `kindOf`, `wouldMainOutCycle`.
- `engine/src/InputMixer.cpp` — current `processDeviceInputs` stereo-gather + `ChannelStrip<Audio>::process` pattern (reuse the gather logic), `processBuffer` tape enqueue, `addChannel`/`removeChannel`.
- `engine/include/ida/TapeWriter.h` — `TapeWriteMessage { ChannelId id; Rational lmcTime{0}; size_t payloadByteCount; std::array<std::byte, kMaxTapeWriteMessageBytes=32768> samples; }`; `bool tryEnqueue(const TapeWriteMessage&) noexcept`.
- `tests/InputMixerTests.cpp` — existing `[input-mixer]` harness (constructs `TapeWriter` with a temp dir, `OverloadProtection`, `NotificationBus`); reuse its fixtures.

---

## File Structure

- `engine/include/ida/InputMixer.h` — add graph/bus members, constants, `addBus`/`addFxReturn`, main-out + send API, `renderInputGraph`.
- `engine/src/InputMixer.cpp` — implement the above; constructor auto-creates RVB + DLY returns.
- `engine/include/ida/MixerGraph.h` — add a public read accessor for send edges (audio-thread, const noexcept).
- `tests/InputMixerTests.cpp` — add `[input-routing]` cases.

---

## Task 1: MixerGraph public send-edge accessor

The input traversal must read all send edges on the audio thread (to sum sources into FX returns). `MixerGraph` stores them privately. Expose a const-noexcept read view. Additive, behavior-preserving (`OutputMixer` ignores it).

**Files:**
- Modify: `engine/include/ida/MixerGraph.h`
- Test: `tests/MixerGraphTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/MixerGraphTests.cpp`:

```cpp
TEST_CASE ("MixerGraph exposes its send edges for audio-thread traversal",
           "[mixer-graph][sends]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto fxA = g.addNode (MixerNodeKind::FxReturn);
    const auto fxB = g.addNode (MixerNodeKind::FxReturn);
    REQUIRE (g.setSend (ch, fxA, 0.5f));
    REQUIRE (g.setSend (ch, fxB, 0.25f));

    const auto& edges = g.sendEdges();
    REQUIRE (edges.size() == 2);

    float toA = 0.0f, toB = 0.0f;
    for (const auto& e : edges)
    {
        CHECK (e.source == ch);
        if (e.fxReturn == fxA) toA = e.level;
        if (e.fxReturn == fxB) toB = e.level;
    }
    CHECK (toA == 0.5f);
    CHECK (toB == 0.25f);
}

static_assert (noexcept (std::declval<const MixerGraph&>().sendEdges()),
               "MixerGraph::sendEdges must be noexcept (audio-thread read)");
```

- [ ] **Step 2: Verify it fails to compile**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — no `sendEdges()`, `SendEdge` is private.

- [ ] **Step 3: Implement**

In `engine/include/ida/MixerGraph.h`, move the `SendEdge` struct from `private:` to a public nested type and add the accessor. In the `public:` section (e.g. just after `evaluationOrder()`), add:

```cpp
    /// One leveled send edge (source -> FX return). Public so the owning mixer's
    /// audio-thread traversal can sum sources into FX returns. Read-only view.
    struct SendEdge { MixerNodeId source; MixerNodeId fxReturn; float level; };

    /// Audio-thread read: all send edges. const& to a pre-built vector — no alloc.
    const std::vector<SendEdge>& sendEdges() const noexcept { return sends_; }
```

In `private:`, REMOVE the now-duplicated `struct SendEdge { ... };` line (keep `std::vector<SendEdge> sends_;`, which now refers to the public type).

- [ ] **Step 4: Verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer-graph]"`
Expected: PASS — all `[mixer-graph]` cases (was 12, now 13).

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/MixerGraph.h tests/MixerGraphTests.cpp
git commit -m "feat: MixerGraph exposes sendEdges() for audio-thread send summing"
```

---

## Task 2: InputMixer graph + bus/FX-return registry

Add the routing members and registration. The graph is multi-terminal; channels register as graph nodes (default main-out → Tape, the primary). The constructor auto-creates a default **RVB** and **DLY** FX return (empty chains — internal FX DSP is the follow-on).

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/InputMixerTests.cpp` (add `#include "ida/Bus.h"`, `#include "ida/MixerGraph.h"` near the top if absent):

```cpp
TEST_CASE ("InputMixer constructs with Tape+HardwareOutput terminals and default RVB/DLY returns",
           "[input-routing]")
{
    ida::InputMixer mixer;
    // Two FX returns auto-created (RVB, DLY); both are FxReturn-kind buses.
    CHECK (mixer.busCount() == 2);
    CHECK (mixer.busKindAt (0) == ida::BusKind::FxReturn);
    CHECK (mixer.busKindAt (1) == ida::BusKind::FxReturn);
}

TEST_CASE ("InputMixer addBus registers a graph node defaulting its main-out to the tape terminal",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    CHECK (bus.value() != 0);
    CHECK (mixer.busCount() == 3); // RVB, DLY, Drums
    // A freshly added bus routes to the tape terminal by default.
    CHECK (mixer.busMainOutIsTape (bus));
}

TEST_CASE ("InputMixer addChannel registers a Channel graph node; removeChannel drops it",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    CHECK (mixer.channelIsRegisteredInGraph (ch));
    mixer.removeChannel (ch);
    CHECK_FALSE (mixer.channelIsRegisteredInGraph (ch));
}
```

(The `busCount`/`busKindAt`/`busMainOutIsTape`/`channelIsRegisteredInGraph` accessors are message-thread test/inspection helpers added in this task. They are also genuinely useful to the UI phases for rendering strips, so they are not test-only scaffolding.)

- [ ] **Step 2: Verify it fails to compile**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — none of the new methods/members exist.

- [ ] **Step 3: Implement the header additions**

In `engine/include/ida/InputMixer.h`:
- Add includes: `#include "ida/Bus.h"` and `#include "ida/MixerGraph.h"`.
- Add public constants and methods:

```cpp
    /// Routing caps mirror the Output Mixer's. Buses/FX-returns share the count.
    static constexpr int kMaxInputChannels = 32;
    static constexpr int kMaxInputBuses    = 64;

    // Routing registry (message-thread) -----------------------------------
    BusId addBus (BusConfig config);
    /// Convenience for an FxReturn-kind bus (input is sends only).
    BusId addFxReturn (const std::string& name);

    int     busCount() const noexcept;
    BusId   busIdAt (int index) const noexcept;            // index into the bus vector
    BusKind busKindAt (int index) const noexcept;          // index into the bus vector
    bool    channelIsRegisteredInGraph (ChannelId) const noexcept;
    bool    busMainOutIsTape (BusId) const noexcept;        // inspection for tests + UI picker
```

- Add private members (after the existing maps):

```cpp
    MixerGraph graph_ { { MixerTerminal::Tape, MixerTerminal::HardwareOutput } };
    std::unordered_map<std::int64_t, MixerNodeId> channelNodeIds_; // ChannelId -> node
    std::vector<Bus>          buses_;
    std::vector<MixerNodeId>  busNodeIds_;  // parallel to buses_
    std::int64_t              nextBusId_ { 1 }; // 0 = invalid sentinel (no master on input side)

    MixerNodeId nodeForBus (BusId) const noexcept;          // private helper
```

- [ ] **Step 4: Implement the .cpp**

In `engine/src/InputMixer.cpp`:
- In the constructor body (after the scratch initializer list), reserve and auto-create the default returns:

```cpp
    buses_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    busNodeIds_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    addFxReturn ("RVB"); // default Tape main-out for now; Task 3 routes these to
    addFxReturn ("DLY"); // the hardware output (returns monitor, they don't capture).
```

**Deferred to Task 3 (the setter doesn't exist yet):** the default RVB/DLY returns
must MONITOR (hardware output), not capture — a node routed to the tape terminal
enqueues every block, so an FX return defaulting to tape would write silent tape
blocks. Task 3's first step appends two
`setBusMainOutToHardwareOutput(...)` calls to this constructor once that setter
exists. Channels still default to tape (= capture); an operator-created bus still
defaults to tape. The Task 2 default-returns test asserts only kind.

- Add the methods:

```cpp
BusId InputMixer::addBus (BusConfig config)
{
    if (buses_.size() >= static_cast<std::size_t> (kMaxInputBuses))
    {
        jassertfalse; // fail loud — losing a routing node silently corrupts the graph
        return BusId { 0 };
    }
    const BusId id { nextBusId_++ };
    buses_.emplace_back (id, config);
    const auto kind = (config.kind == BusKind::FxReturn) ? MixerNodeKind::FxReturn
                                                         : MixerNodeKind::Bus;
    busNodeIds_.push_back (graph_.addNode (kind)); // defaults main-out to primary (Tape)
    return id;
}

BusId InputMixer::addFxReturn (const std::string& name)
{
    return addBus (BusConfig { 2, name, BusKind::FxReturn });
}

int InputMixer::busCount() const noexcept { return static_cast<int> (buses_.size()); }

BusId InputMixer::busIdAt (int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (buses_.size())) return BusId { 0 };
    return buses_[static_cast<std::size_t> (index)].id();
}

BusKind InputMixer::busKindAt (int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (buses_.size())) return BusKind::Bus;
    return buses_[static_cast<std::size_t> (index)].config().kind;
}

MixerNodeId InputMixer::nodeForBus (BusId id) const noexcept
{
    for (std::size_t i = 0; i < buses_.size(); ++i)
        if (buses_[i].id() == id) return busNodeIds_[i];
    return MixerNodeId {};
}

bool InputMixer::busMainOutIsTape (BusId id) const noexcept
{
    const MixerNodeId node = nodeForBus (id);
    return node.isValid()
        && graph_.mainOutOf (node) == graph_.terminalNode (MixerTerminal::Tape);
}

bool InputMixer::channelIsRegisteredInGraph (ChannelId id) const noexcept
{
    return channelNodeIds_.find (id.value()) != channelNodeIds_.end();
}
```

- In `addChannel`, after `channels_.emplace(...)`, register the graph node and store the mapping:

```cpp
    channelNodeIds_.emplace (id.value(), graph_.addNode (MixerNodeKind::Channel));
```

- In `removeChannel`, drop the graph node + mapping (before/after the existing `channels_.erase`):

```cpp
    if (auto it = channelNodeIds_.find (id.value()); it != channelNodeIds_.end())
    {
        graph_.removeNode (it->second);
        channelNodeIds_.erase (it);
    }
```

- [ ] **Step 5: Verify**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-mixer],[input-routing]"`
(Comma = OR in this Catch2 build; space-separated bracket args are ANDed and match nothing.)
Expected: the 3 new `[input-routing]` cases PASS; all pre-existing `[input-mixer]` cases still PASS (the graph members are additive; `processBuffer`/`processDeviceInputs` are unchanged).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer gains a multi-terminal MixerGraph + bus/FX-return registry"
```

---

## Task 3: InputMixer main-out + send routing API

Expose the two signal movements: each channel/bus has one **main-out** (→ bus / tape terminal / hardware-output terminal); channels and buses have leveled **sends** (→ FX return). All delegate to `graph_` (which enforces acyclicity and rejects invalid destinations). Mutators are message-thread only.

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer default RVB/DLY returns monitor the hardware output, not tape",
           "[input-routing]")
{
    ida::InputMixer mixer;
    // The two auto-created returns are buses_[0] (RVB) and buses_[1] (DLY).
    CHECK (mixer.busMainOut (mixer.busIdAt (0)) == ida::InputMixer::MainOutDest::HardwareOutput);
    CHECK (mixer.busMainOut (mixer.busIdAt (1)) == ida::InputMixer::MainOutDest::HardwareOutput);
}

TEST_CASE ("InputMixer routes a channel main-out to a bus, the tape, or a hardware output",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });

    SECTION ("default is the tape terminal")
    {
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Tape);
    }
    SECTION ("to a bus")
    {
        CHECK (mixer.setChannelMainOutToBus (ch, bus));
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Bus);
    }
    SECTION ("to the hardware output (RME direct-out monitoring)")
    {
        CHECK (mixer.setChannelMainOutToHardwareOutput (ch));
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::HardwareOutput);
    }
}

TEST_CASE ("InputMixer rejects a channel send to a non-FX-return and accepts one to an FX return",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto rvb = mixer.addFxReturn ("RVB2");

    CHECK_FALSE (mixer.setChannelSend (ch, bus, 0.5f)); // a Bus is not an FX return
    CHECK (mixer.setChannelSend (ch, rvb, 0.5f));
    CHECK (mixer.channelSendLevel (ch, rvb) == 0.5f);
}

TEST_CASE ("InputMixer rejects a bus main-out assignment that would create a cycle",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto a = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b)); // A -> B
    CHECK_FALSE (mixer.setBusMainOutToBus (b, a)); // closing the loop is refused
}
```

- [ ] **Step 2: Verify it fails to compile**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `MainOutDest`, the setters, and the accessors don't exist.

- [ ] **Step 3: Implement the header additions**

In `engine/include/ida/InputMixer.h`, add (public):

```cpp
    /// Where a node's single full-level main-out goes. Tape = capture terminal
    /// (primary/default); HardwareOutput = RME-TotalMix direct-out monitoring;
    /// Bus = a subgroup. (Input channels never route to "master" — there is none.)
    enum class MainOutDest { Tape, HardwareOutput, Bus };

    // Main-out (message-thread). Return false on unknown id / cycle / bad dest.
    bool setChannelMainOutToBus (ChannelId, BusId);
    bool setChannelMainOutToHardwareOutput (ChannelId);
    bool setChannelMainOutToTape (ChannelId);
    bool setBusMainOutToBus (BusId from, BusId to);
    bool setBusMainOutToHardwareOutput (BusId);
    bool setBusMainOutToTape (BusId);
    MainOutDest channelMainOut (ChannelId) const noexcept;
    MainOutDest busMainOut (BusId) const noexcept;

    // Sends (message-thread). source = channel|bus, dest must be an FX return.
    bool  setChannelSend (ChannelId, BusId fxReturn, float level);
    bool  setBusSend (BusId source, BusId fxReturn, float level);
    float channelSendLevel (ChannelId, BusId fxReturn) const noexcept;
```

Add private helpers:

```cpp
    MixerNodeId nodeForChannel (ChannelId) const noexcept;
    MainOutDest classifyMainOut (MixerNodeId dest) const noexcept;
```

- [ ] **Step 4: Implement the .cpp**

First, route the auto-created default returns to the hardware output (now that the
setter exists). In the `InputMixer` constructor, change the two return-creation
lines to:

```cpp
    setBusMainOutToHardwareOutput (addFxReturn ("RVB"));
    setBusMainOutToHardwareOutput (addFxReturn ("DLY"));
```

Then add the routing methods:

```cpp
MixerNodeId InputMixer::nodeForChannel (ChannelId id) const noexcept
{
    if (auto it = channelNodeIds_.find (id.value()); it != channelNodeIds_.end())
        return it->second;
    return MixerNodeId {};
}

InputMixer::MainOutDest InputMixer::classifyMainOut (MixerNodeId dest) const noexcept
{
    if (dest == graph_.terminalNode (MixerTerminal::Tape))            return MainOutDest::Tape;
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput))  return MainOutDest::HardwareOutput;
    return MainOutDest::Bus;
}

bool InputMixer::setChannelMainOutToBus (ChannelId ch, BusId bus)
{
    return graph_.setMainOut (nodeForChannel (ch), nodeForBus (bus));
}
bool InputMixer::setChannelMainOutToHardwareOutput (ChannelId ch)
{
    return graph_.setMainOut (nodeForChannel (ch),
                              graph_.terminalNode (MixerTerminal::HardwareOutput));
}
bool InputMixer::setChannelMainOutToTape (ChannelId ch)
{
    return graph_.setMainOut (nodeForChannel (ch),
                              graph_.terminalNode (MixerTerminal::Tape));
}
bool InputMixer::setBusMainOutToBus (BusId from, BusId to)
{
    return graph_.setMainOut (nodeForBus (from), nodeForBus (to));
}
bool InputMixer::setBusMainOutToHardwareOutput (BusId bus)
{
    return graph_.setMainOut (nodeForBus (bus),
                              graph_.terminalNode (MixerTerminal::HardwareOutput));
}
bool InputMixer::setBusMainOutToTape (BusId bus)
{
    return graph_.setMainOut (nodeForBus (bus),
                              graph_.terminalNode (MixerTerminal::Tape));
}
InputMixer::MainOutDest InputMixer::channelMainOut (ChannelId ch) const noexcept
{
    return classifyMainOut (graph_.mainOutOf (nodeForChannel (ch)));
}
InputMixer::MainOutDest InputMixer::busMainOut (BusId bus) const noexcept
{
    return classifyMainOut (graph_.mainOutOf (nodeForBus (bus)));
}
bool InputMixer::setChannelSend (ChannelId ch, BusId fxReturn, float level)
{
    return graph_.setSend (nodeForChannel (ch), nodeForBus (fxReturn), level);
}
bool InputMixer::setBusSend (BusId source, BusId fxReturn, float level)
{
    return graph_.setSend (nodeForBus (source), nodeForBus (fxReturn), level);
}
float InputMixer::channelSendLevel (ChannelId ch, BusId fxReturn) const noexcept
{
    return graph_.sendLevel (nodeForChannel (ch), nodeForBus (fxReturn));
}
```

- [ ] **Step 5: Verify**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-routing]"`
Expected: the new cases PASS (`setChannelSend` to a Bus returns false because `graph_.setSend` rejects a non-FxReturn dest; the cycle case returns false because `graph_.setMainOut` rejects it).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer main-out (bus/tape/hardware-output) + send routing API"
```

---

## Task 4: renderInputGraph — channel main-out + send delivery (Steps 1–2)

The RT-safe traversal. This task delivers **channel** outputs: gather device inputs → `ChannelStrip<Audio>::process` → route the processed block to the channel's main-out destination (bus mix buffer / tape enqueue / direct-out accumulate) and accumulate its sends into FX-return mix buffers. (Bus/FX-return processing is Task 5.)

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/InputMixerTests.cpp`. The harness already builds a `TapeWriter` over a temp dir (copy that setup from the existing `[process-buffer]`/`[finalize]` cases). A channel is fed by binding a 2-channel `deviceIn` and a source descriptor.

```cpp
TEST_CASE ("renderInputGraph: default-graph tape-routed channel enqueues its processed block",
           "[input-routing][render]")
{
    // TapeWriter over a temp dir (mirror the existing [process-buffer] fixture).
    const auto dir = makeTempTapeDir(); // existing test helper / inline as in other cases
    ida::TapeWriter writer (dir, /*queueCapacity*/ 8);
    ida::InputMixer mixer;
    mixer.setTapeWriter (&writer);

    const auto ch = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, /*stereo*/ true);
    mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    // Default main-out is the Tape terminal — no routing call needed.

    constexpr int n = 64;
    std::vector<float> left (n, 0.5f), right (n, 0.25f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { nullptr, nullptr };

    mixer.renderInputGraph (deviceIn, 2, directOut, 0, n);

    // Flush + read the partial: it contains n stereo frames (interleaved float32).
    const auto partial = writer.flushChannel (ch);
    CHECK (std::filesystem::file_size (partial) == static_cast<std::uintmax_t> (n) * 2 * sizeof (float));
}

TEST_CASE ("renderInputGraph: a channel routed to the hardware output sums into direct-out, not tape",
           "[input-routing][render]")
{
    const auto dir = makeTempTapeDir();
    ida::TapeWriter writer (dir, 8);
    ida::InputMixer mixer;
    mixer.setTapeWriter (&writer);

    const auto ch = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch)); // direct-out, NOT tape

    constexpr int n = 32;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    // Assert ROUTING, not exact DSP: the strip applies an equal-power pan law
    // (centre ≈ 0.707, not unity), so don't assert exact sample values. The point
    // is that signal reached direct-out and NOT the tape.
    CHECK (outL[0] != 0.0f);
    CHECK (outR[0] != 0.0f);
    const auto partial = writer.flushChannel (ch);
    CHECK (std::filesystem::file_size (partial) == 0u); // routed to hardware output, not tape
}
```

(Factor a `makeTempTapeDir()` helper at the top of the test file if one doesn't exist; the existing tape cases already create temp dirs — reuse that exact idiom.)

- [ ] **Step 2: Verify it fails to compile**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `renderInputGraph` doesn't exist.

- [ ] **Step 3: Implement**

In `engine/include/ida/InputMixer.h` add (public, audio-thread):

```cpp
    /// Audio-thread render of the full input routing graph. Gathers each channel's
    /// device source through its ChannelStrip, then routes each node's main-out to
    /// its destination (bus mix buffer / tape enqueue / direct-out) and its sends
    /// into FX returns, walking the graph's topological evaluation order. Tape
    /// delivery enqueues a stereo-interleaved TapeWriteMessage; hardware-output
    /// delivery accumulates into directOut. RT-safe: no alloc, no locks, no I/O,
    /// noexcept. directOut may be null/0 when no hardware-output route is active.
    void renderInputGraph (const float* const* deviceIn, int numDeviceChannels,
                           float* const* directOut, int numDirectOutChannels,
                           int numSamples) noexcept;
```

Add a private helper to enqueue a stereo block to the tape terminal:

```cpp
    void enqueueToTape (ChannelId, const float* left, const float* right, int numSamples) noexcept;
```

In `engine/src/InputMixer.cpp`:

```cpp
void InputMixer::enqueueToTape (ChannelId id, const float* left, const float* right,
                                int numSamples) noexcept
{
    if (tapeWriter_ == nullptr) return;

    // Stereo interleaved float32 → 2*sizeof(float) per frame, clamped to the
    // message capacity (kMaxTapeWriteMessageBytes = 32768 = 4096 stereo frames).
    constexpr int kMaxFrames = static_cast<int> (kMaxTapeWriteMessageBytes
                                                  / (2 * sizeof (float)));
    const int frames = std::min (numSamples, kMaxFrames);

    TapeWriteMessage msg;
    msg.id = id;
    msg.lmcTime = Rational (0);
    msg.payloadByteCount = static_cast<std::size_t> (frames) * 2 * sizeof (float);
    float* out = reinterpret_cast<float*> (msg.samples.data());
    for (int s = 0; s < frames; ++s) { out[2*s] = left[s]; out[2*s + 1] = right[s]; }

    if (! tapeWriter_->tryEnqueue (msg))
    {
        if (notificationBus_ != nullptr)
            notificationBus_->post (NotificationLevel::Warning, Category::CpuPressure,
                                    "audio thread missed deadline — tape buffer dropped");
        if (overload_ != nullptr) overload_->reportLoad (1.0);
    }
}

void InputMixer::renderInputGraph (const float* const* deviceIn, int numDeviceChannels,
                                   float* const* directOut, int numDirectOutChannels,
                                   int numSamples) noexcept
{
    if (deviceIn == nullptr || numDeviceChannels <= 0 || numSamples <= 0) return;
    const int n = std::min (numSamples, static_cast<int> (kMaxScratchSamples));

    const MixerNodeId tapeNode = graph_.terminalNode (MixerTerminal::Tape);
    const MixerNodeId hwNode   = graph_.terminalNode (MixerTerminal::HardwareOutput);

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
        const MixerNodeId dest    = graph_.mainOutOf (chNode);

        // Main-out delivery.
        if (dest == tapeNode)
        {
            if (channel.tapeMode != TapeMode::NoTape)
                enqueueToTape (channel.id, scratchLeft_.data(), scratchRight_.data(), n);
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

        // Sends → FX returns (leveled). Read levels from the graph.
        for (const auto& e : graph_.sendEdges())
        {
            if (e.source != chNode) continue;
            accumulateIntoBus (e.fxReturn, scratchLeft_.data(), scratchRight_.data(), e.level, n);
        }
    }

    // Step 3 (bus/FX-return processing) is added in Task 5.
}
```

Add the `accumulateIntoBus` private helper (used by both channel routing here and Task 5):

```cpp
    void accumulateIntoBus (MixerNodeId busNode, const float* left, const float* right,
                            float level, int numSamples) noexcept;
```

```cpp
void InputMixer::accumulateIntoBus (MixerNodeId busNode, const float* left, const float* right,
                                    float level, int numSamples) noexcept
{
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
    {
        if (busNodeIds_[i] != busNode) continue;
        float* bl = buses_[i].mixBufferChannel (0);
        float* br = buses_[i].mixBufferChannel (1);
        if (bl == nullptr || br == nullptr) return;
        for (int s = 0; s < numSamples; ++s) { bl[s] += left[s] * level; br[s] += right[s] * level; }
        return;
    }
}
```

- [ ] **Step 4: Verify**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-routing]"`
Expected: the two `[render]` cases PASS (tape-routed channel → partial sized `n*2*sizeof(float)`; hardware-output-routed channel → direct-out carries the unity-strip signal, tape partial empty).

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: renderInputGraph channel main-out + send delivery (tape/hardware-output/bus)"
```

---

## Task 5: renderInputGraph — bus/FX-return processing (Step 3) + RT-safety assert

Walk `evaluationOrder()`; for each registered bus/FX-return node, process it (`Bus::process`, which runs its effect chain and zeros its own mix buffer) into its main-out destination: another bus's mix buffer, the tape terminal (enqueue), or the hardware-output terminal (direct-out). Add the audio-thread `noexcept` static_assert.

**Files:**
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("renderInputGraph: channel -> bus -> tape delivers the bus output to tape",
           "[input-routing][render]")
{
    const auto dir = makeTempTapeDir();
    ida::TapeWriter writer (dir, 8);
    ida::InputMixer mixer;
    mixer.setTapeWriter (&writer);

    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    // We need a tape ChannelId for the bus's tape delivery — buses enqueue under
    // their own BusId-derived ChannelId (see implementation note).
    REQUIRE (mixer.setChannelMainOutToBus (ch, bus));   // channel -> bus
    REQUIRE (mixer.setBusMainOutToTape (bus));          // bus -> tape

    constexpr int n = 48;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { nullptr, nullptr };

    mixer.renderInputGraph (deviceIn, 2, directOut, 0, n);

    const auto partial = writer.flushChannel (ida::ChannelId { bus.value() });
    CHECK (std::filesystem::file_size (partial) == static_cast<std::uintmax_t> (n) * 2 * sizeof (float));
}

TEST_CASE ("renderInputGraph: a channel send reaches an FX return, which delivers to direct-out",
           "[input-routing][render]")
{
    ida::InputMixer mixer;
    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    const auto rvb = mixer.addFxReturn ("RVB2");
    // Isolate the SEND path: the channel's DRY main-out goes to the TAPE (so it
    // does NOT reach direct-out), while the send -> FX return -> hardware output is
    // the ONLY path that can put signal on direct-out. Non-zero direct-out then
    // proves the send + FX-return processing reached the hardware output.
    REQUIRE (mixer.setChannelMainOutToTape (ch));        // dry -> tape (off direct-out)
    REQUIRE (mixer.setChannelSend (ch, rvb, 1.0f));      // wet send at unity
    REQUIRE (mixer.setBusMainOutToHardwareOutput (rvb)); // FX return -> direct-out

    constexpr int n = 16;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    // The FX return has no DSP yet (empty chain = pass-through); the only way
    // direct-out is non-zero is the send -> FX-return -> hardware-output path.
    CHECK (outL[0] != 0.0f);
    CHECK (outR[0] != 0.0f);
}
```

- [ ] **Step 2: Verify it fails**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-routing][render]"`
Expected: FAIL — the bus/FX-return outputs are never processed (Step 3 missing), so tape partial is empty and direct-out only has the dry 0.5.

- [ ] **Step 3: Implement Step 3 in `renderInputGraph`**

Replace the `// Step 3 ... is added in Task 5.` comment with the traversal. It mirrors `OutputMixer::renderBuffer` Step 3 but resolves three destination kinds:

```cpp
    // ── Step 3: process each bus / FX return into its main-out destination ──
    // RT-safety: evaluationOrder() is a const& to a pre-built vector (no alloc);
    // busNodeIds_ lookups are bounded linear scans; no graph mutators are called.
    for (const MixerNodeId nodeId : graph_.evaluationOrder())
    {
        std::size_t busIdx = busNodeIds_.size();
        for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
            if (busNodeIds_[i] == nodeId) { busIdx = i; break; }
        if (busIdx >= busNodeIds_.size()) continue; // channel or terminal node

        const Bus& bus        = buses_[busIdx];
        const MixerNodeId dest = graph_.mainOutOf (nodeId);

        if (dest == tapeNode)
        {
            // Process into the L/R scratch, then enqueue under the bus's id.
            std::memset (scratchLeft_.data(),  0, static_cast<std::size_t> (n) * sizeof (float));
            std::memset (scratchRight_.data(), 0, static_cast<std::size_t> (n) * sizeof (float));
            float* sc[2] { scratchLeft_.data(), scratchRight_.data() };
            bus.process (sc, 2, n);
            if (tapeWriter_ != nullptr)
                enqueueToTape (ChannelId { bus.id().value() },
                               scratchLeft_.data(), scratchRight_.data(), n);
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
```

Note: the scratch reuse in the tape branch is safe — by Step 3 all channel gathering is complete, so `scratchLeft_`/`scratchRight_` are free. The `memset` before `Bus::process` is required because `Bus::process` *accumulates* into the destination.

- [ ] **Step 4: Add the RT-safety static_assert**

In `tests/InputMixerTests.cpp` near the top:

```cpp
static_assert (noexcept (std::declval<ida::InputMixer&>().renderInputGraph (
                   nullptr, 0, nullptr, 0, 0)),
               "InputMixer::renderInputGraph must be noexcept (RT-safety contract §6)");
```

- [ ] **Step 5: Verify**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-routing],[input-mixer]"`
Expected: all `[input-routing]` cases PASS (bus→tape partial sized `n*2*sizeof(float)`; the send-isolated direct-out is non-zero); all pre-existing `[input-mixer]` cases still PASS.

- [ ] **Step 6: Full suite + commit**

Run: `ctest --test-dir build`
Expected: baseline green (483 + the new `[input-routing]` cases passing; the only non-pass remains `MainComponentPluginEditorTests_NOT_BUILT`).

```bash
git add engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: renderInputGraph bus/FX-return processing into bus/tape/hardware-output"
```

---

## Verification (end-to-end)

1. `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release` (engine-only; no GUI bundle).
2. `cmake --build build --target IdaTests` — compiles clean under `-Werror`.
3. `./build/tests/IdaTests "[input-routing]"` — all routing + render cases pass.
4. `./build/tests/IdaTests "[input-mixer]"` and `"[mixer-graph]"` and `"[output-mixer]"` — no regressions (OutputMixer untouched; InputMixer's existing tape/metering paths unchanged).
5. `ctest --test-dir build` — full suite green except `MainComponentPluginEditorTests_NOT_BUILT`.
6. No operator eyes-on / `rm -rf build` — engine logic only, no GUI surface touched.

## Honest scope notes (record in todo.md at close-out)

- **`renderInputGraph` is not yet wired into production.** `AudioCallback`/`MainComponent` still call `processBuffer` (per-device-channel tape) + `processDeviceInputs` (metering). The new traversal is exercised only by tests until the Input Mixer UI (P6/P7) provides the bus/route creation gestures and migrates the audio callback. This matches the repo's established "tested seam, production wiring follows" pattern (e.g., the M8 S4 WetCapture sink).
- **Bus→tape delivery enqueues under a `ChannelId` derived from the bus's `BusId`.** The `TapeId`/channel-vs-bus tape-identity mapping is an M11 IAF concern; this is a reasonable interim key (parallel to the existing `TapeId → content` deferral).
- **FX returns have no DSP.** RVB/DLY are empty `EffectChain`s (pass-through) until the internal-FX follow-on spec. Sends therefore deliver dry signal through the return for now — the wiring is what Phase 3 proves.
- **Send pre/post-fader:** post-fader only (sends read the post-strip scratch). The pre/post toggle remains an Open Item.

## Close-out (mandatory per operator instruction)

Phase 3 is NOT complete until:
- All commits pushed to `origin/master` (authorized; no PR, no force-push).
- `todo.md` updated with the scope notes above.
- `continue.md` updated: commits shipped, ctest count, and **Phase 4 (per-node insert chains)** as next with its first moves (channels gain an `EffectChain` + `IEffectChainHost` dispatch like `Bus` already has; 8-slot cap on every node; external VST/CLAP via the M7 host; built-in FX deferred to the follow-on).
