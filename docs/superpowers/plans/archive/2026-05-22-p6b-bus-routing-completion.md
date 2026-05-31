# P6b — Bus-routing completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make every bus-row strip (plain buses AND FX returns) fully routable — its output can go to a tape, Direct out, or another plain bus — with feedback cycles filtered out of the menu, and make both channel and bus pickers tick/label their *specific* current destination.

**Why:** P6 shipped bus strips you can create but not route (operator-flagged) and a channel→bus picker that shows a generic "Bus" label. Per `feedback_sirius_done_right_and_complete`, that's a half-feature. This completes it.

**Architecture:** One engine task (add the missing const accessors + a cycle-precheck wrapper to `InputMixer`, TDD against `tests/InputMixerTests.cpp`) then one UI task (bus-row destination pickers + specific tick-back on both rows, in `app/MainComponent.cpp`). The cycle filter uses the existing `MixerGraph::wouldMainOutCycle` (const) surfaced through `InputMixer`.

**Tech Stack:** C++17, Catch2 (engine tests), JUCE 8 (UI).

**STATUS (2026-05-22):** Task A ✅ SHIPPED at `f384a1e` (`channelMainOutBus`/`busMainOutBus`/`busMainOutToBusWouldCycle` + 2 Catch2 cases; ctest 569 pass / 1 Not-Run). **Task B (UI) is NEXT and not started.**

---

## Scope (complete, not half)
- **Bus-row destination picker on BOTH kinds** (plain `Bus` and `FxReturn`/RVB/DLY). An FX return's *output* routing (e.g. record wet reverb to tape, or feed a subgroup) is legitimate and is included. (FX returns are *fed* by sends — that's the separate Sends tab, P7 — but their *output* is a normal main-out and gets the picker here.)
- **Destinations offered:** pooled tapes, Direct out, and other **plain buses** — excluding self, excluding FX returns as targets (FX returns are send-only destinations), and excluding any target that would create a cycle (filtered via the new query).
- **Tick-back:** channel and bus pickers show + tick the *specific* current destination (tape name / bus name / "Direct out").
- Routing edits are audio-callback-bracketed (topology mutation).

---

### Task A: Engine — main-out "which bus" accessors + cycle-precheck (TDD)

**Files:**
- Modify: `engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp`
- Test: `tests/InputMixerTests.cpp`

Add three message-thread const methods to `InputMixer` (declare near the existing `channelMainOut`/`busMainOut` at InputMixer.h:67-68):

```cpp
    /// The specific bus a node's main-out targets, or BusId{0} (invalid sentinel)
    /// when the main-out is not a bus (tape / hardware output) or the node is
    /// unknown. Complements channelMainOut/busMainOut (which return only the
    /// MainOutDest category). Message-thread only.
    BusId channelMainOutBus (ChannelId) const noexcept;
    BusId busMainOutBus (BusId) const noexcept;

    /// True iff routing `from`'s main-out to bus `to` would close a feedback
    /// cycle (so the UI can omit it from the picker). Non-mutating wrapper over
    /// MixerGraph::wouldMainOutCycle. False for unknown ids. Message-thread only.
    bool busMainOutToBusWouldCycle (BusId from, BusId to) const noexcept;
```

Implementation notes (InputMixer.cpp): reverse-map the graph destination node back to a BusId. `graph_.mainOutOf(nodeForBus(id))` (or `nodeForChannel`) gives the dest `MixerNodeId`; scan `busNodeIds_` for the matching node and return the parallel `buses_[i].id()`; return `BusId{0}` if no bus matches (i.e. it's a terminal). For the cycle wrapper, translate both BusIds via `nodeForBus` and call `graph_.wouldMainOutCycle(fromNode, toNode)`; return false if either id is unknown. `BusId{0}` is the invalid sentinel (bus ids start at 1, mirroring TapeId{0}).

- [ ] **Step 1: Write failing tests** in `tests/InputMixerTests.cpp` (follow the existing `[input-mixer]` style, e.g. the cycle test at lines 617-625):
```cpp
TEST_CASE ("InputMixer reports which bus a node's main-out targets", "[input-mixer]")
{
    ida::InputMixer mixer;
    const auto a = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);

    REQUIRE (mixer.setChannelMainOutToBus (ch, a));
    REQUIRE (mixer.channelMainOutBus (ch).value() == a.value());
    REQUIRE (mixer.setBusMainOutToBus (a, b));
    REQUIRE (mixer.busMainOutBus (a).value() == b.value());

    // Non-bus main-out → invalid sentinel.
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch));
    REQUIRE (mixer.channelMainOutBus (ch).value() == 0);
    REQUIRE (mixer.setBusMainOutToTape (b));
    REQUIRE (mixer.busMainOutBus (b).value() == 0);
}

TEST_CASE ("InputMixer flags bus main-out routes that would cycle", "[input-mixer]")
{
    ida::InputMixer mixer;
    const auto a = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b));        // a -> b
    REQUIRE (mixer.busMainOutToBusWouldCycle (b, a)); // b -> a would close the loop
    REQUIRE_FALSE (mixer.busMainOutToBusWouldCycle (a, b)); // already the live edge, no NEW cycle
}
```
- [ ] **Step 2: Run, confirm they fail to compile** (methods undeclared): `cmake --build build --target IdaTests` → FAIL.
- [ ] **Step 3: Implement** the three methods in InputMixer.h/.cpp per the notes above.
- [ ] **Step 4: Build + run** `cmake --build build --target IdaTests && ctest --test-dir build` → the two new cases pass; full baseline still green (was 567 pass / 1 Not-Run; now +2 cases pass).
- [ ] **Step 5: Commit + push** (`feat: P6b — InputMixer channelMainOutBus/busMainOutBus + bus-cycle precheck`).

### Task B: UI — bus-row destination pickers + specific tick-back

**Files:** Modify `app/MainComponent.cpp` (`InputMixerPane` + `refreshInputDestinations` + relay wiring + `app/MainComponent.h` if a new vector/method is needed).

Pattern to mirror: the channel destination picker (`destButtons_`, `stripDests_`, `showDestinationMenu`, `setDestinations`, `onDestinationChosen`) shipped in P6 T3. Bus strips need the parallel apparatus.

- [ ] **Step 1:** Give `InputMixerPane` a parallel bus-picker apparatus: `std::vector<std::unique_ptr<juce::TextButton>> busDestButtons_;` and `std::vector<StripDest> busStripDests_;` built in `setBusStrips` (one button per bus strip, `onClick` → `showBusDestinationMenu(i)`); a `std::function<void(int busIdx, DestChoice)> onBusDestinationChosen;`; `showBusDestinationMenu(int)` mirroring `showDestinationMenu`; and a `setBusDestinations(const std::vector<DestChoice>& choices, const std::vector<StripDest>& perBus)` mirroring `setDestinations`. The `choices` for bus pickers are computed per-bus by MainComponent (they differ per bus because cycle-excluded targets differ), so `showBusDestinationMenu` reads a per-bus choice list — store `std::vector<std::vector<DestChoice>> busChoices_` (one list per bus strip) instead of the single shared `choices_`.
- [ ] **Step 2:** In `resized()`, lay a picker button beneath each bus strip (extend the existing `pickerRow` handling to also cover the bus columns, or add a parallel row under the bus group). Keep alignment with the bus strips.
- [ ] **Step 3:** In MainComponent, extend `refreshInputDestinations()` to also build, per bus strip `i` (busStripIds_[i] = busId):
  - choices = tapes + Direct out + every *plain* bus `b` (busKindAt==Bus) where `b != busId` and `!inputMixer_->busMainOutToBusWouldCycle(busId, b)`;
  - current StripDest from `busMainOut(busId)` → Tape (which tape via `busMainOutIsTape(busId, t.id)`), Bus (which bus via `busMainOutBus(busId)`, look up its name + set currentId), HardwareOutput ("Direct out");
  - push to the pane via `setBusDestinations`.
  Also FIX the channel tick-back: in the `MainOutDest::Bus` case of the per-channel loop, replace the generic "Bus" label with the specific bus via `channelMainOutBus(chId)` (set `currentId = thatBus.value()`, `currentName = busForId(thatBus)->config().name`). Remove the now-obsolete "no which-bus accessor" comment.
- [ ] **Step 4:** Wire `onBusDestinationChosen` in the relay block: bracket with remove/addAudioCallback, switch on `dest.kind` → `setBusMainOutToTape(busId, TapeId(dest.id))` / `setBusMainOutToBus(busId, BusId(dest.id))` / `setBusMainOutToHardwareOutput(busId)`, then `refreshInputMixer()`. (busId = busStripIds_[busIdx].)
- [ ] **Step 5:** Build `cmake --build build --target IDA` clean; `ctest --test-dir build` baseline holds.
- [ ] **Step 6:** Commit + push (`feat: P6b — bus-row destination pickers + specific bus tick-back on both rows`).

## Verification
- Engine: the two new Catch2 cases pass; existing cycle/routing cases still green.
- UI (operator eyes-on after clean `rm -rf build` rebuild): every bus strip (incl. RVB/DLY) has a destination button; it lists tapes + Direct out + other plain buses; selecting routes audibly; a bus that would feed back is absent from the menu; channel→bus now shows the bus's NAME and ticks it on reopen.

## Self-review
- Coverage: bus routing to tape/direct/bus ✓ (Task B S3-S4); cycle exclusion ✓ (busMainOutToBusWouldCycle); specific tick-back both rows ✓; FX-return output routable ✓ (picker on both kinds). No half: the only thing still out is sends-INTO-FX-returns (a distinct feature = P7 Sends tab).
- Type consistency: `DestChoice`/`StripDest`/`DestKind` reused from P6 T3; `BusId{0}` sentinel consistent with `TapeId{0}`.
