# OttoHost embeds OTTOProcessor — Implementation Plan (S1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace OttoHost's bare `PlayerManager`+`TransportTracker` pair with a fully-embedded `OTTOProcessor`. Operator-invisible engine swap that unblocks S2 (audibility), S3 (transport bar), and S4 (preset state) by bringing OTTO's Conductor + Pattern + MIDI dispatch + internal-FX machinery inside IDA for free. ctest `[otto-host-render]` + `[otto-host-transport]` baselines (6 cases / 187 assertions combined) are preserved as regression guards.

**Architecture:** OTTOProcessor IS a `juce::AudioProcessor`. Calling `prepareToPlay` once and `processBlock` per audio buffer runs OTTO's full pipeline — including the per-block driving logic that previously required porting Conductor + Pattern + MIDI dispatch into the IDA-side bridge. The 32 per-output accessors (`getOttoOutputLeft/Right`) keep their shape by reading through OTTO's `PlayerManager::getGlobalMixer()` directly (processBlock populates GlobalMixer as a side effect; we ignore the JUCE buffer parameter, which OTTO would route through its MasterBus per OutputRouter::Mode and is not what we want here). One cross-project edit adds a `getPlayerManager()` accessor to `OTTOProcessor` so the bridge can reach the GlobalMixer from outside the class.

**Tech Stack:** C++20, JUCE 8 (`juce::AudioProcessor`, `juce::AudioBuffer<float>`, `juce::MidiBuffer`), Catch2 v3, CMake + Ninja. OTTO submodule (`external/OTTO/`) consumed under the cross-project inbox protocol per `CLAUDE.md`.

**Spec:** `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` §1.2 commitment 2 + §2.1 + §3.1 + §5.1.

**Doctrinal anchor:** Whitepaper V10 §5.7 — "OTTO as bundled rhythm engine and tempo-map source." See in particular the "two roles, one engine" paragraph.

**Out of scope for S1:** S2 OttoPane tab, S3 TransportBar + boundary-conversion helper, S4 PresetManager extension, S5/S6 OTTO-strip detail panel + routing, S7 stereo mix. Each gets its own plan when reached.

---

## File Structure

### Cross-project (OTTO submodule at `external/OTTO/`)

**Modify:**
- `external/OTTO/src/otto-plugin/PluginProcessor.h:108` — add a public `PlayerManager& getPlayerManager() noexcept` accessor immediately after the existing `getPluginState()` accessors, mirroring their shape. Header-only inline method; no `.cpp` change.

**Append:**
- `external/OTTO/CROSS_PROJECT_INBOX.md` — new `[FROM IDA → OTTO]` entry describing the accessor addition + the IDA SHA that needs it.

### IDA-side

**Modify:**
- `otto-bridge/src/OttoHost.cpp` — `Impl` swaps `PlayerManager playerManager` + `TransportTracker transportTracker` for `std::unique_ptr<OTTOProcessor> processor` + scratch `juce::AudioBuffer<float> scratchBuffer` + `juce::MidiBuffer scratchMidi` + cached `int maxBlockSize`. `prepare` calls `processor->prepareToPlay`. `renderBlock` calls `processor->processBlock(view, scratchMidi)` with a non-owning view sized to `numSamples`. Accessors reach `getPlayerManager().getGlobalMixer()` through `processor->getPlayerManager()`. ~80 lines diff; the public header `otto-bridge/include/ida/OttoHost.h` is unchanged.
- `otto-bridge/CMakeLists.txt:36-45` — change the `otto::core` PRIVATE link to also pull `otto::otto-plugin` (or whichever target exposes `OTTOProcessor`); add `juce::juce_audio_processors` PRIVATE for the `juce::AudioProcessor` base. Audit OTTO's top-level CMake at task start to confirm the target name.

**Submodule SHA bump:**
- `external/OTTO` pointer in IDA's index advances from `4cdbad3e` to the new OTTO HEAD that contains the accessor.

**Test surface (unchanged file count; new test cases append into existing files):**
- `tests/OttoHostRenderTests.cpp` — existing 6 cases / 157 assertions are the regression baseline. Append one new case `"OttoHost embeds OTTOProcessor"` under tag `[otto-host-render][processor-embed]` that pins the new architecture (processor != nullptr after prepare; renderBlock survives without a deviceManager attached).
- `tests/OttoHostTransportTests.cpp` — existing 6 cases / 30 assertions preserved unchanged. The EventBus subscription path is unaffected by the embed (OTTO's internal TransportTracker publishes to the same singleton bus that OttoHost subscribes to).

---

## Task 1 — Add `getPlayerManager()` accessor to OTTOProcessor (cross-project)

**Files:**
- Modify: `external/OTTO/src/otto-plugin/PluginProcessor.h:108` (insert public accessor next to `getPluginState`)
- Modify: `external/OTTO/CROSS_PROJECT_INBOX.md` (append `[FROM IDA → OTTO]` entry)
- Commit + push: OTTO submodule (`external/OTTO/`)

- [ ] **Step 1: Verify OTTO submodule is on a clean working tree at the current pin**

Run:
```bash
git -C external/OTTO status --short
git -C external/OTTO rev-parse HEAD
```

Expected: clean tree; HEAD = `4cdbad3e...` (or whatever IDA's submodule pin is — confirm with `git ls-tree HEAD external/OTTO` from IDA root). If OTTO has unrelated WIP, stop and surface to the operator.

- [ ] **Step 2: Read the existing PluginProcessor.h accessors to match house style**

Run:
```bash
sed -n '95,120p' external/OTTO/src/otto-plugin/PluginProcessor.h
```

Expected output shows two accessors:
```cpp
const otto::state::PluginState& getPluginState() const { return pluginState_; }
otto::state::PluginState& getPluginState() { return pluginState_; }
```

These are header-only inline returns of a private member by ref + const-ref pair. Match this shape.

- [ ] **Step 3: Add the new accessor in PluginProcessor.h**

In `external/OTTO/src/otto-plugin/PluginProcessor.h`, after line ~108 (just below the existing `getPluginState()` non-const overload), insert:

```cpp
    /**
     * @brief Direct access to the embedded PlayerManager.
     *
     * Added 2026-05-27 for IDA's embedded-mode integration: IDA's OttoHost
     * wraps OTTOProcessor and reads the 32 stereo per-output buffers via
     * playerManager_.getGlobalMixer() after processBlock completes. The
     * existing per-player level/peak/voice-count accessors above already
     * delegate to playerManager_ — this exposes the full object for
     * GlobalMixer access.
     *
     * Audio thread: const accessor is safe (GlobalMixer accessor pattern).
     * Non-const accessor is message-thread only.
     */
    otto::manager::PlayerManager& getPlayerManager() noexcept { return playerManager_; }
    const otto::manager::PlayerManager& getPlayerManager() const noexcept { return playerManager_; }
```

Header inclusion: `<otto/manager/PlayerManager.h>` is already pulled transitively via existing OTTOProcessor members; no new include needed. Verify with `grep PlayerManager external/OTTO/src/otto-plugin/PluginProcessor.h | head -5` after the edit.

- [ ] **Step 4: Build OTTO to verify the accessor compiles**

Run from IDA root:
```bash
cd external/OTTO && cmake --build build 2>&1 | tail -20
```

If OTTO's `build/` directory doesn't exist, configure first:
```bash
cmake -B external/OTTO/build -S external/OTTO -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build external/OTTO/build 2>&1 | tail -20
```

Expected: build succeeds (the addition is header-only and additive; nothing depends on its absence).

- [ ] **Step 5: Append the inbox entry**

Append to `external/OTTO/CROSS_PROJECT_INBOX.md`, immediately above the existing `## [FROM OTTO → IDA]` divider line:

```markdown
### 2026-05-27 — IMPLEMENTATION: getPlayerManager() accessor added to OTTOProcessor (S1 of OTTO integration plan)

Direction: IDA → OTTO
IDA commit: (filled at IDA-side commit time — see Task 5)
OTTO commit: (filled at Task 1 commit time below)
Files touched:
- src/otto-plugin/PluginProcessor.h (one accessor pair added next to getPluginState; header-only inline)

Why: IDA's S1 slice swaps OttoHost's internal architecture from a bare PlayerManager+TransportTracker pair to an embedded OTTOProcessor (the realization from the 2026-05-27 brainstorm — bringing OTTO's Conductor + Pattern + MIDI dispatch into IDA for free by embedding the AudioProcessor itself). After processBlock fires, OTTO's 32 stereo per-output buffers are populated inside GlobalMixer. The IDA-side accessors getOttoOutputLeft/Right(idx) need a path from OTTOProcessor → PlayerManager → GlobalMixer; the existing per-player level/peak accessors already delegate to playerManager_ but don't expose the object itself.

For OTTO's Claude: do NOT revert. Match the existing `getPluginState` accessor pair shape (header-only inline ref + const-ref). Audio-thread safety: the const accessor is RT-safe (the underlying GlobalMixer accessor pattern is RT-safe per OTTO's own design). Non-const is message-thread only by convention.

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

- [ ] **Step 6: Commit OTTO + push**

Run from IDA root:
```bash
cd external/OTTO
git add src/otto-plugin/PluginProcessor.h CROSS_PROJECT_INBOX.md
git commit -m "feat: getPlayerManager() accessor on OTTOProcessor (Ida-Origin: pending — S1 of OTTO integration plan; IDA's OttoHost embeds OTTOProcessor and reads the 32 stereo per-output buffers through this accessor's GlobalMixer chain; matches existing getPluginState() inline ref+const-ref pair shape; header-only addition with zero behavior change)"
git push origin main
git rev-parse HEAD  # capture for Step 5 of Task 5 below
cd -
```

Note: the `Ida-Origin:` trailer would normally hold the IDA SHA, but that SHA doesn't exist yet (IDA-side commit is Task 5). The "pending" marker is OK — the matching IDA commit will reference this OTTO SHA, and the durable audit trail via `git log --grep='Ida-Origin'` in OTTO will still surface the linkage.

---

## Task 2 — Bump OTTO submodule SHA in IDA + verify accessor reachable

**Files:**
- Modify: `external/OTTO` submodule pointer (IDA-side)

- [ ] **Step 1: Capture the new OTTO HEAD SHA**

Run from IDA root:
```bash
git -C external/OTTO rev-parse HEAD
```

Save this SHA — it's what IDA's index will point at.

- [ ] **Step 2: Stage the submodule bump**

Run:
```bash
git add external/OTTO
git status --short
```

Expected `git status --short` output:
```
M external/OTTO
```

(With possibly `m external/sfizz` if that submodule still shows the historical local-change marker — that's unrelated and stays as-is.)

- [ ] **Step 3: Verify the accessor is reachable from IDA's include path**

Run:
```bash
grep -A2 "PlayerManager& getPlayerManager" external/OTTO/src/otto-plugin/PluginProcessor.h
```

Expected: shows the two accessor declarations added in Task 1 Step 3.

Do NOT commit the submodule bump yet — it lands as part of Task 5's final IDA commit so the IDA-side code change + submodule bump are atomic.

---

## Task 3 — Refactor OttoHost::Impl to embed OTTOProcessor

**Files:**
- Modify: `otto-bridge/src/OttoHost.cpp:1-216` (substantial refactor of Impl + prepare + isPrepared + renderBlock + the two accessor methods; transport-listener fan-out is untouched)

- [ ] **Step 1: Update OttoHost.cpp includes**

Replace lines 6-11 of `otto-bridge/src/OttoHost.cpp`:

```cpp
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <juce_events/juce_events.h>
```

with:

```cpp
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <PluginProcessor.h>  // OTTOProcessor — the AudioProcessor we embed

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
```

Rationale: `PluginProcessor.h` defines `OTTOProcessor` (the class is in the global namespace per OTTO's plugin convention; not in `otto::`). `juce_audio_processors` is needed for the `juce::AudioProcessor` base type. `juce_audio_basics` for `AudioBuffer` / `MidiBuffer`. PlayerManager + GlobalMixer + TransportTracker includes stay — they're still referenced via OTTOProcessor's accessor return types.

- [ ] **Step 2: Update otto-bridge/CMakeLists.txt to link OTTO's plugin target + juce_audio_processors**

Read OTTO's plugin target name first:
```bash
grep -E "add_library|add_executable.*otto[-_]plugin" external/OTTO/src/otto-plugin/CMakeLists.txt | head -5
```

Capture the target name (likely `otto-plugin` or `OTTOPlugin`). Then in `otto-bridge/CMakeLists.txt:36-45`, append to the PRIVATE link list (the block that already contains `otto::core`):

```cmake
target_link_libraries(IdaOttoBridge
    PUBLIC  Ida::Core
    PRIVATE otto::core
            otto::otto-plugin    # ← NEW: provides OTTOProcessor (S1 — engine swap)
            juce::juce_audio_basics
            juce::juce_audio_processors  # ← NEW: juce::AudioProcessor base
            juce::juce_events
            lsfx::lsfx_tapecolor
            Ida::Engine)
```

Replace `otto::otto-plugin` with whatever Step 2's grep returned (the OTTO-side target alias).

- [ ] **Step 3: Refactor the Impl struct — replace PlayerManager/TransportTracker with OTTOProcessor + scratch buffers**

In `otto-bridge/src/OttoHost.cpp`, replace the `Impl` struct (lines 49-131) with this version:

```cpp
struct OttoHost::Impl : public juce::Timer
{
    Impl()
        : transportRing (kTransportRingCapacity)
        , processor     (std::make_unique<OTTOProcessor>())
    {
        // Subscription to OTTO's transport EventBus — unchanged from M-OTTO-3.
        // The fan-out path (audio-thread publish → SPSC ring → message-thread
        // drainer → listener->onOttoTransport) is unaffected by the embed:
        // OTTOProcessor's internal TransportTracker publishes to the same
        // singleton EventBus this subscription reads from.
        subscription = ::otto::EventBus::instance().subscribe<::otto::TransportEvent> (
            [this] (const ::otto::TransportEvent& event) noexcept
            {
                TransportSnapshot snap;
                snap.kind       = translateKind (event.type);
                snap.bpm        = event.state.bpm;
                snap.isPlaying  = event.state.isPlaying;
                snap.timeSigNum = event.state.timeSignature.numerator;
                snap.timeSigDen = event.state.timeSignature.denominator;
                (void) transportRing.push (snap);
            });

        startTimerHz (kDrainTimerHz);
    }

    ~Impl() override
    {
        // Stop the drain timer first, then let the SubscriptionHandle dtor
        // (declared last → destroyed first) unsubscribe from OTTO's bus.
        // OTTOProcessor's dtor runs when `processor` (declared before the
        // subscription) falls out of scope — after the bus is detached, so
        // no in-flight TransportEvent publish can race the processor's
        // destruction.
        stopTimer();
    }

    void timerCallback() override
    {
        drain();
    }

    void drain()
    {
        TransportSnapshot snap;
        while (transportRing.pop (snap))
        {
            const auto snapshot = listeners;
            for (auto* listener : snapshot)
                if (listener != nullptr)
                    listener->onOttoTransport (snap);
        }
    }

    // OTTOProcessor IS a juce::AudioProcessor; prepareToPlay + processBlock
    // run OTTO's full pipeline (Conductor + Pattern + MIDI dispatch + the
    // PlayerManager-driven GlobalMixer + internal FX). The 32 per-output
    // buffers are populated inside processor->getPlayerManager()
    // .getGlobalMixer() as a side effect of processBlock and are read out
    // via the existing IDA accessor shape.
    std::unique_ptr<OTTOProcessor> processor;

    // Scratch buffer view storage for processBlock. OTTOProcessor's default
    // bus layout has 32 stereo outputs (64 mono channels) — only "Main"
    // (channels 0-1) is enabled at construction, but the GlobalMixer-direct
    // read path bypasses the JUCE bus layout entirely. The scratch buffer
    // is a 2-channel throwaway sized to maxBlockSize that satisfies
    // processBlock's API contract; its contents are discarded.
    juce::AudioBuffer<float> scratchBuffer;
    juce::MidiBuffer         scratchMidi;
    int                      cachedMaxBlock { 0 };
    bool                     prepared       { false };

    LockFreeSpscQueue<TransportSnapshot> transportRing;
    std::vector<IOttoTransportListener*> listeners;       // message-thread only

    // Declared LAST so it is destroyed FIRST (before processor).
    ::otto::SubscriptionHandle subscription;
};
```

- [ ] **Step 4: Refactor OttoHost::prepare + isPrepared**

Replace lines 140-148 of `otto-bridge/src/OttoHost.cpp`:

```cpp
void OttoHost::prepare (double sampleRate, int maxBlockSize)
{
    impl_->playerManager.prepare (sampleRate, maxBlockSize);
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->playerManager.isPrepared();
}
```

with:

```cpp
void OttoHost::prepare (double sampleRate, int maxBlockSize)
{
    // Allocate scratch buffer once at the maxBlockSize ceiling. Subsequent
    // renderBlock calls reuse the storage via a non-owning AudioBuffer view
    // sized to the actual numSamples (no per-block allocation).
    impl_->scratchBuffer.setSize (2, maxBlockSize,
                                  /*keepExistingContent=*/false,
                                  /*clearExtraSpace=*/true,
                                  /*avoidReallocating=*/false);
    impl_->scratchMidi.ensureSize (256);  // enough for one block's worth of MIDI hits
    impl_->cachedMaxBlock = maxBlockSize;

    // Drive OTTO's AudioProcessor prepareToPlay. This propagates internally
    // to PlayerManager::prepare (the original call site) and to every other
    // subsystem OTTOProcessor owns (Conductor, Pattern engine, internal FX).
    impl_->processor->prepareToPlay (sampleRate, maxBlockSize);
    impl_->prepared = true;
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->prepared;
}
```

- [ ] **Step 5: Refactor OttoHost::renderBlock**

Replace lines 174-184 of `otto-bridge/src/OttoHost.cpp`:

```cpp
void OttoHost::renderBlock (int numSamples) noexcept
{
    if (! impl_->playerManager.isPrepared() || numSamples <= 0)
        return;

    impl_->playerManager.processGlobalMixer (numSamples);
}
```

with:

```cpp
void OttoHost::renderBlock (int numSamples) noexcept
{
    if (! impl_->prepared || numSamples <= 0 || numSamples > impl_->cachedMaxBlock)
        return;

    // Non-owning view into the scratch buffer's first `numSamples` samples.
    // The AudioBuffer (const channel-pointer array, channels, samples) ctor
    // does not allocate — just a header wrapper. processBlock fills the view
    // per its OutputRouter::Mode (default Stereo → channels 0-1 only); we
    // discard the view's contents and read the 32 per-output buffers from
    // GlobalMixer below, which processBlock populates as a side effect.
    float* const channelPtrs[2] = {
        impl_->scratchBuffer.getWritePointer (0),
        impl_->scratchBuffer.getWritePointer (1)
    };
    juce::AudioBuffer<float> view (channelPtrs, 2, numSamples);

    // OTTO's processBlock may add to the MIDI buffer (drum hits); clear
    // before each call so we don't accumulate forever. clear() does not
    // allocate (ensureSize was called once in prepare).
    impl_->scratchMidi.clear();

    impl_->processor->processBlock (view, impl_->scratchMidi);
}
```

- [ ] **Step 6: Refactor the two accessor methods**

Replace lines 186-214 of `otto-bridge/src/OttoHost.cpp`:

```cpp
const float* OttoHost::getOttoOutputLeft (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->playerManager.isPrepared())
        return nullptr;

    const auto& mixer = impl_->playerManager.getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputLeft (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputLeft (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputLeft (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

const float* OttoHost::getOttoOutputRight (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->playerManager.isPrepared())
        return nullptr;

    const auto& mixer = impl_->playerManager.getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputRight (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputRight (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputRight (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}
```

with:

```cpp
const float* OttoHost::getOttoOutputLeft (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputLeft (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputLeft (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputLeft (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

const float* OttoHost::getOttoOutputRight (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputRight (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputRight (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputRight (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}
```

Diff vs. the previous version: only the read source changed — `impl_->playerManager.isPrepared()` → `impl_->prepared`; `impl_->playerManager.getGlobalMixer()` → `impl_->processor->getPlayerManager().getGlobalMixer()`. The range-dispatch logic is byte-identical.

- [ ] **Step 7: Build otto-bridge in isolation to catch compile errors fast**

Run from IDA root:
```bash
cmake --build build --target IdaOttoBridge 2>&1 | tail -30
```

If `build/` doesn't exist:
```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaOttoBridge 2>&1 | tail -30
```

Expected: clean build. Most likely failure modes:
- "no member named getPlayerManager" → Task 1 didn't land or Task 2's submodule bump is stale. Re-run `git -C external/OTTO log -1 --oneline` and confirm the OTTO commit is present.
- "PluginProcessor.h not found" → wrong include path. Add `${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-plugin` to IdaOttoBridge's PRIVATE include dirs in `otto-bridge/CMakeLists.txt` (target_include_directories ... PRIVATE).
- "OTTOProcessor unknown linker symbol" → the new `otto::otto-plugin` link target name is wrong; re-check Step 2's grep output.

---

## Task 4 — Add the embed-pin regression test + run the full suite

**Files:**
- Modify: `tests/OttoHostRenderTests.cpp` (append one new TEST_CASE after the existing 6)

- [ ] **Step 1: Append the new regression-pin test**

Append to the end of `tests/OttoHostRenderTests.cpp` (after the existing closing `}` of "OttoHost::renderBlock tolerates zero / negative numSamples without crashing"):

```cpp
TEST_CASE ("OttoHost embeds OTTOProcessor — full processBlock path is driven",
           "[otto-host-render][processor-embed]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    REQUIRE (host.isPrepared());

    // After prepare, every in-range accessor must return non-null even
    // before any renderBlock — the GlobalMixer's per-channel storage is
    // allocated by PlayerManager::prepare (which OTTOProcessor::prepareToPlay
    // calls transitively).
    for (int i = 0; i < OttoHost::kNumOttoOutputs; ++i)
    {
        CHECK (host.getOttoOutputLeft  (i) != nullptr);
        CHECK (host.getOttoOutputRight (i) != nullptr);
    }

    // Multiple processBlock calls — the OTTOProcessor embed must survive
    // sustained per-block driving without leaking or accumulating internal
    // state that would surface as a crash, allocation explosion, or
    // pointer instability. (RT-safety of OTTOProcessor::processBlock is
    // OTTO's own contract per its CLAUDE.md AUDIO THREAD RULES; this test
    // pins the wiring, not the OTTO-side invariants.)
    const float* l0_first = host.getOttoOutputLeft (0);

    for (int block = 0; block < 100; ++block)
        host.renderBlock (kTestBlockSize);

    CHECK (host.getOttoOutputLeft (0) == l0_first);  // pointer stability
    CHECK (host.getOttoOutputRight (OttoHost::kOttoPlayerBusRangeBegin) != nullptr);
}
```

- [ ] **Step 2: Build IdaTests**

Run:
```bash
cmake --build build --target IdaTests 2>&1 | tail -20
```

Expected: clean build. If the link fails with "undefined symbol OTTOProcessor", the IdaTests CMake at `tests/CMakeLists.txt:181-200` may need `otto::otto-plugin` added to its PRIVATE link list — the bridge's PRIVATE link to otto-plugin doesn't propagate, but the test binary only needs it via the bridge's symbols, so adding it is only required if tests directly construct an OTTOProcessor (they don't in this plan — the new test exercises OttoHost only).

- [ ] **Step 3: Run the OTTO-host test tags to confirm the regression baseline + new test**

Run:
```bash
cd build && ctest --output-on-failure -R "OttoHost"
```

Expected: 12 tests pass (6 from OttoHostRenderTests + 6 from OttoHostTransportTests + the 1 new processor-embed case, but Catch2 counts each TEST_CASE as one ctest case so the count is `[otto-host-render]` 7 cases + `[otto-host-transport]` 6 cases = 13 reported via ctest if each TEST_CASE is registered individually; check the exact count against your ctest registration scheme). The new `[processor-embed]` case must pass.

If the new test FAILS:
- Accessors return nullptr after prepare → step into `Impl::Impl` and verify `processor` was constructed; verify `prepare` set `prepared = true` and the processor's internal PlayerManager has its GlobalMixer allocated.
- Pointer stability fails (l0_first changes) → OTTO's GlobalMixer may be reallocating its per-channel buffers in processBlock, which would be an OTTO regression. Verify against `git -C external/OTTO log` since the previous SHA — nothing should have changed in GlobalMixer.

- [ ] **Step 4: Run the full ctest suite**

Run:
```bash
ctest --test-dir build 2>&1 | tail -10
```

Expected: **790 passed, 1 not-run** (the 1 not-run is the separately-built `MainComponentPluginEditorTests` per `continue.md`'s baseline; same as the pre-S1 baseline).

If any test that was previously passing now fails: stop, do not commit, investigate. The S1 refactor should not touch any user-visible behavior — a regression here is a real bug in the embed wiring.

- [ ] **Step 5: Verify the IDA app builds + links**

Run:
```bash
cmake --build build --target IDA 2>&1 | tail -10
```

Expected: clean build. The `IDA.app` bundle at `build/app/IDA_artefacts/Release/IDA.app` should be updated.

The MainComponent code at `app/MainComponent.cpp:4274` (`ottoHost_ = std::make_unique<ida::OttoHost>();`) and the audio-callback wiring at line 4281 (`audioCallback_->setOttoRenderSource (ottoHost_.get());`) are unchanged — the public OttoHost surface is preserved exactly.

---

## Task 5 — Commit the IDA-side change + submodule bump + push

**Files:**
- Commit: `otto-bridge/src/OttoHost.cpp` + `otto-bridge/CMakeLists.txt` + `tests/OttoHostRenderTests.cpp` + `external/OTTO` (the submodule pointer bump)

- [ ] **Step 1: Stage the IDA-side files**

Run:
```bash
git add otto-bridge/src/OttoHost.cpp \
        otto-bridge/CMakeLists.txt \
        tests/OttoHostRenderTests.cpp \
        external/OTTO
git status --short
```

Expected `git status --short` (with possible additional unrelated `m external/sfizz` line):
```
M  otto-bridge/CMakeLists.txt
M  otto-bridge/src/OttoHost.cpp
M  tests/OttoHostRenderTests.cpp
M  external/OTTO
```

- [ ] **Step 2: Commit with a descriptive single-line message**

Capture the new OTTO SHA first:
```bash
NEW_OTTO_SHA=$(git -C external/OTTO rev-parse --short HEAD)
echo "Bumping OTTO submodule to ${NEW_OTTO_SHA}"
```

Then commit:
```bash
git commit -m "feat: S1 — OttoHost embeds OTTOProcessor (replaces bare PlayerManager+TransportTracker pair with full juce::AudioProcessor embed; OTTO's Conductor + Pattern + MIDI dispatch + internal-FX machinery now drive inside renderBlock for free; prepare allocates scratch buffer + MidiBuffer once at maxBlockSize, renderBlock builds a non-owning view sized to numSamples and calls processor->processBlock; accessors keep their shape by reading via the new OTTOProcessor::getPlayerManager() accessor's GlobalMixer chain; transport-listener fan-out via the singleton EventBus subscription is unaffected since OTTOProcessor's internal TransportTracker publishes to the same bus; OTTO submodule bumped to ${NEW_OTTO_SHA} for the new accessor; otto-bridge/CMakeLists.txt links otto-plugin + juce_audio_processors; tests/OttoHostRenderTests.cpp gains one new [otto-host-render][processor-embed] case pinning the embed surface; existing 6 [otto-host-render] + 6 [otto-host-transport] cases preserved as regression baseline; ctest 790/791 baseline preserved; public OttoHost.h surface unchanged so MainComponent + AudioCallback wiring at app/MainComponent.cpp:4274/4281 is byte-identical; S2 audibility unblocked since OttoPane can now call processor->createEditor() once the tab lands)"
```

- [ ] **Step 3: Update the OTTO-side Ida-Origin trailer (optional retro-fix)**

The OTTO commit from Task 1 Step 6 has `Ida-Origin: pending`. The accurate IDA SHA now exists. The audit trail via `git log --grep='Ida-Origin'` will still surface the linkage via the inbox entry's IDA-commit reference, so amending the OTTO commit is optional — and amending after push requires force-push, which CLAUDE.md forbids. **Skip the amend**; the inbox entry + the matching IDA commit message together provide sufficient cross-reference.

- [ ] **Step 4: Push IDA**

Per `feedback_claude_commits_and_pushes_master`:
```bash
git push origin master
git log -1 --oneline
```

Expected: push succeeds. `git log -1 --oneline` shows the new S1 commit at HEAD.

---

## Verification (end-to-end S1 acceptance)

After Task 5 lands:

- [ ] **Engine acceptance.** `ctest --test-dir build` reports 790 passed / 1 not-run — the pre-S1 baseline. The new `[otto-host-render][processor-embed]` case is included in the pass count.
- [ ] **Public-surface preservation.** `git diff 4cdbad3e..HEAD -- otto-bridge/include/ida/OttoHost.h` returns empty (header unchanged). The slice 4b external API shape is byte-identical.
- [ ] **Audit trail.** `git -C external/OTTO log -1 --format='%H %s'` shows the new accessor commit at OTTO HEAD. `cat external/OTTO/CROSS_PROJECT_INBOX.md | grep -A1 "getPlayerManager"` shows the inbox entry's `Status: needs-ack`.
- [ ] **App build.** `cmake --build build --target IDA` succeeds. The `IDA.app` bundle at `build/app/IDA_artefacts/Release/IDA.app` is the updated build.
- [ ] **No operator-visible change.** Launching the app from the desktop `IDA` alias and clicking "Add OTTO source ▶" in OutputMixerPane still lists the 32 canonical names (Kick / Snare / SideStick / ... / PlayerOut1..4) and creating OTTO strips still works as it did pre-S1 — the engine swap is invisible at this layer.

After S1 lands, the next plan (S2 — OttoPane tab via `OTTOProcessor::createEditor()`) closes the M-OTTO-4 audibility gap. Run `superpowers:writing-plans` against §7 row S2 of `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md`.

---

## Self-review pass

- [x] **Spec coverage.** Maps to spec §1.2 commitment 2 (OttoHost embeds OTTOProcessor), §2.1 (component evolution), §3.1 (audio data flow), §5.1 (S1 testing: [otto-host-render] baseline preserved + new [otto-host-processor] case). Doctrinal anchor V10 §5.7 referenced.
- [x] **Placeholder scan.** No "TBD" / "TODO" / "implement later". Every step has either exact code, exact command + expected output, or exact verification. The two read-then-substitute steps (Task 1 Step 2's "match house style" + Task 3 Step 2's "capture target name") are concrete commands with expected output shapes.
- [x] **Type consistency.** `OttoHost::Impl` member names consistent across steps (`processor`, `scratchBuffer`, `scratchMidi`, `cachedMaxBlock`, `prepared`, `transportRing`, `listeners`, `subscription`). The new accessor's signature (`PlayerManager&` ref + const-ref pair) is used consistently in Task 1 Step 3 + Task 3 Step 6.
- [x] **Scope discipline.** S1 only. S2-S7 explicitly out-of-scope at the top + named in Verification as "next plan." No drive-by improvements to MainComponent wiring, OutputMixerPane, or any other consumer of OttoHost — the public header is preserved on purpose.
