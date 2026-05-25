# Whitepaper V9 Conformance — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the IDA engine + UI + persistence into conformance with whitepaper V9 (commit `0f49ee3`): collapse `MonitorMode` to a bool, delete the DirectLayer's bypass-the-OutputMixer write path, route MON-on signals through an auto-created OutputMixer channel (the §5.2 / §7.2 spec), and replace the input strip's three-state cycle button with a two-state MON toggle.

**Architecture:** Replace the DirectLayer's direct-to-output-buffer write with a **pointer-seam handoff**. InputMixer exposes its per-channel post-strip buffer as a stable pointer; when MON goes on for an input, InputMixer creates an OutputMixer channel and wires it via `setChannelAudioSource(channelId, postStripL, postStripR)` (the seam landed in `f6d894b`); when MON goes off, the channel is removed. The DirectLayer module is deleted entirely. The Output Mixer becomes the single funnel for everything reaching the hardware outputs (the whitepaper §6.6 / §7.1 I/O ownership rule).

**Tech Stack:** C++17, JUCE, Catch2, CMake/Ninja. Existing audio-thread invariants (no allocation, no locks, no I/O on the hot path) hold throughout.

**Out of scope for this plan** (designed in V9 but blocked on later milestones):
- **Dry-tap → FX migration to phrase's local effects.** Depends on the tape→phrase-Constituent capture path landing (M6+); plan adds the API surface (Slice 6) and tags the hook site for the next person.
- **MIDI input live-through to OutputMixer VST channel.** MIDI is not wired into `AudioCallback` at all yet; the V9 design is forward-compatible but no work happens here.
- **Per-channel UI for Tape on/off + wet-tap/dry-tap toggles.** The engine's `TapeMode` enum already supports the model (`CommitToTape` ↔ wet, `NonDestructive` ↔ dry, `NoTape` ↔ off); exposing those in the strip UI is a separate UI slice.

---

## File map

**Modify (engine):**
- `core/include/ida/MonitorMode.h` — collapse enum
- `engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp` — post-strip buffer surface + MON lifecycle owns an OutputMixer channel
- `engine/include/ida/OutputMixer.h` — drop DirectLayer references in comments
- `core/include/ida/Constituent.h` — add `withEffectChainClonedFrom` helper

**Delete:**
- `engine/include/ida/DirectLayer.h`, `engine/src/DirectLayer.cpp`
- `tests/DirectLayerTests.cpp`
- DirectLayer references in `engine/CMakeLists.txt`, `tests/CMakeLists.txt`

**Modify (audio):**
- `audio/include/ida/AudioCallback.h` — drop `setMonitoringEnabled` / `isMonitoringEnabled`
- `audio/src/AudioCallback.cpp` — drop `dispatchDirectLayer` and its invocation

**Modify (persistence):**
- `persistence/src/SessionFormat.cpp` — new wire tokens `"Off"` / `"On"`; reader accepts legacy `"Raw"` / `"Processed"` as `On`

**Modify (UI):**
- `app/MainComponent.cpp` — two-state MON button; drop the right-click + long-press scaffolding for the deleted Raw state

**Modify (tests):**
- `tests/SessionFormatMonitorModeTests.cpp` — new tokens + back-compat
- `tests/InputMixerMonitorModeLifecycleTests.cpp` — two-state lifecycle + new OutputMixer-channel ownership assertions
- `tests/AudioCallbackTests.cpp` — drop DirectLayer scenarios
- New: `tests/InputMixerPostStripBufferTests.cpp` (Slice 2)
- New: `tests/InputMixerMonOutputChannelTests.cpp` (Slice 3)
- New: `tests/ConstituentEffectChainCloneTests.cpp` (Slice 6)

**Modify (docs):**
- `CLAUDE.md` — bump whitepaper reference from V7/V8 to V9
- `continue.md` — session handoff refresh

---

## Slice 1: Collapse `MonitorMode` to bool

**Files:**
- Modify: `core/include/ida/MonitorMode.h`
- Modify: `persistence/src/SessionFormat.cpp:951-968` (token helpers), `:989-1016` (round-trip)
- Modify: `tests/SessionFormatMonitorModeTests.cpp`

- [ ] **Step 1: Read MonitorMode.h to confirm current shape**

Run: `cat core/include/ida/MonitorMode.h`
Expected: enum class with `Off = 0`, `Raw`, `Processed`.

- [ ] **Step 2: Write the failing test — V9 wire tokens**

Append to `tests/SessionFormatMonitorModeTests.cpp` (or create if necessary):

```cpp
TEST_CASE("SessionFormat V9 MonitorMode wire tokens", "[session][monitor]")
{
    SECTION("writes Off as \"Off\"")
    {
        auto token = ida::SessionFormat::monitorModeToken(ida::MonitorMode::Off);
        REQUIRE(token == "Off");
    }
    SECTION("writes On as \"On\"")
    {
        auto token = ida::SessionFormat::monitorModeToken(ida::MonitorMode::On);
        REQUIRE(token == "On");
    }
    SECTION("reads \"On\" as On")
    {
        REQUIRE(ida::SessionFormat::monitorModeFromString("On") == ida::MonitorMode::On);
    }
    SECTION("reads legacy \"Processed\" as On (V8 back-compat)")
    {
        REQUIRE(ida::SessionFormat::monitorModeFromString("Processed") == ida::MonitorMode::On);
    }
    SECTION("reads legacy \"Raw\" as On (V8 back-compat — collapsed in V9)")
    {
        REQUIRE(ida::SessionFormat::monitorModeFromString("Raw") == ida::MonitorMode::On);
    }
}
```

- [ ] **Step 3: Run the test — must fail to compile (`MonitorMode::On` does not exist)**

Run: `cmake --build build --target IdaTests`
Expected: compile error referencing `MonitorMode::On`.

- [ ] **Step 4: Collapse the enum**

Replace the enum body in `core/include/ida/MonitorMode.h`:

```cpp
enum class MonitorMode
{
    Off = 0,
    On
};
```

Update the file-level doc-comment above the enum to describe V9's single-mode semantics (post-strip tap, auto-creates an OutputMixer channel — cite whitepaper §7.2). Drop all `Raw` / `Processed` discussion.

- [ ] **Step 5: Update `SessionFormat.cpp` token helpers**

In `persistence/src/SessionFormat.cpp:951-968`:

```cpp
juce::String monitorModeToken (MonitorMode m) noexcept
{
    switch (m)
    {
        case MonitorMode::Off: return "Off";
        case MonitorMode::On:  return "On";
    }
    return "Off";
}

MonitorMode monitorModeFromString (const juce::String& s) noexcept
{
    if (s == "On" || s == "Raw" || s == "Processed") return MonitorMode::On;
    return MonitorMode::Off;  // covers "Off" and any unknown token
}
```

Update the back-compat comment near `:1013-1016` to note that V8's `"Raw"` / `"Processed"` tokens collapse to `On` on read.

- [ ] **Step 6: Build and run the test**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R SessionFormat -V`
Expected: all `SessionFormat V9 MonitorMode wire tokens` sections pass.

- [ ] **Step 7: Sweep all callers of `MonitorMode::Raw` / `MonitorMode::Processed`**

Run: `grep -rnE "MonitorMode::(Raw|Processed)" --include="*.cpp" --include="*.h" .` (exclude `build/`, `external/`)
Expected: any remaining hits are in code about to be deleted (DirectLayer in Slice 4, UI in Slice 5) or in `InputMixer` / `AudioCallback` that needs adapting in later slices. List them and confirm none are left dangling after Slice 5 lands.

- [ ] **Step 8: Commit**

```bash
git add core/include/ida/MonitorMode.h persistence/src/SessionFormat.cpp tests/SessionFormatMonitorModeTests.cpp
git commit -m "$(cat <<'EOF'
refactor: V9 — collapse MonitorMode to Off|On; legacy tokens read as On

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 2: Expose InputMixer's per-channel post-strip buffer as a stable pointer

**Files:**
- Modify: `engine/include/ida/InputMixer.h` — add `postStripPointer(ChannelId, int side) const noexcept`
- Modify: `engine/src/InputMixer.cpp` — allocate per-channel post-strip storage; write into it during `renderInputGraph` / `processBuffer`; free on channel-remove
- New: `tests/InputMixerPostStripBufferTests.cpp`

The post-strip buffer is the seam Slice 3 will hand to `OutputMixer::setChannelAudioSource`. It must be stable (same pointer for the channel's lifetime) and RT-safe (no allocation inside the audio thread).

- [ ] **Step 1: Write the failing test**

Create `tests/InputMixerPostStripBufferTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ida/InputMixer.h"
#include "ida/InputDescriptor.h"

TEST_CASE("InputMixer post-strip buffer lifecycle", "[input-mixer][post-strip]")
{
    ida::InputMixer mixer;
    mixer.prepare(48000.0, 256, 2);  // sr, blockSize, maxChannels

    ida::InputDescriptor desc;
    desc.inputIndexL = 0;
    desc.inputIndexR = 1;
    desc.rawDirectMonitor = false;
    desc.enabled = true;
    auto chId = mixer.addChannel(desc);

    SECTION("pointer is non-null while channel exists")
    {
        REQUIRE(mixer.postStripPointer(chId, 0) != nullptr);
        REQUIRE(mixer.postStripPointer(chId, 1) != nullptr);
    }
    SECTION("pointer becomes null after channel removal")
    {
        mixer.removeChannel(chId);
        REQUIRE(mixer.postStripPointer(chId, 0) == nullptr);
    }
    SECTION("pointer is stable across renderInputGraph calls")
    {
        const float* p0 = mixer.postStripPointer(chId, 0);
        float dummyIn[256] = {0};
        const float* inputs[2] = { dummyIn, dummyIn };
        mixer.renderInputGraph(inputs, 2, nullptr, 0, 256);
        REQUIRE(mixer.postStripPointer(chId, 0) == p0);  // same pointer
    }
}
```

- [ ] **Step 2: Run test — must fail to compile (`postStripPointer` does not exist)**

Run: `cmake --build build --target IdaTests`
Expected: compile error referencing `postStripPointer`.

- [ ] **Step 3: Add the API surface to `InputMixer.h`**

In `engine/include/ida/InputMixer.h`, near the other message-thread accessors (~line 234 area), add:

```cpp
/// Message-thread accessor. Returns a stable pointer to the channel's
/// post-strip output buffer for the requested side (0=L, 1=R), or
/// `nullptr` if no channel with that id exists. The pointer remains
/// valid until `removeChannel(id)` or destruction. Audio-thread readers
/// (OutputMixer) cache the pointer once and re-read into it every block.
///
/// Slice 2 of the V9 conformance plan introduced this seam; Slice 3
/// uses it to wire MON-on to an auto-created OutputMixer channel via
/// `OutputMixer::setChannelAudioSource(monChannelId, L, R)`.
const float* postStripPointer (ChannelId id, int side) const noexcept;
```

Add a per-channel `std::array<std::vector<float>, 2>` post-strip storage member alongside `channels_` (or extend the existing per-channel state struct).

- [ ] **Step 4: Allocate the storage on channel add**

In `engine/src/InputMixer.cpp`, in the body of `addChannel` (find it via `git grep -n "addChannel" engine/src/InputMixer.cpp`), reserve both sides to `preparedBlockSize_` (a member stashed by `prepare()`):

```cpp
postStrip_[id.value()][0].assign(preparedBlockSize_, 0.0f);
postStrip_[id.value()][1].assign(preparedBlockSize_, 0.0f);
```

Free it in `removeChannel`:

```cpp
postStrip_.erase(id.value());
```

- [ ] **Step 5: Wire the strip's output into the post-strip buffer in `renderInputGraph`**

After the channel's `ChannelStrip<Audio>::process(...)` call in the per-channel loop of `renderInputGraph`, **copy** the strip's output into the post-strip buffer (use `std::memcpy` — RT-safe; the size is bounded by `preparedBlockSize_` and the inner loop is already non-allocating):

```cpp
auto& dst = postStrip_[id.value()];
std::memcpy(dst[0].data(), stripOutLeft,  numSamples * sizeof(float));
std::memcpy(dst[1].data(), stripOutRight, numSamples * sizeof(float));
```

If the strip writes directly into a scratch buffer the InputMixer already owns, point `dst` *at* that scratch instead of memcpy-ing — but verify the scratch survives across blocks (it must be a member, not a stack buffer).

- [ ] **Step 6: Add the accessor implementation**

In `engine/src/InputMixer.cpp`:

```cpp
const float* InputMixer::postStripPointer (ChannelId id, int side) const noexcept
{
    if (side < 0 || side > 1) return nullptr;
    auto it = postStrip_.find(id.value());
    if (it == postStrip_.end()) return nullptr;
    return it->second[side].data();
}
```

- [ ] **Step 7: Run the test**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R InputMixerPostStripBuffer -V`
Expected: all sections pass.

- [ ] **Step 8: Run the full suite — confirm no regression**

Run: `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"`
Expected: 730 pass (the previous 729 baseline + the new test).

- [ ] **Step 9: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerPostStripBufferTests.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: InputMixer exposes per-channel post-strip buffer as stable pointer (V9 Slice 2)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 3: Auto-create / remove an OutputMixer channel from InputMixer's MON state

**Files:**
- Modify: `engine/include/ida/InputMixer.h` — add `attachOutputMixer(OutputMixer&)`; extend `MonitorRouteState` with `outputChannelId`
- Modify: `engine/src/InputMixer.cpp` — `setChannelMonitorMode` now adds/removes an OutputMixer channel and wires `setChannelAudioSource`
- Modify: `tests/InputMixerMonitorModeLifecycleTests.cpp` — replace tri-state DirectLayer route assertions with two-state OutputMixer-channel assertions
- New: `tests/InputMixerMonOutputChannelTests.cpp`

This is the core V9 mechanism — the MON button's effect is now "create an OutputMixer channel that reads my post-strip buffer," not "register a DirectLayer route."

- [ ] **Step 1: Write the failing test**

Create `tests/InputMixerMonOutputChannelTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ida/InputMixer.h"
#include "ida/OutputMixer.h"
#include "ida/InputDescriptor.h"

TEST_CASE("InputMixer MON owns an OutputMixer channel", "[input-mixer][mon][output-mixer]")
{
    ida::OutputMixer output;
    output.prepare(48000.0, 256);

    ida::InputMixer input;
    input.prepare(48000.0, 256, 2);
    input.attachOutputMixer(output);

    ida::InputDescriptor desc;
    desc.inputIndexL = 0;
    desc.inputIndexR = 1;
    desc.enabled = true;
    auto chId = input.addChannel(desc);

    SECTION("MON off → no OutputMixer channel created for this input")
    {
        REQUIRE(output.channelCount() == 0);
    }

    SECTION("MON on → creates exactly one OutputMixer channel")
    {
        input.setChannelMonitorMode(chId, ida::MonitorMode::On);
        REQUIRE(output.channelCount() == 1);
    }

    SECTION("MON off after MON on → removes the OutputMixer channel")
    {
        input.setChannelMonitorMode(chId, ida::MonitorMode::On);
        input.setChannelMonitorMode(chId, ida::MonitorMode::Off);
        REQUIRE(output.channelCount() == 0);
    }

    SECTION("MON on twice is idempotent")
    {
        input.setChannelMonitorMode(chId, ida::MonitorMode::On);
        auto firstCount = output.channelCount();
        input.setChannelMonitorMode(chId, ida::MonitorMode::On);
        REQUIRE(output.channelCount() == firstCount);
    }

    SECTION("removing the input channel while MON is on cleans up the output channel")
    {
        input.setChannelMonitorMode(chId, ida::MonitorMode::On);
        input.removeChannel(chId);
        REQUIRE(output.channelCount() == 0);
    }
}
```

- [ ] **Step 2: Run test — must fail to compile (`attachOutputMixer` does not exist)**

Run: `cmake --build build --target IdaTests`
Expected: compile error on `attachOutputMixer`.

- [ ] **Step 3: Add `attachOutputMixer` to InputMixer.h**

```cpp
/// Wires the InputMixer to an OutputMixer. The MON button's lifecycle
/// owns an auto-created channel on this OutputMixer; the InputMixer
/// uses the OutputMixer's setChannelAudioSource() seam to hand off
/// post-strip buffer pointers per whitepaper V9 §5.2 / §7.2.
///
/// Non-owning. The OutputMixer must outlive the InputMixer. Call
/// before the audio thread starts.
void attachOutputMixer (OutputMixer& output) noexcept;
```

Extend `MonitorRouteState` (struct at `engine/include/ida/InputMixer.h:335-341`):

```cpp
struct MonitorRouteState
{
    MonitorMode      mode { MonitorMode::Off };
    OutputChannelId  outputChannelId {};      // valid when mode == On
};
```

Drop the old `route` / `outputPair` fields — the DirectLayer route concept is gone.

- [ ] **Step 4: Update `setChannelMonitorMode` in InputMixer.cpp**

Replace the body with the new auto-channel-ownership logic:

```cpp
void InputMixer::setChannelMonitorMode (ChannelId id, MonitorMode mode)
{
    if (channels_.find(id.value()) == channels_.end()) return;  // unknown channel

    auto& route = channelMonitorRoutes_[id.value()];

    if (mode == MonitorMode::Off)
    {
        if (route.outputChannelId.isValid() && outputMixer_ != nullptr)
        {
            outputMixer_->removeChannel(route.outputChannelId);
        }
        channelMonitorRoutes_.erase(id.value());
        return;
    }

    // mode == On
    if (route.outputChannelId.isValid()) return;  // idempotent — already wired

    if (outputMixer_ == nullptr) return;  // not attached — silent no-op

    auto monChId = outputMixer_->addChannel(SignalType::Audio);
    outputMixer_->setChannelAudioSource(monChId,
                                        postStripPointer(id, 0),
                                        postStripPointer(id, 1));
    route.mode = MonitorMode::On;
    route.outputChannelId = monChId;
}
```

Also update the existing `outputPair` parameter to be gone from the signature (`setChannelMonitorMode(ChannelId, MonitorMode)` only).

- [ ] **Step 5: Clean up channel destruction**

In `removeChannel(ChannelId id)` (find via `git grep -n "void InputMixer::removeChannel" engine/src/InputMixer.cpp`), also remove the OutputMixer channel if MON was on:

```cpp
if (auto it = channelMonitorRoutes_.find(id.value()); it != channelMonitorRoutes_.end())
{
    if (it->second.outputChannelId.isValid() && outputMixer_ != nullptr)
        outputMixer_->removeChannel(it->second.outputChannelId);
    channelMonitorRoutes_.erase(it);
}
```

In the destructor (`~InputMixer`), do the same sweep across every entry in `channelMonitorRoutes_`.

- [ ] **Step 6: Run the test**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R InputMixerMonOutputChannel -V`
Expected: all 5 sections pass.

- [ ] **Step 7: Update `tests/InputMixerMonitorModeLifecycleTests.cpp`**

Read the existing test; rewrite each section that asserted DirectLayer route presence to instead assert `outputMixer.channelCount()`. Drop any case testing `MonitorMode::Raw` — it no longer exists.

- [ ] **Step 8: Run the full suite — confirm no regression**

Run: `ctest --test-dir build -E "(PluginEditor|MainComponentPlug|DirectLayer)"`
(Exclude DirectLayer tests temporarily — they'll be deleted in Slice 4.)
Expected: every test passes.

- [ ] **Step 9: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerMonitorModeLifecycleTests.cpp tests/InputMixerMonOutputChannelTests.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: MON now owns an auto-created OutputMixer channel via setChannelAudioSource (V9 Slice 3)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 4: Delete the DirectLayer module

**Files:**
- Delete: `engine/include/ida/DirectLayer.h`, `engine/src/DirectLayer.cpp`
- Delete: `tests/DirectLayerTests.cpp`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt` — drop DirectLayer sources
- Modify: `audio/include/ida/AudioCallback.h:148` — drop `setMonitoringEnabled` / `isMonitoringEnabled`
- Modify: `audio/src/AudioCallback.cpp:38-87` — delete `dispatchDirectLayer`; `:154-164` — delete the gated invocation
- Modify: `engine/include/ida/OutputMixer.h:327-331` — drop the comment about DirectLayer routes

Now that MON's signal path is alive via Slice 3, the DirectLayer is dead weight.

- [ ] **Step 1: Verify nothing else still references DirectLayer**

Run: `grep -rnE "DirectLayer|dispatchDirectLayer|setMonitoringEnabled" --include="*.cpp" --include="*.h" --include="*.cmake" --include="CMakeLists.txt" . | grep -v build/ | grep -v external/`
Expected: only the files listed above; if anything else surfaces, deal with it before the delete.

- [ ] **Step 2: Delete the files**

```bash
git rm engine/include/ida/DirectLayer.h engine/src/DirectLayer.cpp tests/DirectLayerTests.cpp
```

- [ ] **Step 3: Drop DirectLayer from `engine/CMakeLists.txt`**

Find the line(s) referencing `DirectLayer.h` or `DirectLayer.cpp` and remove them.

- [ ] **Step 4: Drop DirectLayerTests from `tests/CMakeLists.txt`**

Find the line registering `DirectLayerTests.cpp` and remove it.

- [ ] **Step 5: Drop `dispatchDirectLayer` and its invocation from AudioCallback**

In `audio/src/AudioCallback.cpp` delete:
- The `dispatchDirectLayer(...)` definition (the block at `:38-87`).
- The invocation in the audio callback (at `:154-164` — the `if (monitoringEnabled_) { dispatchDirectLayer(...); }` block).

In `audio/include/ida/AudioCallback.h`:
- Delete `setMonitoringEnabled` / `isMonitoringEnabled` at line 148-155.
- Delete the `monitoringEnabled_` member.
- Delete any `DirectLayer*` member if present.
- Delete `#include "ida/DirectLayer.h"`.

- [ ] **Step 6: Drop the DirectLayer-route comment in `OutputMixer.h`**

In `engine/include/ida/OutputMixer.h:327-331`, replace the comment about DirectLayer routes with V9-accurate language pointing to InputMixer's MON ownership.

- [ ] **Step 7: Build clean and run the full suite**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"
```

Expected: clean build, all tests pass. The DirectLayer test suite is gone (~handful of tests dropped); new InputMixerMonOutputChannelTests + InputMixerPostStripBufferTests add tests; net count should land at ≈ 730 pass.

- [ ] **Step 8: Commit**

```bash
git add -u && git add engine/CMakeLists.txt tests/CMakeLists.txt audio/
git commit -m "$(cat <<'EOF'
refactor: delete DirectLayer module; MON path is now via auto-created OutputMixer channel (V9 Slice 4)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 5: Replace the three-state cycle button with a two-state MON toggle

**Files:**
- Modify: `app/MainComponent.cpp` — strip rendering, click handler, label/tooltip

- [ ] **Step 1: Two-state label/tooltip rendering**

In `app/MainComponent.cpp:1469-1494`, replace `setMonitorModeAt` with a two-state version:

```cpp
void MainComponent::setMonitorModeAt (int idx, ida::MonitorMode mode, bool notify)
{
    if (idx < 0 || idx >= (int) mons_.size() || mons_[idx] == nullptr) return;
    auto* btn = mons_[idx];
    switch (mode)
    {
        case ida::MonitorMode::Off:
            btn->setButtonText("Off");
            btn->setTooltip("MON off — you do not hear this input through IDA. "
                            "Click to enable monitoring.");
            break;
        case ida::MonitorMode::On:
            btn->setButtonText("MON");
            btn->setTooltip("MON on — you hear this input through IDA, "
                            "post-strip processing. Click to disable.");
            break;
    }
    if (notify && onMonitorModeChanged_)
        onMonitorModeChanged_(idx, mode);
}
```

- [ ] **Step 2: Two-state click handler**

In `app/MainComponent.cpp:1498-1504`, replace `cycleMonitorModeAt` with:

```cpp
void MainComponent::cycleMonitorModeAt (int idx)
{
    auto current = monitorModeAt(idx);
    auto next = (current == ida::MonitorMode::Off) ? ida::MonitorMode::On
                                                   : ida::MonitorMode::Off;
    setMonitorModeAt(idx, next, /*notify=*/true);
}
```

- [ ] **Step 3: Drop the right-click + long-press scaffolding**

Find any code (look near `:1514-1515`) that wired right-click or long-press to cycle through Raw — delete it. The single click is the only gesture now.

- [ ] **Step 4: Drop the `outputPair` parameter at the call site**

Find the callback that calls `inputMixer_->setChannelMonitorMode(...)` (around `:1750s` per Explore report). Update to the new single-arg form `setChannelMonitorMode(chId, mode)`.

- [ ] **Step 5: Clean rebuild + launch the app**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
open ~/Desktop/IDA
```

- [ ] **Step 6: Operator verification (cannot be unit-tested)**

Verify visually:
- Each input strip shows a single MON button with two states.
- Click toggles `Off ↔ MON`. No third state exists.
- With MON on, the performer hears the input through main outputs (alongside any phrase playback once phrases are wired at M6+).
- With MON off, the input is silent through IDA.
- Master meter is no longer dead while MON is on — the auto-created OutputMixer channel funnels through master.

If any of the above fails, surface it; the operator-verified behavior is the contract.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "$(cat <<'EOF'
feat: input strip MON button is two-state (Off ↔ MON); drop tri-state cycle (V9 Slice 5)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 6: Add `Constituent::withEffectChainClonedFrom` for the future dry-tap FX-migration hook

**Files:**
- Modify: `core/include/ida/Constituent.h` — add the builder
- New: `tests/ConstituentEffectChainCloneTests.cpp`

The whitepaper V9 §6.3.2 commits: *"when tape tap = dry, the Input strip's FX chain is copied to the resulting phrase's channel in the output mixer as its initial local effects."* The phrase-capture-creates-Constituent path isn't wired in code yet (M6+ work), so this slice lands the **API surface** plus the test, and tags the call site that the next person wiring capture must invoke.

- [ ] **Step 1: Write the failing test**

Create `tests/ConstituentEffectChainCloneTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ida/Constituent.h"
#include "ida/EffectChain.h"
#include "ida/ChannelStrip.h"

TEST_CASE("Constituent::withEffectChainClonedFrom", "[constituent][effect-chain][v9-dry-tap]")
{
    SECTION("cloning from a ChannelStrip<Audio> copies its EffectChain into the Constituent")
    {
        ida::ChannelStrip<ida::SignalType::Audio> strip;
        // Configure the strip with one insert effect (exact API depends on
        // current EffectChain; if a no-op identity insert is available, use
        // that; otherwise reach for whatever the existing strip tests use).
        strip.setGainDb(-3.0f);
        // ... add a single insert effect (look at ChannelStripTests for the pattern).

        auto base = ida::Constituent::makeEmptyPhrase("test-phrase");
        REQUIRE_FALSE(base.hasEffectChain());

        auto cloned = base.withEffectChainClonedFrom(strip.effectChain());
        REQUIRE(cloned.hasEffectChain());
        // Verify deep-copy semantics — mutating the strip after clone does
        // not change the Constituent's chain:
        strip.setGainDb(-12.0f);
        // (assert the Constituent's chain is unaffected — exact assertion
        // depends on the EffectChain API; the principle is deep-copy)
    }

    SECTION("cloning from an empty chain still yields an empty optional")
    {
        ida::ChannelStrip<ida::SignalType::Audio> strip;
        auto base = ida::Constituent::makeEmptyPhrase("test-phrase");
        auto cloned = base.withEffectChainClonedFrom(strip.effectChain());
        REQUIRE_FALSE(cloned.hasEffectChain());
    }
}
```

(The exact `ChannelStrip` + `EffectChain` API calls may need adjustment based on the current code — read `tests/ChannelStripTests.cpp` for the established pattern before writing.)

- [ ] **Step 2: Run test — must fail to compile (`withEffectChainClonedFrom` does not exist)**

Run: `cmake --build build --target IdaTests`
Expected: compile error on `withEffectChainClonedFrom`.

- [ ] **Step 3: Add the builder to `Constituent.h`**

In `core/include/ida/Constituent.h:102-103` (near `withEffectChain` / `withoutEffectChain`):

```cpp
/// Copy-on-write builder that deep-clones `source` into a new
/// Constituent's `effectChain_`. If `source` is empty (no inserts),
/// the resulting Constituent has no effect chain (consistent with
/// `withoutEffectChain()`).
///
/// V9 §6.3.2 dry-tap rule: when an input channel is in non-destructive
/// (dry-tap) mode and a tape capture produces a new phrase Constituent,
/// the capture-side code must invoke this builder with the input strip's
/// EffectChain so that the phrase's local effects start as a copy of
/// the strip's chain at capture time. The phrase's chain then evolves
/// independently (copy-on-write per §6.8).
///
/// CALL SITE PENDING (M6+): the phrase-capture path that creates a
/// Constituent from tape data — once it lands — must call this builder
/// on every dry-tap channel's resulting phrase. Grep for this comment
/// to find this rule when wiring capture.
[[nodiscard]] Constituent withEffectChainClonedFrom (const EffectChain& source) const;
```

- [ ] **Step 4: Implement the builder**

Add to `core/src/Constituent.cpp` (or wherever the other builders live — check `git grep -n "::withEffectChain " core/src/`):

```cpp
Constituent Constituent::withEffectChainClonedFrom (const EffectChain& source) const
{
    if (source.isEmpty())
        return withoutEffectChain();
    return withEffectChain(source);  // EffectChain copy-ctor handles deep clone
}
```

If `EffectChain` does not yet have an `isEmpty()` method, add it as a 2-line predicate (or use the existing accessor that exposes insert count).

- [ ] **Step 5: Run the test**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R ConstituentEffectChainClone -V`
Expected: both sections pass.

- [ ] **Step 6: Tag the call site in InputMixer.h with a forward-pointer**

In `engine/include/ida/InputMixer.h` near the channel-state struct, add a comment:

```cpp
// V9 dry-tap FX migration: when a phrase is captured from a channel
// whose `TapeMode == NonDestructive`, the capture-path code must call
// `Constituent::withEffectChainClonedFrom(strip.effectChain())` to
// seed the resulting phrase's local effects with a copy of the strip's
// chain at capture time. See whitepaper §6.3.2. Call site lands when
// the tape→phrase-capture path is wired (M6+).
```

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/Constituent.h core/src/Constituent.cpp engine/include/ida/InputMixer.h tests/ConstituentEffectChainCloneTests.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: Constituent::withEffectChainClonedFrom — API surface for V9 dry-tap FX migration (Slice 6)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Slice 7: Final clean-build + operator verification + push

**Files:** none modified; verification only.

- [ ] **Step 1: Clean build per `[[feedback_clean_builds]]`**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
cmake --build build --target IdaTests
```

Expected: both targets build cleanly.

- [ ] **Step 2: Full ctest sweep**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"
```

Expected: ≈730 pass. (Pre-V9 baseline 729; net change: -DirectLayer tests + InputMixerPostStripBufferTests + InputMixerMonOutputChannelTests + ConstituentEffectChainCloneTests + updated SessionFormat + lifecycle tests.)

- [ ] **Step 3: Plugin-editor test (operator-side)**

```bash
bash bash/test-s7.sh
```

Expected: passes.

- [ ] **Step 4: Launch the app**

```bash
open ~/Desktop/IDA
```

- [ ] **Step 5: Operator-verify the V9 contract**

Verify each:
- **MON button is two-state.** Click cycles `Off ↔ MON`. No third state.
- **MON on routes signal to main outs through master.** Plug an input in, enable MON on its strip, you hear it through speakers.
- **MON off mutes the input through IDA.** (The amp-in-the-room scenario.)
- **Master meter is alive when MON is on.** The previously-dead master meter now moves with MON-on input level — confirms the auto-created OutputMixer channel is routed through master, not bypassing it.
- **Persistence round-trip.** Save a session with MON on for one channel, close, reopen — MON is still on for that channel.

If any contract fails, the executor returns to the responsible slice and fixes it. Do not commit `passes: true` over an unmet contract.

- [ ] **Step 6: Push**

```bash
git push origin master
git log origin/master..HEAD
```

Expected: push succeeds; second command returns empty (no unpushed commits).

---

## Slice 8: Documentation handoff

**Files:**
- Modify: `CLAUDE.md` — bump whitepaper reference from `IDA Whitepaper V7.md` (the stale CLAUDE.md path) to `IDA_Whitepaper_V9.md`
- Modify: `continue.md` — refresh per `[[feedback_update_continue_md_every_session]]`

- [ ] **Step 1: Fix the whitepaper path in CLAUDE.md**

In `CLAUDE.md`, find the line referencing the whitepaper canonical doc:

> Canonical design doc: **`docs/IDA Whitepaper V7.md`** (the "why").

Replace with:

> Canonical design doc: **`docs/IDA_Whitepaper_V9.md`** (the "why").

- [ ] **Step 2: Refresh continue.md**

Write a fresh `continue.md` summarizing:
- V9 whitepaper landed (commit `0f49ee3`).
- This plan landed across commits — list the SHAs.
- ctest baseline now: ≈730 pass.
- Out-of-scope items captured for the next plan: dry-tap FX migration (hook tagged in `InputMixer.h`), MIDI live-through (still no AudioCallback MIDI path), per-channel Tape-on/off + wet/dry UI exposure.

- [ ] **Step 3: Commit + push**

```bash
git add CLAUDE.md continue.md
git commit -m "docs: CLAUDE.md V9 reference + continue.md handoff after V9 conformance"
git push origin master
```

---

## Self-review checklist

After execution, verify:

- ✅ Every whitepaper V9 §7.2 + §5.2 + §6.3.1 contract is implementable / tested or explicitly deferred with a tagged call site.
- ✅ No `MonitorMode::Raw` / `MonitorMode::Processed` references remain (Slice 1 Step 7 sweep).
- ✅ No `DirectLayer` / `dispatchDirectLayer` / `setMonitoringEnabled` references remain (Slice 4 Step 1 sweep).
- ✅ Operator-verified the MON contract end-to-end (Slice 7 Step 5).
- ✅ ctest baseline ≥ 729 (Slice 7 Step 2).
- ✅ Pushed to `origin/master` (Slice 7 Step 6, Slice 8 Step 3).
- ✅ Out-of-scope items are tagged in code (Slice 6 Step 6) and recorded in continue.md (Slice 8 Step 2).
