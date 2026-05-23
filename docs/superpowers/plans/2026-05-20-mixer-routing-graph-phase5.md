# Mixer Routing-Graph Phase 5: Routing-Graph Persistence — Implementation Plan

> **HISTORICAL — superseded by minimal-defaults rule (2026-05-22):** the InputMixer ctor no longer seeds RVB(busId 1)/DLY(busId 2); the "reuse ctor-seeded buses on import" path described below collapsed to "every persisted bus is minted via addBus". Plan body preserved as the as-implemented record at that time.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make each mixer's routing graph (buses, FX returns, main-out assignments, send levels, terminal assignments, per-node insert chains) survive a session save/load round-trip, while pre-graph sessions still load clean.

**Architecture:** A JUCE-free plain-data snapshot type lives in `core/` (`MixerGraphState.h`). The engine gets member functions `exportGraphState()` / `importGraphState()` on `InputMixer` and `OutputMixer` that translate live mixer state ↔ the snapshot (members, so they read/write private state directly — no bulk accessor surface). Persistence gets `serializeMixerGraphState()` / `deserializeMixerGraphState()` in `SessionFormat.cpp` that translate the snapshot ↔ JSON, reusing the existing anonymous-namespace helpers. The existing `serializeSession(const Constituent&)` API is untouched.

**Tech stack:** C++17, JUCE (`juce::var`/`juce::JSON` in persistence only), Catch2 tests, CMake/Ninja. Layering: `IdaPersistence` links only `Ida::Core` (JUCE-free core); `IdaEngine` links `Ida::Persistence` privately. So the snapshot MUST live in core and carry only JUCE-free, engine-free types — persistence cannot include engine headers (cycle).

**Scope note:** This ships **apparatus + tests**, not `MainComponent` wiring. Writing/reading the graph sections into the on-disk session file on actual save/load lands with the UI phases (P6/P7), matching Phase 4's posture. Record that deferral in `todo.md`.

---

## File structure

- **Create** `core/include/ida/MixerGraphState.h` — the snapshot value types + `operator==`. Header-only POD. JUCE-free, engine-free. Includes only `EffectChain.h`, `SignalType.h`, `TapeMode.h` (all already in core).
- **Modify** `engine/include/ida/InputMixer.h` / `engine/src/InputMixer.cpp` — add `setBusEffectChain`, `exportGraphState`, `importGraphState` + private helpers.
- **Modify** `engine/include/ida/OutputMixer.h` / `engine/src/OutputMixer.cpp` — add `exportGraphState`, `importGraphState` + private helpers.
- **Modify** `persistence/include/ida/SessionFormat.h` / `persistence/src/SessionFormat.cpp` — add the four serialize/deserialize free functions + their `ToVar`/`FromVar` helpers.
- **Create** `tests/MixerGraphStateTests.cpp` — snapshot equality (`[mixergraphstate]`).
- **Modify** `tests/InputMixerTests.cpp`, `tests/OutputMixerTests.cpp` — engine export/import round-trip.
- **Modify** `tests/SessionFormatTests.cpp` — snapshot serialize/deserialize round-trip + forward-compat + malformed (`[sessionformat]`).
- **Create** `tests/MixerGraphPersistenceTests.cpp` — full both-mixer integration (`[sessionformat][mixer]`).
- **Modify** `tests/CMakeLists.txt` — register the two new test files.

**Confirmed enum values** (for the persistence string mappings): `SignalType { Audio, Midi, Video, File }`; `TapeMode { CommitToTape, NonDestructive, NoTape }`; `BusKind { Bus, FxReturn }` (engine, `Bus.h:20`); `MixerTerminal { Tape, HardwareOutput }` (engine, `MixerGraph.h:18`). The core snapshot mirrors `BusKind`/`MixerTerminal` as `MixerBusKind`/`MixerTerminalKind`; the engine translates at the seam.

**Build/run after each task:**
```bash
cmake --build build --target IdaTests
./build/tests/IdaTests "[mixergraphstate]"   # (substitute the task's tag)
```
If `build/` does not exist: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release` first.

---

## Task 0: Core snapshot value type + equality

**Files:**
- Create: `core/include/ida/MixerGraphState.h`
- Create test: `tests/MixerGraphStateTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the snapshot header**

Create `core/include/ida/MixerGraphState.h`:

```cpp
#pragma once

#include "ida/EffectChain.h"
#include "ida/SignalType.h"
#include "ida/TapeMode.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sirius
{

/// Routing-graph Phase 5 — a JUCE-free, engine-free plain-data snapshot of one
/// mixer's routing graph, for persistence. The engine exports a live mixer into
/// this and imports it back; persistence serializes it to/from JSON. Core stays
/// engine-free, so the engine enums BusKind / MixerTerminal are mirrored here as
/// MixerBusKind / MixerTerminalKind and translated at the engine export/import seam.

enum class MixerBusKind { Bus, FxReturn };
enum class MixerTerminalKind { Tape, HardwareOutput };

/// A node's single main-out destination: a terminal (tape / hardware-output) or
/// another bus (subgroup / master).
struct MixerMainOut
{
    enum class Kind { Terminal, Bus };
    Kind              kind     { Kind::Terminal };
    MixerTerminalKind terminal { MixerTerminalKind::Tape }; // valid when kind == Terminal
    std::int64_t      busId    { 0 };                       // valid when kind == Bus

    bool operator== (const MixerMainOut& o) const noexcept
    { return kind == o.kind && terminal == o.terminal && busId == o.busId; }
    bool operator!= (const MixerMainOut& o) const noexcept { return ! (*this == o); }
};

/// A leveled send into another bus (an FX return on the input side; any bus on
/// the output-channel send matrix).
struct MixerSend
{
    std::int64_t busId { 0 };
    float        level { 0.0f };

    bool operator== (const MixerSend& o) const noexcept
    { return busId == o.busId && level == o.level; }
    bool operator!= (const MixerSend& o) const noexcept { return ! (*this == o); }
};

/// Which device channel(s) feed an input channel's stereo strip (RME model).
struct MixerChannelSource
{
    int  left   { 0 };
    int  right  { 1 };
    bool stereo { true };

    bool operator== (const MixerChannelSource& o) const noexcept
    { return left == o.left && right == o.right && stereo == o.stereo; }
    bool operator!= (const MixerChannelSource& o) const noexcept { return ! (*this == o); }
};

/// One bus or FX return: identity, config, graph main-out, sends, insert chain.
/// Shared by both mixers. On the output side, buses[0] is the master (busId 0)
/// and bus sends are empty (output routing uses main-out + the channel matrix).
struct MixerBusState
{
    std::int64_t           busId        { 0 };
    int                    channelCount { 2 };
    std::string            name;
    MixerBusKind           kind         { MixerBusKind::Bus };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const MixerBusState& o) const noexcept
    {
        return busId == o.busId && channelCount == o.channelCount && name == o.name
            && kind == o.kind && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts;
    }
    bool operator!= (const MixerBusState& o) const noexcept { return ! (*this == o); }
};

/// Input-side channel: input source id, device source, tape mode, single
/// main-out (tape / hardware-output / bus), sends into FX returns, insert chain.
struct InputChannelState
{
    std::int64_t           channelId     { 0 };
    SignalType             signalType    { SignalType::Audio };
    std::int64_t           inputSourceId { 0 };
    MixerChannelSource     source;
    TapeMode               tapeMode      { TapeMode::NoTape };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const InputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && inputSourceId == o.inputSourceId && source == o.source
            && tapeMode == o.tapeMode && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts;
    }
    bool operator!= (const InputChannelState& o) const noexcept { return ! (*this == o); }
};

/// Output-side channel: routes into buses via the send matrix (no single
/// main-out, no input source / tape mode).
struct OutputChannelState
{
    std::int64_t           channelId  { 0 };
    SignalType             signalType { SignalType::Audio };
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const OutputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && sends == o.sends && inserts == o.inserts;
    }
    bool operator!= (const OutputChannelState& o) const noexcept { return ! (*this == o); }
};

struct InputMixerGraphState
{
    std::vector<MixerBusState>     buses;
    std::vector<InputChannelState> channels;
    std::int64_t                   nextBusId     { 1 };
    std::int64_t                   nextChannelId { 1 };

    bool operator== (const InputMixerGraphState& o) const noexcept
    {
        return buses == o.buses && channels == o.channels
            && nextBusId == o.nextBusId && nextChannelId == o.nextChannelId;
    }
    bool operator!= (const InputMixerGraphState& o) const noexcept { return ! (*this == o); }
};

struct OutputMixerGraphState
{
    std::vector<MixerBusState>      buses;     // buses[0] == master (busId 0)
    std::vector<OutputChannelState> channels;
    std::int64_t                    nextBusId     { 1 };
    std::int64_t                    nextChannelId { 1 };

    bool operator== (const OutputMixerGraphState& o) const noexcept
    {
        return buses == o.buses && channels == o.channels
            && nextBusId == o.nextBusId && nextChannelId == o.nextChannelId;
    }
    bool operator!= (const OutputMixerGraphState& o) const noexcept { return ! (*this == o); }
};

} // namespace sirius
```

- [ ] **Step 2: Write the failing equality test**

Create `tests/MixerGraphStateTests.cpp`:

```cpp
#include "ida/MixerGraphState.h"

#include <catch2/catch_test_macros.hpp>

using namespace sirius;

namespace
{
    EffectChainEntry makeEntry (const std::string& name)
    {
        EffectChainEntry e;
        e.displayName = name;
        return e;
    }

    InputMixerGraphState populatedInput()
    {
        InputMixerGraphState s;
        MixerBusState bus;
        bus.busId = 1; bus.name = "Drums"; bus.kind = MixerBusKind::Bus;
        bus.mainOut.kind = MixerMainOut::Kind::Terminal;
        bus.mainOut.terminal = MixerTerminalKind::Tape;
        bus.inserts = EffectChain{}.withAppended (makeEntry ("comp"));
        s.buses.push_back (bus);

        InputChannelState ch;
        ch.channelId = 5; ch.signalType = SignalType::Audio; ch.inputSourceId = 2;
        ch.source = { 2, 3, true };
        ch.tapeMode = TapeMode::CommitToTape;
        ch.mainOut.kind = MixerMainOut::Kind::Bus; ch.mainOut.busId = 1;
        ch.sends.push_back ({ 7, 0.5f });
        ch.inserts = EffectChain{}.withAppended (makeEntry ("eq"));
        s.channels.push_back (ch);

        s.nextBusId = 2; s.nextChannelId = 6;
        return s;
    }
}

TEST_CASE ("equal InputMixerGraphStates compare equal", "[mixergraphstate]")
{
    CHECK (populatedInput() == populatedInput());
}

TEST_CASE ("InputMixerGraphState inequality is detected per field", "[mixergraphstate]")
{
    SECTION ("bus name")        { auto s = populatedInput(); s.buses[0].name = "X";              CHECK (s != populatedInput()); }
    SECTION ("bus kind")        { auto s = populatedInput(); s.buses[0].kind = MixerBusKind::FxReturn; CHECK (s != populatedInput()); }
    SECTION ("bus main-out")    { auto s = populatedInput(); s.buses[0].mainOut.terminal = MixerTerminalKind::HardwareOutput; CHECK (s != populatedInput()); }
    SECTION ("bus inserts")     { auto s = populatedInput(); s.buses[0].inserts = EffectChain{}; CHECK (s != populatedInput()); }
    SECTION ("channel main-out"){ auto s = populatedInput(); s.channels[0].mainOut.busId = 99;  CHECK (s != populatedInput()); }
    SECTION ("send level")      { auto s = populatedInput(); s.channels[0].sends[0].level = 0.9f; CHECK (s != populatedInput()); }
    SECTION ("source")          { auto s = populatedInput(); s.channels[0].source.stereo = false; CHECK (s != populatedInput()); }
    SECTION ("tape mode")       { auto s = populatedInput(); s.channels[0].tapeMode = TapeMode::NoTape; CHECK (s != populatedInput()); }
    SECTION ("channel inserts") { auto s = populatedInput(); s.channels[0].inserts = EffectChain{}; CHECK (s != populatedInput()); }
    SECTION ("nextBusId")       { auto s = populatedInput(); s.nextBusId = 99;                   CHECK (s != populatedInput()); }
    SECTION ("nextChannelId")   { auto s = populatedInput(); s.nextChannelId = 99;               CHECK (s != populatedInput()); }
}

TEST_CASE ("OutputMixerGraphState equality and per-field inequality", "[mixergraphstate]")
{
    OutputMixerGraphState a;
    MixerBusState master; master.busId = 0; master.name = "Master";
    master.mainOut.kind = MixerMainOut::Kind::Terminal;
    master.mainOut.terminal = MixerTerminalKind::HardwareOutput;
    a.buses.push_back (master);
    OutputChannelState ch; ch.channelId = 1; ch.sends.push_back ({ 0, 1.0f });
    a.channels.push_back (ch);
    a.nextBusId = 1; a.nextChannelId = 2;

    CHECK (a == a);
    SECTION ("send")    { auto b = a; b.channels[0].sends[0].level = 0.3f; CHECK (a != b); }
    SECTION ("master name") { auto b = a; b.buses[0].name = "M"; CHECK (a != b); }
    SECTION ("nextChannelId") { auto b = a; b.nextChannelId = 9; CHECK (a != b); }
}
```

- [ ] **Step 3: Register the test file**

In `tests/CMakeLists.txt`, add `MixerGraphStateTests.cpp` to the `add_executable(IdaTests ...)` source list (alongside `EffectChainTests.cpp` at line 33):

```cmake
    EffectChainTests.cpp
    MixerGraphStateTests.cpp
```

- [ ] **Step 4: Run the test — verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixergraphstate]"`
Expected: PASS (all sections).

- [ ] **Step 5: Commit**

```bash
git add core/include/ida/MixerGraphState.h tests/MixerGraphStateTests.cpp tests/CMakeLists.txt
git commit -m "feat: core MixerGraphState snapshot type + equality"
```

---

## Task 1: InputMixer export

**Files:**
- Modify: `engine/include/ida/InputMixer.h`
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing export test**

Add to `tests/InputMixerTests.cpp` (include `"ida/MixerGraphState.h"` and `"ida/ChannelStrip.h"` at the top if not already present):

```cpp
TEST_CASE ("InputMixer exportGraphState reflects buses, routing, sends, inserts", "[input-mixer][persistence]")
{
    ida::InputMixer mixer;
    mixer.registerInput (ida::InputId (1), ida::InputDescriptor{});

    const auto drums = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto reverb = mixer.addFxReturn ("Reverb");

    ida::EffectChainEntry comp; comp.displayName = "comp";
    mixer.setBusEffectChain (drums, ida::EffectChain{}.withAppended (comp));

    const auto ch = mixer.addChannel (ida::InputId (1), ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 2, 3, true);
    mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    mixer.setChannelMainOutToBus (ch, drums);
    mixer.setChannelSend (ch, reverb, 0.5f);

    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<ida::ChannelStrip<ida::SignalType::Audio>*> (chain);
    ida::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (ida::EffectChain{}.withAppended (eq));

    const auto state = mixer.exportGraphState();

    REQUIRE (state.buses.size() == 2);
    // Buses are exported in registration order: drums then reverb.
    CHECK (state.buses[0].busId == drums.value());
    CHECK (state.buses[0].name == "Drums");
    CHECK (state.buses[0].kind == ida::MixerBusKind::Bus);
    CHECK (state.buses[0].inserts.size() == 1);
    CHECK (state.buses[1].kind == ida::MixerBusKind::FxReturn);

    REQUIRE (state.channels.size() == 1);
    const auto& c = state.channels[0];
    CHECK (c.channelId == ch.value());
    CHECK (c.inputSourceId == 1);
    CHECK (c.source == ida::MixerChannelSource { 2, 3, true });
    CHECK (c.tapeMode == ida::TapeMode::CommitToTape);
    CHECK (c.mainOut.kind == ida::MixerMainOut::Kind::Bus);
    CHECK (c.mainOut.busId == drums.value());
    REQUIRE (c.sends.size() == 1);
    CHECK (c.sends[0].busId == reverb.value());
    CHECK (c.sends[0].level == 0.5f);
    CHECK (c.inserts.size() == 1);
}
```

- [ ] **Step 2: Run it — verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL to compile — `setBusEffectChain` and `exportGraphState` are not members of `InputMixer`.

- [ ] **Step 3: Declare the new members**

In `engine/include/ida/InputMixer.h`, add `#include "ida/MixerGraphState.h"` to the include block, and in the public section (after `setBusSend`, near line 67):

```cpp
    /// Message-thread setter — copies the chain into the named bus (parity
    /// with OutputMixer::setBusEffectChain). No-op if the BusId is unknown.
    void setBusEffectChain (BusId id, EffectChain chain);

    /// Message-thread snapshot of the entire routing graph for persistence
    /// (routing-graph Phase 5). Reads buses, FX returns, per-node main-outs,
    /// sends, channel input sources, tape modes, and every node's insert chain.
    /// Message-thread only — never call from the audio thread.
    InputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction of the routing graph from a snapshot.
    /// Replays buses/channels with their persisted ids, then main-outs, sends,
    /// and insert chains. Call on a freshly-constructed mixer (it assumes an
    /// empty graph). Message-thread only.
    void importGraphState (const InputMixerGraphState&);
```

Add the includes `#include "ida/EffectChain.h"` and `#include "ida/ChannelStrip.h"` to `InputMixer.h` if not transitively present (Bus.h already pulls EffectChain.h; ChannelStrip.h is needed for the strip down-cast in the .cpp — include it there instead to keep the header light). Add a private helper declaration in the private section (near `classifyMainOut`, line 179):

```cpp
    MixerMainOut mainOutSnapshot (MixerNodeId node) const noexcept;
    std::vector<MixerSend> sendSnapshot (MixerNodeId node) const;
    const EffectChain* channelInsertChain (ChannelId) const noexcept;
```

- [ ] **Step 4: Implement `setBusEffectChain` + export in the .cpp**

In `engine/src/InputMixer.cpp`, add `#include "ida/ChannelStrip.h"` and `#include "ida/MixerGraphState.h"` near the existing includes. Implement (place beside the existing bus/graph methods):

```cpp
void InputMixer::setBusEffectChain (BusId id, EffectChain chain)
{
    for (auto& bus : buses_)
        if (bus.id() == id)
        {
            bus.setEffectChain (std::move (chain));
            return;
        }
}

MixerMainOut InputMixer::mainOutSnapshot (MixerNodeId node) const noexcept
{
    const auto dest = graph_.mainOutOf (node);
    MixerMainOut out;
    if (dest == graph_.terminalNode (MixerTerminal::Tape))
    {
        out.kind = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::Tape;
        return out;
    }
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput))
    {
        out.kind = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::HardwareOutput;
        return out;
    }
    out.kind = MixerMainOut::Kind::Bus;
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
        if (busNodeIds_[i] == dest)
        {
            out.busId = buses_[i].id().value();
            break;
        }
    return out;
}

std::vector<MixerSend> InputMixer::sendSnapshot (MixerNodeId node) const
{
    std::vector<MixerSend> sends;
    for (const auto& edge : graph_.sendEdges())
        if (edge.source == node)
            for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
                if (busNodeIds_[i] == edge.fxReturn)
                {
                    sends.push_back ({ buses_[i].id().value(), edge.level });
                    break;
                }
    return sends;
}

const EffectChain* InputMixer::channelInsertChain (ChannelId id) const noexcept
{
    auto it = channels_.find (id.value());
    if (it == channels_.end() || it->second.processing == nullptr)
        return nullptr;
    if (it->second.signalType != SignalType::Audio)
        return nullptr;
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (it->second.processing.get());
    return &strip->effectChain();
}

InputMixerGraphState InputMixer::exportGraphState() const
{
    InputMixerGraphState state;

    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = mainOutSnapshot (busNodeIds_[i]);
        entry.sends        = sendSnapshot (busNodeIds_[i]);
        entry.inserts      = bus.effectChain();
        state.buses.push_back (std::move (entry));
    }

    // channels_ is an unordered_map; export in ascending channel-id order so the
    // snapshot (and its round-trip equality) is deterministic.
    std::vector<std::int64_t> ids;
    ids.reserve (channels_.size());
    for (const auto& kv : channels_) ids.push_back (kv.first);
    std::sort (ids.begin(), ids.end());

    for (auto rawId : ids)
    {
        const auto& ch = channels_.at (rawId);
        InputChannelState entry;
        entry.channelId     = ch.id.value();
        entry.signalType    = ch.signalType;
        entry.inputSourceId = ch.source.value();
        entry.tapeMode      = ch.tapeMode;
        if (auto src = channelSources_.find (rawId); src != channelSources_.end())
            entry.source = { src->second.left, src->second.right, src->second.stereo };
        const auto node = channelNodeIds_.at (rawId);
        entry.mainOut = mainOutSnapshot (node);
        entry.sends   = sendSnapshot (node);
        if (auto* chain = channelInsertChain (ch.id)) entry.inserts = *chain;
        state.channels.push_back (std::move (entry));
    }

    state.nextBusId     = nextBusId_;
    state.nextChannelId = nextChannelId_;
    return state;
}
```

Add `#include <algorithm>` to `InputMixer.cpp` if not present (for `std::sort`).

- [ ] **Step 5: Run the test — verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-mixer]"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer exportGraphState + setBusEffectChain"
```

---

## Task 2: InputMixer import (engine round-trip)

**Files:**
- Modify: `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing round-trip + forward-compat test**

Add to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer importGraphState round-trips an exported graph", "[input-mixer][persistence]")
{
    ida::InputMixer source;
    source.registerInput (ida::InputId (1), ida::InputDescriptor{});
    const auto drums  = source.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto reverb = source.addFxReturn ("Reverb");
    ida::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (drums, ida::EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (ida::InputId (1), ida::SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);

    const auto exported = source.exportGraphState();

    ida::InputMixer loaded;
    loaded.importGraphState (exported);

    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("InputMixer importGraphState of an empty snapshot keeps only the ctor FX returns", "[input-mixer][persistence]")
{
    ida::InputMixer mixer;
    mixer.importGraphState (ida::InputMixerGraphState{});

    // The ctor seeds RVB (busId 1) + DLY (busId 2); an empty import adds no user
    // buses and must not duplicate or rewind them. This is the pre-graph
    // "loads clean" default (the dedicated returns are the baseline, not extra).
    CHECK (mixer.busCount() == 2);

    // A channel added after a clean import default-routes to the Tape terminal.
    mixer.registerInput (ida::InputId (1), ida::InputDescriptor{});
    const auto ch = mixer.addChannel (ida::InputId (1), ida::SignalType::Audio);
    CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Tape);
}
```

> **Note for the round-trip test above:** use a non-default `ida::InputDescriptor`
> if the default constructor is unavailable (Task 1 found `InputDescriptor` has a
> non-default-constructible `TapeId`; build one e.g. `ida::InputDescriptor{ ida::TapeId (1), ... }`
> matching the constructor — read `InputDescriptor.h`). The `source` mixer's
> `addBus("Drums")` will mint busId 3 (after ctor RVB=1/DLY=2), and `addFxReturn("Reverb")`
> busId 4 — the channel's send target is `reverb` (busId 4), its main-out is `drums`
> (busId 3). Equality is on the full snapshot, so this all just needs to round-trip;
> the exact ids above are FYI.

- [ ] **Step 2: Run it — verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL to compile — `importGraphState` is not a member.

- [ ] **Step 3: Implement import**

> Before writing, read `InputMixer::addChannel` and `addBus`/`addFxReturn` in `engine/src/InputMixer.cpp` to mirror exactly what each does internally (construct the registry entry, call `graph_.addNode`, record the node-id map). `importGraphState` replicates that registration directly with the *persisted* ids rather than calling the public mutators, so ids round-trip exactly. It then applies main-outs/sends/inserts via the existing public mutators (which look up by domain id), and finally restores the id counters.

In `engine/src/InputMixer.cpp`:

> **⚠ Ctor-seeded FX returns (discovered in Task 1):** the `InputMixer` constructor
> pre-creates two FX returns — RVB (busId 1) and DLY (busId 2), both main-out →
> HardwareOutput — via `addFxReturn`, leaving `nextBusId_ == 3`. A fresh mixer
> built for import therefore ALREADY has these two buses. Import must NOT re-create
> a bus whose id already exists (else it duplicates RVB/DLY); it applies the
> snapshot's state to them instead. `addBus` mints dense sequential ids, and the
> ctor returns occupy 1 and 2, so replaying `addBus` for the snapshot's user buses
> (id ≥ 3, in ascending order — which `buses_` already is) reproduces their ids
> exactly. This mirrors the OutputMixer master-bus pattern. Use `std::max` when
> restoring `nextBusId_`/`nextChannelId_` so an empty/default snapshot
> (`nextBusId == 1`) never rewinds the ctor's `nextBusId_ == 3`.

```cpp
void InputMixer::importGraphState (const InputMixerGraphState& state)
{
    auto busExists = [this] (std::int64_t id)
    {
        for (const auto& bus : buses_) if (bus.id().value() == id) return true;
        return false;
    };

    // 1. Buses / FX returns. The ctor pre-creates RVB (busId 1) + DLY (busId 2);
    //    apply the snapshot's chain to an existing bus rather than re-creating it.
    //    Create the rest with addBus, which mints the persisted (dense) busId.
    for (const auto& b : state.buses)
    {
        if (! busExists (b.busId))
        {
            BusConfig config;
            config.channelCount = b.channelCount;
            config.name         = b.name;
            config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
            addBus (config); // mints b.busId (dense) and registers the graph node
        }
        setBusEffectChain (BusId (b.busId), b.inserts);
    }

    // 2. Channels — register with persisted ChannelIds (constructed directly so
    //    removeChannel-induced id gaps round-trip), build the graph nodes, set
    //    device source + tape mode + strip insert chain. Mirror what addChannel
    //    does internally (read it first).
    for (const auto& c : state.channels)
    {
        const ChannelId id (c.channelId);
        channels_.emplace (c.channelId,
                           Channel (id, c.signalType, InputId (c.inputSourceId), c.tapeMode));
        channelSources_[c.channelId] = { c.source.left, c.source.right, c.source.stereo };
        channelNodeIds_[c.channelId] = graph_.addNode (MixerNodeKind::Channel);

        if (c.signalType == SignalType::Audio)
            if (auto* chain = channels_.at (c.channelId).processing.get())
                static_cast<ChannelStrip<SignalType::Audio>*> (chain)->setEffectChain (c.inserts);
    }

    // 3. Apply main-outs (nodes all exist now, so no cycle false-positives).
    for (const auto& b : state.buses)    applyBusMainOut (BusId (b.busId), b.mainOut);
    for (const auto& c : state.channels) applyChannelMainOut (ChannelId (c.channelId), c.mainOut);

    // 4. Apply sends.
    for (const auto& b : state.buses)
        for (const auto& s : b.sends) setBusSend (BusId (b.busId), BusId (s.busId), s.level);
    for (const auto& c : state.channels)
        for (const auto& s : c.sends) setChannelSend (ChannelId (c.channelId), BusId (s.busId), s.level);

    // 5. Advance id counters — never rewind (the ctor already set nextBusId_ == 3).
    nextBusId_     = std::max (nextBusId_, state.nextBusId);
    nextChannelId_ = std::max (nextChannelId_, state.nextChannelId);
}
```

Also apply the Task-1 code-review hardening (same file, exercised by import): add
`jassertfalse; // graph invariant: dest is a Bus node absent from busNodeIds_`
immediately before the final `return out;` in `InputMixer::mainOutSnapshot` (the
bus-miss fall-through), matching the fail-loud style of `addBus`'s capacity guard.

The `applyMainOut` step needs a small private helper that dispatches a `MixerMainOut` onto the right setter. Add to `InputMixer.cpp` (and declare in the header private section):

```cpp
// Header (private):
//   void applyChannelMainOut (ChannelId, const MixerMainOut&);
//   void applyBusMainOut (BusId, const MixerMainOut&);

void InputMixer::applyChannelMainOut (ChannelId id, const MixerMainOut& m)
{
    if (m.kind == MixerMainOut::Kind::Bus)               setChannelMainOutToBus (id, BusId (m.busId));
    else if (m.terminal == MixerTerminalKind::HardwareOutput) setChannelMainOutToHardwareOutput (id);
    else                                                  setChannelMainOutToTape (id);
}

void InputMixer::applyBusMainOut (BusId id, const MixerMainOut& m)
{
    if (m.kind == MixerMainOut::Kind::Bus)               setBusMainOutToBus (id, BusId (m.busId));
    else if (m.terminal == MixerTerminalKind::HardwareOutput) setBusMainOutToHardwareOutput (id);
    else                                                  setBusMainOutToTape (id);
}
```

- [ ] **Step 4: Run the test — verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[input-mixer]"`
Expected: PASS. If round-trip equality fails, diff the exported-vs-reexported snapshot field by field; the usual culprit is a main-out classification or send ordering mismatch (sends are exported in `sendEdges()` order — if equality is order-sensitive and the graph reorders edges, sort `sends` by `busId` in both `sendSnapshot` and the test setup).

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer importGraphState — engine round-trip"
```

---

## Task 3: OutputMixer export + import

**Files:**
- Modify: `engine/include/ida/OutputMixer.h`, `engine/src/OutputMixer.cpp`
- Test: `tests/OutputMixerTests.cpp`

> Mirrors Tasks 1–2 for the output side. Differences: a single terminal (`HardwareOutput`); the master bus is `BusId{0}`, auto-created in the ctor (do NOT re-add on import — apply only its insert chain + main-out); output channels route through the dense `sendMatrix_` (no single main-out), exported as a `sends` list via `sendLevelFor`. `channels_`/`buses_` are dense vectors with sequential ids, so import replays in order; still restore the id counters to be safe.

- [ ] **Step 1: Write the failing export/import test**

Add to `tests/OutputMixerTests.cpp` (include `"ida/MixerGraphState.h"`):

```cpp
TEST_CASE ("OutputMixer export/import round-trips buses, sends, subgroups, inserts", "[output-mixer][persistence]")
{
    ida::OutputMixer source;
    const auto aux = source.addBus (ida::BusConfig { 2, "Aux", ida::BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, ida::BusId (0)));   // aux -> master

    ida::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (aux, ida::EffectChain{}.withAppended (comp));

    const auto ch = source.addChannel (ida::SignalType::Audio);
    auto strip = std::make_unique<ida::ChannelStrip<ida::SignalType::Audio>>();
    ida::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (ida::EffectChain{}.withAppended (eq));
    source.setChannelStrip (ch, std::move (strip));
    source.routeChannelToBus (ch, ida::BusId (0), 0.7f);    // non-default master level
    source.routeChannelToBus (ch, aux, 0.4f);                 // send to aux

    const auto exported = source.exportGraphState();
    REQUIRE (exported.buses.size() == 2);              // master (0) + aux
    CHECK (exported.buses[0].busId == 0);              // master first

    ida::OutputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("OutputMixer import of an empty snapshot keeps only the master bus", "[output-mixer][persistence]")
{
    ida::OutputMixer mixer;
    mixer.importGraphState (ida::OutputMixerGraphState{});
    const auto state = mixer.exportGraphState();
    REQUIRE (state.buses.size() == 1);
    CHECK (state.buses[0].busId == 0);
    CHECK (state.channels.empty());
}
```

- [ ] **Step 2: Run it — verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL to compile — `exportGraphState` / `importGraphState` not members of `OutputMixer`.

- [ ] **Step 3: Declare + implement**

In `engine/include/ida/OutputMixer.h` add `#include "ida/MixerGraphState.h"` and, in the public section:

```cpp
    /// Message-thread snapshot of the routing graph for persistence (Phase 5).
    /// buses[0] is the master (BusId 0). Message-thread only.
    OutputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction from a snapshot. The master bus already
    /// exists (ctor); its insert chain + main-out are applied, never re-added.
    /// Call on a freshly-constructed mixer. Message-thread only.
    void importGraphState (const OutputMixerGraphState&);
```

In `engine/src/OutputMixer.cpp` (add `#include "ida/MixerGraphState.h"`, `#include <algorithm>`):

```cpp
namespace
{
    ida::MixerMainOut busMainOutSnapshot (const ida::MixerGraph& graph,
                                             ida::MixerNodeId node,
                                             const std::vector<ida::MixerNodeId>& busNodeIds,
                                             const std::vector<ida::Bus>& buses)
    {
        using namespace sirius;
        const auto dest = graph.mainOutOf (node);
        MixerMainOut out;
        if (dest == graph.terminalNode (MixerTerminal::HardwareOutput))
        {
            out.kind = MixerMainOut::Kind::Terminal;
            out.terminal = MixerTerminalKind::HardwareOutput;
            return out;
        }
        out.kind = MixerMainOut::Kind::Bus;
        for (std::size_t i = 0; i < busNodeIds.size(); ++i)
            if (busNodeIds[i] == dest) { out.busId = buses[i].id().value(); break; }
        return out;
    }
}

OutputMixerGraphState OutputMixer::exportGraphState() const
{
    OutputMixerGraphState state;

    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = busMainOutSnapshot (graph_, busNodeIds_[i], busNodeIds_, buses_);
        entry.inserts      = bus.effectChain();
        state.buses.push_back (std::move (entry));   // master is index 0 by construction
    }

    for (const auto& ce : channels_)
    {
        OutputChannelState entry;
        entry.channelId  = ce.id.value();
        entry.signalType = ce.signalType;
        if (ce.strip != nullptr) entry.inserts = ce.strip->effectChain();
        for (std::size_t b = 0; b < buses_.size(); ++b)
        {
            const auto busId = buses_[b].id();
            const auto level = sendLevelFor (ce.id, busId);
            // Always emit the master (BusId 0) send, even at 0: addChannel defaults
            // it to 1.0, so a deliberately-zeroed master level must be persisted
            // explicitly to survive import. Aux sends default to 0, so dropping
            // their zeros is safe.
            if (busId.value() == 0 || level > 0.0f)
                entry.sends.push_back ({ busId.value(), level });
        }
        state.channels.push_back (std::move (entry));
    }

    state.nextBusId     = nextBusId_;
    state.nextChannelId = nextOutputChannelId_;
    return state;
}

void OutputMixer::importGraphState (const OutputMixerGraphState& state)
{
    // Master (BusId 0) already exists from the ctor. Re-create only aux buses.
    for (const auto& b : state.buses)
    {
        if (b.busId == 0)
        {
            setBusEffectChain (BusId (0), b.inserts);     // master main-out is the terminal; nothing to set
            continue;
        }
        BusConfig config;
        config.channelCount = b.channelCount;
        config.name         = b.name;
        config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
        const auto created = addBus (config);             // mints BusId{busId} (dense, in order)
        setBusEffectChain (created, b.inserts);
    }
    // Apply aux-bus subgroup routing now that all buses exist.
    for (const auto& b : state.buses)
        if (b.busId != 0 && b.mainOut.kind == MixerMainOut::Kind::Bus)
            routeBusToBus (BusId (b.busId), BusId (b.mainOut.busId));

    for (const auto& c : state.channels)
    {
        const auto created = addChannel (c.signalType);   // mints OutputChannelId in order
        auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
        strip->setEffectChain (c.inserts);
        setChannelStrip (created, std::move (strip));
        for (const auto& s : c.sends) routeChannelToBus (created, BusId (s.busId), s.level);
    }

    nextBusId_           = state.nextBusId;
    nextOutputChannelId_ = state.nextChannelId;
}
```

> If `addBus`/`addChannel` do not assign exactly the persisted ids (verify in `OutputMixer.cpp`), the round-trip equality test will catch it; in that case map persisted→minted ids the same way Task 2 does. For dense, gap-free output registries (no `removeChannel`/`removeBus` on `OutputMixer`), in-order replay reproduces the ids.

- [ ] **Step 4: Run the test — verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[output-mixer]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp tests/OutputMixerTests.cpp
git commit -m "feat: OutputMixer export/import graph state"
```

---

## Task 4: Persistence serialize/deserialize of the snapshot

**Files:**
- Modify: `persistence/include/ida/SessionFormat.h`, `persistence/src/SessionFormat.cpp`
- Test: `tests/SessionFormatTests.cpp`

- [ ] **Step 1: Write the failing serialize/deserialize tests**

Add to `tests/SessionFormatTests.cpp` (include `"ida/MixerGraphState.h"`):

```cpp
namespace
{
    ida::InputMixerGraphState sampleInputState()
    {
        using namespace sirius;
        InputMixerGraphState s;
        MixerBusState bus; bus.busId = 1; bus.channelCount = 2; bus.name = "Drums";
        bus.kind = MixerBusKind::Bus;
        bus.mainOut.kind = MixerMainOut::Kind::Terminal; bus.mainOut.terminal = MixerTerminalKind::Tape;
        EffectChainEntry comp; comp.displayName = "comp"; comp.bypassed = true;
        bus.inserts = EffectChain{}.withAppended (comp);
        s.buses.push_back (bus);
        MixerBusState rev; rev.busId = 2; rev.name = "Reverb"; rev.kind = MixerBusKind::FxReturn;
        rev.mainOut.kind = MixerMainOut::Kind::Terminal; rev.mainOut.terminal = MixerTerminalKind::Tape;
        s.buses.push_back (rev);
        InputChannelState ch; ch.channelId = 5; ch.signalType = SignalType::Audio; ch.inputSourceId = 1;
        ch.source = { 2, 3, true }; ch.tapeMode = TapeMode::CommitToTape;
        ch.mainOut.kind = MixerMainOut::Kind::Bus; ch.mainOut.busId = 1;
        ch.sends.push_back ({ 2, 0.5f });
        EffectChainEntry eq; eq.displayName = "eq";
        ch.inserts = EffectChain{}.withAppended (eq);
        s.channels.push_back (ch);
        s.nextBusId = 3; s.nextChannelId = 6;
        return s;
    }
}

TEST_CASE ("InputMixerGraphState round-trips through JSON", "[sessionformat]")
{
    const auto original = sampleInputState();
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (restored == original);
    // Byte-stable: serialize -> deserialize -> serialize yields identical JSON.
    CHECK (ida::persistence::serializeMixerGraphState (restored) == json);
}

TEST_CASE ("OutputMixerGraphState round-trips through JSON", "[sessionformat]")
{
    using namespace sirius;
    OutputMixerGraphState s;
    MixerBusState master; master.busId = 0; master.name = "Master";
    master.mainOut.kind = MixerMainOut::Kind::Terminal; master.mainOut.terminal = MixerTerminalKind::HardwareOutput;
    s.buses.push_back (master);
    OutputChannelState ch; ch.channelId = 1; ch.sends.push_back ({ 0, 1.0f });
    s.channels.push_back (ch);
    s.nextBusId = 1; s.nextChannelId = 2;

    const auto json     = ida::persistence::serializeMixerGraphState (s);
    const auto restored = ida::persistence::deserializeOutputMixerGraphState (json);
    CHECK (restored == s);
}

TEST_CASE ("a pre-graph (empty) mixer document deserializes to defaults", "[sessionformat]")
{
    // A document carrying only a version and empty arrays — what a forward
    // session that never populated the graph would write.
    const ida::InputMixerGraphState empty;
    const auto json     = ida::persistence::serializeMixerGraphState (empty);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (restored.buses.empty());
    CHECK (restored.channels.empty());
    CHECK (restored.nextBusId == 1);
    CHECK (restored.nextChannelId == 1);
}

TEST_CASE ("malformed mixer-graph JSON is rejected with a hard error", "[sessionformat]")
{
    CHECK_THROWS_AS (ida::persistence::deserializeInputMixerGraphState ("{not json}"),
                     std::runtime_error);
    CHECK_THROWS_AS (ida::persistence::deserializeInputMixerGraphState ("[1,2,3]"),
                     std::runtime_error);
}
```

- [ ] **Step 2: Run it — verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL to compile — the four `serialize/deserialize*MixerGraphState` functions don't exist.

- [ ] **Step 3: Declare the functions**

In `persistence/include/ida/SessionFormat.h`, add `#include "ida/MixerGraphState.h"` and:

```cpp
/// Serializes one mixer's routing-graph snapshot (routing-graph Phase 5) to a
/// self-contained JSON document. Independent of the Constituent session document
/// (the two mixers are separate consoles). Reuses the EffectChain serialization.
juce::String serializeMixerGraphState (const InputMixerGraphState&);
juce::String serializeMixerGraphState (const OutputMixerGraphState&);

/// Reconstructs a mixer snapshot from `serializeMixerGraphState`. Throws
/// std::runtime_error on malformed input. Absent optional keys default (an
/// empty graph), so a pre-graph document loads clean.
InputMixerGraphState  deserializeInputMixerGraphState  (const juce::String& json);
OutputMixerGraphState deserializeOutputMixerGraphState (const juce::String& json);
```

- [ ] **Step 4: Implement in the .cpp (reuse the existing helpers)**

In `persistence/src/SessionFormat.cpp`, add `#include "ida/MixerGraphState.h"` to the include block. Inside the existing anonymous namespace (so `fail`, `requireProperty`, `optionalProperty`, `requireInt`, `requireInt64`, `makeObject`, `objectVar`, `effectChainToVar`, `effectChainFromVar` are all in scope), add — after `effectChainFromVar` (line 603):

```cpp
    constexpr int currentMixerGraphVersion = 1;

    const char* signalTypeToString (SignalType t) noexcept
    {
        switch (t)
        {
            case SignalType::Audio: return "Audio";
            case SignalType::Midi:  return "Midi";
            case SignalType::Video: return "Video";
            case SignalType::File:  return "File";
        }
        return "Audio";
    }
    SignalType signalTypeFromString (const juce::String& s)
    {
        if (s == "Audio") return SignalType::Audio;
        if (s == "Midi")  return SignalType::Midi;
        if (s == "Video") return SignalType::Video;
        if (s == "File")  return SignalType::File;
        fail (("Unknown signalType: " + s).toStdString());
        return SignalType::Audio;
    }

    const char* tapeModeToString (TapeMode m) noexcept
    {
        switch (m)
        {
            case TapeMode::CommitToTape:   return "CommitToTape";
            case TapeMode::NonDestructive: return "NonDestructive";
            case TapeMode::NoTape:         return "NoTape";
        }
        return "NoTape";
    }
    TapeMode tapeModeFromString (const juce::String& s)
    {
        if (s == "CommitToTape")   return TapeMode::CommitToTape;
        if (s == "NonDestructive") return TapeMode::NonDestructive;
        if (s == "NoTape")         return TapeMode::NoTape;
        fail (("Unknown tapeMode: " + s).toStdString());
        return TapeMode::NoTape;
    }

    const char* mixerBusKindToString (MixerBusKind k) noexcept
    { return k == MixerBusKind::FxReturn ? "FxReturn" : "Bus"; }
    MixerBusKind mixerBusKindFromString (const juce::String& s)
    {
        if (s == "FxReturn") return MixerBusKind::FxReturn;
        if (s == "Bus")      return MixerBusKind::Bus;
        fail (("Unknown busKind: " + s).toStdString());
        return MixerBusKind::Bus;
    }

    const char* terminalKindToString (MixerTerminalKind k) noexcept
    { return k == MixerTerminalKind::HardwareOutput ? "HardwareOutput" : "Tape"; }
    MixerTerminalKind terminalKindFromString (const juce::String& s)
    {
        if (s == "HardwareOutput") return MixerTerminalKind::HardwareOutput;
        if (s == "Tape")           return MixerTerminalKind::Tape;
        fail (("Unknown terminal: " + s).toStdString());
        return MixerTerminalKind::Tape;
    }

    juce::var mainOutToVar (const MixerMainOut& m)
    {
        auto obj = makeObject();
        obj->setProperty ("kind", m.kind == MixerMainOut::Kind::Bus ? "Bus" : "Terminal");
        if (m.kind == MixerMainOut::Kind::Bus)
            obj->setProperty ("busId", juce::int64 (m.busId));
        else
            obj->setProperty ("terminal", terminalKindToString (m.terminal));
        return objectVar (obj);
    }
    MixerMainOut mainOutFromVar (const juce::var& v)
    {
        MixerMainOut m;
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "Bus")
        {
            m.kind = MixerMainOut::Kind::Bus;
            m.busId = requireInt64 (requireProperty (v, "busId"), "mainOut.busId");
        }
        else if (kind == "Terminal")
        {
            m.kind = MixerMainOut::Kind::Terminal;
            m.terminal = terminalKindFromString (requireProperty (v, "terminal").toString());
        }
        else fail (("Unknown mainOut.kind: " + kind).toStdString());
        return m;
    }

    juce::var sendsToVar (const std::vector<MixerSend>& sends)
    {
        juce::Array<juce::var> arr;
        for (const auto& s : sends)
        {
            auto obj = makeObject();
            obj->setProperty ("busId", juce::int64 (s.busId));
            obj->setProperty ("level", double (s.level));
            arr.add (objectVar (obj));
        }
        return arr;
    }
    std::vector<MixerSend> sendsFromVar (const juce::var& v)
    {
        std::vector<MixerSend> out;
        if (v.isVoid()) return out;
        if (! v.isArray()) fail ("sends must be an array");
        for (int i = 0; i < v.size(); ++i)
        {
            MixerSend s;
            s.busId = requireInt64 (requireProperty (v[i], "busId"), "send.busId");
            s.level = float (double (requireProperty (v[i], "level")));
            out.push_back (s);
        }
        return out;
    }

    juce::var busStateToVar (const MixerBusState& b)
    {
        auto obj = makeObject();
        obj->setProperty ("busId",        juce::int64 (b.busId));
        obj->setProperty ("channelCount", b.channelCount);
        obj->setProperty ("name",         juce::String (b.name));
        obj->setProperty ("kind",         mixerBusKindToString (b.kind));
        obj->setProperty ("mainOut",      mainOutToVar (b.mainOut));
        obj->setProperty ("sends",        sendsToVar (b.sends));
        obj->setProperty ("inserts",      effectChainToVar (b.inserts));
        return objectVar (obj);
    }
    MixerBusState busStateFromVar (const juce::var& v)
    {
        MixerBusState b;
        b.busId        = requireInt64 (requireProperty (v, "busId"), "bus.busId");
        b.channelCount = requireInt (requireProperty (v, "channelCount"), "bus.channelCount");
        b.name         = requireProperty (v, "name").toString().toStdString();
        b.kind         = mixerBusKindFromString (requireProperty (v, "kind").toString());
        b.mainOut      = mainOutFromVar (requireProperty (v, "mainOut"));
        b.sends        = sendsFromVar (optionalProperty (v, "sends"));
        b.inserts      = effectChainFromVar (requireProperty (v, "inserts"));
        return b;
    }

    juce::var inputChannelToVar (const InputChannelState& c)
    {
        auto src = makeObject();
        src->setProperty ("left",   c.source.left);
        src->setProperty ("right",  c.source.right);
        src->setProperty ("stereo", c.source.stereo);
        auto obj = makeObject();
        obj->setProperty ("channelId",     juce::int64 (c.channelId));
        obj->setProperty ("signalType",    signalTypeToString (c.signalType));
        obj->setProperty ("inputSourceId", juce::int64 (c.inputSourceId));
        obj->setProperty ("source",        objectVar (src));
        obj->setProperty ("tapeMode",      tapeModeToString (c.tapeMode));
        obj->setProperty ("mainOut",       mainOutToVar (c.mainOut));
        obj->setProperty ("sends",         sendsToVar (c.sends));
        obj->setProperty ("inserts",       effectChainToVar (c.inserts));
        return objectVar (obj);
    }
    InputChannelState inputChannelFromVar (const juce::var& v)
    {
        InputChannelState c;
        c.channelId     = requireInt64 (requireProperty (v, "channelId"), "channel.channelId");
        c.signalType    = signalTypeFromString (requireProperty (v, "signalType").toString());
        c.inputSourceId = requireInt64 (requireProperty (v, "inputSourceId"), "channel.inputSourceId");
        const auto src  = requireProperty (v, "source");
        c.source.left   = requireInt (requireProperty (src, "left"),  "source.left");
        c.source.right  = requireInt (requireProperty (src, "right"), "source.right");
        c.source.stereo = bool (requireProperty (src, "stereo"));
        c.tapeMode      = tapeModeFromString (requireProperty (v, "tapeMode").toString());
        c.mainOut       = mainOutFromVar (requireProperty (v, "mainOut"));
        c.sends         = sendsFromVar (optionalProperty (v, "sends"));
        c.inserts       = effectChainFromVar (requireProperty (v, "inserts"));
        return c;
    }

    juce::var outputChannelToVar (const OutputChannelState& c)
    {
        auto obj = makeObject();
        obj->setProperty ("channelId",  juce::int64 (c.channelId));
        obj->setProperty ("signalType", signalTypeToString (c.signalType));
        obj->setProperty ("sends",      sendsToVar (c.sends));
        obj->setProperty ("inserts",    effectChainToVar (c.inserts));
        return objectVar (obj);
    }
    OutputChannelState outputChannelFromVar (const juce::var& v)
    {
        OutputChannelState c;
        c.channelId  = requireInt64 (requireProperty (v, "channelId"), "channel.channelId");
        c.signalType = signalTypeFromString (requireProperty (v, "signalType").toString());
        c.sends      = sendsFromVar (optionalProperty (v, "sends"));
        c.inserts    = effectChainFromVar (requireProperty (v, "inserts"));
        return c;
    }

    template <typename BusVec, typename ChannelVec, typename ChannelToVar>
    juce::String serializeMixerDoc (const BusVec& buses, const ChannelVec& channels,
                                    std::int64_t nextBusId, std::int64_t nextChannelId,
                                    ChannelToVar channelToVar)
    {
        juce::Array<juce::var> busArr;
        for (const auto& b : buses) busArr.add (busStateToVar (b));
        juce::Array<juce::var> chArr;
        for (const auto& c : channels) chArr.add (channelToVar (c));
        auto root = makeObject();
        root->setProperty ("version",       currentMixerGraphVersion);
        root->setProperty ("buses",         busArr);
        root->setProperty ("channels",      chArr);
        root->setProperty ("nextBusId",     juce::int64 (nextBusId));
        root->setProperty ("nextChannelId", juce::int64 (nextChannelId));
        return juce::JSON::toString (objectVar (root));
    }

    juce::var parseMixerDoc (const juce::String& json)
    {
        const auto parsed = juce::JSON::parse (json);
        if (! parsed.isObject()) fail ("mixer graph document must be a JSON object");
        const auto version = requireInt (requireProperty (parsed, "version"), "version");
        if (version != currentMixerGraphVersion)
            fail ("unsupported mixer graph version: " + std::to_string (version));
        return parsed;
    }
```

Then add the public free functions OUTSIDE the anonymous namespace (near `serializeSession`):

```cpp
juce::String serializeMixerGraphState (const InputMixerGraphState& s)
{
    return serializeMixerDoc (s.buses, s.channels, s.nextBusId, s.nextChannelId,
                              [] (const InputChannelState& c) { return inputChannelToVar (c); });
}

juce::String serializeMixerGraphState (const OutputMixerGraphState& s)
{
    return serializeMixerDoc (s.buses, s.channels, s.nextBusId, s.nextChannelId,
                              [] (const OutputChannelState& c) { return outputChannelToVar (c); });
}

InputMixerGraphState deserializeInputMixerGraphState (const juce::String& json)
{
    const auto root = parseMixerDoc (json);
    InputMixerGraphState s;
    if (const auto buses = optionalProperty (root, "buses"); buses.isArray())
        for (int i = 0; i < buses.size(); ++i) s.buses.push_back (busStateFromVar (buses[i]));
    if (const auto chans = optionalProperty (root, "channels"); chans.isArray())
        for (int i = 0; i < chans.size(); ++i) s.channels.push_back (inputChannelFromVar (chans[i]));
    if (const auto n = optionalProperty (root, "nextBusId");     ! n.isVoid()) s.nextBusId = requireInt64 (n, "nextBusId");
    if (const auto n = optionalProperty (root, "nextChannelId"); ! n.isVoid()) s.nextChannelId = requireInt64 (n, "nextChannelId");
    return s;
}

OutputMixerGraphState deserializeOutputMixerGraphState (const juce::String& json)
{
    const auto root = parseMixerDoc (json);
    OutputMixerGraphState s;
    if (const auto buses = optionalProperty (root, "buses"); buses.isArray())
        for (int i = 0; i < buses.size(); ++i) s.buses.push_back (busStateFromVar (buses[i]));
    if (const auto chans = optionalProperty (root, "channels"); chans.isArray())
        for (int i = 0; i < chans.size(); ++i) s.channels.push_back (outputChannelFromVar (chans[i]));
    if (const auto n = optionalProperty (root, "nextBusId");     ! n.isVoid()) s.nextBusId = requireInt64 (n, "nextBusId");
    if (const auto n = optionalProperty (root, "nextChannelId"); ! n.isVoid()) s.nextChannelId = requireInt64 (n, "nextChannelId");
    return s;
}
```

Confirm `<juce_core/juce_core.h>` (for `juce::JSON`, already used by `serializeSession`) and `<string>` are included — both already are.

- [ ] **Step 5: Run the tests — verify they pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[sessionformat]"`
Expected: PASS (new cases + the existing `[sessionformat]` cases still green).

- [ ] **Step 6: Commit**

```bash
git add persistence/include/ida/SessionFormat.h persistence/src/SessionFormat.cpp tests/SessionFormatTests.cpp
git commit -m "feat: SessionFormat serialize/deserialize mixer graph state"
```

---

## Task 5: Full both-mixer integration round-trip

**Files:**
- Create: `tests/MixerGraphPersistenceTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the end-to-end test**

Create `tests/MixerGraphPersistenceTests.cpp`:

```cpp
#include "ida/InputMixer.h"
#include "ida/OutputMixer.h"
#include "ida/MixerGraphState.h"
#include "ida/SessionFormat.h"
#include "ida/ChannelStrip.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace sirius;

TEST_CASE ("InputMixer survives export -> serialize -> deserialize -> import", "[sessionformat][mixer]")
{
    InputMixer source;
    source.registerInput (InputId (1), InputDescriptor{});
    const auto drums  = source.addBus (BusConfig { 2, "Drums", BusKind::Bus });
    const auto reverb = source.addFxReturn ("Reverb");
    EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (drums, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (InputId (1), SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (source.processingChainFor (ch));
    EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (EffectChain{}.withAppended (eq));

    const auto original = source.exportGraphState();
    const auto json     = persistence::serializeMixerGraphState (original);

    InputMixer loaded;
    loaded.importGraphState (persistence::deserializeInputMixerGraphState (json));

    const auto reexported = loaded.exportGraphState();
    CHECK (reexported == original);
    // Insert chains specifically survived end-to-end.
    REQUIRE (reexported.buses[0].inserts.size() == 1);
    REQUIRE (reexported.channels[0].inserts.size() == 1);
}

TEST_CASE ("OutputMixer survives export -> serialize -> deserialize -> import", "[sessionformat][mixer]")
{
    OutputMixer source;
    const auto aux = source.addBus (BusConfig { 2, "Aux", BusKind::Bus });
    REQUIRE (source.routeBusToBus (aux, BusId (0)));
    EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (aux, EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (SignalType::Audio);
    auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
    EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (EffectChain{}.withAppended (eq));
    source.setChannelStrip (ch, std::move (strip));
    source.routeChannelToBus (ch, BusId (0), 1.0f);
    source.routeChannelToBus (ch, aux, 0.4f);

    const auto original = source.exportGraphState();
    const auto json     = persistence::serializeMixerGraphState (original);

    OutputMixer loaded;
    loaded.importGraphState (persistence::deserializeOutputMixerGraphState (json));

    CHECK (loaded.exportGraphState() == original);
}

TEST_CASE ("a pre-graph session (no mixer-graph JSON) loads as empty mixers", "[sessionformat][mixer]")
{
    // Forward-compat: a pre-graph session simply never serialized a mixer graph,
    // so nothing is imported. Both mixers stay at their ctor defaults: the input
    // mixer with its dedicated RVB+DLY FX returns, the output mixer with master.
    InputMixer  input;
    OutputMixer output;

    CHECK (input.busCount() == 2);          // ctor RVB (busId 1) + DLY (busId 2)
    const auto outState = output.exportGraphState();
    REQUIRE (outState.buses.size() >= 1);   // master at index 0 (plus any ctor returns)
    CHECK (outState.buses[0].busId == 0);
    CHECK (outState.channels.empty());

    // And a channel added to the fresh input mixer default-routes to its tape terminal.
    input.registerInput (InputId (1), InputDescriptor { /* see note */ });
    const auto ch = input.addChannel (InputId (1), SignalType::Audio);
    CHECK (input.channelMainOut (ch) == InputMixer::MainOutDest::Tape);
}
```

> **Notes for Task 5:** (1) `InputDescriptor` is not default-constructible (Task 1
> found a non-default-constructible `TapeId` member) — build a valid one matching
> its constructor (read `core`/`engine` `InputDescriptor.h`), e.g. the same form the
> Task 1/2 tests used. (2) `OutputMixer`'s ctor auto-creates the master (busId 0);
> if it ALSO seeds RVB/DLY returns (the "both mixers carry dedicated RVB+DLY"
> decision — verify by reading the ctor in Task 3), adjust the `buses.size()`
> expectation accordingly (hence `>= 1` above) and ensure the master is at index 0.

- [ ] **Step 2: Register the test file**

In `tests/CMakeLists.txt`, add to the `add_executable(IdaTests ...)` source list:

```cmake
    MixerGraphStateTests.cpp
    MixerGraphPersistenceTests.cpp
```

(The first line was added in Task 0; add the second beneath it.)

- [ ] **Step 3: Run it — verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[mixer]"`
Expected: PASS.

- [ ] **Step 4: Full suite + clean-rebuild gate**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
ctest --test-dir build
```
Expected: green at the documented baseline (506/506; #506 is the `MainComponentPluginEditorTests_NOT_BUILT` placeholder run separately by `bash/test-s7.sh`; #281 is the known transient SIGTERM flake — rerun if it trips).

- [ ] **Step 5: Commit**

```bash
git add tests/MixerGraphPersistenceTests.cpp tests/CMakeLists.txt
git commit -m "feat: end-to-end mixer routing-graph persistence round-trip"
```

---

## Acceptance criteria (from the spec, Phase 5)

- [ ] Round-trip equality (graph in == graph out) for the **Input** mixer (Tasks 2, 5).
- [ ] Round-trip equality for the **Output** mixer (Tasks 3, 5).
- [ ] Forward-compat: a pre-graph session loads clean — empty graph, channels default-routed to their terminal (Tasks 2, 3, 5).
- [ ] Per-node insert chains (bus + channel) survive the round-trip (Tasks 1–5; asserted explicitly in Task 5).
- [ ] Buses, FX returns, main-out assignments, send levels, terminal assignments all persisted (Tasks 1, 3, 4).
- [ ] `serializeSession(const Constituent&)` and its existing `[sessionformat]` tests remain green (no signature change).

## Deferred (record in `todo.md`)

- `MainComponent` session-file wiring (writing/reading these JSON sections on actual save/load) — UI phases P6/P7.
- Input registration registry (`InputState`/`InputDescriptor`) is NOT persisted — only the channel's `InputId` value is, enough to reconstruct the `Channel`. Device-registration persistence is a separate device-config concern.
- Built-in IDA FX / union slot model — separate follow-on spec.

## Post-completion

Update `continue.md`: commits, ctest count, and next = **Phase 6** (Input Mixer UI: creation gesture + Bus/FXReturn strips + destination picker, operator-verified). Mandatory per the handoff rule.
```

