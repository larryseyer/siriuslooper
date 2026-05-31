# Bus State Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Round-trip bus pan/width/gain/muted (both mixers) and bus→FX-return
sends (OutputMixer side) through session save+load so operator-set bus state
survives a project reload.

**Architecture:** The engine surface for bus pan/width/gain/muted already
exists (`Bus::setPan/setWidth/setGain/setMuted` + DSP in
`applyGainPanWidthStereo`); the mixer surface for `setBusSend` on both
mixers already exists. The gap is persistence: `MixerBusState` doesn't carry
pan/width/gain/muted, `OutputMixer::exportGraphState` doesn't capture bus
sends, and `importGraphState` doesn't reapply any of it. This slice extends
`MixerBusState` and wires both sides through the export/import + JSON
serializers, preserving backward compat (default-on-absent for reads,
default-suppress for writes).

**Tech Stack:** C++ / JUCE / Catch2.

---

## Task 1: Extend `MixerBusState` + JSON serializer

**Files:**
- Modify: `core/include/ida/MixerGraphState.h:85-102` — add 4 fields + update operator==
- Modify: `persistence/src/SessionFormat.cpp:924-947` — extend `busStateToVar` / `busStateFromVar`
- Test: `tests/SessionFormatTests.cpp` (add a new section)

- [ ] **Step 1: Add fields to `MixerBusState`**

In `core/include/ida/MixerGraphState.h`, replace the `MixerBusState` struct:

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
    // 2026-05-25 — operator-set fader / pan / width / mute, mirroring the
    // engine atomics on `Bus`. Defaults equal `Bus`'s defaults so existing
    // sessions (which never wrote these) load with no audible change.
    float                  gainLinear   { 1.0f };
    bool                   muted        { false };
    float                  pan          { 0.5f };
    float                  width        { 1.0f };

    bool operator== (const MixerBusState& o) const noexcept
    {
        return busId == o.busId && channelCount == o.channelCount && name == o.name
            && kind == o.kind && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts
            && gainLinear == o.gainLinear && muted == o.muted
            && pan == o.pan && width == o.width;
    }
    bool operator!= (const MixerBusState& o) const noexcept { return ! (*this == o); }
};
```

- [ ] **Step 2: Build (compile check before touching the serializer)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -10
```

Expected: clean — adding fields to a POD-shaped struct with defaults doesn't
break existing call sites.

- [ ] **Step 3: Find the SessionFormat round-trip test that covers buses**

```bash
grep -n "MixerBusState\|busStateTo\|round-trip\|round_trip\|roundTrip" /Users/larryseyer/IDA/tests/SessionFormatTests.cpp | head -10
```

Note the existing pattern (probably uses `requireProperty` / round-trip-via-JSON helpers).

- [ ] **Step 4: Write a failing test for the bus pan/width/gain/muted round-trip**

Add to `tests/SessionFormatTests.cpp` (place near the existing MixerBusState
tests; if none, add at the end of the file but inside the existing namespace
block):

```cpp
TEST_CASE ("MixerBusState round-trips pan / width / gain / muted",
           "[SessionFormat][MixerBusState]")
{
    ida::MixerBusState in;
    in.busId        = 7;
    in.channelCount = 2;
    in.name         = "AuxA";
    in.kind         = ida::MixerBusKind::Bus;
    in.gainLinear   = 0.75f;
    in.muted        = true;
    in.pan          = 0.25f;
    in.width        = 1.5f;

    // Round-trip via the same JSON entry-point the session envelope uses
    // (deserializeInputMixerGraphState wraps SessionFormat.cpp's
    // busStateFromVar). The full envelope is overkill; build a tiny
    // InputMixerGraphState with this one bus and round-trip that.
    ida::InputMixerGraphState s;
    s.buses.push_back (in);

    const auto json   = ida::persistence::serializeMixerGraphState (s);
    const auto parsed = ida::persistence::deserializeInputMixerGraphState (json);
    REQUIRE (parsed.buses.size() == 1);
    CHECK (parsed.buses[0] == in);
}

TEST_CASE ("MixerBusState legacy load (no pan/width/gain/muted keys) returns defaults",
           "[SessionFormat][MixerBusState]")
{
    // A pre-2026-05-25 bus serialization had no gain/muted/pan/width keys.
    // Construct that exact shape manually and assert the deserializer fills
    // in the documented defaults rather than throwing.
    const juce::String legacy = R"({
      "buses": [ {
        "busId": 3, "channelCount": 2, "name": "Legacy",
        "kind": "Bus",
        "mainOut": { "kind": "Terminal", "busId": 0, "hardwareOutPair": 0 },
        "inserts": { "slots": [] }
      } ],
      "channels": [],
      "nextBusId": 4, "nextChannelId": 1
    })";
    const auto parsed = ida::persistence::deserializeInputMixerGraphState (legacy);
    REQUIRE (parsed.buses.size() == 1);
    CHECK (parsed.buses[0].gainLinear == 1.0f);
    CHECK (parsed.buses[0].muted      == false);
    CHECK (parsed.buses[0].pan        == 0.5f);
    CHECK (parsed.buses[0].width      == 1.0f);
}
```

- [ ] **Step 5: Run the new tests; they should fail (compile or assertion)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "MixerBusState round-trips|MixerBusState legacy load" --output-on-failure
```

Expected: round-trip FAILS because the serializer drops the new fields;
legacy-load PASSES (defaults are in the struct definition, no serializer
change needed for that direction yet).

- [ ] **Step 6: Update `busStateToVar` / `busStateFromVar`**

In `persistence/src/SessionFormat.cpp`, replace:

```cpp
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
```

with:

```cpp
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
    // Default-suppress so legacy sessions stay byte-identical and freshly-
    // saved projects with default bus state stay compact.
    if (b.gainLinear != 1.0f) obj->setProperty ("gain",  b.gainLinear);
    if (b.muted)              obj->setProperty ("muted", true);
    if (b.pan   != 0.5f)      obj->setProperty ("pan",   b.pan);
    if (b.width != 1.0f)      obj->setProperty ("width", b.width);
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
    // Optional reads: absent → struct default (1.0 / false / 0.5 / 1.0).
    if (auto pg = optionalProperty (v, "gain");  ! pg.isVoid()) b.gainLinear = static_cast<float> ((double) pg);
    if (auto pm = optionalProperty (v, "muted"); ! pm.isVoid()) b.muted      = (bool) pm;
    if (auto pp = optionalProperty (v, "pan");   ! pp.isVoid()) b.pan        = static_cast<float> ((double) pp);
    if (auto pw = optionalProperty (v, "width"); ! pw.isVoid()) b.width      = static_cast<float> ((double) pw);
    return b;
}
```

- [ ] **Step 7: Run the tests; both should pass**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "MixerBusState round-trips|MixerBusState legacy load" --output-on-failure
```

Expected: both PASS.

- [ ] **Step 8: Run the full SessionFormat suite (regression check on existing bus tests)**

```bash
ctest --test-dir build -R "SessionFormat" --output-on-failure 2>&1 | tail -15
```

Expected: every SessionFormat test passes — no regression from the
default-suppress writes (existing test fixtures use defaults).

- [ ] **Step 9: Commit**

```bash
git add core/include/ida/MixerGraphState.h persistence/src/SessionFormat.cpp tests/SessionFormatTests.cpp
git commit -m "feat: MixerBusState carries pan/width/gain/muted; SessionFormat round-trips with default-suppress"
git push origin master
```

---

## Task 2: InputMixer bus pan/width/gain/muted export+import

**Files:**
- Modify: `engine/src/InputMixer.cpp:427-440` — capture in `exportGraphState`
- Modify: `engine/src/InputMixer.cpp:498-507` — apply in `importGraphState`
- Test: `tests/InputMixerTests.cpp` (add a section)

- [ ] **Step 1: Write the failing engine round-trip test**

Add to `tests/InputMixerTests.cpp` (place alongside other `exportGraphState`
round-trip tests; grep for `exportGraphState` to find the section):

```cpp
TEST_CASE ("InputMixer round-trips bus pan / width / gain / muted",
           "[InputMixer][persistence]")
{
    ida::InputMixer mixer;
    const auto busId = mixer.addBus (ida::BusConfig { 2, "AuxA", ida::BusKind::Bus });
    auto* bus = mixer.busForId (busId);
    REQUIRE (bus != nullptr);
    bus->setGain  (0.5f);
    bus->setMuted (true);
    bus->setPan   (0.25f);
    bus->setWidth (1.5f);

    const auto state = mixer.exportGraphState();
    ida::InputMixer restored;
    restored.importGraphState (state);

    auto* restoredBus = restored.busForId (busId);
    REQUIRE (restoredBus != nullptr);
    CHECK (restoredBus->gain()  == 0.5f);
    CHECK (restoredBus->muted() == true);
    CHECK (restoredBus->pan()   == 0.25f);
    CHECK (restoredBus->width() == 1.5f);
}
```

- [ ] **Step 2: Run the test; it should fail (round-trip drops the values)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "InputMixer round-trips bus" --output-on-failure
```

Expected: FAIL — restored bus has defaults (1.0 / false / 0.5 / 1.0).

- [ ] **Step 3: Capture the fields in `exportGraphState`**

In `engine/src/InputMixer.cpp`, replace the bus export loop body:

```cpp
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
```

with:

```cpp
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
        entry.gainLinear   = bus.gain();
        entry.muted        = bus.muted();
        entry.pan          = bus.pan();
        entry.width        = bus.width();
        state.buses.push_back (std::move (entry));
    }
```

- [ ] **Step 4: Apply the fields in `importGraphState`**

In `engine/src/InputMixer.cpp`, replace the bus import loop:

```cpp
    for (const auto& b : state.buses)
    {
        jassert (! busExists (b.busId));
        BusConfig config;
        config.channelCount = b.channelCount;
        config.name         = b.name;
        config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
        addBus (config); // mints b.busId (dense) and registers the graph node
        setBusEffectChain (BusId (b.busId), b.inserts);
    }
```

with:

```cpp
    for (const auto& b : state.buses)
    {
        jassert (! busExists (b.busId));
        BusConfig config;
        config.channelCount = b.channelCount;
        config.name         = b.name;
        config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
        addBus (config); // mints b.busId (dense) and registers the graph node
        setBusEffectChain (BusId (b.busId), b.inserts);
        if (auto* freshBus = busForId (BusId (b.busId)))
        {
            freshBus->setGain  (b.gainLinear);
            freshBus->setMuted (b.muted);
            freshBus->setPan   (b.pan);
            freshBus->setWidth (b.width);
        }
    }
```

- [ ] **Step 5: Run the new test; should pass**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "InputMixer round-trips bus" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Run the full InputMixer suite**

```bash
ctest --test-dir build -R "InputMixer" --output-on-failure 2>&1 | tail -10
```

Expected: every InputMixer test passes (no regression).

- [ ] **Step 7: Commit**

```bash
git add engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer exports + imports bus pan/width/gain/muted"
git push origin master
```

---

## Task 3: OutputMixer bus pan/width/gain/muted export+import

**Files:**
- Modify: `engine/src/OutputMixer.cpp:934-947` — capture in `exportGraphState`
- Modify: `engine/src/OutputMixer.cpp:993-1005` — apply in `importGraphState`
- Test: `tests/OutputMixerTests.cpp`

- [ ] **Step 1: Write the failing OutputMixer round-trip test**

Add to `tests/OutputMixerTests.cpp` (alongside other `exportGraphState`
tests; grep `exportGraphState` to find the right section):

```cpp
TEST_CASE ("OutputMixer round-trips bus pan / width / gain / muted",
           "[OutputMixer][persistence]")
{
    ida::OutputMixer mixer;
    const auto busId = mixer.addBus (ida::BusConfig { 2, "OutAuxA", ida::BusKind::Bus });
    auto* bus = mixer.busForId (busId);
    REQUIRE (bus != nullptr);
    bus->setGain  (0.5f);
    bus->setMuted (true);
    bus->setPan   (0.25f);
    bus->setWidth (1.5f);

    const auto state = mixer.exportGraphState();
    ida::OutputMixer restored;
    restored.importGraphState (state);

    auto* restoredBus = restored.busForId (busId);
    REQUIRE (restoredBus != nullptr);
    CHECK (restoredBus->gain()  == 0.5f);
    CHECK (restoredBus->muted() == true);
    CHECK (restoredBus->pan()   == 0.25f);
    CHECK (restoredBus->width() == 1.5f);
}
```

- [ ] **Step 2: Run the test; it should fail**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "OutputMixer round-trips bus pan" --output-on-failure
```

Expected: FAIL (restored bus has defaults).

- [ ] **Step 3: Capture the fields in `OutputMixer::exportGraphState`**

In `engine/src/OutputMixer.cpp`, replace the bus export loop body:

```cpp
    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = busMainOutSnapshot (graph_, busNodeIds_[i], busNodeIds_, buses_,
                                                 busHardwareOutPair_[i]);
        entry.inserts      = bus.effectChain();
        state.buses.push_back (std::move (entry));   // master is index 0 by construction
    }
```

with:

```cpp
    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = busMainOutSnapshot (graph_, busNodeIds_[i], busNodeIds_, buses_,
                                                 busHardwareOutPair_[i]);
        entry.inserts      = bus.effectChain();
        entry.gainLinear   = bus.gain();
        entry.muted        = bus.muted();
        entry.pan          = bus.pan();
        entry.width        = bus.width();
        state.buses.push_back (std::move (entry));   // master is index 0 by construction
    }
```

- [ ] **Step 4: Apply the fields in `OutputMixer::importGraphState`**

In `engine/src/OutputMixer.cpp`, replace the bus import loop body:

```cpp
    for (const auto& b : state.buses)
    {
        if (! busExists (b.busId))
        {
            BusConfig config;
            config.channelCount = b.channelCount;
            config.name         = b.name;
            config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
            addBus (config);
        }
        setBusEffectChain (BusId (b.busId), b.inserts);
    }
```

with:

```cpp
    for (const auto& b : state.buses)
    {
        if (! busExists (b.busId))
        {
            BusConfig config;
            config.channelCount = b.channelCount;
            config.name         = b.name;
            config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
            addBus (config);
        }
        setBusEffectChain (BusId (b.busId), b.inserts);
        if (auto* freshBus = busForId (BusId (b.busId)))
        {
            freshBus->setGain  (b.gainLinear);
            freshBus->setMuted (b.muted);
            freshBus->setPan   (b.pan);
            freshBus->setWidth (b.width);
        }
    }
```

- [ ] **Step 5: Run the test; should pass**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "OutputMixer round-trips bus pan" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Run the full OutputMixer suite**

```bash
ctest --test-dir build -R "OutputMixer" --output-on-failure 2>&1 | tail -10
```

Expected: every OutputMixer test passes.

- [ ] **Step 7: Commit**

```bash
git add engine/src/OutputMixer.cpp tests/OutputMixerTests.cpp
git commit -m "feat: OutputMixer exports + imports bus pan/width/gain/muted"
git push origin master
```

---

## Task 4: OutputMixer bus→FX-return sends export+import

**Files:**
- Modify: `engine/src/OutputMixer.cpp:934-947` — capture bus sends in `exportGraphState`
- Modify: `engine/src/OutputMixer.cpp` (after the bus-creation loop in importGraphState) — replay sends
- Test: `tests/OutputMixerTests.cpp`

- [ ] **Step 1: Write the failing round-trip test**

Add to `tests/OutputMixerTests.cpp`:

```cpp
TEST_CASE ("OutputMixer round-trips bus->FX-return send levels",
           "[OutputMixer][persistence]")
{
    ida::OutputMixer mixer;
    const auto srcBus = mixer.addBus (ida::BusConfig { 2, "SrcBus", ida::BusKind::Bus });
    const auto fxRet  = mixer.addBus (ida::BusConfig { 2, "FxRet",  ida::BusKind::FxReturn });
    REQUIRE (mixer.setBusSend (srcBus, fxRet, 0.65f));

    const auto state = mixer.exportGraphState();
    ida::OutputMixer restored;
    restored.importGraphState (state);
    CHECK (restored.busSendLevel (srcBus, fxRet) == 0.65f);
}
```

- [ ] **Step 2: Run the test; should fail (send level lost)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "OutputMixer round-trips bus->FX" --output-on-failure
```

Expected: FAIL — `restored.busSendLevel(srcBus, fxRet)` returns 0.0f.

- [ ] **Step 3: Capture bus sends in `exportGraphState`**

In `engine/src/OutputMixer.cpp`, inside the bus export loop (the same one
edited in Task 3 Step 3), add the bus-sends snapshot right after the
existing fields. Insert *after* `entry.width = bus.width();` (which was
added in Task 3):

```cpp
        // Capture bus -> any-bus sends (typically FX returns; the master
        // is a legal source per `setBusSend`). Walk every other bus; skip
        // self-sends (`setBusSend` rejects them). Default 0 levels are
        // dropped to keep the JSON small.
        for (std::size_t j = 0; j < buses_.size(); ++j)
        {
            if (j == i) continue;
            const auto targetId = buses_[j].id();
            const float level = busSendLevel (bus.id(), targetId);
            if (level > 0.0f)
                entry.sends.push_back ({ targetId.value(), level });
        }
```

- [ ] **Step 4: Replay sends in `importGraphState`**

In `engine/src/OutputMixer.cpp`, after the existing bus-creation loop (and
before any code that uses sends elsewhere — likely right after the
`setBusEffectChain` block edited in Task 3), add a second pass that walks
`state.buses` and replays each bus's `sends`:

```cpp
    // Replay bus -> any-bus sends now that every bus exists (forward
    // references between buses are legal). `setBusSend` enforces non-self
    // and cycle detection; jassert on failure so a malformed snapshot
    // halts the load rather than silently dropping routing.
    for (const auto& b : state.buses)
    {
        for (const auto& s : b.sends)
        {
            const bool ok = setBusSend (BusId (b.busId), BusId (s.busId), s.level);
            jassert (ok);
            juce::ignoreUnused (ok);
        }
    }
```

- [ ] **Step 5: Run the new test; should pass**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5 && ctest --test-dir build -R "OutputMixer round-trips bus->FX" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Run the full OutputMixer + SessionFormat suites**

```bash
ctest --test-dir build -R "OutputMixer|SessionFormat" --output-on-failure 2>&1 | tail -20
```

Expected: every test passes.

- [ ] **Step 7: Commit**

```bash
git add engine/src/OutputMixer.cpp tests/OutputMixerTests.cpp
git commit -m "feat: OutputMixer exports + imports bus->FX-return send levels"
git push origin master
```

---

## Task 5: Clean build, full suite, operator verification + continue.md

- [ ] **Step 1: Clean rebuild (CMake config + per-house-rules pre-eyes-on)**

```bash
rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
cmake --build build --target IDA IdaTests 2>&1 | tail -5
```

Expected: both targets build clean.

- [ ] **Step 2: Run the full test suite**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" --output-on-failure 2>&1 | tail -10
```

Expected: 709+ tests pass (new tests bump the count; pre-existing skip stays).

- [ ] **Step 3: Launch the .app and ask the operator to verify**

```bash
open /Users/larryseyer/IDA/build/app/IDA_artefacts/Release/IDA.app
```

Verification script for the operator:
1. Open the running .app. Create or open a project that has at least one aux bus.
2. Select an aux bus → change its Pan (e.g. hard left), Width (e.g. 0.0 mono),
   gain (down ~20 dB), and Mute it.
3. If FX returns exist: open the aux bus's detail panel → Sends tab → set
   one FX-return send level to ~0.6.
4. Save the project (File → Save / Cmd-S). Quit the app.
5. Relaunch the app. Load the project just saved.
6. Reselect the same aux bus. Confirm Pan, Width, gain, mute, and the FX-return
   send level are exactly what was set in step 2-3 (within float epsilon for
   the floats; the booleans are exact).
7. (Repeat for an OutputMixer-side aux bus + FX-return send if both mixers
   have one — the persistence is symmetric so both should round-trip.)

- [ ] **Step 4: Refresh `continue.md`**

Replace this chat's section with a brief summary noting the 4 task commits
+ baseline test count, retire the bus-state-persistence item from §5 if
it was queued there, and prune `todo.md`'s "2026-05-24 — Bus pan + width +
bus-side sends (NEXT SLICE)" entry (the slice is now complete).

- [ ] **Step 5: Commit the doc updates**

```bash
git add continue.md todo.md
git commit -m "docs: continue.md + todo.md — bus state persistence landed (pan/width/gain/muted both mixers; OM bus sends)"
git push origin master
```

---

## Self-Review

**Spec coverage:**
- Bus pan/width/gain/muted struct + JSON: Task 1. ✓
- InputMixer round-trip: Task 2. ✓
- OutputMixer round-trip: Task 3. ✓
- OutputMixer bus->FX-return sends (the gap InputMixer doesn't have): Task 4. ✓
- Operator verify + handoff: Task 5. ✓

**Placeholder scan:** Every code block is concrete. The `optionalProperty(v,
"gain")` etc. reads follow the existing `optionalProperty` helper pattern
already used in `busStateFromVar:944` for `sends`. The `juce::ignoreUnused`
+ jassert pattern in Task 4 Step 4 mirrors `InputMixer.cpp:554` exactly.

**Type consistency:**
- `MixerBusState` field types match `Bus`'s atomics (float / bool / float /
  float). Default values match `Bus`'s defaults (1.0f / false / 0.5f / 1.0f).
- `setGain` / `setMuted` / `setPan` / `setWidth` are the exact `Bus` setter
  names (verified in `engine/include/ida/Bus.h:101-128`).
- `busForId` returns `Bus*` (verified in both mixers' headers).
- `setBusSend` signature `(BusId, BusId, float) -> bool` matches both
  `InputMixer::setBusSend:927` and `OutputMixer::setBusSend:596`.
- `busSendLevel (BusId, BusId) const noexcept -> float` matches both
  mixers' implementations.
- `MixerSend { std::int64_t busId; float level; }` shape used in Task 4
  Step 3 matches the existing struct (verified by the existing
  `entry.sends.push_back ({ busId.value(), level })` site at
  `OutputMixer.cpp:973`).
