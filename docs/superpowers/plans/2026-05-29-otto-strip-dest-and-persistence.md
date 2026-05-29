# S6 — OTTO output-strip DEST picker + save/load persistence (implementation plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land per-OTTO-strip routing on IDA's Output Mixer — a DEST picker that mirrors the phrase-channel pattern, with save/load persistence that re-mints the OTTO strip on import (binding the right OTTO output pointer, not silently respawning as a phrase channel).

**Architecture:** Engine carries one new int per output channel (`ottoSource`, default `-1` for phrase channels, `0..31` for OTTO outputs, `-2` reserved for the future S7 stereo-mix sentinel). The engine never reads it at runtime — purely transport metadata for export/import. MainComponent owns the OTTO-specific buffer-pointer rebind after `OutputMixer::importGraphState`. UI surface mirrors the existing `phraseDestButtons_` scaffolding.

**Tech Stack:** C++20, JUCE 8.x, Catch2 (`IdaTests` target), CMake + Ninja. Engine modules `core` / `engine` / `persistence` stay JUCE-free in their public headers; `app/` owns the UI + integration glue.

**Spec:** `docs/superpowers/specs/2026-05-29-otto-strip-dest-and-persistence-design.md` (commit 96a16f2). All design forks closed inline; no open items.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `core/include/ida/MixerGraphState.h` | Plain-data snapshot of a mixer's routing graph | **Modify** — add `int ottoSource { -1 };` to `OutputChannelState`; extend `operator==`. |
| `engine/include/ida/OutputMixer.h` | OutputMixer public surface + private state | **Modify** — add `setOttoSource` / `getOttoSource` public; add `channelOttoSource_` parallel vector private. |
| `engine/src/OutputMixer.cpp` | OutputMixer implementation | **Modify** — ctor `reserve`, `registerChannelWithId` push_back, `removeChannel` swap-erase, `exportGraphState` read, `importGraphState` write, setter/getter bodies. |
| `persistence/src/SessionFormat.cpp` | JSON (de)serialize for mixer graph state | **Modify** — `outputChannelToVar` emit-if-non-default; `outputChannelFromVar` read with `-1` default. |
| `app/OttoStripRebind.h` | Free helper declaration | **Create** — `ida::app::rebindOttoChannelsAfterImport(OutputMixer&, OttoHost&, std::unordered_map<int, OutputChannelId>&)` declaration. |
| `app/OttoStripRebind.cpp` | Free helper implementation | **Create** — iterates `OutputMixer`'s channels (via a small new const accessor), finds `ottoSource >= 0` entries, binds the OTTO output L/R pointers, updates the map. |
| `app/MainComponent.cpp` | App-level glue + nested `OutputMixerPane` | **Modify** — (a) `addOttoOutputStrip` calls `setOttoSource`; (b) `OutputMixerPane` gains `ottoDestButtons_` / `ottoStripDests_` / `ottoChoices_` / `showOttoDestinationMenu` / `onOttoDestinationChosen`; (c) `appendOttoStripImpl` constructs the DEST button + lays it out in `resized()`; (d) `refreshOutputDestinations()` populates `ottoChoices_` + syncs labels; (e) MainComponent wires `onOttoDestinationChosen` lambda; (f) `chooseFileAndLoad` calls `rebindOttoChannelsAfterImport` after `importGraphState`. |
| `tests/MixerGraphPersistenceTests.cpp` | OutputChannelState struct-level round-trip | **Modify** — add `ottoSource` cases. |
| `tests/OutputMixerTests.cpp` | OutputMixer behavioral tests | **Modify** — add `[otto-source]` setter/getter + export/import cases. |
| `tests/SessionFormatTests.cpp` | JSON serialize/deserialize tests | **Modify** — extend the `OutputMixerGraphState round-trips through JSON` case to assert `ottoSource` survives. |
| `tests/OttoStripDestPersistenceTests.cpp` | End-to-end OTTO strip persistence + rebind | **Create** — three TEST_CASEs: HardwareOutput route round-trips, Bus route round-trips, idempotent re-import. |
| `tests/CMakeLists.txt` | Test target registration | **Modify** — register `OttoStripDestPersistenceTests.cpp` + pull in `${CMAKE_SOURCE_DIR}/app/OttoStripRebind.cpp` (same pattern `OttoPane.cpp` uses). |

---

## Task ordering rationale

Tasks 1–4 are engine + persistence + helper, fully headless TDD, no UI surface. Each is independently committable and pushable. Tasks 5–6 are MainComponent UI surface and require the engine work to compile. Task 7 is the end-to-end test on top of the new helper. Task 8 is the operator T-checklist (eyes-on verification). Implementation order = task order; tasks 1 and 2 cannot be parallelized because Task 2's serialize-the-field test depends on Task 1's field existing.

---

## Task 1: Engine — `ottoSource` field + parallel vector + struct round-trip

**Files:**
- Modify: `core/include/ida/MixerGraphState.h` (around lines 179–201)
- Modify: `engine/include/ida/OutputMixer.h` (declarations around line 119; private state around line 411)
- Modify: `engine/src/OutputMixer.cpp` (ctor lines 80–85; `registerChannelWithId` lines 104–125; `removeChannel` lines 200–215; `exportGraphState` lines 969–994; `importGraphState` lines 1065–1110)
- Test: `tests/MixerGraphPersistenceTests.cpp`
- Test: `tests/OutputMixerTests.cpp`

- [ ] **Step 1.1: Write the failing OutputChannelState round-trip case**

In `tests/MixerGraphPersistenceTests.cpp`, add a TEST_CASE after the existing OutputChannelState cases:

```cpp
TEST_CASE ("OutputChannelState round-trips ottoSource (-1, 0..31, -2 reserved)",
           "[output-channel-state][round-trip][otto-source]")
{
    using namespace ida;

    SECTION ("default ottoSource is -1")
    {
        OutputChannelState a;
        REQUIRE (a.ottoSource == -1);
    }

    SECTION ("operator== includes ottoSource")
    {
        OutputChannelState a; a.channelId = 7;
        OutputChannelState b; b.channelId = 7;
        REQUIRE (a == b);
        b.ottoSource = 3;
        REQUIRE (a != b);
        a.ottoSource = 3;
        REQUIRE (a == b);
    }

    SECTION ("a few representative values survive copy-construct + assign")
    {
        for (int src : { -1, 0, 17, 31, -2 })
        {
            OutputChannelState a; a.channelId = 1; a.ottoSource = src;
            OutputChannelState copy = a;
            REQUIRE (copy.ottoSource == src);
            OutputChannelState assigned;
            assigned = a;
            REQUIRE (assigned.ottoSource == src);
        }
    }
}
```

- [ ] **Step 1.2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[output-channel-state][otto-source]"`
Expected: compile-error on `a.ottoSource` ("no member named 'ottoSource'") — confirms the field doesn't exist yet.

- [ ] **Step 1.3: Add `ottoSource` to OutputChannelState**

In `core/include/ida/MixerGraphState.h`, around line 188, extend `OutputChannelState`:

```cpp
struct OutputChannelState
{
    std::int64_t             channelId       { 0 };
    SignalType               signalType      { SignalType::Audio };
    std::vector<MixerSend>   sends;
    EffectChain              inserts;
    int                      hardwareOutPair { 0 };
    bool                     preFaderSends   { false };
    OutputChannelMainOutKind mainOutKind     { OutputChannelMainOutKind::Bus };
    std::int64_t             mainOutBus      { 0 }; // valid when mainOutKind == Bus
    /// S6 (2026-05-29) — channel-provenance marker for persistence + import-time
    /// rebind. -1 = phrase channel (the default); 0..31 = the OTTO output index
    /// this channel was minted from via MainComponent::addOttoOutputStrip;
    /// -2 reserved for the future S7 OTTO Stereo Mix sentinel. The engine never
    /// reads this at runtime — pure transport metadata round-tripped through
    /// exportGraphState/importGraphState so MainComponent's post-import rebind
    /// pass can identify OTTO channels and rebind their buffer pointers via
    /// OttoHost::getOttoOutputLeft/Right.
    int                      ottoSource      { -1 };

    bool operator== (const OutputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && sends == o.sends && inserts == o.inserts
            && hardwareOutPair == o.hardwareOutPair
            && preFaderSends == o.preFaderSends
            && mainOutKind == o.mainOutKind
            && (mainOutKind == OutputChannelMainOutKind::HardwareOutput
                    || mainOutBus == o.mainOutBus)
            && ottoSource == o.ottoSource;
    }
    bool operator!= (const OutputChannelState& o) const noexcept { return ! (*this == o); }
};
```

- [ ] **Step 1.4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[output-channel-state][otto-source]"`
Expected: PASS (3 sections, ~6 assertions).

- [ ] **Step 1.5: Write the failing OutputMixer setter/getter test**

In `tests/OutputMixerTests.cpp`, add at the end:

```cpp
TEST_CASE ("OutputMixer::setOttoSource / getOttoSource round-trip through "
           "exportGraphState + importGraphState",
           "[output-mixer][otto-source]")
{
    using namespace ida;

    OutputMixer mix;

    const auto chPhrase = mix.addChannel (SignalType::Audio);
    const auto chOtto0  = mix.addChannel (SignalType::Audio);
    const auto chOtto31 = mix.addChannel (SignalType::Audio);

    SECTION ("freshly added channels default ottoSource to -1")
    {
        REQUIRE (mix.getOttoSource (chPhrase)  == -1);
        REQUIRE (mix.getOttoSource (chOtto0)   == -1);
        REQUIRE (mix.getOttoSource (chOtto31)  == -1);
        REQUIRE (mix.getOttoSource (OutputChannelId { 9999 }) == -1); // unknown id
    }

    SECTION ("setOttoSource writes; getter reads back")
    {
        mix.setOttoSource (chOtto0, 0);
        mix.setOttoSource (chOtto31, 31);

        REQUIRE (mix.getOttoSource (chPhrase)  == -1);
        REQUIRE (mix.getOttoSource (chOtto0)   == 0);
        REQUIRE (mix.getOttoSource (chOtto31)  == 31);
    }

    SECTION ("ottoSource round-trips through export + import")
    {
        mix.setOttoSource (chOtto0, 0);
        mix.setOttoSource (chOtto31, 31);

        const auto state = mix.exportGraphState();

        OutputMixer fresh;
        fresh.importGraphState (state);

        REQUIRE (fresh.getOttoSource (chPhrase)  == -1);
        REQUIRE (fresh.getOttoSource (chOtto0)   == 0);
        REQUIRE (fresh.getOttoSource (chOtto31)  == 31);
    }

    SECTION ("removeChannel cleans up the per-channel ottoSource (swap-erase parity)")
    {
        mix.setOttoSource (chOtto0,  0);
        mix.setOttoSource (chOtto31, 31);
        mix.removeChannel (chOtto0);

        // The remaining OTTO-flagged channel must still report its source.
        REQUIRE (mix.getOttoSource (chOtto31) == 31);
        // A fresh channel reuses the freed id slot — must default to -1, not
        // inherit the removed channel's ottoSource.
        const auto chReplay = mix.addChannel (SignalType::Audio);
        REQUIRE (mix.getOttoSource (chReplay) == -1);
    }
}
```

- [ ] **Step 1.6: Run test to verify it fails**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[output-mixer][otto-source]"`
Expected: compile-error on `mix.setOttoSource` / `getOttoSource` — confirms API doesn't exist.

- [ ] **Step 1.7: Declare API on OutputMixer**

In `engine/include/ida/OutputMixer.h`, after line 119 (the `channelMainOutHardwareOutPair` accessor):

```cpp
    /// S6 (2026-05-29) — sets the OTTO-source provenance marker for `channel`.
    /// -1 = phrase channel; 0..31 = OTTO output index; -2 reserved for the
    /// S7 OTTO Stereo Mix sentinel. The engine never reads this at runtime —
    /// it is purely transport metadata for `exportGraphState`/`importGraphState`
    /// so MainComponent's post-import rebind pass can identify OTTO channels
    /// and rebind their buffer pointers. No-op for unknown ids. Message-thread.
    void setOttoSource (OutputChannelId channel, int ottoSource) noexcept;

    /// Reads channel `id`'s recorded OTTO-source marker. Returns -1 for
    /// unknown ids (the phrase-channel default — same defensive default as
    /// `channelMainOutHardwareOutPair`). Message-thread accessor.
    int getOttoSource (OutputChannelId id) const noexcept;
```

Then in the private section, after line 424 (the `channelMainOutBus_` declaration), add:

```cpp
    /// S6 (2026-05-29) — per-channel OTTO-source provenance marker. -1 = phrase
    /// channel; 0..31 = OTTO output index. Parallel to `channels_` /
    /// `channelHardwareOutPair_` / `channelMainOutKind_` — push_back/swap-erase
    /// in lockstep. Sized to `kMaxOutputChannels` in the ctor so `push_back` in
    /// `addChannel` never reallocates.
    std::vector<int>          channelOttoSource_;
```

- [ ] **Step 1.8: Wire the parallel vector in OutputMixer.cpp**

In `engine/src/OutputMixer.cpp`:

(a) In the ctor `reserve` block (around line 85), after `channelMainOutBus_.reserve(...)`:

```cpp
    channelOttoSource_.reserve        (static_cast<std::size_t> (kMaxOutputChannels));
```

(b) In `registerChannelWithId` (around line 121), after `channelMainOutBus_.push_back(...)`:

```cpp
    channelOttoSource_.push_back     (-1);   // S6: default phrase channel
```

(c) In `removeChannel`'s swap-erase block (around lines 204–215), in the swap-from-last group add:

```cpp
        channelOttoSource_[idx]      = channelOttoSource_[last];
```

…and in the pop_back group:

```cpp
    channelOttoSource_.pop_back();
```

(d) Add the setter/getter near the existing `channelMainOutHardwareOutPair` accessor body (just after, file order preserved):

```cpp
void OutputMixer::setOttoSource (OutputChannelId channel, int ottoSource) noexcept
{
    for (std::size_t i = 0; i < channels_.size(); ++i)
    {
        if (channels_[i].id == channel)
        {
            channelOttoSource_[i] = ottoSource;
            return;
        }
    }
    // Unknown id — silent no-op, mirror of `setChannelMainOutToHardwareOutput`.
}

int OutputMixer::getOttoSource (OutputChannelId id) const noexcept
{
    for (std::size_t i = 0; i < channels_.size(); ++i)
        if (channels_[i].id == id)
            return channelOttoSource_[i];
    return -1;
}
```

(e) In `exportGraphState` (around line 980, after `entry.mainOutBus = ...`):

```cpp
        entry.ottoSource      = channelOttoSource_[ci];   // S6
```

(f) In `importGraphState` (around line 1109, after `setChannelSendIsPreFader(...)`):

```cpp
        setOttoSource (created, c.ottoSource);            // S6
```

- [ ] **Step 1.9: Run tests to verify everything passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[output-mixer][otto-source]" && build/tests/IdaTests "[output-channel-state][otto-source]"`
Expected: PASS — all sections green.

- [ ] **Step 1.10: Full ctest pass to confirm no regression**

Run: `ctest --test-dir build`
Expected: ≥ 813 passed, 1 not-run (baseline) + the new cases. Zero new failures.

- [ ] **Step 1.11: Commit**

```bash
git add core/include/ida/MixerGraphState.h \
        engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp \
        tests/MixerGraphPersistenceTests.cpp tests/OutputMixerTests.cpp
git commit -m "feat: S6 T1 — OutputChannelState.ottoSource + OutputMixer set/getOttoSource"
git push origin master
```

---

## Task 2: Persistence — JSON (de)serialize `ottoSource`

**Files:**
- Modify: `persistence/src/SessionFormat.cpp` (`outputChannelToVar` around line 1044; `outputChannelFromVar` around line 1067)
- Test: `tests/SessionFormatTests.cpp` (extend the existing `OutputMixerGraphState round-trips through JSON` case around line 410)

- [ ] **Step 2.1: Write the failing JSON round-trip test**

In `tests/SessionFormatTests.cpp`, find the existing `"OutputMixerGraphState round-trips through JSON"` case (around line 410) and add a new TEST_CASE immediately after it:

```cpp
TEST_CASE ("OutputMixerGraphState carries ottoSource through JSON",
           "[sessionformat][otto-source]")
{
    using namespace ida;

    OutputMixerGraphState state;
    // Master bus has to be present for the round-trip helpers to accept the doc.
    MixerBusState master; master.busId = 0; master.name = "Master"; master.kind = MixerBusKind::Bus;
    state.buses.push_back (master);

    OutputChannelState chPhrase; chPhrase.channelId = 1;
    OutputChannelState chOtto0;  chOtto0.channelId  = 2; chOtto0.ottoSource  = 0;
    OutputChannelState chOtto31; chOtto31.channelId = 3; chOtto31.ottoSource = 31;
    chPhrase.sends.push_back ({ 0, 1.0f });
    chOtto0.sends.push_back  ({ 0, 1.0f });
    chOtto31.sends.push_back ({ 0, 1.0f });
    state.channels.push_back (chPhrase);
    state.channels.push_back (chOtto0);
    state.channels.push_back (chOtto31);
    state.nextChannelId = 4;

    const auto json = serializeOutputMixerGraphState (state);
    const auto restored = deserializeOutputMixerGraphState (json);

    REQUIRE (restored.channels.size() == 3);
    REQUIRE (restored.channels[0].ottoSource == -1);
    REQUIRE (restored.channels[1].ottoSource == 0);
    REQUIRE (restored.channels[2].ottoSource == 31);
    REQUIRE (restored == state);   // S6: equality includes ottoSource
}
```

- [ ] **Step 2.2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[sessionformat][otto-source]"`
Expected: FAIL — `restored.channels[1].ottoSource` is `-1` (the default) because SessionFormat doesn't serialize the field yet.

- [ ] **Step 2.3: Serialize `ottoSource` (emit-if-non-default)**

In `persistence/src/SessionFormat.cpp`, inside `outputChannelToVar` (around line 1044), after the `mainOutBus` block (line 1063):

```cpp
        // S6: emit ottoSource only when non-default (-1 = phrase channel,
        // the addChannel baseline). Mirror of the slice-E3 emit-if-non-default
        // pattern used for mainOutKind/mainOutBus.
        if (c.ottoSource != -1)
            obj->setProperty ("ottoSource", c.ottoSource);
```

Then inside `outputChannelFromVar` (around line 1067), after the existing `mainOutBus` read (line 1085):

```cpp
        if (const auto s = optionalProperty (v, "ottoSource"); ! s.isVoid())
            c.ottoSource = requireInt (s, "channel.ottoSource");
```

- [ ] **Step 2.4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[sessionformat][otto-source]"`
Expected: PASS — restored ottoSource matches written values; full-state equality holds.

- [ ] **Step 2.5: Full ctest pass**

Run: `ctest --test-dir build`
Expected: baseline + 1 new TEST_CASE. Zero new failures.

- [ ] **Step 2.6: Commit**

```bash
git add persistence/src/SessionFormat.cpp tests/SessionFormatTests.cpp
git commit -m "feat: S6 T2 — JSON (de)serialize OutputChannelState.ottoSource"
git push origin master
```

---

## Task 3: MainComponent — `addOttoOutputStrip` stamps `ottoSource`

**Files:**
- Modify: `app/MainComponent.cpp` (`addOttoOutputStrip` around line 6832)

This task is the smallest — one new line in an existing function. Splitting it from T1 keeps the engine commit pure-engine and lets the app-side change land separately on master.

- [ ] **Step 3.1: Stamp ottoSource in `addOttoOutputStrip`**

In `app/MainComponent.cpp`, inside `MainComponent::addOttoOutputStrip` (around line 6870), after `outputMixer_->setChannelAudioSource(chId, leftSrc, rightSrc);` and before the `addAudioCallback`:

```cpp
    outputMixer_->setOttoSource (chId, ottoOutputIndex);   // S6: provenance for save/load rebind
```

(The setter is message-thread only; it sits inside the same `removeAudioCallback ... addAudioCallback` bracket as the other engine mutations.)

- [ ] **Step 3.2: Build + run the OTTO-host pump tests as a smoke check**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[otto-host-pump]"`
Expected: PASS (4 test cases) — the existing pump tests don't exercise `setOttoSource` directly, but a regression in the existing `addOttoOutputStrip` path would surface here.

- [ ] **Step 3.3: Full ctest pass**

Run: `ctest --test-dir build`
Expected: baseline + cases from T1/T2 = baseline + 2 (no new cases from T3). Zero new failures.

- [ ] **Step 3.4: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: S6 T3 — addOttoOutputStrip stamps OutputMixer::setOttoSource"
git push origin master
```

---

## Task 4: `app/OttoStripRebind.{h,cpp}` — extracted helper + standalone test

**Files:**
- Create: `app/OttoStripRebind.h`
- Create: `app/OttoStripRebind.cpp`
- Create: `tests/OttoStripDestPersistenceTests.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test source + pull in `app/OttoStripRebind.cpp`)

The helper is a free function so the integration test can call it without instantiating MainComponent. Lives in `ida::app::` namespace, alongside `StripContextOverlay`.

- [ ] **Step 4.1: Write the failing rebind test**

Create `tests/OttoStripDestPersistenceTests.cpp`:

```cpp
// S6 T4 — end-to-end OTTO strip persistence + rebind. Modeled on
// OttoHostRenderTests.cpp (prepared OttoHost + freshly-constructed OutputMixer,
// no GUI). Exercises ida::app::rebindOttoChannelsAfterImport directly so the
// MainComponent post-import logic is verifiable without instantiating MainComponent.

#include "OttoStripRebind.h"
#include "ida/OttoHost.h"
#include "ida/OutputMixer.h"

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 64;
}

TEST_CASE ("rebindOttoChannelsAfterImport binds OTTO output pointers + populates the map",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    OutputMixer mix;
    const auto chPhrase = mix.addChannel (SignalType::Audio);
    const auto chOtto0  = mix.addChannel (SignalType::Audio);
    const auto chOtto7  = mix.addChannel (SignalType::Audio);

    mix.setOttoSource (chPhrase, -1);
    mix.setOttoSource (chOtto0,  0);
    mix.setOttoSource (chOtto7,  7);

    SECTION ("rebind binds the OTTO output L/R pointers + populates the map")
    {
        std::unordered_map<int, OutputChannelId> ottoMap;
        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);

        REQUIRE (ottoMap.size() == 2);
        REQUIRE (ottoMap.at (0) == chOtto0);
        REQUIRE (ottoMap.at (7) == chOtto7);

        // The phrase channel is left alone — no entry in the map.
        REQUIRE (ottoMap.count (-1) == 0);
    }

    SECTION ("rebind is idempotent — second call is a no-op")
    {
        std::unordered_map<int, OutputChannelId> ottoMap;
        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);
        const auto firstSize = ottoMap.size();

        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);
        REQUIRE (ottoMap.size() == firstSize);
        REQUIRE (ottoMap.at (0) == chOtto0);
        REQUIRE (ottoMap.at (7) == chOtto7);
    }
}

TEST_CASE ("OTTO strip HardwareOutput route survives export + import + rebind",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    OutputMixer mix;
    const auto chOtto5 = mix.addChannel (SignalType::Audio);
    mix.setOttoSource (chOtto5, 5);
    mix.setChannelMainOutToHardwareOutput (chOtto5, /*pairIndex*/ 1);

    const auto state = mix.exportGraphState();

    OutputMixer fresh;
    fresh.importGraphState (state);

    std::unordered_map<int, OutputChannelId> ottoMap;
    ida::app::rebindOttoChannelsAfterImport (fresh, host, ottoMap);

    REQUIRE (ottoMap.count (5) == 1);
    const auto rebound = ottoMap.at (5);
    REQUIRE (fresh.getOttoSource (rebound) == 5);
    REQUIRE (fresh.channelMainOut (rebound) == OutputMixer::MainOutDest::HardwareOutput);
    REQUIRE (fresh.channelMainOutHardwareOutPair (rebound) == 1);
}

TEST_CASE ("OTTO strip Bus route survives export + import + rebind",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    OutputMixer mix;
    const auto auxBus = mix.addBus ({ /*channelCount*/ 2, "Aux", BusKind::Bus });
    const auto chOtto12 = mix.addChannel (SignalType::Audio);
    mix.setOttoSource (chOtto12, 12);
    mix.routeChannelToBus (chOtto12, auxBus, 1.0f);  // radio: zeros master, sets mainOut=Bus(aux)

    const auto state = mix.exportGraphState();

    OutputMixer fresh;
    fresh.importGraphState (state);

    std::unordered_map<int, OutputChannelId> ottoMap;
    ida::app::rebindOttoChannelsAfterImport (fresh, host, ottoMap);

    REQUIRE (ottoMap.count (12) == 1);
    const auto rebound = ottoMap.at (12);
    REQUIRE (fresh.getOttoSource (rebound) == 12);
    REQUIRE (fresh.channelMainOut (rebound) == OutputMixer::MainOutDest::Bus);
    REQUIRE (fresh.channelMainOutBus (rebound) == auxBus);
}
```

- [ ] **Step 4.2: Try to run — should fail at link/compile time**

Run: `cmake --build build --target IdaTests`
Expected: compile-error — `OttoStripRebind.h` doesn't exist + `ida::app::rebindOttoChannelsAfterImport` undefined.

- [ ] **Step 4.3: Create the helper header**

Create `app/OttoStripRebind.h`:

```cpp
#pragma once

#include "ida/Channel.h"      // OutputChannelId

#include <unordered_map>

namespace ida { class OttoHost; class OutputMixer; }

namespace ida::app
{

/// S6 (2026-05-29) — post-import OTTO strip rebind. Iterates every channel in
/// `mix`; for channels whose `getOttoSource() >= 0`, binds the OTTO output L/R
/// pointers via `OttoHost::getOttoOutputLeft/Right(ottoSource)` AND inserts
/// `(ottoSource → OutputChannelId)` into `ottoMap`.
///
/// Idempotent: calling twice produces the same map and rebinds the same
/// pointers (the underlying setters are write-only). Safe to call on a mixer
/// with no OTTO channels — the map is left unchanged.
///
/// `host` MUST be prepared (`host.prepare(sr, blockSize)` already called)
/// BEFORE this is invoked; OTTO's per-output buffer pointers are stable for
/// the OttoHost's lifetime but null pre-prepare. Channels whose source pointer
/// returns null are skipped (logged via Logger::writeToLog elsewhere — not
/// here; this helper is a pure rebind step).
///
/// Existing entries in `ottoMap` are overwritten in-place if their key
/// (ottoSource) matches a freshly-rebound channel — this is the
/// "operator imports a session into a session that already has OTTO strips"
/// path. Existing entries whose key has no corresponding `ottoSource>=0`
/// channel in the imported state are NOT removed by this helper; the caller
/// is responsible for clearing the map BEFORE calling rebind if the import
/// is a full replace (mirror of how MainComponent clears phraseStrips_ before
/// rebuilding from the imported state).
void rebindOttoChannelsAfterImport (OutputMixer& mix,
                                    OttoHost& host,
                                    std::unordered_map<int, OutputChannelId>& ottoMap);

} // namespace ida::app
```

- [ ] **Step 4.4: Create the helper body**

Create `app/OttoStripRebind.cpp`:

```cpp
#include "OttoStripRebind.h"

#include "ida/OttoHost.h"
#include "ida/OutputMixer.h"

namespace ida::app
{

void rebindOttoChannelsAfterImport (OutputMixer& mix,
                                    OttoHost& host,
                                    std::unordered_map<int, OutputChannelId>& ottoMap)
{
    const int total = mix.channelCount();
    for (int i = 0; i < total; ++i)
    {
        const auto chId = mix.channelIdAt (i);
        if (chId.value() == 0) continue;

        const int ottoSource = mix.getOttoSource (chId);
        if (ottoSource < 0) continue;        // phrase channel — leave alone

        const float* const leftSrc  = host.getOttoOutputLeft  (ottoSource);
        const float* const rightSrc = host.getOttoOutputRight (ottoSource);
        if (leftSrc == nullptr || rightSrc == nullptr) continue;  // host not prepared

        mix.setChannelAudioSource (chId, leftSrc, rightSrc);
        ottoMap[ottoSource] = chId;
    }
}

} // namespace ida::app
```

- [ ] **Step 4.5: Verify `OutputMixer::channelIdAt` exists, add if missing**

Run: `grep -n "channelIdAt" engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp`

If present: skip to Step 4.6.

If absent: add this indexed accessor (mirror of `busIdAt`) — in `engine/include/ida/OutputMixer.h` after `channelCount()` (around line 242):

```cpp
    /// Indexed channel accessor (0..channelCount()-1). Returns the invalid
    /// sentinel `OutputChannelId{0}` for out-of-range indices — same
    /// defensive default as `busIdAt`. Message-thread.
    OutputChannelId channelIdAt (int index) const noexcept;
```

…and in `engine/src/OutputMixer.cpp` near `busIdAt`'s body:

```cpp
OutputChannelId OutputMixer::channelIdAt (int index) const noexcept
{
    if (index < 0 || static_cast<std::size_t> (index) >= channels_.size())
        return OutputChannelId { 0 };
    return channels_[static_cast<std::size_t> (index)].id;
}
```

- [ ] **Step 4.6: Register new test + helper source in tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, find the block where `OttoHostPumpTests.cpp` is registered (around line 211). Add immediately after:

```cmake
    # S6 T4 — OTTO strip persistence + rebind. Exercises the
    # ida::app::rebindOttoChannelsAfterImport helper end-to-end: addChannel +
    # setOttoSource + exportGraphState + importGraphState into a fresh mixer +
    # rebind binds the OTTO output buffer pointers (HardwareOutput and Bus
    # route variants + idempotent re-rebind). Pulls in app/OttoStripRebind.cpp
    # directly (same pattern OttoPaneTests uses for app/OttoPane.cpp).
    OttoStripDestPersistenceTests.cpp
    ${CMAKE_SOURCE_DIR}/app/OttoStripRebind.cpp
```

- [ ] **Step 4.7: Run the test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[otto-strip][persistence][end-to-end]"`
Expected: PASS (3 test cases, ~10 assertions).

- [ ] **Step 4.8: Full ctest pass**

Run: `ctest --test-dir build`
Expected: baseline + 3 new cases. Zero new failures.

- [ ] **Step 4.9: Commit**

```bash
git add app/OttoStripRebind.h app/OttoStripRebind.cpp \
        tests/OttoStripDestPersistenceTests.cpp tests/CMakeLists.txt
# Conditional: include the engine/include/ida/OutputMixer.h + .cpp delta IF Step 4.5 added channelIdAt.
git add engine/include/ida/OutputMixer.h engine/src/OutputMixer.cpp 2>/dev/null || true
git commit -m "feat: S6 T4 — rebindOttoChannelsAfterImport helper + end-to-end tests"
git push origin master
```

---

## Task 5: OutputMixerPane — DEST button surface for OTTO strips

**Files:**
- Modify: `app/MainComponent.cpp` — the nested `OutputMixerPane` class. Key sites:
  - private state around line 3486-3489 (phrase row scaffolding to mirror)
  - private state around line 3507-3508 (existing OTTO row vectors to extend)
  - `appendOttoStripImpl` around line 3449 (construct DEST button per strip)
  - `setOttoStrips` around line 2617 (clear ottoDestButtons_ before rebuilding)
  - `resized()` around line 2995 (lay out the DEST button under each strip)
  - `showDestinationMenu` around line 1596 (pattern to mirror for OTTO)

Pure GUI-pane scaffolding, no engine touch. After this task the OTTO strip has a DEST button that opens a menu but `onOttoDestinationChosen` isn't wired yet (Task 6).

- [ ] **Step 5.1: Add OTTO DEST scaffolding to OutputMixerPane**

In `app/MainComponent.cpp`, in the `OutputMixerPane` class's private member block (around line 3507, immediately after the existing `ottoStrips_` / `ottoOverlays_` declarations):

```cpp
    // S6 (2026-05-29) — OTTO strip DEST picker scaffolding. Parallel to the
    // phrase row's phraseDestButtons_ / phraseStripDests_ / phraseChoices_.
    // One entry per OTTO strip, kept in lockstep with `ottoStrips_`. The
    // strip's id (= ottoOutputIndex) is the key passed back through
    // `onOttoDestinationChosen` — no row→index translation.
    std::vector<std::unique_ptr<juce::TextButton>>  ottoDestButtons_;
    std::vector<StripDest>                          ottoStripDests_;
    std::vector<std::vector<DestChoice>>            ottoChoices_;
```

- [ ] **Step 5.2: Declare the public callback + the show-menu helper**

In `OutputMixerPane`'s public section (find `std::function<void(int, DestChoice)> onBusDestinationChosen;` around line 1010 in the listener-callbacks zone, or wherever similar callbacks live — `git grep -n "onBusDestinationChosen" app/MainComponent.cpp` finds the exact line in this revision). Add immediately after the bus-destination callback:

```cpp
    /// S6 — OTTO strip DEST picker selection. `ottoOutputIndex` is the strip's
    /// id (0..31). Mirror of `onBusDestinationChosen`.
    std::function<void(int, DestChoice)>  onOttoDestinationChosen;
```

And in the same class, immediately after `showBusDestinationMenu` (around line 1616):

```cpp
    /// Builds + shows OTTO strip's destination menu from its own choice list
    /// (`ottoChoices_[idx]`). Selecting fires `onOttoDestinationChosen` with the
    /// strip's `ottoOutputIndex` (read from the strip itself) and the chosen
    /// `DestChoice`. Mirror of `showBusDestinationMenu`.
    void showOttoDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= static_cast<int> (ottoStrips_.size())) return;
        if (idx >= static_cast<int> (ottoChoices_.size())) return;
        const auto& choices = ottoChoices_[static_cast<std::size_t> (idx)];
        if (choices.empty()) return;
        const auto& cur = ottoStripDests_[static_cast<std::size_t> (idx)];
        const int ottoOutputIndex = ottoStrips_[static_cast<std::size_t> (idx)]->getChannelIndex();
        juce::PopupMenu menu;
        for (const auto& choice : choices)
        {
            const bool ticked = choice.kind == cur.currentKind && choice.id == cur.currentId;
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, ottoOutputIndex, d]
                          {
                              if (onOttoDestinationChosen)
                                  onOttoDestinationChosen (ottoOutputIndex, d);
                          });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            ottoDestButtons_[static_cast<std::size_t> (idx)].get()));
    }
```

- [ ] **Step 5.3: Construct the DEST button in `appendOttoStripImpl`**

In `appendOttoStripImpl` (line 3449), at the end of the function (after `ottoOverlays_.push_back (std::move (overlay));`):

```cpp
        // S6 — DEST button paired with the strip. The button index in
        // `ottoDestButtons_` is parallel to the strip's index in `ottoStrips_`,
        // so resized() can lay them out in lockstep.
        auto destButton = std::make_unique<juce::TextButton>();
        destButton->setButtonText ("—");                  // placeholder until refresh
        destButton->setTooltip ("Route this OTTO output");
        const int idx = static_cast<int> (ottoStrips_.size()) - 1;
        destButton->onClick = [this, idx] { showOttoDestinationMenu (idx); };
        addAndMakeVisible (*destButton);
        ottoDestButtons_.push_back (std::move (destButton));
        ottoStripDests_.push_back ({});                   // currentKind=Bus, currentId=0 default
        ottoChoices_.emplace_back();                      // empty until refreshOutputDestinations
```

- [ ] **Step 5.4: Clear OTTO DEST scaffolding in `setOttoStrips`**

In `setOttoStrips` (line 2617), at the top after `ottoStrips_.clear();`:

```cpp
        ottoDestButtons_.clear();
        ottoStripDests_.clear();
        ottoChoices_.clear();
```

(And mirror the same clears in any other place that wipes `ottoStrips_` — search `git grep -n "ottoStrips_.clear" app/MainComponent.cpp` and add the three matching `*.clear()` lines immediately after every match.)

- [ ] **Step 5.5: Lay out the OTTO DEST button in `resized()`**

Open `app/MainComponent.cpp` and search `grep -n "ottoStrips_\[static_cast<std::size_t> (i)\]->setBounds" app/MainComponent.cpp`. That single line is where the OTTO strip's bounds are set inside the `ottoStrips_` loop in `resized()`. Read the 30 lines AROUND the match to find:

(a) the local `juce::Rectangle` the strip's `setBounds` came from (call it `stripBounds`),
(b) whether the surrounding code already has a "picker row" rectangle for the OTTO band (it does for phrase rows around line 1276 — look for `pickerRow` in the OTTO loop's nearest sibling-row context). If a `pickerRow` is computed for the OTTO band: append exactly the line `ottoDestButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromLeft (otto::ui::CompactFaderStrip::kStripWidth));` immediately after the strip's `setBounds` call.

If NO `pickerRow` exists for the OTTO band yet (4b never added one): synthesize one by taking the same horizontal slice the OTTO strip uses but one strip-height-tall band below it. The exact code, to place immediately after the strip's `setBounds` call, is:

```cpp
            // S6 — DEST button band: a strip-width × kPickerRowHeight slot
            // directly below each OTTO strip. Mirrors the phrase-row picker
            // band layout (search for `pickerRow.removeFromLeft (kStripW)`
            // in the phrase-row block of resized() — line ~1276 at HEAD —
            // for the canonical pattern).
            const int kPickerRowHeight = 24;
            auto destSlot = stripBounds.removeFromBottom (kPickerRowHeight);
            ottoDestButtons_[static_cast<std::size_t> (i)]->setBounds (destSlot);
```

If the surrounding code uses a different `kPickerRowHeight` constant (search `grep -n "kPickerRowHeight\|pickerRow.removeFromLeft" app/MainComponent.cpp`), use that one verbatim instead of `24`.

- [ ] **Step 5.6: Handle visibility toggles**

Run `grep -n "for (auto& b : destButtons_)" app/MainComponent.cpp` — this yields exactly two matches (the hide-all-rows block around line 1178 and the show-all-rows block around line 1237). Both blocks already have `setVisible(false)` / `setVisible(true)` calls for every other row's dest buttons.

At BOTH match sites, immediately after the existing `for (auto& b : busDestButtons_) b->setVisible (false);` (or `true`) line in the same block, add the OTTO mirror:

```cpp
        for (auto& b : ottoDestButtons_)      b->setVisible (false);   // S6
```

…in the hide block, and

```cpp
        for (auto& b : ottoDestButtons_)      b->setVisible (true);    // S6
```

…in the show block.

- [ ] **Step 5.7: Build to confirm no compile errors**

Run: `cmake --build build --target IDA`
Expected: succeeds.

- [ ] **Step 5.8: Full ctest pass**

Run: `ctest --test-dir build`
Expected: baseline + cases from T1/T2/T4 = baseline + 6. Zero new failures.

- [ ] **Step 5.9: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: S6 T5 — OutputMixerPane OTTO strip DEST button scaffolding"
git push origin master
```

---

## Task 6: MainComponent — wire `onOttoDestinationChosen` + extend `refreshOutputDestinations`

**Files:**
- Modify: `app/MainComponent.cpp` — find `onBusDestinationChosen = ...` lambda (around line 5037) and the `refreshOutputDestinations()` definition (search `git grep -n "void refreshOutputDestinations" app/MainComponent.cpp`)

After this task the OTTO strip's DEST button is fully live: operator clicks → picker opens → selection routes via engine + persistence.

- [ ] **Step 6.1: Add the `onOttoDestinationChosen` lambda**

In `app/MainComponent.cpp`, after the `onBusDestinationChosen` lambda assignment (around line 5037) and before the `onMasterDestinationChosen` lambda (around line 5057):

```cpp
        // S6 — OTTO strip DEST picker. Mirror of `onBusDestinationChosen`.
        // `ottoOutputIndex` is the strip id (= OTTO output index, 0..31);
        // resolve it via `ottoChannelByOutputIndex_` to find the matching
        // OutputChannelId. Topology mutation, so bracket the audio callback
        // identically to the bus picker.
        outputMixerPane_->onOttoDestinationChosen =
            [this] (int ottoOutputIndex, OutputMixerPane::DestChoice dest)
            {
                const auto it = ottoChannelByOutputIndex_.find (ottoOutputIndex);
                if (it == ottoChannelByOutputIndex_.end()) return;
                const auto chId = it->second;
                audioDeviceManager_.removeAudioCallback (audioCallback_.get());
                switch (dest.kind)
                {
                    case OutputMixerPane::DestKind::Bus:
                        outputMixer_->routeChannelToBus (chId, ida::BusId{ dest.id }, 1.0f);
                        break;
                    case OutputMixerPane::DestKind::HardwareOutput:
                        outputMixer_->setChannelMainOutToHardwareOutput (chId, dest.pairIndex);
                        break;
                }
                audioDeviceManager_.addAudioCallback (audioCallback_.get());
                refreshOutputDestinations();
            };
```

- [ ] **Step 6.2: Add OutputMixerPane accessors needed by the refresh loop**

`refreshOutputDestinations` is a single ~200-line function that inlines its phrase/bus/MON loops; `buildChannelDestChoices` / `currentChannelDest` / `destLabelFor` named helpers do NOT exist. The OTTO loop will inline the same shape the phrase loop uses (lines ~7081–7118 at HEAD `001fd98`). First, give that loop a way to enumerate the OTTO strips by adding accessors + a setter to `OutputMixerPane`.

Inside the `OutputMixerPane` class's public section (next to the existing `busStripCount()` / `setBusDestinations(...)` declarations), add:

```cpp
    /// S6 — OTTO strip count (parallel to ottoStrips_.size()). Mirror of
    /// busStripCount / phraseStripCount.
    int ottoStripCount() const noexcept
    {
        return static_cast<int> (ottoStrips_.size());
    }

    /// S6 — OTTO output index for the OTTO strip at `idx`. Out-of-range
    /// returns -1 (the phrase-channel sentinel). The strip's id is the
    /// OTTO output index, set at appendOttoStripImpl time.
    int ottoOutputIndexAt (int idx) const noexcept
    {
        if (idx < 0 || idx >= static_cast<int> (ottoStrips_.size())) return -1;
        return ottoStrips_[static_cast<std::size_t> (idx)]->getChannelIndex();
    }

    /// S6 — OTTO row DEST picker push from MainComponent. Mirror of
    /// `setBusDestinations`. Updates `ottoChoices_` + `ottoStripDests_`
    /// AND refreshes each `ottoDestButtons_[i]`'s button-text label to
    /// match the current destination (so the operator sees the picker's
    /// "what am I routed to" without opening the menu).
    void setOttoDestinations (std::vector<std::vector<DestChoice>> choices,
                              std::vector<StripDest>               dests)
    {
        ottoChoices_     = std::move (choices);
        ottoStripDests_  = std::move (dests);
        for (std::size_t i = 0; i < ottoStripDests_.size() && i < ottoDestButtons_.size(); ++i)
        {
            const auto& name = ottoStripDests_[i].currentName;
            ottoDestButtons_[i]->setButtonText (name.isEmpty() ? juce::String ("—") : name);
        }
    }
```

- [ ] **Step 6.3: Add the OTTO loop to `refreshOutputDestinations` (inline, mirroring the phrase loop)**

Find `refreshOutputDestinations`'s phrase-loop tail at `outputMixerPane_->setPhraseDestinations (perPhraseChoices, perPhrase);` (around line 7120 at HEAD `001fd98`). Immediately after that line, before the MON-strip loop, paste this OTTO block:

```cpp
    // S6 — OTTO row DEST picker rebuild. OTTO strips are first-class
    // OutputMixer channels, so the choice set + current-dest read use the
    // SAME engine surface as the phrase loop above (master + every aux bus
    // + every hardware-output pair; engine's channelMainOut / channelMainOutBus
    // / channelMainOutHardwareOutPair drive the current dest). Mirror of the
    // phrase loop at lines ~7081-7118.
    const int on = outputMixerPane_->ottoStripCount();
    std::vector<std::vector<OutputMixerPane::DestChoice>> perOttoChoices (static_cast<std::size_t> (on));
    std::vector<OutputMixerPane::StripDest>               perOtto        (static_cast<std::size_t> (on));

    for (int i = 0; i < on; ++i)
    {
        const int ottoOutputIndex = outputMixerPane_->ottoOutputIndexAt (i);
        const auto it = ottoChannelByOutputIndex_.find (ottoOutputIndex);
        if (it == ottoChannelByOutputIndex_.end()) continue;
        const auto chId = it->second;

        auto& choices = perOttoChoices[static_cast<std::size_t> (i)];
        choices.push_back ({ DestKind::Bus, /*id*/ 0, "Master", /*pairIndex*/ 0 });
        for (const auto& busId : outputBusStripIds_)
            if (auto* bus = outputMixer_->busForId (busId))
                choices.push_back ({ DestKind::Bus, busId.value(),
                                     juce::String (bus->config().name),
                                     /*pairIndex*/ 0 });
        for (const auto& p : hwPairs)
            choices.push_back ({ DestKind::HardwareOutput, /*id*/ 0, p.label, p.pairIndex });

        auto& dest = perOtto[static_cast<std::size_t> (i)];
        if (outputMixer_->channelMainOut (chId) == ida::OutputMixer::MainOutDest::HardwareOutput)
        {
            const int pair = outputMixer_->channelMainOutHardwareOutPair (chId);
            dest.currentKind      = DestKind::HardwareOutput;
            dest.currentId        = 0;
            dest.currentPairIndex = pair;
            dest.currentName      = labelForPair (pair);
        }
        else
        {
            const auto activeBus = outputMixer_->channelMainOutBus (chId);
            dest.currentKind      = DestKind::Bus;
            dest.currentId        = activeBus.value();
            dest.currentPairIndex = 0;
            if (activeBus.value() == 0)
                dest.currentName = "Master";
            else if (auto* bus = outputMixer_->busForId (activeBus))
                dest.currentName = juce::String (bus->config().name);
        }
    }

    outputMixerPane_->setOttoDestinations (std::move (perOttoChoices), std::move (perOtto));
```

`hwPairs` and `labelForPair` are the function-local stack values defined at the top of `refreshOutputDestinations` (lines ~6985 / ~6996); this block reuses them in-scope.

- [ ] **Step 6.4: Build + visual sanity check (no Catch2 test for GUI surface)**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA`
Expected: clean build succeeds.

- [ ] **Step 6.5: Full ctest pass**

Run: `ctest --test-dir build`
Expected: baseline + cases from T1/T2/T4 = baseline + 6 (no new cases from T5/T6 — GUI surface is operator-verified per project rules). Zero new failures.

- [ ] **Step 6.6: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: S6 T6 — wire onOttoDestinationChosen + OTTO row in refreshOutputDestinations"
git push origin master
```

---

## Task 7: MainComponent — call `rebindOttoChannelsAfterImport` after `importGraphState`

**Files:**
- Modify: `app/MainComponent.cpp` (`chooseFileAndLoad` import block, around line 8336 — `outputMixer_->importGraphState (*loadedOutputMixer);`)

The helper exists from T4 and is unit-tested headlessly. T7 wires it into the live save/load path so an operator-driven Save → quit → relaunch → Load round-trip rebinds the OTTO strips.

- [ ] **Step 7.1: Call the rebind helper after `outputMixer_->importGraphState`**

In `app/MainComponent.cpp`, immediately after the existing `outputMixer_->importGraphState (*loadedOutputMixer);` call (around line 8336):

```cpp
                            // S6 — re-mint OTTO strips that the imported state declared via
                            // ottoSource. Clears the old map first so a fresh import doesn't
                            // accumulate stale entries from the prior session.
                            ottoChannelByOutputIndex_.clear();
                            if (ottoHost_ != nullptr)
                                ida::app::rebindOttoChannelsAfterImport (
                                    *outputMixer_, *ottoHost_, ottoChannelByOutputIndex_);

                            // Mirror the OTTO row in the pane. Clear first so the rebuild
                            // is from scratch — same pattern as the phrase-channel rebuild.
                            if (outputMixerPane_ != nullptr)
                            {
                                std::vector<OutputMixerPane::OttoStripInfo> infos;
                                infos.reserve (ottoChannelByOutputIndex_.size());
                                for (const auto& kv : ottoChannelByOutputIndex_)
                                    infos.push_back ({ kv.first,
                                        OutputMixerPane::ottoFriendlyName (kv.first) });
                                outputMixerPane_->setOttoStrips (infos);
                            }
                            refreshOutputDestinations();
```

Add `#include "OttoStripRebind.h"` near the existing `app/` local includes at the top of `MainComponent.cpp` (search the existing `#include "StripContextOverlay.h"` line, add after it).

- [ ] **Step 7.2: Build + clean ctest pass**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests && ctest --test-dir build`
Expected: baseline + 6 new cases (from T1/T2/T4), zero new failures.

Build target the main app too: `cmake --build build --target IDA` — succeeds.

- [ ] **Step 7.3: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: S6 T7 — chooseFileAndLoad calls rebindOttoChannelsAfterImport"
git push origin master
```

---

## Task 8: Operator T-checklist (manual, eyes-on)

Mirror of S3c T17. Headless TDD does not cover GUI gestures or audio-output routing audibly — the operator verifies.

The agent does:

- [ ] **Step 8.1: Clean rebuild + launch IDA**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
open build/app/IDA_artefacts/Release/IDA.app
```

- [ ] **Step 8.2: Hand the operator this numbered T-checklist**

```text
T-S6-1. In IDA, open the Output Mixer tab.
T-S6-2. Use the "Add OTTO source ▶" picker to add OTTO output 0 (Player 1 L/R).
        Verify a strip appears in the OTTO band with a DEST button under it
        labeled "—" (default master) or "Master".
T-S6-3. Click the OTTO strip's DEST button. The popup should list:
          • Master (Bus 0)
          • every aux bus that exists
          • every hardware-output stereo pair the device exposes
        (Tape destinations should NOT appear — output-side routing only.)
T-S6-4. Choose a non-master hardware-output pair (e.g. "Outputs 3-4").
        Verify the DEST button label updates to that pair's name.
T-S6-5. Hit OTTO's Play (via IDA's transport bar). Confirm audible drumming
        comes out the chosen pair, NOT the master pair.
T-S6-6. File → Save As → save the session.
T-S6-7. Quit IDA (Cmd+Q).
T-S6-8. Re-launch IDA. File → Open → re-open the session from T-S6-6.
T-S6-9. Verify the OTTO strip is back in the band, with the same DEST button
        label, AND audible drumming on Play still comes out the same pair —
        no operator re-click needed.
T-S6-10. Optional: add OTTO output 5, route it to an aux bus, repeat the save
         + reload to confirm the Bus route also round-trips.

If T-S6-5 fails (Play silent at the chosen pair) — the rebind helper
landed but isn't being called, or the channel was minted as a phrase
channel on load. Check `MainComponent::chooseFileAndLoad` for the
`rebindOttoChannelsAfterImport` call (T7 Step 7.1).

If T-S6-9 fails (strip vanished or audio at the wrong pair after reload)
— the persistence write or read dropped ottoSource. Run `git diff
HEAD~7 persistence/src/SessionFormat.cpp` and verify the
`obj->setProperty("ottoSource", ...)` write + the matching read are
both in `outputChannel{To,From}Var`.
```

- [ ] **Step 8.3: If operator reports PASS, write the close-out commit**

```bash
git add docs/superpowers/plans/2026-05-29-otto-strip-dest-and-persistence.md continue.md todo.md
git commit -m "docs: S6 SHIPPED — operator T-checklist PASS; OTTO DEST + persistence live"
git push origin master
```

(`continue.md` + `todo.md` updates land in the same commit per session-end protocol — refresh both to reflect S6 SHIPPED, then commit.)

---

## Verification baseline

| Check | Before S6 | After S6 |
|---|---|---|
| `ctest --test-dir build` total | 813 passed / 1 not-run | 819 passed / 1 not-run (+6: T1×3 cases, T2×1, T4×3 — minus 1 overlap if T1's struct case folds with T2; recount at end) |
| `git rev-parse HEAD` | `96a16f2` (spec) | one commit per T1..T7 + one T8 close-out (~8 commits on top) |
| `IDA.app` clean rebuild | passes | passes |
| Operator T-S6-9 audible-Play-after-reload | (gap — strip exists but cannot route + cannot persist) | PASS |

---

## Notes for the subagent runner

- Each task ends with a push, so the loop's "land + verify + advance" cadence works cleanly.
- T5 has the largest surface (UI-pane scaffolding spread across constructor / `appendOttoStripImpl` / `setOttoStrips` / `resized` / visibility blocks). If subagents struggle, split T5 into T5a (declarations + clear/append helpers) and T5b (`resized` + visibility); preserve the same commit-per-subtask cadence.
- T6 references `buildChannelDestChoices` + `currentChannelDest` — if those don't already exist as named functions in `MainComponent.cpp` (the phrase row may have inlined the logic), the implementer must factor them out first as part of T6. Add that factoring as the first step of T6 in that case — don't open it as a separate task.
- T8 is the only operator-loop step; the agent stops at Step 8.1/8.2 and waits for verbal PASS/FAIL before committing 8.3.
- Per `feedback_subagents_push_to_master`: implementer subagents push their own task commits. Do not `--amend` a task commit afterward — land follow-on fixes as new commits on top.
- Per S3c convention: spec compliance review + code-quality review run after each task. Fix-loop on Important findings; ship Minor findings as follow-on polish in the next task or queue in `todo.md`.
