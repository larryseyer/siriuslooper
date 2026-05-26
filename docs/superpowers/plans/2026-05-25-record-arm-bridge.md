# Per-channel record arm + ≥1-channel→tape floor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a per-channel record-arm gesture (strip right-click / long-press → "Record to tape" checkable menu item) that flips `TapeMode` between `CommitToTape` and `NoTape`, with active engine enforcement that the count of channels with `TapeMode != NoTape` cannot drop below 1.

**Architecture:** The engine already gates direct channel→tape writes on `channel.tapeMode != TapeMode::NoTape` (`engine/src/InputMixer.cpp:1140`) and round-trips the field through both `InputMixerGraphState` and JSON. Only three layers remain: (1) a const accessor + `bool`-returning setter with floor enforcement in `InputMixer`, (2) a load-handler floor guard for corrupt sessions, (3) UI wiring — a new callback on `InputMixerPane`, a new checkable item in the strip's existing right-click / long-press menu, dim-on-NoTape rendering, and a refusal banner via the existing `CaptureBanner`.

**Spec:** `docs/superpowers/specs/2026-05-25-record-arm-bridge-design.md`

**Tech Stack:** C++17/20, JUCE 8 (juce::Component, juce::PopupMenu, juce::Colour::withMultipliedBrightness), Catch2, CMake/Ninja. Single-process, message-thread-only UI changes; engine changes are RT-safe (no audio-thread reach-through).

---

## File map

**Modify (no new files):**
- `engine/include/ida/InputMixer.h` — add `channelTapeMode` accessor, add `canDisarmChannelRecording` predicate, change `setChannelTapeMode` return type `void` → `bool`.
- `engine/src/InputMixer.cpp` — implement the three above + floor-counting helper.
- `tests/InputMixerTests.cpp` — add 6 new test cases for accessor + predicate + bool-setter behaviour.
- `app/MainComponent.cpp` — `InputMixerPane` class: add `onToggleChannelRecording` callback, `stripTapeModes_` state mirror, `setChannelTapeModes` pusher, extend `showToggleMenu`, dim-when-NoTape on strip background. `MainComponent` class: wire the callback (audio-callback-bracket + refusal banner), call `setChannelTapeModes` from `refreshInputDestinations`, add `chooseFileAndLoad` post-import floor guard.
- `tests/MixerGraphStateTests.cpp` — add 1 new test for post-import floor guard (engine-level test of the corruption-handling path).

**No new files. No persistence touched** — `tapeMode` is already serialized in `persistence/src/SessionFormat.cpp:1001 / :1027` via `tapeModeToString`/`tapeModeFromString`, AND round-tripped through `InputMixerGraphState` via `engine/src/InputMixer.cpp:534 / :600`. Verified during plan-writing.

---

## Task 1 — Engine: `channelTapeMode(ChannelId) const` accessor

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (add declaration immediately below `setChannelTapeMode` at line 221)
- Modify: `engine/src/InputMixer.cpp` (add implementation immediately below `setChannelTapeMode` at line 369)
- Modify: `tests/InputMixerTests.cpp` (add 1 test case)

- [ ] **Step 1: Write the failing test**

Add to `tests/InputMixerTests.cpp` (place near other channel-MON tests, anywhere in the file). The accessor must read the engine's current TapeMode and default to `NoTape` for unknown ids (mirrors `channelMonitorMode`'s unknown-id behaviour at line 245 of the header).

```cpp
TEST_CASE ("InputMixer::channelTapeMode reads engine state; unknown id returns NoTape",
           "[input-mixer][tape-mode]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);
    const auto ch  = mixer.addChannel (src, ida::SignalType::Audio);

    // addChannel default — Channel ctor seeds NoTape (per
    // ChannelDefaults::defaultTapeMode in core/include/ida/ChannelDefaults.h).
    CHECK (mixer.channelTapeMode (ch) == ida::TapeMode::NoTape);

    mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    CHECK (mixer.channelTapeMode (ch) == ida::TapeMode::CommitToTape);

    mixer.setChannelTapeMode (ch, ida::TapeMode::NoTape);
    CHECK (mixer.channelTapeMode (ch) == ida::TapeMode::NoTape);

    // Unknown id → NoTape (defensive default, same shape as
    // channelMonitorMode unknown-id behaviour).
    CHECK (mixer.channelTapeMode (ida::ChannelId { 9999 }) == ida::TapeMode::NoTape);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -5
```

Expected: compile FAIL with `'channelTapeMode' is not a member of 'ida::InputMixer'`.

- [ ] **Step 3: Add the declaration**

In `engine/include/ida/InputMixer.h`, immediately below the `setChannelTapeMode` declaration at line 221, add:

```cpp
    /// Message-thread accessor. Unknown id reads as `TapeMode::NoTape`.
    /// Mirror of `channelMonitorMode(ChannelId) const`.
    TapeMode channelTapeMode (ChannelId) const noexcept;
```

- [ ] **Step 4: Add the implementation**

In `engine/src/InputMixer.cpp`, immediately below `setChannelTapeMode` (after the closing brace at line 369), add:

```cpp
TapeMode InputMixer::channelTapeMode (ChannelId id) const noexcept
{
    auto it = channels_.find (id.value());
    return (it != channels_.end()) ? it->second.tapeMode : TapeMode::NoTape;
}
```

- [ ] **Step 5: Build + run the new test**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][tape-mode]" 2>&1 | tail -10
```

Expected: 1 test, all assertions pass.

- [ ] **Step 6: Commit + push**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::channelTapeMode(ChannelId) const accessor"
git push origin master
```

---

## Task 2 — Engine: `canDisarmChannelRecording` predicate + `setChannelTapeMode` returns `bool` with floor enforcement

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (add predicate declaration, change setter signature `void` → `bool`)
- Modify: `engine/src/InputMixer.cpp` (implement predicate, rewrite setter)
- Modify: `app/MainComponent.cpp` (sole non-test caller at line 6425 — wrap in `juce::ignoreUnused`)
- Modify: `tests/InputMixerTests.cpp` (add 5 test cases)

- [ ] **Step 1: Write the failing tests**

Add to `tests/InputMixerTests.cpp` (same area as Task 1's test).

```cpp
TEST_CASE ("InputMixer::canDisarmChannelRecording — true with multiple armed; false at floor; false on unknown",
           "[input-mixer][tape-mode][floor]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);

    const auto a = mixer.addChannel (src, ida::SignalType::Audio);
    const auto b = mixer.addChannel (src, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));
    REQUIRE (mixer.setChannelTapeMode (b, ida::TapeMode::CommitToTape));

    // Two armed channels → disarming either is safe.
    CHECK (mixer.canDisarmChannelRecording (a));
    CHECK (mixer.canDisarmChannelRecording (b));

    // Disarm a; b is now the only armed channel.
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::NoTape));
    CHECK_FALSE (mixer.canDisarmChannelRecording (b));   // last one — floor

    // Unknown id → false (cannot disarm what doesn't exist).
    CHECK_FALSE (mixer.canDisarmChannelRecording (ida::ChannelId { 9999 }));

    // A channel that is already NoTape — disarming is a no-op transition, so
    // the predicate returns true (nothing to refuse).
    CHECK (mixer.canDisarmChannelRecording (a));
}

TEST_CASE ("InputMixer::setChannelTapeMode refuses the last-armed-channel disarm",
           "[input-mixer][tape-mode][floor]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);
    const auto a   = mixer.addChannel (src, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));

    // Only armed channel; floor refuses.
    CHECK_FALSE (mixer.setChannelTapeMode (a, ida::TapeMode::NoTape));
    CHECK (mixer.channelTapeMode (a) == ida::TapeMode::CommitToTape);  // unchanged
}

TEST_CASE ("InputMixer::setChannelTapeMode allows the last-channel disarm when a sibling is armed",
           "[input-mixer][tape-mode][floor]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);
    const auto a = mixer.addChannel (src, ida::SignalType::Audio);
    const auto b = mixer.addChannel (src, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));
    REQUIRE (mixer.setChannelTapeMode (b, ida::TapeMode::CommitToTape));

    // Two armed → either may disarm.
    CHECK (mixer.setChannelTapeMode (a, ida::TapeMode::NoTape));
    CHECK (mixer.channelTapeMode (a) == ida::TapeMode::NoTape);
    CHECK (mixer.channelTapeMode (b) == ida::TapeMode::CommitToTape);   // sibling unaffected
}

TEST_CASE ("InputMixer::setChannelTapeMode is idempotent — same mode to same mode returns true",
           "[input-mixer][tape-mode]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);
    const auto a   = mixer.addChannel (src, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));

    // CommitToTape → CommitToTape — true, no state change.
    CHECK (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));
    CHECK (mixer.channelTapeMode (a) == ida::TapeMode::CommitToTape);

    // Idempotence also applies on NoTape, even when at the floor (the
    // predicate is for *transitions*; same-mode-to-same-mode is not a
    // transition).
    const auto b = mixer.addChannel (src, ida::SignalType::Audio);
    REQUIRE (mixer.setChannelTapeMode (b, ida::TapeMode::NoTape));   // sibling NoTape from the start
    CHECK (mixer.setChannelTapeMode (b, ida::TapeMode::NoTape));     // no-op stays true
}

TEST_CASE ("InputMixer::setChannelTapeMode — unknown id returns false (no-op preserved, now visible)",
           "[input-mixer][tape-mode]")
{
    ida::InputMixer mixer;
    CHECK_FALSE (mixer.setChannelTapeMode (ida::ChannelId { 9999 }, ida::TapeMode::CommitToTape));
    CHECK_FALSE (mixer.setChannelTapeMode (ida::ChannelId { 9999 }, ida::TapeMode::NoTape));
}

TEST_CASE ("InputMixer::setChannelTapeMode — re-arming a NoTape channel always succeeds",
           "[input-mixer][tape-mode]")
{
    ida::InputMixer mixer;
    const auto src = mixer.addInput (ida::SignalType::Audio);
    const auto a   = mixer.addChannel (src, ida::SignalType::Audio);

    // Channel starts NoTape (Channel ctor default). Going NoTape → CommitToTape
    // is always allowed — re-arming never violates the floor.
    CHECK (mixer.setChannelTapeMode (a, ida::TapeMode::CommitToTape));
    CHECK (mixer.channelTapeMode (a) == ida::TapeMode::CommitToTape);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target IdaTests -j 2>&1 | tail -10
```

Expected: compile FAIL — `canDisarmChannelRecording` not declared, `setChannelTapeMode` return-type mismatch in the new tests (REQUIRE wraps a void).

- [ ] **Step 3: Update the header declarations**

In `engine/include/ida/InputMixer.h`, change line 221 from:

```cpp
    void setChannelTapeMode (ChannelId, TapeMode);
```

to:

```cpp
    /// Returns `true` on success (including idempotent same-mode → same-mode).
    /// Returns `false` in exactly two cases:
    ///   - Floor violation: the call would set this channel to `NoTape` AND
    ///     `canDisarmChannelRecording(id)` is false. Channel state unchanged.
    ///   - Unknown `ChannelId`. No-op preserved.
    /// Mirror of `channelMonitorMode` accessor / `setChannelMonitorMode` pattern.
    bool setChannelTapeMode (ChannelId, TapeMode);

    /// Returns `false` iff disarming this channel's recording (setting its
    /// TapeMode to `NoTape`) would drop the count of channels with
    /// `TapeMode != NoTape` below 1. A channel that is already `NoTape`
    /// returns `true` (disarming a non-armed channel is a no-op transition,
    /// not a floor-violating transition). Unknown id returns `false`.
    /// Constant-time linear scan over the channel registry; message-thread
    /// caller; no audio-thread reach-through.
    bool canDisarmChannelRecording (ChannelId) const noexcept;
```

- [ ] **Step 4: Rewrite the setter; add the predicate**

In `engine/src/InputMixer.cpp`, replace the entire body at lines 354-369 with:

```cpp
bool InputMixer::setChannelTapeMode (ChannelId id, TapeMode mode)
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return false;

    // Floor enforcement: refuse a NoTape transition if this is the only
    // armed channel. Same-mode → same-mode (NoTape → NoTape on a channel
    // that's already at the floor) is *not* a transition and is allowed
    // (idempotent), matching the channel-MON setter pattern.
    if (mode == TapeMode::NoTape && it->second.tapeMode != TapeMode::NoTape
        && ! canDisarmChannelRecording (id))
        return false;

    it->second.tapeMode = mode;

    // For NonDestructive channels, ensure the params partial file exists as
    // soon as the mode is set. Touching here (message thread, set-once) avoids
    // any RT-safety deviation that would result from doing filesystem I/O on the
    // audio thread inside processBuffer.
    if (mode == TapeMode::NonDestructive && tapeWriter_ != nullptr)
        tapeWriter_->touchParamsPartial (id);

    return true;
}

bool InputMixer::canDisarmChannelRecording (ChannelId id) const noexcept
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return false;

    // If this channel is already NoTape, the "disarm" transition is a no-op
    // and trivially safe — no floor concern.
    if (it->second.tapeMode == TapeMode::NoTape) return true;

    // Count *other* channels (excluding `id`) with TapeMode != NoTape. If at
    // least one exists, disarming `id` is safe.
    for (const auto& kv : channels_)
    {
        if (kv.first == id.value()) continue;
        if (kv.second.tapeMode != TapeMode::NoTape) return true;
    }
    return false;
}
```

- [ ] **Step 5: Fix the existing caller in `MainComponent.cpp`**

Find line 6425 in `app/MainComponent.cpp` (the strip-creation site that seeds CommitToTape):

```cpp
        inputMixer_->setChannelTapeMode (chId, TapeMode::CommitToTape);
```

Replace with:

```cpp
        // Re-arming a freshly-added channel — the floor is never violated by
        // a NoTape → CommitToTape transition, so success is guaranteed; the
        // bool return is checked only by operator-gesture call sites that can
        // surface a refusal banner.
        juce::ignoreUnused (inputMixer_->setChannelTapeMode (chId, TapeMode::CommitToTape));
```

- [ ] **Step 6: Build + run the tape-mode tests**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[input-mixer][tape-mode]" 2>&1 | tail -15
```

Expected: 7 tests (Task 1's + the 6 added here), all assertions pass.

- [ ] **Step 7: Run the full suite to catch any other void-return caller I missed**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: 727 + 6 = 733 tests pass. If a compile failure surfaces from a test that calls `setChannelTapeMode` expecting void, wrap it with `juce::ignoreUnused` (or `REQUIRE` if floor-relevant) in the test, but DO NOT add new behaviours — the test was relying on the prior return type.

- [ ] **Step 8: Commit + push**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp app/MainComponent.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::setChannelTapeMode returns bool with ≥1-channel-armed floor enforcement (V9 §7.2 / looper invariant)"
git push origin master
```

---

## Task 3 — Load-handler floor guard

**Files:**
- Modify: `app/MainComponent.cpp` (`chooseFileAndLoad`, after `importGraphState`)
- Modify: `tests/MixerGraphStateTests.cpp` (add 1 test case)

A session JSON could arrive with zero `CommitToTape` channels (externally edited, or a bug in a prior export). The engine's in-session floor enforcement (Task 2) prevents the count from ever reaching zero through *gestures*, but it can't guard against an arriving graph that's already there. Guard the load handler explicitly.

- [ ] **Step 1: Write the failing test**

Add to `tests/MixerGraphStateTests.cpp`:

```cpp
TEST_CASE ("InputMixerGraphState all-NoTape import: engine accepts the snapshot as-is "
           "(load-handler is responsible for floor restoration, not importGraphState)",
           "[mixer-graph-state][tape-mode][floor]")
{
    // The engine's importGraphState is a faithful state replacement — it does
    // NOT enforce the floor invariant (that's the load-handler's job, per the
    // bridge-slice design §6.2). This test pins the contract so the load-
    // handler test (next test) can rely on the engine accepting whatever it's
    // given.
    ida::InputMixer mixer;
    ida::InputMixerGraphState state;
    state.channels.push_back ({});
    state.channels.back().channelId    = 1;
    state.channels.back().signalType   = ida::SignalType::Audio;
    state.channels.back().inputSourceId = 1;
    state.channels.back().tapeMode     = ida::TapeMode::NoTape;     // intentional corruption

    state.channels.push_back ({});
    state.channels.back().channelId    = 2;
    state.channels.back().signalType   = ida::SignalType::Audio;
    state.channels.back().inputSourceId = 1;
    state.channels.back().tapeMode     = ida::TapeMode::NoTape;

    // engine accepts the all-NoTape import without complaint
    CHECK_NOTHROW (mixer.importGraphState (state));

    // No armed channels exist — predicate says "no channel id is disarmable"
    // (every channel is already NoTape, so disarming each is a no-op and
    // therefore allowed; the floor is conceptually violated but the engine
    // doesn't enforce on the snapshot itself).
    CHECK (mixer.channelTapeMode (ida::ChannelId { 1 }) == ida::TapeMode::NoTape);
    CHECK (mixer.channelTapeMode (ida::ChannelId { 2 }) == ida::TapeMode::NoTape);

    // Floor restoration is the LOAD HANDLER's job (Task 3 step 4 in the
    // bridge-slice plan). MainComponent::chooseFileAndLoad does it after
    // importGraphState returns.
}
```

- [ ] **Step 2: Run the test (should pass — it pins existing behaviour)**

```bash
cmake --build build --target IdaTests -j && \
  ./build/tests/IdaTests "[mixer-graph-state][tape-mode]" 2>&1 | tail -5
```

Expected: 1 test, all assertions pass (the test documents that the engine accepts the all-NoTape state; the load-handler is what restores the floor).

- [ ] **Step 3: Find the load-handler import site**

Search `app/MainComponent.cpp` for the block immediately after `inputMixer_->importGraphState (*loadedInputMixer);` in `chooseFileAndLoad` (around line 7263 per the prior session's continue.md). It currently has the channel-MON and (after the bus-MON slice) bus-MON replay blocks.

- [ ] **Step 4: Add the floor guard immediately after the existing replay blocks**

In `app/MainComponent.cpp`, immediately after the bus-MON replay block landed in commit `f6909e6` (the most recent block in this branch of `chooseFileAndLoad`), add:

```cpp
                        // Bridge slice (2026-05-25) — looper invariant floor.
                        // A session file may arrive with zero CommitToTape
                        // channels (externally edited, or a bug in a prior
                        // export). The engine's setChannelTapeMode prevents
                        // this state from being REACHED via gestures, but
                        // importGraphState is a faithful state replacement —
                        // it accepts whatever it's given. Restore the floor
                        // by arming the first channel if no other channel is
                        // armed after import.
                        if (loadedInputMixer.has_value())
                        {
                            bool anyArmed = false;
                            for (const auto& c : loadedInputMixer->channels)
                                if (c.tapeMode != ida::TapeMode::NoTape)
                                {
                                    anyArmed = true;
                                    break;
                                }
                            if (! anyArmed && ! loadedInputMixer->channels.empty())
                            {
                                const auto firstId = ida::ChannelId (
                                    loadedInputMixer->channels.front().channelId);
                                juce::ignoreUnused (
                                    inputMixer_->setChannelTapeMode (firstId,
                                                                     ida::TapeMode::CommitToTape));
                                captureBanner_->show (
                                    "Session contained no record-armed channels; armed channel 1.");
                            }
                        }
```

- [ ] **Step 5: Compile**

```bash
cmake --build build --target IDA -j 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 6: Commit + push**

```bash
git add app/MainComponent.cpp tests/MixerGraphStateTests.cpp
git commit -m "feat: chooseFileAndLoad — restore ≥1-channel-armed floor after corrupt-session import (bridge slice)"
git push origin master
```

---

## Task 4 — `InputMixerPane`: callback + state mirror + menu item + dim-when-NoTape

**Files:**
- Modify: `app/MainComponent.cpp` (`InputMixerPane` class — declarations near other callbacks, state-mirror near other parallel vectors, `setChannelTapeModes` pusher near `setMonitorModes`, `showToggleMenu` extension, `CompactFaderStrip`-or-equivalent dim rendering)

This task is UI-only — no unit tests (gesture/painting is operator-eyes-on per project convention). The five sub-edits are independent and can land together since they all live in the same class.

- [ ] **Step 1: Add the callback declaration**

In `app/MainComponent.cpp`, in the `InputMixerPane` class's public callback section (just below `onToggleStereoMono` at line 525), add:

```cpp
    /// Strip context-menu "Record to tape" toggle. `record` is the TARGET
    /// state (the inverse of what the menu showed). MainComponent translates
    /// to InputMixer::setChannelTapeMode(chId, record ? CommitToTape : NoTape)
    /// inside the audio-callback bracket, and shows the CaptureBanner refusal
    /// message if the engine returns false (looper-floor violation).
    std::function<void (int idx, bool record)> onToggleChannelRecording;
```

- [ ] **Step 2: Add the state mirror**

In the same class, near `monitorModes_` (line 1560 per the post-MON-slice layout), add:

```cpp
    /// Parallel to `strips_`. Mirrors engine's TapeMode for each input strip.
    /// Pushed by MainComponent::refreshInputDestinations via
    /// `setChannelTapeModes`. Used by `showToggleMenu` (for the check state of
    /// the "Record to tape" item) and by `setBoundsForStrip`/paint code that
    /// dims the strip face when the mode is NoTape.
    std::vector<ida::TapeMode> stripTapeModes_;
```

Also extend the strip-rebuild path: find every `strips_.clear()` / `strips_.push_back(...)` site in this class and add `stripTapeModes_.clear()` / `stripTapeModes_.push_back (ida::TapeMode::CommitToTape)` alongside. (Default-push CommitToTape matches the strip-creation seed in MainComponent.cpp:6425.) Mirror the exact sites where `monitorModes_` is treated the same way — there's already a pattern to copy.

- [ ] **Step 3: Add the `setChannelTapeModes` pusher**

In the same class, immediately below `setMonitorModes` (line 782 per the post-MON-slice layout), add:

```cpp
    /// Refresh strip TapeMode state from the engine. `modes` is parallel to
    /// `strips_`. Called by MainComponent::refreshInputDestinations after any
    /// engine-side change (load, gesture relay). Updates the local mirror,
    /// pushes the dim state to each strip, and triggers a repaint.
    void setChannelTapeModes (const std::vector<ida::TapeMode>& modes)
    {
        const auto n = std::min (modes.size(), strips_.size());
        stripTapeModes_.assign (modes.begin(), modes.begin() + static_cast<std::ptrdiff_t> (n));
        // Pad if fewer modes than strips (defensive; shouldn't happen in
        // practice — modes.size() should equal strips_.size()).
        while (stripTapeModes_.size() < strips_.size())
            stripTapeModes_.push_back (ida::TapeMode::CommitToTape);

        for (std::size_t i = 0; i < strips_.size(); ++i)
        {
            const bool dim = (stripTapeModes_[i] == ida::TapeMode::NoTape);
            strips_[i]->setTapeModeDimming (dim);
        }
    }
```

- [ ] **Step 4: Extend `showToggleMenu`**

Find `showToggleMenu` at line 1365 in `app/MainComponent.cpp`. Replace its body with:

```cpp
    void showToggleMenu (int idx, juce::Point<int> screenPos)
    {
        if (idx < 0 || idx >= stripCount()) return;
        juce::PopupMenu menu;

        // Bridge slice (2026-05-25) — per-channel "Record to tape" toggle.
        // Check state mirrors engine TapeMode: CommitToTape (or any
        // non-NoTape mode) reads as checked. Selecting flips to the inverse
        // and fires onToggleChannelRecording with the TARGET state.
        const bool currentlyRecording =
            (idx < static_cast<int> (stripTapeModes_.size()))
                ? stripTapeModes_[static_cast<std::size_t> (idx)] != ida::TapeMode::NoTape
                : true;   // defensive default — strip without TapeMode mirror reads as armed
        menu.addItem ("Record to tape",
                      /*enabled*/ true,
                      /*checked*/ currentlyRecording,
                      [this, idx, currentlyRecording]
                      {
                          if (onToggleChannelRecording)
                              onToggleChannelRecording (idx, ! currentlyRecording);
                      });

        menu.addSeparator();

        const bool stereo = stripStereo_[static_cast<std::size_t> (idx)];
        menu.addItem (stereo ? "Split to two mono channels" : "Collapse to stereo",
                      [this, idx] { if (onToggleStereoMono) onToggleStereoMono (idx); });

        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
    }
```

- [ ] **Step 5: Add `setTapeModeDimming` on the strip class**

Find the strip class used by `InputMixerPane` — it's `otto::ui::CompactFaderStrip`. The dim is a per-instance background tint, so the cleanest place to apply it is at the InputMixerPane wrapper level, not inside `CompactFaderStrip` itself (which is OTTO-vendored — see `[[project_otto_is_a_submodule_now]]` — and should stay byte-faithful).

Instead, add a `juce::Component`-derived sibling overlay or wrap the strip in a one-method shim. Simpler: paint a translucent dim rectangle in `InputMixerPane::paintOverChildren` (or `paint` if no overlay layer exists) for every strip whose `stripTapeModes_[i] == NoTape`.

Find `InputMixerPane::paint` (search for `paint (juce::Graphics&` inside the InputMixerPane class). After its existing body, add (or create the override if it doesn't exist):

```cpp
    void paintOverChildren (juce::Graphics& g) override
    {
        // Bridge slice (2026-05-25) — dim the faceplate of any NoTape strip
        // so the operator can see at a glance which channels will be silent
        // on capture. 30 % luminance reduction via translucent dark overlay
        // — equivalent to withMultipliedBrightness(0.7f) on the underlying
        // colours. Painted over children so it covers the strip body but
        // leaves the MON / INS / dest button / fader / mute / solo controls
        // legible (those are top-aligned in the strip; the dim only covers
        // the strip's central faceplate band — see strip layout in
        // CompactFaderStrip::resized).
        for (std::size_t i = 0; i < strips_.size() && i < stripTapeModes_.size(); ++i)
        {
            if (stripTapeModes_[i] != ida::TapeMode::NoTape) continue;
            // Strip's bounds in pane coordinates.
            const auto stripBounds = strips_[i]->getBounds();
            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.fillRect (stripBounds);
        }
    }
```

If `InputMixerPane` already has `paintOverChildren`, integrate the loop into it. Also remove the `setTapeModeDimming` call from `setChannelTapeModes` (step 3) — the dim is paint-time, not state-on-strip, so the pusher just triggers a `repaint()` instead:

```cpp
    void setChannelTapeModes (const std::vector<ida::TapeMode>& modes)
    {
        const auto n = std::min (modes.size(), strips_.size());
        stripTapeModes_.assign (modes.begin(), modes.begin() + static_cast<std::ptrdiff_t> (n));
        while (stripTapeModes_.size() < strips_.size())
            stripTapeModes_.push_back (ida::TapeMode::CommitToTape);
        repaint();
    }
```

(This replaces step 3's pusher body. Step 4's `showToggleMenu` still reads `stripTapeModes_` correctly.)

- [ ] **Step 6: Compile (sanity check — the menu wiring + paint don't have unit tests)**

```bash
cmake --build build --target IDA -j 2>&1 | tail -5
```

Expected: clean. There may be a warning if `paintOverChildren` was already overridden — if so, integrate the loop into the existing override (don't re-declare).

- [ ] **Step 7: Commit + push**

```bash
git add app/MainComponent.cpp
git commit -m "feat: InputMixerPane — strip Record-to-tape menu item + dim-when-NoTape paint overlay (bridge slice)"
git push origin master
```

---

## Task 5 — `MainComponent` wiring: callback handler + refresh push

**Files:**
- Modify: `app/MainComponent.cpp` (callback wiring near `onBusMonitorModeChanged` at line 3988; refresh push near the MON-modes block in `refreshInputDestinations`)

- [ ] **Step 1: Wire `onToggleChannelRecording`**

In `app/MainComponent.cpp`, near where the other `inputMixerPane_->on*` callbacks are bound (search for `inputMixerPane_->onBusMonitorModeChanged = [this]` at line 3988 — wire immediately after that block), add:

```cpp
        inputMixerPane_->onToggleChannelRecording = [this] (int idx, bool record)
        {
            if (idx < 0 || idx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (idx)];
            const auto target = record ? ida::TapeMode::CommitToTape : ida::TapeMode::NoTape;

            // Bridge slice (2026-05-25) — bracket the audio callback. The
            // setter is metadata-only (no graph topology change), so the
            // bracket is defensive; matches the pattern every other input-
            // side config mutator on this pane uses.
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            const bool ok = inputMixer_->setChannelTapeMode (chId, target);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());

            if (! ok)
            {
                // Engine refused — looper-floor violation (this was the last
                // armed channel). Surface via the existing CaptureBanner.
                captureBanner_->show ("At least one channel must record to a tape.");
                return;
            }

            // Refresh the destination row so the menu's check state + dim
            // overlay update on next gesture.
            refreshInputDestinations();
        };
```

- [ ] **Step 2: Add the refresh push in `refreshInputDestinations`**

Find `refreshInputDestinations` at line 6242 in `app/MainComponent.cpp`. Immediately after the per-channel-MON push block (the `inputMixerPane_->setMonitorModes (monitorModes);` line near line 6319) and BEFORE the bus-MON push block, add:

```cpp
    // Bridge slice (2026-05-25) — push engine TapeMode into the pane so the
    // strip context menu's "Record to tape" check state stays in sync with
    // engine state (loaded projects, programmatic edits) and the dim overlay
    // tracks NoTape strips.
    {
        std::vector<ida::TapeMode> tapeModes;
        tapeModes.reserve (inputStripChannelIds_.size());
        for (const auto& chId : inputStripChannelIds_)
            tapeModes.push_back (inputMixer_->channelTapeMode (chId));
        inputMixerPane_->setChannelTapeModes (tapeModes);
    }
```

- [ ] **Step 3: Compile + full ctest**

```bash
cmake --build build --target IDA IdaTests -j && \
  ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: clean build, all tests pass (count = 727 baseline + 7 from this slice = 734).

- [ ] **Step 4: Commit + push**

```bash
git add app/MainComponent.cpp
git commit -m "feat: MainComponent — onToggleChannelRecording wiring + setChannelTapeModes refresh push (bridge slice)"
git push origin master
```

---

## Task 6 — Clean rebuild + operator eyes-on recipe

**Files:**
- Modify: `continue.md` (refresh the handoff with the bridge-slice completion + operator-verify recipe)
- Modify: `todo.md` (remove the "input→output bridge slice" entry — its scope is now fully satisfied)

- [ ] **Step 1: Clean rebuild per CLAUDE.md**

```bash
rm -rf build && \
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && \
  cmake --build build --target IDA -j 2>&1 | tail -3
```

Expected: clean Release build (only the standing ld duplicate-libs warning).

- [ ] **Step 2: Full ctest on the clean build**

```bash
cmake --build build --target IdaTests -j && \
  ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" 2>&1 | tail -3
```

Expected: 734/734 tests pass.

- [ ] **Step 3: Refresh `continue.md` with the operator-verify recipe**

Replace the existing recipe block in `continue.md` §1 (currently the bus-MON 7-step) with the bridge-slice 6-step:

```
1. Launch IDA via the Desktop alias.
2. Right-click (desktop) or long-press (iOS) on input strip 1. The menu
   shows "☑ Record to tape" (checked, since strips default armed).
3. Click "Record to tape" — the strip's faceplate dims; reopen the menu
   to see "☐ Record to tape" (unchecked).
4. Repeat on strip 2, then strip 3, until only one strip is armed.
   Try to uncheck the last armed strip — the CaptureBanner appears at
   the top of the window: "At least one channel must record to a tape."
   The strip stays armed (faceplate not dimmed); the menu re-checks.
5. Re-arm a previously-disarmed strip by clicking "Record to tape"
   again — the dim lifts. Save the session.
6. Quit, relaunch, load the session. The disarmed strips reload dimmed;
   the armed strips reload at full brightness. Refusal banner does not
   fire on load.
```

- [ ] **Step 4: Remove the deferral entry from `todo.md`**

In `/Users/larryseyer/IDA/todo.md`, find and delete the entire entry headed:

```
### 2026-05-22 — input→output bridge slice: ≥1-channel→≥1-tape enforcement + per-channel direct-out opt-out (MOVED here from "slice 4")
```

(Including its bullet body — typically 8-12 lines.)

- [ ] **Step 5: Commit + push**

```bash
git add continue.md todo.md
git commit -m "docs: continue.md — bridge slice complete; operator-verify recipe + todo.md deferral removed"
git push origin master
```

End of slice. Hand off to the operator with: "Bridge slice landed across 6 commits, ending at HEAD <sha>. Clean Release build on disk. Walk through the recipe in continue.md."

---

## Self-review notes

**Spec coverage:** §2.1 (gesture surface) → Task 4 step 4. §2.2 (dim) → Task 4 step 5. §2.3 (refusal banner) → Task 5 step 1. §3.1 (predicate) → Task 2 step 3-4. §3.2 (bool setter) → Task 2 step 3-4. §4 (persistence) → verified during plan-writing, no task needed. §5 (UI contract) → Task 4 + Task 5. §6.1 (NonDestructive preservation) → handled by the spec's documented behaviour (operator-facing toggle is two-state; programmatic NonDestructive survives until the operator toggles off); no extra task. §6.2 (load-handler floor) → Task 3. §6.3 (strip vs channel count) → covered by the channel-count predicate in Task 2 + the strip mirror in Task 4 step 2. §6.4 (first-channel-armed) → covered by setter's "re-arm always succeeds" branch (Task 2 step 4) + tested by Task 2 step 1 case 6. §7 tests all mapped to Task 2 step 1.

**Placeholder scan:** no TBD / TODO / "appropriate error handling" / "similar to" / type references undefined elsewhere. Step 5 of Task 4 has a layout-discovery note ("find every `strips_.clear()` site") rather than a verbatim line number — that's intentional (the strip-rebuild sites are multiple and pattern-match the existing `monitorModes_` treatment, which the implementer follows as the canonical example).

**Type consistency:** `TapeMode`, `ChannelId`, `InputMixer::setChannelTapeMode`, `InputMixer::canDisarmChannelRecording`, `InputMixer::channelTapeMode`, `InputMixerPane::onToggleChannelRecording`, `InputMixerPane::setChannelTapeModes`, `InputMixerPane::stripTapeModes_`, `captureBanner_->show`, `inputStripChannelIds_` — all names match across declaration and usage sites and against the existing codebase symbols verified during plan-writing.
