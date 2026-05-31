# MON + Phrase Follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close three follow-ups left by the MON-output-strips slice
(`continue.md` §5 items 1-3):

1. **Phrase strip gain/mute → engine wiring.** `onPhraseGain` /
   `onPhraseMute` are currently no-op stubs (`MainComponent.cpp:4148-4149`).
   The MON-strip wiring landed in the previous slice is the template.
2. **`inputStripChannelIds_` staleness after session-load investigation +
   fix.** The MON-replay loop @ `MainComponent.cpp:6877-6889` relies on
   `inputStripChannelIds_` being current, but no code path obviously
   refreshes it during session load. Confirm whether it's a real bug; if
   so, fix it.
3. **MON detail panel binding** — operator-locked this chat: identical to
   phrase (Pan/Width + Sends + EQ + CMP), opens on click. Mirror the
   `onPhraseSelect` + `onPhraseEqConfigChanged` etc. wiring for MON. The
   slots are presently no-op stubs at `MainComponent.cpp:4459-4467`.

**Architecture:**

All three tasks live in `app/MainComponent.cpp` (plus minor pane additions
for #3). They share one pattern: the relay lambda resolves a strip-row
index → engine OutputChannelId, then drives `outputMixer_->...` accessors.
The MON-output-strips slice that landed in `0a58265` is the working
template — Task 1 is "copy that template to phrase," Task 3 is "extend
that template to detail panel."

**Tech Stack:** C++ / JUCE — same surfaces as the prior MON slice. No
engine changes.

---

## Task 1: Phrase strip gain/mute → engine

**Files:**
- Modify: `app/MainComponent.cpp:4148-4149` — replace the two `[] (int, ...) {}` stubs.

- [ ] **Step 1: Locate the template in the running codebase**

The MON wiring at `MainComponent.cpp:4360-4374` (after the previous slice
landed in `0a58265`) is the exact shape to mirror, just with phrase ids
instead of MON. For phrase, the resolver helper already exists right
above the current stubs — `resolvePhraseStrip` at
`MainComponent.cpp:4185-4194` returns a `ChannelStrip<SignalType::Audio>*`.
Grep to confirm:

```bash
grep -n "resolvePhraseStrip\b\|onPhraseGain\b\|onPhraseMute\b" /Users/larryseyer/IDA/app/MainComponent.cpp
```

Expected: `resolvePhraseStrip` is a `this`-capturing lambda already in
scope at the call site of the stubs.

- [ ] **Step 2: Replace the stubs with engine-driving lambdas**

Find this exact block in `app/MainComponent.cpp` (currently at lines 4148-4149):

```cpp
        outputMixerPane_->onPhraseGain               = [] (int, float) {};
        outputMixerPane_->onPhraseMute               = [] (int, bool)  {};
        outputMixerPane_->onPhraseInsertChainClicked = [] (int)        {};
```

The `resolvePhraseStrip` lambda is defined later in scope (line ~4185), so
move the assignment for `onPhraseGain` and `onPhraseMute` to AFTER
`resolvePhraseStrip` is defined. Or: define a small `resolvePhraseStripEarly`
above the stubs. Simplest: copy the resolver body inline.

Replace the two stubs above with:

```cpp
        outputMixerPane_->onPhraseInsertChainClicked = [] (int)        {};
```

(keeping the InsertChainClicked stub — that's a separate slice).

Then, AFTER `resolvePhraseStrip` is defined (around line 4194), add:

```cpp
        outputMixerPane_->onPhraseGain = [resolvePhraseStrip] (int phraseIdx, float gainLinear)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setGain (gainLinear);
        };
        outputMixerPane_->onPhraseMute = [resolvePhraseStrip] (int phraseIdx, bool muted)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setMuted (muted);
        };
```

This mirrors the MON wiring at `MainComponent.cpp:4360-4374` exactly.

- [ ] **Step 3: Build + sanity check**

```bash
cmake --build build --target IDA
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "$(cat <<'EOF'
fix: phrase strip gain/mute drive the engine ChannelStrip (mirror of MON wiring)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Task 2: `inputStripChannelIds_` staleness after session-load

**Files:**
- Investigate: `app/MainComponent.cpp` — the session-load block around `:6852-6890` and `rebuildInputStrips` around `:5990-6018`.
- Possibly modify: `app/MainComponent.cpp` — add a `rebuildInputStrips()` call (or equivalent) into the session-load path if missing.

- [ ] **Step 1: Grep how inputStripChannelIds_ is populated**

```bash
grep -n "inputStripChannelIds_\b" /Users/larryseyer/IDA/app/MainComponent.cpp
```

Expected: a small handful of sites. The clear/refill pattern lives in
`rebuildInputStrips()` at lines `:5995-6019`. The question is: does any
code call `rebuildInputStrips()` after a session load?

- [ ] **Step 2: Trace the session-load → input-strip-refresh path**

```bash
grep -n "rebuildInputStrips\b" /Users/larryseyer/IDA/app/MainComponent.cpp
```

Walk every call site. The load block is at approximately
`MainComponent.cpp:6750-6900`. Check whether `rebuildInputStrips()` is
called anywhere within that block, or anywhere downstream (e.g. in a
`refreshAll()` helper called after the block).

- [ ] **Step 3: Decide and act**

**If `rebuildInputStrips()` IS called during/after session load:**
- This is a non-issue. The MON replay loop sees the freshly-rebuilt ids.
- Skip Step 4. Go straight to Step 5 (close the task with a no-op commit
  or just delete the queued item).

**If `rebuildInputStrips()` is NOT called during/after session load:**
- That's a real bug. The MON-replay loop @ `MainComponent.cpp:6877-6889`
  walks `loadedInputMixer->channels` directly (so the engine side is
  fine) but the GUI side (`refreshOutputMixerMonChannels()`) walks
  `inputStripChannelIds_`, which would be stale.
- Wait — actually re-read the MON-replay loop carefully: it walks
  `loadedInputMixer->channels` for the ENGINE replay, NOT
  `inputStripChannelIds_`. So engine state is correct. The GUI refresh
  fires later via `refreshPreparation()` → `refreshOutputMixerMonChannels()`
  which DOES walk `inputStripChannelIds_`. If those ids are stale, the
  GUI won't show MON strips for the right inputs.
- Fix: add a `rebuildInputStrips();` call in the session-load block
  after the engine state is restored. Place it right before
  `refreshTapesPane();` at line `:6890`.

```cpp
                // V9: re-derive input strips from the freshly-loaded
                // InputMixer so subsequent refreshOutputMixerMonChannels
                // resolves the right ChannelIds.
                rebuildInputStrips();
                refreshTapesPane();
```

- [ ] **Step 4 (only if Step 3 found a real bug): build + sanity**

```bash
cmake --build build --target IDA
```

Then operator-verify by loading a project saved with MON-on and
confirming the MON strips reappear. (See Task 4 / operator verification.)

- [ ] **Step 5: Commit OR skip**

If a fix landed:
```bash
git add app/MainComponent.cpp
git commit -m "$(cat <<'EOF'
fix: rebuild input strips after session load so MON refresh resolves fresh ids

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

If the investigation revealed no bug, no commit. Note the finding in
`continue.md` so the queued item is retired.

---

## Task 3: MON detail panel binding

**Files:**
- Modify: `app/MainComponent.cpp` — add `showMonDetailFor` on
  `OutputMixerPane`, mirror of `showPhraseDetailFor` at lines `:1910-1932`.
- Modify: `app/MainComponent.cpp` — replace the 5 MON stub relays at
  `:4459-4467` with the real wiring.
- Possibly modify: `app/MainComponent.cpp` — add MON-flavored
  `collectOutputSendsView` / `collectOutputEqView` / `collectOutputCmpView`
  helpers if the existing ones are phrase-only. (Grep first.)

**Design lock (operator answered 2026-05-24):**
- MON detail panel is **identical to phrase** (Pan/Width + Sends + EQ + CMP).
- Clicking a MON strip's name **opens the detail panel** (not just highlight).

- [ ] **Step 1: Grep the phrase detail-panel helpers**

```bash
grep -n "collectOutputSendsView\|collectOutputEqView\|collectOutputCmpView\|showPhraseDetailFor" /Users/larryseyer/IDA/app/MainComponent.cpp
```

Expected: `collectOutput{Sends,Eq,Cmp}View` are MainComponent member
functions taking `int phraseIdx` and resolving to phrase's
OutputChannelId. For MON, you have two options (decide in this step):

**A. Add MON-specific overloads** taking `int monIdx`.
- Pros: parallel structure, easy to grep, mirrors phrase wiring shape.
- Cons: ~3 small near-duplicate functions.

**B. Refactor the existing collectors** to take an `OutputChannelId`
directly, then both phrase and MON call sites pass the resolved id.
- Pros: DRY.
- Cons: changes phrase call sites too; risk of subtle regression.

**Recommendation: A** (parallel structure, safer surgical change matching
the user-level rule "surgical changes — touch only what you must"). If
the operator wants a future cleanup pass, the unification is a separate
slice.

Use option A unless the next chat sees a reason to switch.

- [ ] **Step 2: Add `showMonDetailFor` on OutputMixerPane**

Find `void showPhraseDetailFor` at `MainComponent.cpp:1910`. Add the MON
mirror immediately after it (around line 1933):

```cpp
    void showMonDetailFor (int monIdx, float panMinus1to1, float width,
                           std::vector<ida::ui::FxReturnInfo> fxReturns,
                           std::vector<float> sendLevels,
                           bool preFader,
                           ida::EqConfig eqConfig, bool hasEqSlot,
                           ida::CmpConfig cmpConfig, bool hasCmpSlot)
    {
        if (monIdx < 0 || monIdx >= monStripCount()) return;
        detailPanel_.setChannel (monIdx, otto::ui::ChannelType::FXReturn);
        detailPanel_.panWidTab().setPan (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   preFader });
        detailPanel_.eqTab().setChannelState ({ eqConfig, hasEqSlot });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        // MON strips: identical surface to phrase (operator design lock).
        detailPanel_.setTabsAvailable ({ true, true, true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }
```

- [ ] **Step 3: Add MON-flavored collectors in MainComponent**

Find `collectOutputSendsView`, `collectOutputEqView`, `collectOutputCmpView`
(use grep). Each takes an `int phraseIdx` and reads engine state via a
`phraseChannelByConstituent_` lookup. Add MON-flavored siblings that take
an `int monIdx` and resolve via `inputMixer_->channelMonitorOutputChannel`:

```cpp
// In MainComponent.h: declarations next to the existing collectors.
auto collectMonSendsView (int monIdx);    // same return type as phrase version
auto collectMonEqView    (int monIdx);
auto collectMonCmpView   (int monIdx);
```

```cpp
// In MainComponent.cpp: implementations alongside the phrase versions.
// Resolve monIdx → OutputChannelId via:
//   if (monIdx < 0 || monIdx >= (int) monStripInputChannelIds_.size()) return {};
//   const auto chId = monStripInputChannelIds_[(std::size_t) monIdx];
//   const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
//   if (! outCh.has_value()) return {};
//   // then identical body to the phrase collector, using *outCh.
```

Copy each phrase collector verbatim, swapping the row→OutputChannelId
resolver for the MON one above.

- [ ] **Step 4: Replace the MON stub relays with real wiring**

Find the block at `MainComponent.cpp:4459-4467`:

```cpp
        outputMixerPane_->onMonSelect              = [] (int) {};
        outputMixerPane_->onMonEqConfigChanged     = [] (int, ida::EqConfig)  {};
        outputMixerPane_->onMonEqSlotAddRequested  = [] (int)                 {};
        outputMixerPane_->onMonCmpConfigChanged    = [] (int, ida::CmpConfig) {};
        outputMixerPane_->onMonCmpSlotAddRequested = [] (int)                 {};
```

The `resolveMonChannelId` lambda is already in scope above (line ~4386 —
re-grep to confirm). Replace the 5 stubs with:

```cpp
        outputMixerPane_->onMonSelect = [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* s = outputMixer_->audioStripForChannel (*chOpt);
            if (s == nullptr) return;
            auto sends = collectMonSendsView (monIdx);
            const auto eqProbe  = collectMonEqView  (monIdx);
            const auto cmpProbe = collectMonCmpView (monIdx);
            outputMixerPane_->showMonDetailFor (monIdx,
                                                s->pan() * 2.0f - 1.0f,
                                                s->width(),
                                                std::move (sends.fxReturns),
                                                std::move (sends.sendLevels),
                                                sends.preFader,
                                                eqProbe.config,  eqProbe.hasSlot,
                                                cmpProbe.config, cmpProbe.hasSlot);
        };
        outputMixerPane_->onMonEqConfigChanged =
            [this] (int monIdx, ida::EqConfig cfg)
        {
            const auto probe = collectMonEqView (monIdx);
            if (! probe.hasSlot) return;
            if (monIdx < 0
                || monIdx >= static_cast<int> (monStripInputChannelIds_.size())) return;
            const auto chId = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (outCh->value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMonCmpConfigChanged =
            [this] (int monIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectMonCmpView (monIdx);
            if (! probe.hasSlot) return;
            if (monIdx < 0
                || monIdx >= static_cast<int> (monStripInputChannelIds_.size())) return;
            const auto chId = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
            const auto outCh = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outCh.has_value()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (outCh->value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMonEqSlotAddRequested =
            [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onMonSelect (monIdx);
        };
        outputMixerPane_->onMonCmpSlotAddRequested =
            [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onMonSelect (monIdx);
        };
```

The shape is exactly the phrase wiring from `MainComponent.cpp:4272-4329`
with `phrase` → `mon` substitutions.

- [ ] **Step 5: Handle pan/width/sends/preFader callbacks**

These are already wired this slice — `onMonPan`, `onMonWidth`,
`onMonSendChanged`, `onMonPreFaderToggled` actively drive engine state.
But they only fire when the detail panel is visible (operator can change
Pan/Width). Verify after Step 4 that they still work via the panel —
should be automatic since the panel surface is unchanged.

- [ ] **Step 6: Clean rebuild**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
cmake --build build --target IdaTests
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"
```

Expected: 709/709 pass.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "$(cat <<'EOF'
feat: MON strip detail panel — identical surface to phrase (Pan/Width + Sends + EQ + CMP)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Task 4: Operator verification + continue.md refresh

- [ ] **Step 1: Launch the fresh build**

```bash
open /Users/larryseyer/IDA/build/app/IDA_artefacts/Release/IDA.app
```

- [ ] **Step 2: Verify Task 1 — phrase fader/mute**

Drag a phrase strip's fader → audible level should change (today it does
not, that's the bug we're fixing). Click phrase strip's mute → that
phrase should silence at master.

- [ ] **Step 3: Verify Task 2 — session round-trip with MON**

Set MON on for one input. Save the project. Quit. Relaunch. Load the
project. The MON strip should reappear with its original position. The
fader / mute / destination should still work.

- [ ] **Step 4: Verify Task 3 — MON detail panel**

Click a MON strip's name area. The detail panel should reveal below the
mixer with four tabs (Pan/Width, Sends, EQ, CMP). Each control should
drive the engine on the MON strip:
- Drag Pan → audible position shifts.
- Drag Width → stereo image narrows/widens.
- Add an EQ slot, tweak a band → audible filter on the MON signal.
- Add a CMP slot, tune threshold → audible dynamics on the MON signal.

- [ ] **Step 5: Refresh continue.md**

Note all three follow-ups closed. Remove items 1-3 from `continue.md` §5.

- [ ] **Step 6: Commit continue.md**

```bash
git add continue.md
git commit -m "$(cat <<'EOF'
docs: continue.md — MON+phrase follow-ups landed (gain/mute, session-load, detail panel)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Self-Review

**Spec coverage:**
- Item 1 (phrase gain/mute → engine): Task 1. ✓
- Item 2 (`inputStripChannelIds_` staleness): Task 2 (investigate-first; may be a no-op). ✓
- Item 3 (MON detail panel): Task 3, with operator-locked "identical to phrase, opens on click" design. ✓

**Placeholder scan:** Every code block is concrete. The one "decide here"
moment in Task 3 Step 1 (option A vs option B for collectors) is a real
fork with a clear recommendation and rationale, not a deferral.

**Type consistency:**
- `MonStripInfo`, `monStripCount`, `monStripInputChannelIds_`,
  `resolveMonChannelId` are all already in the codebase (landed
  `0a58265`). The plan references them correctly.
- `collectMonSendsView` / `collectMonEqView` / `collectMonCmpView` are
  NEW in Task 3 Step 3. Their return types must match the existing
  phrase versions — Step 3 instructs to copy-and-modify, which preserves
  type identity.
