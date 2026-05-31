# Input-Bus MON Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `MON` capability to input-mixer aux buses and FX returns — line-for-line mirror of per-channel MON. Whitepaper amendments already landed (§6.6, §7.2, Glossary); this implements the engine + persistence + UI to match.

**Architecture:** New `InputMixer::setBusMonitorMode(BusId, MonitorMode)` mints an OutputMixer channel that reads the bus's `processedBuffer_` via a new `Bus::postProcessingPointer(side)` accessor. State lives in `busMonitorRoutes_` (parallel to `channelMonitorRoutes_`). `MixerBusState` grows a `monitorMode` field with default-suppress JSON. UI: bus strip gets a MON button mirroring the per-channel one; `refreshOutputMixerMonChannels` walks `busStripIds_` as well as `inputStripChannelIds_`.

**Tech Stack:** C++17, JUCE, Catch2, CMake/Ninja, existing IDA engine + persistence + Standalone app.

---

## File map

- `core/include/ida/MixerGraphState.h` — add `MixerBusState::monitorMode` + extend `operator==`.
- `engine/include/ida/Bus.h` — add `postProcessingPointer(int side) const noexcept`.
- `engine/src/Bus.cpp` — impl.
- `engine/include/ida/InputMixer.h` — add `setBusMonitorMode`, `busMonitorMode`, `busMonitorOutputChannel`; add `busMonitorRoutes_` map.
- `engine/src/InputMixer.cpp` — impls; extend `exportGraphState` + `importGraphState`.
- `persistence/src/SessionFormat.cpp` — extend `busStateToVar` / `busStateFromVar` for `monitorMode`.
- `app/MainComponent.cpp` — `onBusMonitorModeChanged` handler, load-handler replay block, `refreshOutputMixerMonChannels` extension, `refreshInputMixer` syncs bus MON button states.
- `app/MainComponent.cpp` (`InputMixerPane`) — bus strip MON button + state vectors + `onBusMonitorModeChanged` callback + `setBusMonitorModes`.
- `tests/MixerGraphStateTests.cpp` — `monitorMode` equality + default tests.
- `tests/InputMixerTests.cpp` — `setBusMonitorMode` engine tests.
- `tests/SessionFormatTests.cpp` — JSON default-suppress.
- `tests/InputMixerBusMonitorPersistenceTests.cpp` (new) — full JSON-layer round-trip.

---

## House rules per CLAUDE.md

- Work on `master`, not a feature branch.
- After every task: `git add` the modified files, single-line `<type>: <subject>` commit, then `git push origin master`.
- Engine tests are TDD: failing test first, then minimal impl, then green.
- Build with `cmake --build build --target IdaTests -j` and `cmake --build build --target IDA -j`.
- Full suite: `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"` — baseline is **715/714 + N** as each task lands new tests.
- Clean rebuild (`rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA -j`) ONLY before asking the operator to eyes-on the GUI changes at the end.
- Subagent contract: subagent commits + pushes its own task. Never `--amend` a prior task's commit.

---

## Task 1 — `MixerBusState::monitorMode` field + equality

**Files:**
- Modify: `core/include/ida/MixerGraphState.h:85-113`
- Test: `tests/MixerGraphStateTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/MixerGraphStateTests.cpp` (one new TEST_CASE):

```cpp
TEST_CASE ("MixerBusState carries monitorMode with Off default and round-trips equality",
           "[mixer-graph-state][bus-monitor]")
{
    ida::MixerBusState a;
    CHECK (a.monitorMode == ida::MonitorMode::Off);

    ida::MixerBusState b;
    CHECK (a == b);                           // default == default

    a.monitorMode = ida::MonitorMode::On;
    CHECK (a != b);                           // a differs on the new field
    b.monitorMode = ida::MonitorMode::On;
    CHECK (a == b);                           // both On → equal again
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[mixer-graph-state][bus-monitor]"
```

Expected: build fails with "no member named 'monitorMode' in 'MixerBusState'".

- [ ] **Step 3: Add the field + extend operator==**

In `core/include/ida/MixerGraphState.h`, replace the `MixerBusState` body block so it reads:

```cpp
struct MixerBusState
{
    std::int64_t           busId        { 0 };
    int                    channelCount { kDefaultBusChannelCount };
    std::string            name;
    MixerBusKind           kind         { MixerBusKind::Bus };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;
    // Operator-set fader / pan / width / mute, mirroring the engine atomics on
    // `Bus`. Defaults equal `Bus`'s defaults so a session that never wrote
    // these fields loads with no audible change.
    float                  gainLinear   { 1.0f };
    bool                   muted        { false };
    float                  pan          { 0.5f };
    float                  width        { 1.0f };
    /// V9 §7.2 (2026-05-25): per-input-side-node MON toggle. `Off` means the
    /// bus is silent in the room (it still writes wherever main-out routes);
    /// `On` mints an OutputMixer channel reading this bus's post-processing
    /// buffer (see `Bus::postProcessingPointer`). Default `Off` per the
    /// explicit-opt-in rule.
    MonitorMode            monitorMode  { MonitorMode::Off };

    bool operator== (const MixerBusState& o) const noexcept
    {
        return busId == o.busId && channelCount == o.channelCount && name == o.name
            && kind == o.kind && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts
            && std::bit_cast<std::uint32_t> (gainLinear) == std::bit_cast<std::uint32_t> (o.gainLinear)
            && muted == o.muted
            && std::bit_cast<std::uint32_t> (pan) == std::bit_cast<std::uint32_t> (o.pan)
            && std::bit_cast<std::uint32_t> (width) == std::bit_cast<std::uint32_t> (o.width)
            && monitorMode == o.monitorMode;
    }
    bool operator!= (const MixerBusState& o) const noexcept { return ! (*this == o); }
};
```

- [ ] **Step 4: Run the test to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[mixer-graph-state][bus-monitor]"
```

Expected: PASS, 4 assertions.

- [ ] **Step 5: Commit**

```bash
git add core/include/ida/MixerGraphState.h tests/MixerGraphStateTests.cpp
git commit -m "feat: MixerBusState carries monitorMode (V9 §7.2 per-input-node MON) with default-Off + equality"
git push origin master
```

---

## Task 2 — `Bus::postProcessingPointer(side)` accessor

**Files:**
- Modify: `engine/include/ida/Bus.h` (public-section accessor declaration; near the bottom of the public block, alongside other const accessors)
- Modify: `engine/src/Bus.cpp` (impl)
- Test: `tests/BusProcessedTapTests.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/BusProcessedTapTests.cpp`:

```cpp
// Tests the seam V9 §7.2's per-bus MON path reads from: a const accessor on
// `Bus` exposing the bus's processed (post-processing) stereo output buffer.
// Mirror of the per-channel `InputMixer::postStripPointer` seam, but on the
// bus side. No allocation, no DSP work — just exposes `Bus::processedBuffer_`
// at a stable address for the bus's lifetime so `OutputMixer::setChannelAudioSource`
// can pin to it once.

#include "ida/Bus.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Bus::postProcessingPointer exposes processedBuffer_ for L and R, nullptr for OOB",
           "[bus][monitor-tap]")
{
    ida::Bus bus { ida::BusId { 7 }, ida::BusConfig { 2, "TapBus" } };

    const auto* l = bus.postProcessingPointer (0);
    const auto* r = bus.postProcessingPointer (1);
    REQUIRE (l != nullptr);
    REQUIRE (r != nullptr);
    CHECK (l != r);                                  // L and R live in distinct regions
    CHECK (r == l + ida::Bus::kMaxBusMixSamples);    // channel-major stride

    CHECK (bus.postProcessingPointer (-1) == nullptr);
    CHECK (bus.postProcessingPointer (2)  == nullptr);

    // Pointer is stable across repeated reads (no allocation on access).
    CHECK (bus.postProcessingPointer (0) == l);
    CHECK (bus.postProcessingPointer (1) == r);
}
```

Add it to `tests/CMakeLists.txt` if your test target enumerates files explicitly (grep for an existing `BusTests`-style entry first; in this repo `tests/CMakeLists.txt` typically globs, so this step is a no-op — confirm with `cmake --build build --target IdaTests -j` after creating the file).

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -10
```

Expected: build fails with "no member named 'postProcessingPointer' in 'ida::Bus'".

- [ ] **Step 3: Declare the accessor in `engine/include/ida/Bus.h`**

Add to the public section (just before the `private:` line is fine — keep it near other read accessors like `pan()`, `width()`):

```cpp
/// V9 §7.2 (per-input-node MON): returns a stable pointer to the bus's
/// post-processing stereo output (`processedBuffer_`) for `side ∈ {0=L, 1=R}`,
/// or `nullptr` if `side` is out of range. Pointer is valid for the bus's
/// lifetime — `processedBuffer_` is sized once in the ctor and never resized.
/// Mirror of `InputMixer::postStripPointer(ChannelId, side)` for buses. The
/// audio-thread reader (`OutputMixer::setChannelAudioSource`) pins this once
/// and re-reads up to the current block size every block; `Bus::process`
/// writes the post-effects samples into `processedBuffer_` before the
/// OutputMixer's render pulls them.
const float* postProcessingPointer (int side) const noexcept;
```

- [ ] **Step 4: Implement in `engine/src/Bus.cpp`**

Append to the file (after the existing function bodies, before the closing `} // namespace ida` if any):

```cpp
const float* Bus::postProcessingPointer (int side) const noexcept
{
    if (side < 0 || side > 1) return nullptr;
    return processedBuffer_.data()
         + static_cast<std::size_t> (side) * kMaxBusMixSamples;
}
```

- [ ] **Step 5: Run the test to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[bus][monitor-tap]"
```

Expected: PASS, 7 assertions.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/Bus.h engine/src/Bus.cpp tests/BusProcessedTapTests.cpp
git commit -m "feat: Bus::postProcessingPointer — stable L/R accessor on processedBuffer_ (V9 §7.2 per-bus MON tap seam)"
git push origin master
```

---

## Task 3 — `InputMixer::setBusMonitorMode(On)` mints an OutputMixer channel

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (add API + `busMonitorRoutes_` map)
- Modify: `engine/src/InputMixer.cpp` (impl + body of `setBusMonitorMode` On-path)
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer::setBusMonitorMode(On) mints an OutputMixer channel and binds it to the bus's processed-tap pointer",
           "[input-mixer][bus-monitor]")
{
    ida::InputMixer  in;
    ida::OutputMixer out;
    in.attachOutputMixer (&out);

    const auto busId = in.addBus (ida::BusConfig { 2, "DrumBus", ida::BusKind::Bus });
    REQUIRE (in.busForId (busId) != nullptr);

    // Default state: MON Off, no auto-channel.
    CHECK (in.busMonitorMode (busId) == ida::MonitorMode::Off);
    CHECK_FALSE (in.busMonitorOutputChannel (busId).has_value());
    const auto channelsBefore = out.channelCount();

    in.setBusMonitorMode (busId, ida::MonitorMode::On);

    CHECK (in.busMonitorMode (busId) == ida::MonitorMode::On);
    REQUIRE (in.busMonitorOutputChannel (busId).has_value());
    const auto monChId = *in.busMonitorOutputChannel (busId);
    CHECK (out.channelCount() == channelsBefore + 1);

    // The minted channel's audio source MUST be the bus's processed tap (so
    // the OutputMixer pulls the bus's post-processing buffer every block).
    const float* expectedL = in.busForId (busId)->postProcessingPointer (0);
    const float* expectedR = in.busForId (busId)->postProcessingPointer (1);
    CHECK (out.channelAudioSourceLeft  (monChId) == expectedL);
    CHECK (out.channelAudioSourceRight (monChId) == expectedR);
}
```

Note: if `OutputMixer::channelAudioSourceLeft / Right` do not yet exist, replace the last two asserts with the existing introspection seam (search OutputMixer.h for `channelAudioSource` / `setChannelAudioSource` and use whatever accessor pair exposes the stored pointer). If no read accessor exists, drop those two lines — they're a nice-to-have, not load-bearing for the rest of the slice.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -10
```

Expected: build fails with "no member named 'setBusMonitorMode' in 'ida::InputMixer'".

- [ ] **Step 3: Declare the API and state in `engine/include/ida/InputMixer.h`**

Right below the existing channel-MON block (`setChannelMonitorMode` / `channelMonitorMode` / `channelMonitorOutputChannel`), add:

```cpp
// V9 §7.2 (2026-05-25) — per-bus MON, mirror of `setChannelMonitorMode`.
// Same lifecycle: `On` auto-creates an OutputMixer channel reading this
// bus's post-processing stereo tap (`Bus::postProcessingPointer`); `Off`
// removes it. Idempotent. Without an attached OutputMixer the request is
// stashed (a later `attachOutputMixer` + replay engages the route). Unknown
// busId is a silent no-op (defensive — mirror of channel).
void                            setBusMonitorMode (BusId, MonitorMode);

/// Message-thread accessor. Unknown id reads as `MonitorMode::Off`.
MonitorMode                     busMonitorMode (BusId) const noexcept;

/// Message-thread accessor — the OutputChannelId of the auto-created monitor
/// channel for this bus on the attached OutputMixer, or `std::nullopt` when
/// MON is Off, the InputMixer has no OutputMixer attached at On time, or the
/// bus id is unknown. Diagnostic / test seam.
std::optional<OutputChannelId>  busMonitorOutputChannel (BusId) const noexcept;
```

In the `private:` section, alongside `channelMonitorRoutes_`, add:

```cpp
/// V9 §7.2 — per-bus MON state. Same shape as `channelMonitorRoutes_` but
/// keyed on `BusId.value()`. Entry present only when MON is On for that bus
/// (the `outputChannelId` is the auto-minted OutputMixer channel reading the
/// bus's processed-tap pointer). MON → Off erases the entry.
std::unordered_map<std::int64_t, MonitorRouteState> busMonitorRoutes_;
```

(Reuse the existing `MonitorRouteState` struct already defined above
`channelMonitorRoutes_`; do not redeclare it.)

- [ ] **Step 4: Implement in `engine/src/InputMixer.cpp`**

Append (placement is fine right after the channel-MON impls at lines 183-258 — the file's organization groups MON code together):

```cpp
void InputMixer::setBusMonitorMode (BusId id, MonitorMode mode)
{
    // Unknown bus → silent no-op (defensive; mirror of channel-MON contract).
    auto* bus = busForId (id);
    if (bus == nullptr) return;

    auto it = busMonitorRoutes_.find (id.value());

    if (mode == MonitorMode::Off)
    {
        if (it != busMonitorRoutes_.end())
        {
            if (it->second.outputChannelId.has_value() && outputMixer_ != nullptr)
                outputMixer_->removeChannel (*it->second.outputChannelId);
            busMonitorRoutes_.erase (it);
        }
        return;
    }

    // mode == On.
    // Idempotent: a second On call while the OutputMixer channel already
    // exists is a no-op (don't mint a duplicate channel).
    if (it != busMonitorRoutes_.end() && it->second.outputChannelId.has_value())
        return;

    // Without an attached OutputMixer, track the mode so a later
    // `attachOutputMixer` + replay path can engage the bus, but mint nothing
    // in the meantime. Mirror of the channel-side deferral.
    if (outputMixer_ == nullptr)
    {
        MonitorRouteState state;
        state.mode = MonitorMode::On;
        busMonitorRoutes_[id.value()] = std::move (state);
        return;
    }

    // Mint a fresh OutputMixer channel and wire its audio source to this
    // bus's post-processing stereo buffer. Pointer is stable for the bus's
    // lifetime; OutputMixer reads it every block. The minted channel also
    // gets its own ChannelStrip<Audio> so the operator can mix it (gain/pan/
    // mute/inserts/sends/destinations) as a peer of phrase channels per
    // whitepaper V9 §7.2 — same shape as the per-channel MON path.
    const auto monChId = outputMixer_->addChannel (SignalType::Audio);
    outputMixer_->setChannelStrip (monChId,
        std::make_unique<ChannelStrip<SignalType::Audio>>());
    outputMixer_->setChannelAudioSource (monChId,
                                         bus->postProcessingPointer (0),
                                         bus->postProcessingPointer (1));

    MonitorRouteState state;
    state.mode            = MonitorMode::On;
    state.outputChannelId = monChId;
    busMonitorRoutes_[id.value()] = std::move (state);
}

MonitorMode InputMixer::busMonitorMode (BusId id) const noexcept
{
    if (auto it = busMonitorRoutes_.find (id.value()); it != busMonitorRoutes_.end())
        return it->second.mode;
    return MonitorMode::Off;
}

std::optional<OutputChannelId>
InputMixer::busMonitorOutputChannel (BusId id) const noexcept
{
    if (auto it = busMonitorRoutes_.find (id.value()); it != busMonitorRoutes_.end())
        return it->second.outputChannelId;
    return std::nullopt;
}
```

- [ ] **Step 5: Run the test to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][bus-monitor]"
```

Expected: PASS. If the L/R audio-source asserts had to be dropped in Step 1 because the accessor pair doesn't exist, the test should still pass on the remaining 5 assertions and prove the channel was minted.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::setBusMonitorMode(On) mints OutputMixer channel from Bus::postProcessingPointer (V9 §7.2 per-bus MON)"
git push origin master
```

---

## Task 4 — `setBusMonitorMode` Off / idempotence / unknown id / attach-deferral

**Files:**
- Test: `tests/InputMixerTests.cpp`

The On-path covered by Task 3 implicitly exercises the early-return branches via the impl already written. This task locks each branch behind its own test so a future refactor that breaks one is caught.

- [ ] **Step 1: Write the failing tests**

Append four TEST_CASEs to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer::setBusMonitorMode(Off) after On removes the auto-minted OutputMixer channel",
           "[input-mixer][bus-monitor]")
{
    ida::InputMixer  in;
    ida::OutputMixer out;
    in.attachOutputMixer (&out);
    const auto busId = in.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    in.setBusMonitorMode (busId, ida::MonitorMode::On);
    const auto countAfterOn = out.channelCount();
    REQUIRE (in.busMonitorOutputChannel (busId).has_value());

    in.setBusMonitorMode (busId, ida::MonitorMode::Off);

    CHECK (in.busMonitorMode (busId) == ida::MonitorMode::Off);
    CHECK_FALSE (in.busMonitorOutputChannel (busId).has_value());
    CHECK (out.channelCount() == countAfterOn - 1);
}

TEST_CASE ("InputMixer::setBusMonitorMode(On) is idempotent — second On does not mint a second channel",
           "[input-mixer][bus-monitor]")
{
    ida::InputMixer  in;
    ida::OutputMixer out;
    in.attachOutputMixer (&out);
    const auto busId = in.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });

    in.setBusMonitorMode (busId, ida::MonitorMode::On);
    const auto chFirst = *in.busMonitorOutputChannel (busId);
    const auto count1  = out.channelCount();

    in.setBusMonitorMode (busId, ida::MonitorMode::On);

    CHECK (*in.busMonitorOutputChannel (busId) == chFirst);
    CHECK (out.channelCount() == count1);
}

TEST_CASE ("InputMixer::setBusMonitorMode for an unknown BusId is a silent no-op",
           "[input-mixer][bus-monitor]")
{
    ida::InputMixer  in;
    ida::OutputMixer out;
    in.attachOutputMixer (&out);
    const auto before = out.channelCount();

    in.setBusMonitorMode (ida::BusId { 9999 }, ida::MonitorMode::On);

    CHECK (in.busMonitorMode (ida::BusId { 9999 }) == ida::MonitorMode::Off);
    CHECK_FALSE (in.busMonitorOutputChannel (ida::BusId { 9999 }).has_value());
    CHECK (out.channelCount() == before);
}

TEST_CASE ("InputMixer::setBusMonitorMode(On) without an attached OutputMixer stashes the mode",
           "[input-mixer][bus-monitor]")
{
    ida::InputMixer in;
    // Deliberately do NOT attach an OutputMixer.
    const auto busId = in.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });

    in.setBusMonitorMode (busId, ida::MonitorMode::On);

    CHECK (in.busMonitorMode (busId) == ida::MonitorMode::On);
    CHECK_FALSE (in.busMonitorOutputChannel (busId).has_value());
    // After the fact, attach and verify that the stash exists but does NOT
    // auto-engage (matching the channel-side contract — caller drives replay).
    ida::OutputMixer out;
    in.attachOutputMixer (&out);
    CHECK (in.busMonitorMode (busId) == ida::MonitorMode::On);
    CHECK_FALSE (in.busMonitorOutputChannel (busId).has_value());
}
```

- [ ] **Step 2: Run them — all four should PASS already**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][bus-monitor]"
```

Expected: PASS (Task 3's impl already covers every branch). If any fails, the impl in Task 3 needs a fix before this task moves on — do NOT loosen the test.

- [ ] **Step 3: Commit**

```bash
git add tests/InputMixerTests.cpp
git commit -m "test: InputMixer::setBusMonitorMode — Off teardown, idempotence, unknown-id, attach-deferral"
git push origin master
```

---

## Task 5 — `exportGraphState` captures bus `monitorMode`

**Files:**
- Modify: `engine/src/InputMixer.cpp:427-444` (the buses loop inside `exportGraphState`)
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer::exportGraphState captures each bus's monitorMode",
           "[input-mixer][bus-monitor][persistence]")
{
    ida::InputMixer  in;
    ida::OutputMixer out;
    in.attachOutputMixer (&out);
    const auto monBus = in.addBus (ida::BusConfig { 2, "OnBus",  ida::BusKind::Bus });
    const auto offBus = in.addBus (ida::BusConfig { 2, "OffBus", ida::BusKind::Bus });
    in.setBusMonitorMode (monBus, ida::MonitorMode::On);
    // offBus left at default Off.

    const auto state = in.exportGraphState();

    REQUIRE (state.buses.size() >= 2);
    bool sawOn = false, sawOff = false;
    for (const auto& b : state.buses)
    {
        if (b.busId == monBus.value()) { sawOn  = true; CHECK (b.monitorMode == ida::MonitorMode::On);  }
        if (b.busId == offBus.value()) { sawOff = true; CHECK (b.monitorMode == ida::MonitorMode::Off); }
    }
    CHECK (sawOn);
    CHECK (sawOff);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "InputMixer::exportGraphState captures each bus's monitorMode"
```

Expected: assertion `b.monitorMode == ida::MonitorMode::On` fails (export still default-Off because the field isn't being populated).

- [ ] **Step 3: Populate the field in `exportGraphState`**

In `engine/src/InputMixer.cpp`, inside the existing `for (std::size_t i = 0; i < buses_.size(); ++i)` loop in `exportGraphState`, after the line `entry.width = bus.width();`, add:

```cpp
entry.monitorMode = busMonitorMode (bus.id());
```

- [ ] **Step 4: Run to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "InputMixer::exportGraphState captures each bus's monitorMode"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::exportGraphState captures bus monitorMode"
git push origin master
```

---

## Task 6 — `importGraphState` replays bus `monitorMode`

**Files:**
- Modify: `engine/src/InputMixer.cpp` (inside `importGraphState`, mirror of the existing channel replay at lines 555-557)
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer::importGraphState replays bus monitorMode and re-mints the OutputMixer channel",
           "[input-mixer][bus-monitor][persistence]")
{
    ida::InputMixer  src;
    ida::OutputMixer srcOut;
    src.attachOutputMixer (&srcOut);
    const auto busId = src.addBus (ida::BusConfig { 2, "MyBus", ida::BusKind::Bus });
    src.setBusMonitorMode (busId, ida::MonitorMode::On);

    const auto snapshot = src.exportGraphState();

    ida::InputMixer  dst;
    ida::OutputMixer dstOut;
    dst.attachOutputMixer (&dstOut);
    dst.importGraphState (snapshot);

    CHECK (dst.busMonitorMode (busId) == ida::MonitorMode::On);
    REQUIRE (dst.busMonitorOutputChannel (busId).has_value());
    CHECK (dstOut.channelCount() == srcOut.channelCount());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "InputMixer::importGraphState replays bus monitorMode"
```

Expected: `dst.busMonitorMode (busId) == On` fails (no replay yet).

- [ ] **Step 3: Add the replay block in `importGraphState`**

In `engine/src/InputMixer.cpp`, locate the existing channel-MON replay block (the comment "V9 Slice 3 — replay the per-channel MON mode" around line 546). Immediately AFTER that for-loop (before "3. Apply main-outs"), add:

```cpp
// V9 §7.2 (2026-05-25) — replay per-bus MON in the same window as per-
// channel MON: after every bus has been minted (step 1 above) and BEFORE
// any structural change that depends on the OutputMixer collaborator's
// channel set. `setBusMonitorMode` will no-op cleanly when the OutputMixer
// is unattached (matching the channel-side stash semantics).
for (const auto& b : state.buses)
    if (b.monitorMode != MonitorMode::Off)
        setBusMonitorMode (BusId (b.busId), b.monitorMode);
```

- [ ] **Step 4: Run to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "InputMixer::importGraphState replays bus monitorMode"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::importGraphState replays bus monitorMode (mirror of channel-side replay)"
git push origin master
```

---

## Task 7 — SessionFormat: default-suppress `monitorMode` in JSON

**Files:**
- Modify: `persistence/src/SessionFormat.cpp:924-957` (`busStateToVar` + `busStateFromVar`)
- Test: `tests/SessionFormatTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/SessionFormatTests.cpp`:

```cpp
TEST_CASE ("MixerBusState JSON: monitorMode default-suppress + On round-trip",
           "[sessionformat][bus-monitor]")
{
    ida::MixerBusState off;
    off.busId = 1; off.name = "Off"; off.kind = ida::MixerBusKind::Bus;
    off.mainOut.kind = ida::MixerMainOut::Kind::Terminal;
    off.mainOut.terminal = ida::MixerTerminalKind::Tape;

    const auto offJson = ida::persistence::busStateForTest (off);
    CHECK_FALSE (offJson.contains ("monitorMode"));   // default-suppressed

    ida::MixerBusState on = off;
    on.busId = 2; on.name = "On";
    on.monitorMode = ida::MonitorMode::On;
    const auto onJson = ida::persistence::busStateForTest (on);
    CHECK (onJson.contains ("monitorMode"));
    CHECK (onJson.contains ("On"));

    // Round-trip through the InputMixerGraphState wrapper (which is what
    // serializeMixerGraphState actually does on disk).
    ida::InputMixerGraphState s;
    s.buses.push_back (off);
    s.buses.push_back (on);
    s.nextBusId = 3;
    const auto wireText = ida::persistence::serializeMixerGraphState (s);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (wireText);
    REQUIRE (restored.buses.size() == 2);
    CHECK (restored.buses[0].monitorMode == ida::MonitorMode::Off);
    CHECK (restored.buses[1].monitorMode == ida::MonitorMode::On);
}
```

If `busStateForTest` doesn't exist, simplify the first half: skip the bare-busState string assertions and rely on the round-trip half of the test alone. The round-trip path is the load-bearing assertion.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "MixerBusState JSON: monitorMode default-suppress"
```

Expected: `restored.buses[1].monitorMode == On` fails (deserialize never reads the field).

- [ ] **Step 3: Extend the serializer**

In `persistence/src/SessionFormat.cpp`, locate `busStateToVar` (around line 924). After the existing `if (b.width != 1.0f) obj->setProperty ("width", b.width);`, add:

```cpp
// V9 §7.2 (2026-05-25) — default-suppress per the opt-in rule (mirror
// of InputChannelState's monitorMode handling).
if (b.monitorMode != MonitorMode::Off)
    obj->setProperty ("monitorMode", juce::String (monitorModeToken (b.monitorMode)));
```

In `busStateFromVar` (immediately below), after the existing width read, add:

```cpp
if (auto pm = optionalProperty (v, "monitorMode"); ! pm.isVoid())
    b.monitorMode = monitorModeFromString (pm.toString());
```

- [ ] **Step 4: Run to verify green**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "MixerBusState JSON: monitorMode default-suppress"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add persistence/src/SessionFormat.cpp tests/SessionFormatTests.cpp
git commit -m "feat: SessionFormat — MixerBusState.monitorMode JSON serialization, default-suppress"
git push origin master
```

---

## Task 8 — Full JSON-layer round-trip (E2E sanity test)

**Files:**
- Test: `tests/InputMixerBusMonitorPersistenceTests.cpp` (new)

This proves the engine + persistence + replay chain end-to-end, mirror of the OutputMixer test pattern added in commit `2da5459`. It is verification-only — no new production code.

- [ ] **Step 1: Write the test**

Create `tests/InputMixerBusMonitorPersistenceTests.cpp`:

```cpp
// E2E round-trip: addBus + setBusMonitorMode(On) → exportGraphState →
// serializeMixerGraphState (JSON) → deserialize → importGraphState into a
// fresh InputMixer with a fresh OutputMixer attached. Bus MON state and
// the auto-minted OutputMixer channel must survive the full disk cycle.
// Mirror of the OutputMixer aux-bus pan/width/gain/muted JSON round-trip
// added in commit 2da5459.

#include "ida/InputMixer.h"
#include "ida/OutputMixer.h"
#include "ida/SessionFormat.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("InputMixer bus monitorMode survives the full JSON round-trip",
           "[input-mixer][bus-monitor][persistence][json]")
{
    ida::InputMixer  src;
    ida::OutputMixer srcOut;
    src.attachOutputMixer (&srcOut);
    const auto monBus = src.addBus (ida::BusConfig { 2, "OnBus",  ida::BusKind::Bus });
    const auto offBus = src.addBus (ida::BusConfig { 2, "OffBus", ida::BusKind::Bus });
    src.setBusMonitorMode (monBus, ida::MonitorMode::On);

    const auto exported = src.exportGraphState();
    const auto wireText = ida::persistence::serializeMixerGraphState (exported);
    const auto reloaded = ida::persistence::deserializeInputMixerGraphState (wireText);

    ida::InputMixer  dst;
    ida::OutputMixer dstOut;
    dst.attachOutputMixer (&dstOut);
    dst.importGraphState (reloaded);

    CHECK (dst.busMonitorMode (monBus) == ida::MonitorMode::On);
    CHECK (dst.busMonitorMode (offBus) == ida::MonitorMode::Off);
    CHECK (dst.busMonitorOutputChannel (monBus).has_value());
    CHECK_FALSE (dst.busMonitorOutputChannel (offBus).has_value());
}
```

- [ ] **Step 2: Run — should PASS immediately**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][bus-monitor][persistence][json]"
```

Expected: PASS, 4 assertions.

- [ ] **Step 3: Commit**

```bash
git add tests/InputMixerBusMonitorPersistenceTests.cpp
git commit -m "test: InputMixer bus MON — full JSON round-trip E2E sanity"
git push origin master
```

---

## Task 9 — InputMixerPane: bus strip MON button

**Files:**
- Modify: `app/MainComponent.cpp` (the `InputMixerPane` class — `setBusStrips` method body + new state vectors + new public methods + new callback)

This task is operator-verified, not headless-tested (CompactFaderStrip is a JUCE GUI component). Plan goal is to mirror the per-channel MON button code line-for-line, just on the bus row.

- [ ] **Step 1: Add state + callback declarations in `InputMixerPane`**

In `app/MainComponent.cpp`, find the `InputMixerPane` class block. Near the existing `onMonitorModeChanged` declaration (around line 600), add the bus-row analog:

```cpp
/// Operator toggled MON on bus row `busIdx`. `mode` is the new MonitorMode
/// (Off/On). MainComponent applies it via `InputMixer::setBusMonitorMode`
/// inside the same audio-callback bracket the channel-row handler uses.
std::function<void (int busIdx, ida::MonitorMode mode)> onBusMonitorModeChanged;
```

Near the existing `monitorButtons_` / `monitorModes_` member vectors (search for `monitorButtons_`), add the bus-row analogs:

```cpp
std::vector<std::unique_ptr<juce::TextButton>> busMonitorButtons_;
std::vector<ida::MonitorMode>                  busMonitorModes_;
```

- [ ] **Step 2: Build the MON button inside `setBusStrips`'s for-loop**

In the `InputMixerPane::setBusStrips` method (the first of the two `setBusStrips` overloads — the one with `busStripInsButtons_`), inside the `for (int i = 0; …)` loop, AFTER the existing INS button block and BEFORE the `auto overlay = …` block, add:

```cpp
// V9 §7.2 (2026-05-25) — MON button on the bus row. Same shape as the
// channel-row MON button (search for `monitorButtons_` in this file):
// two-state toggle, default Off, single-click flips. Without this an
// input aux bus that sums signals has no architectural path to be heard
// (whitepaper §6.6 + §7.2).
auto mon = std::make_unique<juce::TextButton>();
mon->setButtonText ("Off");
mon->setTooltip ("MON off — you do not hear this bus through IDA. "
                 "Click to enable monitoring.");
mon->onClick = [this, idx] { toggleBusMonitorModeAt (idx); };
addAndMakeVisible (*mon);
busMonitorButtons_.push_back (std::move (mon));
busMonitorModes_.push_back (ida::MonitorMode::Off);
```

At the TOP of `setBusStrips` (alongside the existing `busStrips_.clear();` etc.), add:

```cpp
busMonitorButtons_.clear();
busMonitorModes_.clear();
```

- [ ] **Step 3: Add the toggle helper + setter (mirror of the channel-side methods)**

In `InputMixerPane`, find `toggleMonitorModeAt` and `setMonitorModeAt` (and `setMonitorModes`). After them add:

```cpp
/// Mirror of `setMonitorModeAt` for bus rows. Updates the button label /
/// tooltip and (if `notify`) fires `onBusMonitorModeChanged`. V9 §7.2: two-
/// state toggle (Off ↔ On).
void setBusMonitorModeAt (int idx, ida::MonitorMode mode, bool notify)
{
    if (idx < 0 || idx >= static_cast<int> (busMonitorButtons_.size())) return;
    busMonitorModes_[static_cast<std::size_t> (idx)] = mode;

    juce::String label, tooltip;
    switch (mode)
    {
        case ida::MonitorMode::Off:
            label   = "Off";
            tooltip = "MON off — you do not hear this bus through IDA. "
                      "Click to enable monitoring.";
            break;
        case ida::MonitorMode::On:
            label   = "MON";
            tooltip = "MON on — you hear this bus through IDA, "
                      "post-processing. Click to disable monitoring.";
            break;
    }
    busMonitorButtons_[static_cast<std::size_t> (idx)]->setButtonText (label);
    busMonitorButtons_[static_cast<std::size_t> (idx)]->setTooltip (tooltip);
    if (notify && onBusMonitorModeChanged) onBusMonitorModeChanged (idx, mode);
}

/// One-click flip from current mode (Off → On / On → Off).
void toggleBusMonitorModeAt (int idx)
{
    if (idx < 0 || idx >= static_cast<int> (busMonitorModes_.size())) return;
    const auto next = busMonitorModes_[static_cast<std::size_t> (idx)] == ida::MonitorMode::Off
                          ? ida::MonitorMode::On
                          : ida::MonitorMode::Off;
    setBusMonitorModeAt (idx, next, /*notify*/ true);
}

/// Refresh button states from the engine — called by `refreshInputMixer`
/// after any structural change (load, bus add/remove). `modes` is parallel
/// to `busStrips_`.
void setBusMonitorModes (const std::vector<ida::MonitorMode>& modes)
{
    const int n = std::min ((int) modes.size(), (int) busMonitorButtons_.size());
    for (int i = 0; i < n; ++i)
        setBusMonitorModeAt (i, modes[static_cast<std::size_t> (i)], /*notify*/ false);
}
```

- [ ] **Step 4: Position the MON button in `resized()`**

Find `InputMixerPane::resized` (or equivalent). Locate where `monitorButtons_[i]` is laid out for the channel rows. Apply the same layout treatment to each `busMonitorButtons_[i]` inside whatever bus-row layout block already positions `busStripInsButtons_[i]` and `busDestButtons_[i]` — place the MON button next to (above or beside, matching channel-row convention) the INS button. The layout code is mechanical — match the channel-row layout's pattern.

If unsure of the exact positioning, search the file for `busStripInsButtons_[` references in `resized` and add an adjacent line for `busMonitorButtons_[`.

- [ ] **Step 5: Compile-only check**

```bash
cmake --build build --target IDA -j 2>&1 | tail -10
```

Expected: clean build (warnings about unused `onBusMonitorModeChanged` are fine — Task 10 wires it).

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: InputMixerPane — bus strip MON button (label/tooltip mirror of per-channel MON)"
git push origin master
```

---

## Task 10 — MainComponent wiring: `onBusMonitorModeChanged` → engine

**Files:**
- Modify: `app/MainComponent.cpp` (the callback wire-up block where `inputMixerPane_->onMonitorModeChanged` is set — around line 3896)

- [ ] **Step 1: Add the relay**

In `app/MainComponent.cpp`, find the existing `inputMixerPane_->onMonitorModeChanged = [...]` lambda (around line 3896). IMMEDIATELY AFTER its closing `};`, add:

```cpp
// V9 §7.2 (2026-05-25) — bus-row MON relay. Mirror of `onMonitorModeChanged`
// above, addressing the bus by busStripIds_[busIdx] and routing through
// `setBusMonitorMode`. The audio-callback bracket is required because
// `setBusMonitorMode` mutates the OutputMixer's channel registry (addChannel /
// removeChannel are message-thread only).
inputMixerPane_->onBusMonitorModeChanged = [this] (int busIdx, ida::MonitorMode mode)
{
    if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
    const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    inputMixer_->setBusMonitorMode (busId, mode);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    // The Output Mixer pane gains / loses a MON-band strip in lockstep.
    refreshOutputMixerMonChannels();
};
```

- [ ] **Step 2: Compile check**

```bash
cmake --build build --target IDA -j 2>&1 | tail -6
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: MainComponent — onBusMonitorModeChanged relays to InputMixer::setBusMonitorMode (V9 §7.2)"
git push origin master
```

---

## Task 11 — `refreshOutputMixerMonChannels`: walk bus MONs alongside channel MONs

**Files:**
- Modify: `app/MainComponent.cpp:5867-5902` (the body of `refreshOutputMixerMonChannels`)
- Modify: `app/MainComponent.cpp` (add a new parallel vector `monStripInputBusIds_` to MainComponent's private members)
- Modify: `app/MainComponent.cpp` (`refreshOutputDestinations` MON block — use `monStripInputBusIds_` for bus-sourced rows when resolving)
- Modify: `app/MainComponent.cpp:5710-5754` (`refreshOutputMixer` — extend MON-meter loop)

- [ ] **Step 1: Add the parallel vector to MainComponent's private members**

In `app/MainComponent.cpp`, search for `monStripInputChannelIds_` in the MainComponent class's private section. Right below its declaration, add:

```cpp
/// V9 §7.2 (2026-05-25) — for each MON-band row whose SOURCE is a bus
/// (not an input channel), the BusId of the source. Parallel to
/// `monStripInputChannelIds_`: row i is bus-sourced iff
/// `monStripInputBusIds_[i] != ida::BusId{0}` (BusId 0 is master on
/// OutputMixer; on InputMixer there is no BusId 0, so it works as a
/// sentinel). Used by refresh-destination + refresh-meter paths to
/// resolve each row to its OutputChannelId via
/// `InputMixer::busMonitorOutputChannel` instead of
/// `InputMixer::channelMonitorOutputChannel`.
std::vector<ida::BusId> monStripInputBusIds_;
```

- [ ] **Step 2: Extend `refreshOutputMixerMonChannels`**

Find the function (line ~5867). Modify its body so the strip list combines channel-sourced + bus-sourced entries. Full replacement of the function body:

```cpp
void MainComponent::refreshOutputMixerMonChannels()
{
    if (outputMixerPane_ == nullptr) return;
    if (inputMixer_ == nullptr || outputMixer_ == nullptr) return;

    std::vector<OutputMixerPane::MonStripInfo> infos;
    std::vector<ida::ChannelId>                newChanIds;
    std::vector<ida::BusId>                    newBusIds;
    infos.reserve     (inputStripChannelIds_.size() + busStripIds_.size());
    newChanIds.reserve(inputStripChannelIds_.size() + busStripIds_.size());
    newBusIds.reserve (inputStripChannelIds_.size() + busStripIds_.size());

    // 1. Channel-sourced MON strips, in input-strip row order.
    for (std::size_t i = 0; i < inputStripChannelIds_.size(); ++i)
    {
        const auto chId = inputStripChannelIds_[i];
        if (inputMixer_->channelMonitorMode (chId) != ida::MonitorMode::On) continue;
        if (! inputMixer_->channelMonitorOutputChannel (chId).has_value()) continue;
        infos.push_back ({ chId, "MON " + juce::String ((int) i + 1) });
        newChanIds.push_back (chId);
        newBusIds.push_back  (ida::BusId { 0 });   // sentinel — this row is channel-sourced
    }

    // 2. Bus-sourced MON strips (V9 §7.2 2026-05-25), in bus-strip row order.
    //    Labelled by the bus's user-given name (operator-recognizable).
    for (std::size_t i = 0; i < busStripIds_.size(); ++i)
    {
        const auto bId = busStripIds_[i];
        if (inputMixer_->busMonitorMode (bId) != ida::MonitorMode::On) continue;
        if (! inputMixer_->busMonitorOutputChannel (bId).has_value()) continue;
        auto* bus = inputMixer_->busForId (bId);
        if (bus == nullptr) continue;
        // The first field is the source ChannelId; for bus rows it isn't
        // meaningful — pass ChannelId{0} (sentinel; downstream code uses
        // monStripInputBusIds_[row] != BusId{0} to distinguish).
        infos.push_back ({ ida::ChannelId { 0 }, juce::String (bus->config().name) });
        newChanIds.push_back (ida::ChannelId { 0 });
        newBusIds.push_back  (bId);
    }

    if (newChanIds == monStripInputChannelIds_ && newBusIds == monStripInputBusIds_)
    {
        refreshOutputDestinations();   // dest labels may still need a refresh
        return;
    }

    monStripInputChannelIds_ = std::move (newChanIds);
    monStripInputBusIds_     = std::move (newBusIds);
    outputMixerPane_->setMonStrips (infos);
    refreshOutputDestinations();
}
```

- [ ] **Step 3: Extend `refreshOutputDestinations`'s MON block**

In `app/MainComponent.cpp`, find the MON-destinations block landed in commit `2da5459`. Replace its body so each MON row resolves its OutputChannelId via the bus accessor when bus-sourced, else the channel accessor. Replace:

```cpp
const auto outOpt = (inputMixer_ != nullptr)
                        ? inputMixer_->channelMonitorOutputChannel (inChId)
                        : std::optional<ida::OutputChannelId> {};
```

with:

```cpp
std::optional<ida::OutputChannelId> outOpt;
if (inputMixer_ != nullptr)
{
    const auto busId = monStripInputBusIds_[static_cast<std::size_t> (i)];
    if (busId.value() != 0)
        outOpt = inputMixer_->busMonitorOutputChannel (busId);
    else
        outOpt = inputMixer_->channelMonitorOutputChannel (inChId);
}
```

The rest of the block stays as-is.

- [ ] **Step 4: Extend `refreshOutputMixer`'s MON-meter loop**

In `app/MainComponent.cpp:5738-5753` (the `for (std::size_t i = 0; i < monStripInputChannelIds_.size(); ++i)` block), replace the inner body so each MON row resolves to its OutputChannelId via bus-or-channel selector. Full replacement:

```cpp
if (inputMixer_ != nullptr)
{
    for (std::size_t i = 0; i < monStripInputChannelIds_.size(); ++i)
    {
        std::optional<ida::OutputChannelId> outOpt;
        const auto busId = monStripInputBusIds_[i];
        if (busId.value() != 0)
            outOpt = inputMixer_->busMonitorOutputChannel (busId);
        else
            outOpt = inputMixer_->channelMonitorOutputChannel (monStripInputChannelIds_[i]);
        if (! outOpt.has_value()) continue;
        auto* s = outputMixer_->audioStripForChannel (*outOpt);
        if (s == nullptr) continue;
        outputMixerPane_->setMonStripLevelDb (static_cast<int> (i),
                                              linToDb (s->peakLeft()),
                                              linToDb (s->peakRight()));
        outputMixerPane_->setMonStripLufs (static_cast<int> (i),
                                           s->lufsShortTerm());
    }
}
```

- [ ] **Step 5: Compile + run the full test suite**

```bash
cmake --build build --target IDA IdaTests -j && \
  ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: clean build; all tests pass (count = baseline + N new from earlier tasks).

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: refreshOutputMixer{,MonChannels,Destinations} walk bus MONs alongside channel MONs (V9 §7.2)"
git push origin master
```

---

## Task 12 — Load-handler replay block for bus MON

**Files:**
- Modify: `app/MainComponent.cpp:7106-7111` (channel MON replay block — add a sibling for bus MON)

- [ ] **Step 1: Add the replay loop**

In `app/MainComponent.cpp`, find the existing channel-MON replay block in `chooseFileAndLoad`:

```cpp
if (loadedInputMixer.has_value())
    for (const auto& c : loadedInputMixer->channels)
        if (c.monitorMode == ida::MonitorMode::On)
            inputMixer_->setChannelMonitorMode (
                ida::ChannelId (c.channelId),
                ida::MonitorMode::On);
```

IMMEDIATELY AFTER it (still inside the same `if (loadedInputMixer.has_value())` branch — drop into a separate `if` to keep the structure parallel), add:

```cpp
// V9 §7.2 (2026-05-25) — same MON-replay treatment for input buses. The
// `inputMixer_->importGraphState` above ran before this OutputMixer
// attachment, so per-bus MON state was tracked but no OutputMixer channels
// were minted. Now that the attachment is live, replay every MON-on bus —
// setBusMonitorMode(On) is idempotent and mints the auto-created OutputMixer
// channel (+ its ChannelStrip) for any bus whose route was deferred.
if (loadedInputMixer.has_value())
    for (const auto& b : loadedInputMixer->buses)
        if (b.monitorMode == ida::MonitorMode::On)
            inputMixer_->setBusMonitorMode (
                ida::BusId (b.busId),
                ida::MonitorMode::On);
```

- [ ] **Step 2: Compile**

```bash
cmake --build build --target IDA -j 2>&1 | tail -6
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: chooseFileAndLoad — bus MON replay block mirrors channel MON replay (V9 §7.2)"
git push origin master
```

---

## Task 13 — Strip-state sync: `refreshInputMixer` pushes bus MON button states

**Files:**
- Modify: `app/MainComponent.cpp` (`refreshInputMixer` — function defined around line 5680)
- Modify: `app/MainComponent.cpp` (`rebuildBusStrips` — around line 6317; needs to push initial MON states after the strip rebuild)

After load, the engine's bus MON state is correct but the bus strip's MON button still shows whatever it last displayed. Same issue exists on the channel side via `refreshInputMixer` → `inputMixerPane_->setMonitorModes`; mirror that for buses.

- [ ] **Step 1: Push bus MON states in `refreshInputMixer`**

Find `MainComponent::refreshInputMixer`. After the existing `inputMixerPane_->setMonitorModes(...)` line, add:

```cpp
// V9 §7.2 (2026-05-25) — bus-row MON button states. Mirror of the channel-
// row push above; keeps the bus strip's MON button in sync with engine
// state after load / bus add / structural rebuild.
{
    std::vector<ida::MonitorMode> busModes;
    busModes.reserve (busStripIds_.size());
    for (const auto& bId : busStripIds_)
        busModes.push_back (inputMixer_->busMonitorMode (bId));
    inputMixerPane_->setBusMonitorModes (busModes);
}
```

- [ ] **Step 2: Same push at the end of `rebuildBusStrips`**

In `MainComponent::rebuildBusStrips` (line ~6317), after the existing `inputMixerPane_->setBusStrips (infos);` line at the bottom, add:

```cpp
// Newly-created strips default to MON Off; push engine state so an existing
// MON-on bus after a structural rebuild (e.g. load handler) shows the
// correct button label without waiting for the next `refreshInputMixer`.
std::vector<ida::MonitorMode> busModes;
busModes.reserve (busStripIds_.size());
for (const auto& bId : busStripIds_)
    busModes.push_back (inputMixer_->busMonitorMode (bId));
inputMixerPane_->setBusMonitorModes (busModes);
```

- [ ] **Step 3: Compile + full tests**

```bash
cmake --build build --target IDA IdaTests -j && \
  ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: clean build, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: refreshInputMixer + rebuildBusStrips push bus MON button states from engine (V9 §7.2)"
git push origin master
```

---

## Task 14 — Clean rebuild + operator eyes-on

**Files:** none (verification only)

- [ ] **Step 1: Clean rebuild per CLAUDE.md**

```bash
rm -rf build && \
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && \
  cmake --build build --target IDA -j 2>&1 | tail -3
```

Expected: clean Release build.

- [ ] **Step 2: Full test pass on the clean build**

```bash
cmake --build build --target IdaTests -j && \
  ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: all tests pass.

- [ ] **Step 3: Operator verification — write the verification recipe to `continue.md`**

Update `continue.md` with the following recipe for the operator:

```
1. Launch IDA via the Desktop alias.
2. On the Input Mixer, add an aux bus (blank-area gesture).
3. Click the new bus strip's MON button — label flips Off → MON; tooltip
   updates; a new strip appears in the Output Mixer's MON band, labelled
   with the bus's name.
4. Play audio into a channel that routes into the bus; confirm the bus's
   summed signal is audible.
5. Click MON again — label flips MON → Off; the strip vanishes from the
   MON band.
6. Set MON On, save the session, quit, relaunch, load the session.
   Confirm: MON button shows MON, the MON-band strip is back, the audio
   is audible.
7. Repeat 2-3 on an FX return — same behaviour.
```

- [ ] **Step 4: Commit the continue.md refresh + announce ready for operator**

```bash
git add continue.md
git commit -m "docs: continue.md — bus MON slice complete; operator-verify recipe"
git push origin master
```

End of slice. Hand off to the operator with: "Bus MON slice landed across N commits, ending at HEAD <sha>. Clean Release build is on disk. Walk through the recipe in continue.md."

---

## Self-review notes

- **Spec coverage:** §4.1 (engine) → Tasks 1-6. §4.2 (persistence) → Tasks 5-8.
  §4.3 (UI) → Tasks 9-13. §4.4 (independence rules) is implicit in the engine
  contract — covered by the channel-side rules that the bus side mirrors. §5
  (test surface) → Tasks 1-8 cover all six engine-test requirements.
- **Open-question defaults locked:** MON-strip label = bus name (Task 11
  step 2, the bus-sourced infos.push_back uses `bus->config().name`). Bus-MON
  detail panel surface = mirror of channel MON (no new spec — uses the
  existing OutputMixerPane MON detail panel that already carries
  Pan/Width + Sends + EQ + CMP).
- **No placeholders:** every code block is complete and copy-pasteable.
- **Type consistency:** `setBusMonitorMode`, `busMonitorMode`,
  `busMonitorOutputChannel`, `MonitorRouteState`, `busMonitorRoutes_`,
  `monStripInputBusIds_`, `onBusMonitorModeChanged`,
  `setBusMonitorModeAt` / `toggleBusMonitorModeAt` / `setBusMonitorModes`,
  `busMonitorButtons_`, `busMonitorModes_` — all names match across the
  declaration and usage sites.
