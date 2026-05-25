# MON Output-Channel Strips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When MON is turned On for an input channel, the auto-created OutputMixer channel must (a) carry a real `ChannelStrip<Audio>` so its gain/pan/mute/inserts/sends/destination are operator-controllable, and (b) appear as a visible strip in the Output Mixer GUI alongside phrase strips. Today only the engine channel exists, with no strip and no UI — the post-strip-input signal flows straight to master at unity, which violates whitepaper V9 §6.3.1 / §7.2 ("a peer of the per-phrase channels, with its own gain, pan, inserts, and routing").

**Architecture:**

1. **Engine fix (`InputMixer::setChannelMonitorMode`)** — when MON flips On, after `outputMixer_->addChannel(SignalType::Audio)`, also call `outputMixer_->setChannelStrip(monChId, std::make_unique<ChannelStrip<SignalType::Audio>>())` so `OutputMixer::renderBuffer` Step 1's `entry.strip != nullptr` branch fires and the operator-facing strip controls have something to process.

2. **GUI category (`OutputMixerPane`)** — mirror of phrase strips. Add `MonStripInfo` struct, `setMonStrips(...)` method, and the same callback shape as the phrase relays (`onMonGain`, `onMonMute`, `onMonInsertChainClicked`, `onMonDestinationChosen`, `onMonSelect`, `onMonPan`, `onMonWidth`, `onMonSendChanged`, `onMonPreFaderToggled`, `onMonEqConfigChanged`, `onMonEqSlotAddRequested`, `onMonCmpConfigChanged`, `onMonCmpSlotAddRequested`). Position MON strips LEFTMOST in the pane (signal-flow order: live MON → phrase → bus → master).

3. **App wiring (`MainComponent`)** — add `refreshOutputMixerMonChannels()` mirroring `refreshOutputMixerPhraseChannels()`. Discover MON-on input channels, look up their `channelMonitorOutputChannel(id)`, push a `MonStripInfo` list to the pane, and wire the `onMon*` callbacks to the matching `OutputMixer::routeChannelToBus` / `audioStripForChannel(...)->setGain/Muted/...` calls. Trigger the refresh from:
   - `inputMixerPane_->onMonitorModeChanged` (after `setChannelMonitorMode`)
   - Session-load completion (after the post-load `attachOutputMixer` rebind)
   - Input channel add/remove paths

**Tech Stack:** C++ / JUCE / Catch2 — same surfaces touched by the V9 conformance work (`engine/include/ida/InputMixer.h`, `engine/src/InputMixer.cpp`, `app/MainComponent.{h,cpp}`, `tests/InputMixerMonOutputChannelTests.cpp`).

**Out of scope for this plan** (queued for later):
- Visual differentiation of MON vs phrase strips (badge, color band). For now they look identical except for the strip name being `"MON <inputIdx>"`. Operator design pass can refine after seeing the working result.
- Persisting MON strip gain/pan/sends across save+reload. The engine's `OutputMixerGraphState` already exports all OutputMixer channel state, but the rebind path after load will mint a fresh MON channel (since MON-on state on the input side re-fires `setChannelMonitorMode`). Confirming the round-trip is a follow-up verification.
- Generic "output strip" refactor unifying phrase + MON categories.

---

## File Structure

**Modified files (no new files):**

- `engine/src/InputMixer.cpp` — `setChannelMonitorMode(On)` adds `setChannelStrip` call.
- `tests/InputMixerMonOutputChannelTests.cpp` — two new sections pinning the strip + render-path contract.
- `app/MainComponent.h` — declares `refreshOutputMixerMonChannels()` and the `monStripChannelIds_` parallel vector.
- `app/MainComponent.cpp` — adds the `OutputMixerPane` MON-strip surface (callbacks, `setMonStrips`, internal storage), the new `refreshOutputMixerMonChannels()` method, and the three trigger sites.

Each task below is self-contained: tests pass after each commit, the app continues to build after each commit, and each task commits independently per the repo's `[[feedback_claude_commits_and_pushes_master]]` convention.

---

## Task 1: Engine — auto-created MON channel gets a ChannelStrip

**Files:**
- Modify: `engine/src/InputMixer.cpp:223-235` (the MON On branch in `setChannelMonitorMode`)
- Modify: `tests/InputMixerMonOutputChannelTests.cpp` (add two new SECTIONs to the existing `"InputMixer MON owns an OutputMixer channel"` TEST_CASE — sections share the existing fixture)

- [ ] **Step 1: Write the failing test (strip presence)**

Append a new SECTION inside the existing `TEST_CASE ("InputMixer MON owns an OutputMixer channel", ...)` at `tests/InputMixerMonOutputChannelTests.cpp:40`. Add it AFTER the `"removing the input channel while MON is on cleans up the output channel"` section (so it lives near the channel-lifecycle sections):

```cpp
    SECTION ("MON on → the auto-created OutputMixer channel carries a ChannelStrip<Audio>")
    {
        input.setChannelMonitorMode (chId, MonitorMode::On);
        const auto monChId = input.channelMonitorOutputChannel (chId);
        REQUIRE (monChId.has_value());
        REQUIRE (output.audioStripForChannel (*monChId) != nullptr);
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R InputMixerMonOutputChannel -V`
Expected: the new SECTION FAILS with `output.audioStripForChannel (*monChId) != nullptr` returning false (no strip is attached today). The five pre-existing sections still pass.

- [ ] **Step 3: Implement — add setChannelStrip in the engine**

Edit `engine/src/InputMixer.cpp` around line 227. Replace the existing block:

```cpp
    // Mint a fresh OutputMixer channel and wire its audio source to this
    // input's post-strip stereo buffer (the V9 Slice 2 seam). Pointers
    // are stable for the input channel's lifetime; OutputMixer reads
    // them every block.
    const auto monChId = outputMixer_->addChannel (SignalType::Audio);
    outputMixer_->setChannelAudioSource (monChId,
                                         postStripPointer (id, 0),
                                         postStripPointer (id, 1));
```

with:

```cpp
    // Mint a fresh OutputMixer channel and wire its audio source to this
    // input's post-strip stereo buffer (the V9 Slice 2 seam). Pointers
    // are stable for the input channel's lifetime; OutputMixer reads
    // them every block. The minted channel also gets its own
    // ChannelStrip<Audio> so the operator can mix it (gain/pan/mute/
    // inserts/sends/destinations) as a peer of phrase channels per
    // whitepaper V9 §6.3.1 / §7.2 — without a strip the post-strip
    // input signal would land at master at unity with no per-channel
    // control surface.
    const auto monChId = outputMixer_->addChannel (SignalType::Audio);
    outputMixer_->setChannelStrip (monChId,
        std::make_unique<ChannelStrip<SignalType::Audio>>());
    outputMixer_->setChannelAudioSource (monChId,
                                         postStripPointer (id, 0),
                                         postStripPointer (id, 1));
```

(Note: `ChannelStrip.h` is already included transitively via `OutputMixer.h`, which `InputMixer.cpp` already includes. No new `#include` needed.)

- [ ] **Step 4: Add the second failing test (render-path: MON output strip's mute silences master)**

Append another SECTION at the end of the same TEST_CASE. This one exercises the render path to confirm the strip is actually applied:

```cpp
    SECTION ("MON on + MON output strip muted → no signal at master after render")
    {
        using ida::ChannelStrip;

        input.setChannelInputSource (chId, 0, 1, /*stereo=*/true);
        input.setChannelMonitorMode (chId, MonitorMode::On);
        const auto monChId = input.channelMonitorOutputChannel (chId);
        REQUIRE (monChId.has_value());

        constexpr int n = 64;
        std::array<float, n> left {}, right {};
        for (int i = 0; i < n; ++i) { left[i] = 0.5f; right[i] = -0.5f; }
        const float* inputs[2] { left.data(), right.data() };

        std::array<float, n> outL {}, outR {};
        float* outputs[2] { outL.data(), outR.data() };

        // Unmuted: input flows through input strip → post-strip buffer
        // → MON output strip (unmuted) → master → outputs[].
        input.renderInputGraph (inputs, 2, nullptr, 0, n);
        output.renderBuffer (nullptr, 0, outputs, 2, n);
        const bool anyAudibleUnmuted =
            (outL[0] != 0.0f) || (outR[0] != 0.0f)
         || (outL[n / 2] != 0.0f) || (outR[n / 2] != 0.0f);
        REQUIRE (anyAudibleUnmuted);

        // Mute the MON OUTPUT strip (distinct from the input strip's mute —
        // that path is covered by the "MON+mute" test above). Re-render
        // with fresh output buffers and expect silence at master.
        auto* monStrip = output.audioStripForChannel (*monChId);
        REQUIRE (monStrip != nullptr);
        monStrip->setMuted (true);

        outL.fill (0.0f);
        outR.fill (0.0f);
        input.renderInputGraph (inputs, 2, nullptr, 0, n);
        output.renderBuffer (nullptr, 0, outputs, 2, n);
        REQUIRE (outL[0] == 0.0f);
        REQUIRE (outR[0] == 0.0f);
        REQUIRE (outL[n / 2] == 0.0f);
        REQUIRE (outR[n / 2] == 0.0f);
    }
```

- [ ] **Step 5: Run all MON output-channel tests**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R InputMixerMonOutputChannel -V`
Expected: both new sections pass plus all five prior sections plus the existing MON+mute test (7 sections / 2 test cases total in this file).

- [ ] **Step 6: Run the full suite to confirm no regression**

Run: `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"`
Expected: count rises from 710 to 712 (two new sections), all green.

- [ ] **Step 7: Commit**

```bash
git add engine/src/InputMixer.cpp tests/InputMixerMonOutputChannelTests.cpp
git commit -m "$(cat <<'EOF'
fix: V9 MON-on auto-mints a ChannelStrip on the OutputMixer channel so the operator can mix it

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Task 2: OutputMixerPane — MON strip category + callbacks

**Files:**
- Modify: `app/MainComponent.cpp` — the `OutputMixerPane` class declaration around lines 1539-1900. Add MON callbacks alongside the existing phrase callbacks (line 1675-1707), and add `setMonStrips(...)` + internal storage alongside `setPhraseStrips`.
- The implementation files for `setPhraseStrips` and the pane's `resized()`/`paint()` layout — find them via `grep -n "setPhraseStrips\b\|phraseStripChildren_\|phraseStrips_" /Users/larryseyer/IDA/app/MainComponent.cpp` and mirror exactly.

- [ ] **Step 1: Locate the phrase-strip implementation surfaces**

Before writing any code, find every site `setPhraseStrips` touches by running:
```bash
grep -n "setPhraseStrips\b\|phraseStripChildren_\|phraseStrips_\|phraseStripInfos_\|onPhraseGain\b\|onPhraseMute\b\|onPhraseSelect\b\|onPhraseDestinationChosen\b" /Users/larryseyer/IDA/app/MainComponent.cpp
```

Expected: the pane stores phrase strips in some member vector (likely `phraseStrips_` of `std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>>`), parallels them with a `phraseStripInfos_` of `PhraseStripInfo`, lays them out in `resized()`, and routes their listener callbacks to the `onPhrase*` relays. The MON surface mirrors all of this with `mon*_` members and `onMon*` callbacks. Read each site enough to understand the layout math (left band's pixel start/end + per-strip width) — the MON strips will occupy a NEW leftmost sub-band, pushing the phrase band right.

- [ ] **Step 2: Add the MonStripInfo struct + callback declarations**

In the `OutputMixerPane` class declaration, add the following ALONGSIDE the existing `PhraseStripInfo` declaration (around line 1612) and the `onPhrase*` relay declarations (around line 1675-1707):

```cpp
    /// One MON-channel strip's display state. LEFTMOST band in the pane
    /// (left of phrase strips, mirroring signal flow: live monitoring →
    /// phrase playback → buses → master). Carries the input ChannelId
    /// the operator can use to map back to the corresponding input strip,
    /// and a name string MainComponent supplies — typically "MON N" where
    /// N is the 1-based input-strip row. White paper V9 §6.3.1 / §7.2:
    /// peer of phrase channels, full strip controls.
    struct MonStripInfo { ida::ChannelId inputChannelId { 0 }; juce::String name; };
```

And alongside the phrase callbacks at line 1678, add:

```cpp
    // --- MON-channel gesture relays. Mirror of the onPhrase* surface;
    // idx = mon-strip row index, parallel to setMonStrips. Engine-side
    // mapping (mon row index → OutputChannelId) lives in MainComponent.
    std::function<void (int monIdx, float gainLinear)> onMonGain;
    std::function<void (int monIdx, bool muted)>       onMonMute;
    std::function<void (int monIdx)>                   onMonInsertChainClicked;
    std::function<void (int monIdx, DestChoice dest)>  onMonDestinationChosen;
    std::function<void (int monIdx)>                   onMonSelect;
    std::function<void (int monIdx, float pan)>        onMonPan;
    std::function<void (int monIdx, float width)>      onMonWidth;
    std::function<void (int monIdx, int fxReturnIdx, float level)>
                                                       onMonSendChanged;
    std::function<void (int monIdx, bool preFader)>    onMonPreFaderToggled;
    std::function<void (int monIdx, ida::EqConfig cfg)>  onMonEqConfigChanged;
    std::function<void (int monIdx)>                     onMonEqSlotAddRequested;
    std::function<void (int monIdx, ida::CmpConfig cfg)> onMonCmpConfigChanged;
    std::function<void (int monIdx)>                     onMonCmpSlotAddRequested;
```

- [ ] **Step 3: Add `setMonStrips` and parallel storage**

Find `void setPhraseStrips (const std::vector<PhraseStripInfo>& infos)` in the pane. Add a mirror immediately after it:

```cpp
    /// Mirror of setPhraseStrips for MON strips. Drives the leftmost
    /// strip band — strips appear when MainComponent::refreshOutputMixer-
    /// MonChannels sees a MON-on input. Order matches the input-mixer
    /// row order so the operator's spatial mental model is preserved:
    /// MON for input 1 sits leftmost; MON for input 2 sits to its right;
    /// phrase strips follow.
    void setMonStrips (const std::vector<MonStripInfo>& infos)
    {
        // (Implementation body: mirror setPhraseStrips exactly — clear
        // the existing strip child set, rebuild from infos, wire each
        // strip's listener callbacks to the onMon* relays.)
    }
```

The mirror's body MUST be a verbatim shape of `setPhraseStrips`. Read `setPhraseStrips`'s body in `app/MainComponent.cpp` and copy its structure with `phrase` → `mon`, `Phrase` → `Mon`, `onPhraseGain` → `onMonGain`, etc. Use the same `otto::ui::ChannelType` the phrase path uses (likely `ChannelType::Instrument`) so the visual style matches.

The parallel internal storage members go right next to the phrase members (search for `phraseStrips_` or `phraseStripInfos_` — wherever the phrase strip ownership lives — and add identically-shaped `monStrips_` / `monStripInfos_` lines).

- [ ] **Step 4: Extend `resized()` to allocate space for the MON band**

Find the `resized()` method on the pane. Where it computes the phrase-strip band's left edge and width, prepend a MON band that sits LEFT of the phrase band:

- The MON band occupies `monStripCount * stripWidth` pixels at the left.
- The phrase band starts at the MON band's right edge instead of at 0.
- All other bands (buses, master) shift right by the MON band's width.

The exact code is mechanical mirror — find the phrase-band layout block in `resized()`, study how it picks its `xStart` (likely `0` or `leftEdge`), and insert a MON-band layout block before it that uses the same `xStart`. Then bump the phrase band's `xStart` to start at the MON band's right edge.

When the MON band is empty (no MON-on inputs — the common default), it contributes zero width, so phrase strips land exactly where they do today.

- [ ] **Step 5: Extend `rebuildChannelPills` to include MON pills**

Find `void rebuildChannelPills()` (around line 1721). It currently builds pills in the order "phrase strips, then aux buses, then master." Update its leading comment and add MON pills FIRST (leftmost):

```cpp
    /// Channel pill row builder — mirrors InputMixerPane::rebuildChannelPills.
    /// Order: MON strips, then phrase strips, then aux buses, then master.
    void rebuildChannelPills()
    {
        channelPills_.clear();
        // (existing makePill lambda)

        for (int i = 0; i < (int) monStripInfos_.size(); ++i)
            makePill (monStripInfos_[(std::size_t) i].name, i,
                      otto::ui::ChannelType::Instrument);

        // (existing phrase / bus / master pill-building code, unchanged)
    }
```

- [ ] **Step 6: Compile-verify the pane changes**

Run: `cmake --build build --target IDA`
Expected: clean build (the new MON surface is wired into the pane but no MainComponent code calls `setMonStrips` yet, so the pane appears identical at runtime).

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "$(cat <<'EOF'
feat: OutputMixerPane gains a MON strip category (leftmost band, mirror of phrase strips)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Task 3: MainComponent — refreshOutputMixerMonChannels + triggers

**Files:**
- Modify: `app/MainComponent.h` — declare the new private method + parallel vector.
- Modify: `app/MainComponent.cpp` — implement the refresh function, wire `onMon*` relays at the same site phrase relays are wired (around line 3960-4150), call the refresh from `onMonitorModeChanged` and the session-load path.

- [ ] **Step 1: Declare the new method + storage in MainComponent.h**

Find the section in `MainComponent.h` that declares `refreshOutputMixerPhraseChannels()` and `phraseChannelByConstituent_`. Add right next to them:

```cpp
    /// Mirror of refreshOutputMixerPhraseChannels for MON strips. Walks
    /// every input channel; for those with MonitorMode::On, looks up
    /// the auto-created OutputChannelId via
    /// `InputMixer::channelMonitorOutputChannel`, and pushes a parallel
    /// `MonStripInfo` list to the OutputMixerPane. Called from the MON
    /// toggle callback, the session-load post-rebind site, and the input
    /// channel add/remove paths.
    void refreshOutputMixerMonChannels();

    /// Parallel to `phraseStripConstituentIds_`: indexed by mon-strip
    /// row, holds the *input* ChannelId whose MON-on minted that strip.
    /// Used by the onMon* relays to resolve the row to an OutputMixer
    /// channel via `InputMixer::channelMonitorOutputChannel`.
    std::vector<ida::ChannelId> monStripInputChannelIds_;
```

- [ ] **Step 2: Implement `refreshOutputMixerMonChannels`**

Add the implementation in `app/MainComponent.cpp` immediately AFTER `refreshOutputMixerPhraseChannels()` (which ends at line 5357):

```cpp
void MainComponent::refreshOutputMixerMonChannels()
{
    if (outputMixerPane_ == nullptr) return;
    if (inputMixer_ == nullptr || outputMixer_ == nullptr) return;

    // Walk the input strips in operator-visible order so the MON band
    // mirrors input-strip order left-to-right. inputStripChannelIds_ is
    // the same vector inputMixerPane_ uses for its strips, so the order
    // is exactly the operator's row order.
    std::vector<OutputMixerPane::MonStripInfo> infos;
    std::vector<ida::ChannelId>                newIds;
    infos.reserve  (inputStripChannelIds_.size());
    newIds.reserve (inputStripChannelIds_.size());

    for (std::size_t i = 0; i < inputStripChannelIds_.size(); ++i)
    {
        const auto chId = inputStripChannelIds_[i];
        if (inputMixer_->channelMonitorMode (chId) != ida::MonitorMode::On)
            continue;
        if (! inputMixer_->channelMonitorOutputChannel (chId).has_value())
            continue;

        // Display name: "MON N" where N is the 1-based input strip row.
        // Operator-named inputs are a future polish slice.
        infos.push_back ({ chId, "MON " + juce::String ((int) i + 1) });
        newIds.push_back (chId);
    }

    // Skip the pane rebuild when nothing structural changed (mirrors the
    // phrase-strip refresh's short-circuit at line 5339): avoids nuking
    // an in-progress fader drag on an unrelated MON strip.
    if (newIds == monStripInputChannelIds_) return;

    monStripInputChannelIds_ = std::move (newIds);
    outputMixerPane_->setMonStrips (infos);
}
```

- [ ] **Step 3: Wire the `onMon*` callbacks**

Find the block where `onPhrase*` relays are bound (around line 3960). Immediately AFTER that block, add the mirror for MON. Each relay must:
1. Bounds-check `monIdx` against `monStripInputChannelIds_`.
2. Resolve `inputChannelId = monStripInputChannelIds_[monIdx]`.
3. Resolve `outputChannelId = *inputMixer_->channelMonitorOutputChannel(inputChannelId)` (defensive `has_value()` check).
4. Drive the same engine API the phrase relay drives, but addressed by the resolved `OutputChannelId`.

The complete block:

```cpp
        outputMixerPane_->onMonGain = [this] (int monIdx, float gainLinear)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*outCh))
                strip->setGain (gainLinear);
        };
        outputMixerPane_->onMonMute = [this] (int monIdx, bool muted)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*outCh))
                strip->setMuted (muted);
        };
        outputMixerPane_->onMonInsertChainClicked = [this] (int monIdx)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            openInsertChainPopupForOutputChannel (*outCh);
        };
        outputMixerPane_->onMonDestinationChosen = [this]
            (int monIdx, OutputMixerPane::DestChoice dest)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;

            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case OutputMixerPane::DestKind::Bus:
                    outputMixer_->routeChannelToBus (*outCh,
                                                     ida::BusId (dest.id), 1.0f);
                    break;
                case OutputMixerPane::DestKind::HardwareOutput:
                    outputMixer_->setChannelMainOutToHardwareOutput (*outCh,
                                                                    dest.pairIndex);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshOutputDestinations();
        };
        outputMixerPane_->onMonPan = [this] (int monIdx, float pan)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*outCh))
                strip->setPan ((pan + 1.0f) * 0.5f);   // [-1,+1] → [0,1]
        };
        outputMixerPane_->onMonWidth = [this] (int monIdx, float width)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*outCh))
                strip->setWidth (width);
        };
        outputMixerPane_->onMonSendChanged = [this]
            (int monIdx, int fxReturnIdx, float level)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            applyOutputSendChange (*outCh, fxReturnIdx, level);  // mirrors phrase path
        };
        outputMixerPane_->onMonPreFaderToggled = [this] (int monIdx, bool preFader)
        {
            if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return;
            const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            outputMixer_->setChannelSendIsPreFader (*outCh, preFader);
        };
        outputMixerPane_->onMonSelect            = [] (int) {};   // wired in detail-panel slice
        outputMixerPane_->onMonEqConfigChanged   = [] (int, ida::EqConfig) {};
        outputMixerPane_->onMonEqSlotAddRequested = [] (int) {};
        outputMixerPane_->onMonCmpConfigChanged   = [] (int, ida::CmpConfig) {};
        outputMixerPane_->onMonCmpSlotAddRequested = [] (int) {};
```

Note: `openInsertChainPopupForOutputChannel` and `applyOutputSendChange` are the helpers the phrase relays use. If they don't exist by those names today, grep for what the phrase `onPhraseInsertChainClicked` and `onPhraseSendChanged` relays call and use those same helpers verbatim. (The `onMonSelect` / `onMon{Eq,Cmp}*` relays are stubs for now — detail-panel binding for MON is a follow-up; the strips still fully respond to gain/mute/pan/width/INS/destination/sends/pre-fader.)

- [ ] **Step 4: Call the refresh from the MON toggle callback**

Edit `app/MainComponent.cpp:3670-3677`. Append the refresh call inside the `onMonitorModeChanged` lambda body:

```cpp
        inputMixerPane_->onMonitorModeChanged = [this] (int idx, ida::MonitorMode mode)
        {
            if (idx < 0 || idx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (idx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->setChannelMonitorMode (chId, mode);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());

            // V9: MON now owns an OutputMixer channel (Slice 3) AND its
            // own ChannelStrip (this plan's Task 1) — so the Output
            // Mixer pane needs to gain/lose a visible strip in lockstep.
            refreshOutputMixerMonChannels();
        };
```

- [ ] **Step 5: Call the refresh from the session-load rebind site**

Edit `app/MainComponent.cpp:6543` — just after `inputMixer_->attachOutputMixer (outputMixer_.get());`. Sessions can be loaded with MON already on for some channels (the SessionFormat round-trip restores `MonitorMode`), so the refresh must run post-load too. The setChannelMonitorMode-on-load path that engages the OutputMixer channel runs during `importGraphState` — confirm via grep first; if it doesn't auto-engage routes, append a defensive loop here that walks every input channel and re-calls `inputMixer_->setChannelMonitorMode(chId, channelMonitorMode(chId))` to force the auto-channel mint, THEN call `refreshOutputMixerMonChannels()`.

Concretely, add after line 6547:

```cpp
                        // V9: replay MON-on channels into the freshly-
                        // re-attached OutputMixer so the auto-created MON
                        // channels (+ their strips, sources) come back.
                        // setChannelMonitorMode is idempotent for On→On
                        // and a no-op for unknown ids.
                        for (const auto& chId : inputStripChannelIds_)
                            if (inputMixer_->channelMonitorMode (chId) == ida::MonitorMode::On)
                                inputMixer_->setChannelMonitorMode (chId, ida::MonitorMode::On);
                        refreshOutputMixerMonChannels();
```

(If grep shows `importGraphState` ALREADY auto-engages MON-on routes via `attachOutputMixer`'s replay path — see the comment at `InputMixer.h:194-196` — the replay loop above is redundant. In that case, omit the loop and keep only the `refreshOutputMixerMonChannels()` call.)

- [ ] **Step 6: Call the refresh from input-channel add/remove paths**

Grep for sites that mutate `inputStripChannelIds_` (the input-strip ownership vector):

```bash
grep -n "inputStripChannelIds_\.\(push_back\|erase\|emplace_back\|clear\|insert\)\b" /Users/larryseyer/IDA/app/MainComponent.cpp
```

For each mutation site, add `refreshOutputMixerMonChannels();` at the end of the mutation block. Rationale: removing an input channel via the input pane also tears down its MON state in the engine (`InputMixer::removeChannel` triggers the MON cleanup at `InputMixer.cpp:174-181`), but the OutputMixerPane needs to be told to drop the corresponding MON strip.

- [ ] **Step 7: Clean rebuild**

Per the user-level CLAUDE.md "Clean rebuild before asking the operator to eyes-on a GUI change" rule:

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
cmake --build build --target IdaTests
```

Expected: clean build, no warnings about unused variables in the new code.

- [ ] **Step 8: Headless test sweep**

Run: `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"`
Expected: 712/712 pass (710 baseline + 2 new sections from Task 1). No new failures.

- [ ] **Step 9: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "$(cat <<'EOF'
feat: V9 MON strips appear in Output Mixer — refreshOutputMixerMonChannels mirrors phrase strips end-to-end

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Task 4: Operator verification

**Files:** None modified. This task confirms the V9 contract holds in the running app.

- [ ] **Step 1: Launch the freshly-built .app**

```bash
open /Users/larryseyer/IDA/build/app/IDA_artefacts/Release/IDA.app
```

- [ ] **Step 2: Verify the MON-strip-appears-and-vanishes contract**

Walk the operator through this checklist (the agent cannot do it solo — visual confirmation is the operator's per the project's "GUI changes are operator-verified" rule):

1. Open the Output Mixer pane. Note current strip count (should be: master + any aux buses + any phrase strips already present; no MON strips).
2. On any input strip, click MON to flip it On.
3. Output Mixer pane should immediately show a new strip labeled `"MON 1"` (or matching input strip's row index) as the LEFTMOST strip.
4. Confirm the strip carries fader, mute button, INS button, destination picker — identical surface to a phrase strip.
5. Drag the MON strip's fader: the audible level of the monitored input should change in lockstep.
6. Click the MON strip's mute: the monitored input should silence at master, but other strips (phrase, master itself) should continue playing.
7. Click MON off on the input strip. The corresponding Output Mixer strip should disappear immediately.
8. Repeat with a second input strip's MON to confirm two MON strips coexist in the correct row order.

- [ ] **Step 3: Verify session round-trip**

1. With one or more MON strips visible, save the project.
2. Quit the app. Relaunch. Load the project.
3. The same MON strips should reappear with the same channel position. (If they don't, file a follow-up — the session-format MON-replay path in Task 3 Step 5 may need the defensive loop variant.)

- [ ] **Step 4: Update continue.md + any affected memory**

Refresh `continue.md` per `[[feedback_update_continue_md_every_session]]`. Note the new contract surface and the test count uplift.

If the operator-verification revealed any design pivots (e.g. they want visual differentiation for MON strips), capture them in `todo.md` or as a new memory file rather than rolling them into this slice.

- [ ] **Step 5: Final commit (continue.md only)**

```bash
git add continue.md
git commit -m "$(cat <<'EOF'
docs: continue.md — MON output strips landed (4-task slice on top of V9 conformance)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Self-Review Checklist

**Spec coverage:**
- Whitepaper V9 §6.3.1: "the post-strip signal arrives at an auto-created channel in the output mixer … with its own gain, pan, inserts, and routing." Task 1 adds the strip; Tasks 2-3 expose the controls.
- Whitepaper V9 §7.2: "carries gain, pan, inserts, and routing like every other output-mixer channel, and is mixed alongside phrase playback through master." Task 3 wires gain/mute/pan/width/inserts/destination/sends/pre-fader.
- `[[project_input_output_mixers_identical]]`: MON strips share data model, APIs, and UI with phrase strips — identical shape, only difference is the source (auto-created from MON-on vs added per phrase) and the display name. ✓
- `[[feedback_default_to_professional_elegant]]`: leftmost band placement follows pro-audio signal-flow convention (live → recorded → groups → master). ✓
- `[[feedback_short_responses.md]]` does not apply to plan documents — plans deliberately specify the actual code, per the writing-plans skill.

**Placeholder scan:** Each code block in this plan is concrete and self-contained. No "TBD", no "implement later", no "similar to Task N" without code. The one externally-named symbol (`openInsertChainPopupForOutputChannel`, `applyOutputSendChange`) has an explicit grep-and-mirror instruction in Task 3 Step 3.

**Type consistency:**
- `MonStripInfo { ida::ChannelId inputChannelId; juce::String name; }` — `inputChannelId` is the input mixer's ChannelId, not OutputChannelId. The OutputChannelId is looked up via `channelMonitorOutputChannel(chId)` in the relays.
- `monStripInputChannelIds_` stores `ida::ChannelId`, matching `MonStripInfo::inputChannelId`. ✓
- `refreshOutputMixerMonChannels` builds `std::vector<OutputMixerPane::MonStripInfo>` — matches `setMonStrips`'s signature. ✓
- All `onMon*` callbacks take `(int monIdx, ...)` matching the phrase callbacks' `(int phraseIdx, ...)` shape. ✓
