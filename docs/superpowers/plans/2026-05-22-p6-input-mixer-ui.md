# P6 — Input Mixer UI (creation + routing) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Input Mixer pane visible bus / FX-return strips, a blank-area gesture to create them, and a per-channel destination picker that routes a channel's main-out to a tape, a bus, or the hardware (direct) output.

**Architecture:** Pure UI work in `app/MainComponent.cpp` (the `InputMixerPane` inner class) + `app/MainComponent.h`. The engine routing graph, buses, FX returns, and all main-out setters already exist and are unit-tested at the `engine` layer — P6 only wires the existing `InputMixer` API to OTTO-vendored `CompactFaderStrip` widgets. Bus strips are a **second strip vector** alongside the channel strips so the existing channel-strip gestures, pickers, and selection logic are untouched. The destination picker mirrors the just-shipped T6 tape picker (`InputMixerPane`'s own `TextButton` + `PopupMenu`, **not** `CompactFaderStrip`'s output combo).

**Tech Stack:** C++17, JUCE 8, vendored `otto::ui::CompactFaderStrip`, the `sirius::InputMixer` / `sirius::Bus` engine API.

---

## Scope decisions (locked before tasks)

- **Picker is per-channel only.** Channels are pure sources (never a main-out *target*), so channel→{tape,bus,output} can never form a cycle — no acyclic-filtering query is needed. The spec's "offering only acyclic destinations" is satisfied trivially. Routing a **bus** node's main-out (bus→bus can cycle) needs an engine acyclic query that does not exist; adding it would expand a UI-only phase into engine work. **Deferred to P7.** This matches continue.md's wording: *"a per-channel bus destination picker."*
- **No bus removal.** `InputMixer` exposes `addBus`/`addFxReturn` but **no** `removeBus`/`removeFxReturn`. Bus/FX-return removal is therefore out of P6 (no engine support). Do not invent a UI that calls a non-existent setter.
- **Do NOT re-enable `CompactFaderStrip::setOutputComboVisible`.** That combo is OTTO's hardware-pair model. Render the destination picker as the pane's own control (T6 pattern). (continue.md ⚠, overrides the spec's "re-enable the output combo" line, which predates the T6 picker.)
- **Bus strips have no destination picker and no detail panel in P6.** They get fader / mute / solo / dual peak+LUFS meter only. Their default main-out (RVB/DLY → hardware output) is seeded by the `InputMixer` ctor.

## File structure

- **`app/MainComponent.cpp`** — the `InputMixerPane` inner class (lines 442–772) gains a bus-strip vector, a generalized destination model, and a 3-item blank-area menu; the `MainComponent` methods `refreshInputMixer` (1892), `refreshInputDestinations` (1918), `rebuildInputStrips` (1944), and the relay-wiring block (~1544–1598) gain bus enumeration / metering / creation / routing.
- **`app/MainComponent.h`** — declare `rebuildBusStrips()`, the `busStripIds_` vector, and `refreshBusMeters()` (or fold into `refreshInputMixer`).

No other files change. No new CMake targets. No engine edits.

## Testing reality (read before starting)

Per `CLAUDE.md`: **GUI changes are operator-verified, not unit-tested** — `InputMixerPane` lives in `app/` and is not in the `SiriusTests` target. So tasks below do **not** follow red/green TDD; each task ends with a **compile-green gate** (`cmake --build build --target SiriusLooper`) plus a **no-regression gate** (`ctest --test-dir build`, baseline 567 pass / 1 documented Not-Run) and a commit. Final acceptance is operator eyes-on the `.app` after a clean `rm -rf build` rebuild (Task 4). Do not claim a task "works" — claim it "compiles green, ctest baseline holds, ready for eyes-on."

---

### Task 1: Render bus / FX-return strips (display + fader/mute/solo + meter)

On launch the `InputMixer` ctor already seeds two FX returns (RVB busId N, DLY busId N+1). This task makes them appear as strips to the right of the channel strips, with working fader/mute/solo and a live dual peak+LUFS meter.

**Files:**
- Modify: `app/MainComponent.cpp` — `InputMixerPane` (442–772), `refreshInputMixer` (1892–1912), `rebuildInputStrips` (1944–2033), relay-wiring block (~1544–1598)
- Modify: `app/MainComponent.h` — add `rebuildBusStrips()` decl + `busStripIds_` member

- [ ] **Step 1: Add the bus-strip data model + relays to `InputMixerPane`.**

In the public relays block (after `onAddTape`, ~line 490) add:

```cpp
    /// A bus/FX-return strip's fader/mute/solo changed (busIdx = index into the
    /// pane's bus-strip row, parallel to MainComponent::busStripIds_).
    std::function<void (int busIdx, float gainLinear)> onBusGain;
    std::function<void (int busIdx, bool muted)>       onBusMute;
    std::function<void (int busIdx, bool soloed)>      onBusSolo;
```

In the private members block (after `strips_`/`destButtons_`, ~line 762) add:

```cpp
    /// Bus + FX-return strips — a SECOND row to the right of the channel strips.
    /// Kept separate so channel-strip gestures/pickers/selection stay untouched.
    /// These get fader/mute/solo + meter only (no destination picker, no detail
    /// panel) in P6; their index space is independent of the channel strips.
    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>> busStrips_;
```

Add a small struct near `StripInfo` (~line 460):

```cpp
    /// One bus/FX-return strip's display state. `isFxReturn` picks the
    /// CompactFaderStrip ChannelType (FXReturn vs Bus).
    struct BusInfo { juce::String name; bool isFxReturn; };
```

- [ ] **Step 2: Add `setBusStrips`, bus-strip meter setters, and bus-strip layout to `InputMixerPane`.**

Add public methods (near `setStrips`, after line 542):

```cpp
    /// Rebuilds the bus/FX-return strip row from `infos`. Each becomes a
    /// CompactFaderStrip typed Bus or FXReturn. Unlike channel strips these are
    /// NOT given addMouseListener (no pane gesture applies) — only addListener
    /// for fader/mute/solo. No destination picker button is created.
    void setBusStrips (const std::vector<BusInfo>& infos)
    {
        busStrips_.clear();
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            const auto& info = infos[static_cast<std::size_t> (i)];
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, info.isFxReturn ? otto::ui::ChannelType::FXReturn
                                   : otto::ui::ChannelType::Bus);
            strip->setChannelName (info.name);
            strip->setOutputComboVisible (false);   // pane owns routing, not the combo
            strip->addListener (this);               // fader/mute/solo only
            addAndMakeVisible (*strip);
            busStrips_.push_back (std::move (strip));
        }
        resized();
    }

    [[nodiscard]] int busStripCount() const noexcept
    {
        return static_cast<int> (busStrips_.size());
    }

    void setBusStripLevelDb (int busIdx, float dbL, float dbR)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLevel (dbL, dbR);
    }

    void setBusStripLufs (int busIdx, float lufs)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLUFSLevel (lufs);
    }
```

In `resized()` (lines 625–653), after the channel-strip + picker loop, lay out the bus strips to the right with a divider gap:

```cpp
        // Bus / FX-return strips sit to the right of the channel strips, after a
        // wider divider gap. They have no picker row beneath them.
        if (busStripCount() > 0)
            area.removeFromLeft (kGap * 3);   // visual divider between the two groups
        for (int i = 0; i < busStripCount(); ++i)
        {
            busStrips_[static_cast<std::size_t> (i)]->setBounds (area.removeFromLeft (kStripW));
            area.removeFromLeft (kGap);
        }
```

(`kStripW` is already declared earlier in `resized()` at line 645; reuse it — do not redeclare.)

- [ ] **Step 3: Branch the listener callbacks on `ChannelType` so bus strips relay to the bus handlers.**

Replace the three `CompactFaderStripListener` overrides (lines 658–669) so a Bus/FXReturn strip routes to `onBus*` (the `idx` it carries is its bus-strip index) and selection is ignored for bus strips:

```cpp
    void stripGainChanged (int idx, otto::ui::ChannelType type, float gain) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusGain) onBusGain (idx, gain); }
        else if (onGain) onGain (idx, gain);
    }
    void stripMuteChanged (int idx, otto::ui::ChannelType type, bool muted) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusMute) onBusMute (idx, muted); }
        else if (onMute) onMute (idx, muted);
    }
    void stripSoloChanged (int idx, otto::ui::ChannelType type, bool soloed) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusSolo) onBusSolo (idx, soloed); }
        else if (onSolo) onSolo (idx, soloed);
    }
    void stripChannelSelected (int idx, otto::ui::ChannelType type) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
            return;   // bus strips have no detail panel in P6
        for (int i = 0; i < stripCount(); ++i)
            strips_[static_cast<std::size_t> (i)]->setSelected (i == idx);
        selectedStrip_ = idx;
        if (onSelect) onSelect (idx);
    }
```

- [ ] **Step 4: Add `rebuildBusStrips()` + `busStripIds_` to `MainComponent`.**

In `app/MainComponent.h`, near `rebuildInputStrips` and `inputStripChannelIds_`, declare:

```cpp
    void rebuildBusStrips();
    std::vector<sirius::BusId> busStripIds_;   // parallel to InputMixerPane bus strips
```

In `app/MainComponent.cpp`, add after `rebuildInputStrips` (after line 2033):

```cpp
void MainComponent::rebuildBusStrips()
{
    if (inputMixerPane_ == nullptr || inputMixer_ == nullptr) return;

    busStripIds_.clear();
    std::vector<InputMixerPane::BusInfo> infos;
    const int n = inputMixer_->busCount();
    infos.reserve (static_cast<std::size_t> (n));
    busStripIds_.reserve (static_cast<std::size_t> (n));

    const double sampleRate = audioCallback_->currentSampleRate();
    for (int i = 0; i < n; ++i)
    {
        const auto id   = inputMixer_->busIdAt (i);
        const bool isFx = inputMixer_->busKindAt (i) == sirius::BusKind::FxReturn;
        if (auto* bus = inputMixer_->busForId (id))
        {
            bus->prepare (sampleRate, kInputLufsMaxBlock);   // EBU R128, off the audio thread
            infos.push_back ({ juce::String (bus->config().name), isFx });
            busStripIds_.push_back (id);
        }
    }
    inputMixerPane_->setBusStrips (infos);
}
```

(`kInputLufsMaxBlock` is the same constant `rebuildInputStrips` uses at line 2024. `sirius::BusKind` comes from `sirius/Bus.h`, already transitively included via `InputMixer.h`.)

- [ ] **Step 5: Wire the bus fader/mute/solo relays + call `rebuildBusStrips()` at setup.**

In the relay-wiring block, after `inputMixerPane_->onAddTape = ...` (line 1598), add:

```cpp
        inputMixerPane_->onBusGain = [this] (int busIdx, float gain)
        {
            if (busIdx >= 0 && busIdx < static_cast<int> (busStripIds_.size()))
                if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (busIdx)]))
                    bus->setGain (gain);
        };
        inputMixerPane_->onBusMute = [this] (int busIdx, bool muted)
        {
            if (busIdx >= 0 && busIdx < static_cast<int> (busStripIds_.size()))
                if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (busIdx)]))
                    bus->setMuted (muted);
        };
        inputMixerPane_->onBusSolo = [this] (int busIdx, bool soloed)
        {
            // Bus solo is not yet an engine concept on the input side; reflect it
            // on the strip only (no-op on the mix) until a bus-solo slice lands.
            juce::ignoreUnused (busIdx, soloed);
        };
```

Then call `rebuildBusStrips();` immediately after the `rebuildInputStrips();` call in the same setup path (find the existing `rebuildInputStrips()` invocation in the ctor / device-change handler and add the bus rebuild right after it).

- [ ] **Step 6: Add the bus-meter loop to `refreshInputMixer()`.**

In `refreshInputMixer()` (1892–1912), after the channel-strip meter loop (after line 1909, before `refreshInputDestinations();`) add:

```cpp
    for (int i = 0; i < static_cast<int> (busStripIds_.size()); ++i)
        if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (i)]))
        {
            inputMixerPane_->setBusStripLevelDb (i, linToDb (bus->peakLeft()),
                                                 linToDb (bus->peakRight()));
            inputMixerPane_->setBusStripLufs (i, bus->lufsIntegrated());
        }
```

(`linToDb` is the lambda already defined at the top of `refreshInputMixer`.)

- [ ] **Step 7: Compile-green gate.**

Run: `cmake --build build --target SiriusLooper`
Expected: builds with no errors or warnings (the app target is `-Werror`).

- [ ] **Step 8: No-regression gate.**

Run: `ctest --test-dir build`
Expected: 567 pass, 1 Not-Run (the documented `MainComponentPluginEditorTests` exe) — i.e. baseline unchanged (no engine code touched).

- [ ] **Step 9: Commit.**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "feat: P6 T1 — render Input Mixer bus/FX-return strips (RVB/DLY visible, fader+mute+meter)"
```

---

### Task 2: Blank-area creation gesture — Add bus / Add FX return / Add tape

**Files:**
- Modify: `app/MainComponent.cpp` — `showBlankAreaMenu` (723–729), `InputMixerPane` relays (~490), relay-wiring block (~1598)

- [ ] **Step 1: Add `onAddBus` / `onAddFxReturn` relays to `InputMixerPane`.**

After `onAddTape` (line 490):

```cpp
    /// Blank-pane-area "Add bus" / "Add FX return" gestures. MainComponent
    /// creates the engine node (bracketed) and rebuilds the bus-strip row.
    std::function<void()> onAddBus;
    std::function<void()> onAddFxReturn;
```

- [ ] **Step 2: Extend `showBlankAreaMenu` to three items.**

Replace `showBlankAreaMenu` (723–729) with:

```cpp
    void showBlankAreaMenu (juce::Point<int> screenPos)
    {
        juce::PopupMenu menu;
        menu.addItem ("Add bus",       [this] { if (onAddBus)       onAddBus(); });
        menu.addItem ("Add FX return", [this] { if (onAddFxReturn) onAddFxReturn(); });
        menu.addItem ("Add tape",      [this] { if (onAddTape)      onAddTape(); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
    }
```

- [ ] **Step 3: Wire the creation relays in `MainComponent` (bracketed topology mutation).**

After the `onBusSolo` wiring from Task 1 (~line 1598), add:

```cpp
        inputMixerPane_->onAddBus = [this]
        {
            if (inputMixer_->busCount() >= sirius::InputMixer::kMaxInputBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->addBus (sirius::BusConfig{ /*channelCount*/ 2,
                                 "Bus " + std::to_string (inputMixer_->busCount() + 1),
                                 sirius::BusKind::Bus });
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildBusStrips();
            refreshInputDestinations();   // a new bus is a new channel destination
        };
        inputMixerPane_->onAddFxReturn = [this]
        {
            if (inputMixer_->busCount() >= sirius::InputMixer::kMaxInputBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->addFxReturn ("FX " + std::to_string (inputMixer_->busCount() + 1));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildBusStrips();
            refreshInputDestinations();
        };
```

(`addBus`/`addFxReturn` mutate the graph node set → must be bracketed exactly like `addTape` at line 2114. `refreshInputDestinations` is extended in Task 3; calling it now is harmless — it currently rebuilds tape-only choices.)

- [ ] **Step 4: Compile-green gate.**

Run: `cmake --build build --target SiriusLooper`
Expected: builds clean.

- [ ] **Step 5: No-regression gate.**

Run: `ctest --test-dir build`
Expected: baseline unchanged (567 pass / 1 Not-Run).

- [ ] **Step 6: Commit.**

```bash
git add app/MainComponent.cpp
git commit -m "feat: P6 T2 — blank-area gesture creates buses / FX returns (live, bracketed)"
```

---

### Task 3: Per-channel destination picker — tape / bus / direct-out

Generalize the T6 tape-only picker so a channel can route its main-out to a pooled tape, a bus, or the hardware (direct) output, reflecting the channel's current main-out.

**Files:**
- Modify: `app/MainComponent.cpp` — `InputMixerPane` destination structs (462–469), `setDestinations` (548–559), `showDestinationMenu` (734–748), `onDestinationChosen` (484–486 decl + 1585–1595 handler), `refreshInputDestinations` (1918–1942)

- [ ] **Step 1: Generalize the destination model in `InputMixerPane`.**

Replace `TapeChoice` / `StripDest` (462–469) with a tagged model:

```cpp
    /// A routing destination kind for a channel's main-out.
    enum class DestKind { Tape, Bus, HardwareOutput };
    /// One selectable destination in a strip's picker. `id` is the TapeId or
    /// BusId raw value (unused / 0 for HardwareOutput). Identity for ticking is
    /// the (kind, id) pair — names are user-editable and non-unique.
    struct DestChoice { DestKind kind; std::int64_t id; juce::String name; };
    /// A strip's current destination (what the button shows + what ticks).
    /// kind defaults to Tape + id 0 (no-match sentinel; pool ids start at 1).
    struct StripDest { DestKind currentKind { DestKind::Tape };
                       std::int64_t currentId { 0 };
                       juce::String currentName; };
```

Change the `onDestinationChosen` relay (484–486) to carry the choice:

```cpp
    /// A destination was chosen from strip `idx`'s picker. MainComponent applies
    /// the matching engine main-out edit (tape / bus / hardware output).
    std::function<void (int idx, DestChoice dest)> onDestinationChosen;
```

Change the `choices_` member type (line 765) from `std::vector<TapeChoice>` to `std::vector<DestChoice>`.

- [ ] **Step 2: Update `setDestinations` to the new types.**

Replace `setDestinations` (548–559):

```cpp
    void setDestinations (const std::vector<DestChoice>& choices,
                          const std::vector<StripDest>& perStrip)
    {
        choices_ = choices;
        jassert (static_cast<int> (perStrip.size()) == stripCount());
        for (int i = 0; i < stripCount() && i < static_cast<int> (perStrip.size()); ++i)
        {
            stripDests_[static_cast<std::size_t> (i)] = perStrip[static_cast<std::size_t> (i)];
            const auto& label = perStrip[static_cast<std::size_t> (i)].currentName;
            destButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }
```

- [ ] **Step 3: Update `showDestinationMenu` to tick by (kind, id) and emit a `DestChoice`.**

Replace `showDestinationMenu` (734–748):

```cpp
    void showDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= stripCount()) return;
        if (choices_.empty()) return;
        const auto& cur = stripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices_)
        {
            const bool ticked = choice.kind == cur.currentKind && choice.id == cur.currentId;
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onDestinationChosen) onDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            destButtons_[static_cast<std::size_t> (idx)].get()));
    }
```

- [ ] **Step 4: Build the full choice list + current state in `refreshInputDestinations`.**

Replace `refreshInputDestinations` (1918–1942):

```cpp
void MainComponent::refreshInputDestinations()
{
    if (inputMixerPane_ == nullptr) return;
    using Pane = InputMixerPane;

    // Choice list (shared for every strip's popup): pooled tapes, then buses /
    // FX returns, then the direct (hardware-output) terminal.
    std::vector<Pane::DestChoice> choices;
    for (const auto& t : tapePool_.tapes())
        choices.push_back ({ Pane::DestKind::Tape, t.id, juce::String (t.name) });
    for (int i = 0; i < inputMixer_->busCount(); ++i)
        if (auto* bus = inputMixer_->busForId (inputMixer_->busIdAt (i)))
            choices.push_back ({ Pane::DestKind::Bus,
                                 static_cast<std::int64_t> (inputMixer_->busIdAt (i)),
                                 juce::String (bus->config().name) });
    choices.push_back ({ Pane::DestKind::HardwareOutput, 0, "Direct out" });

    // Per-strip current destination, read back from the engine's main-out.
    std::vector<Pane::StripDest> perStrip;
    perStrip.reserve (inputStripChannelIds_.size());
    for (const auto& chId : inputStripChannelIds_)
    {
        Pane::StripDest dest;
        switch (inputMixer_->channelMainOut (chId))
        {
            case sirius::InputMixer::MainOutDest::Tape:
                for (const auto& t : tapePool_.tapes())
                    if (inputMixer_->channelMainOutIsTape (chId, t.id))
                    {
                        dest.currentKind = Pane::DestKind::Tape;
                        dest.currentId   = t.id;
                        dest.currentName = juce::String (t.name);
                        break;
                    }
                break;
            case sirius::InputMixer::MainOutDest::Bus:
                for (int i = 0; i < inputMixer_->busCount(); ++i)
                {
                    const auto bid = inputMixer_->busIdAt (i);
                    // channelMainOut == Bus, but the engine exposes no
                    // "which bus" query; match by re-asking the graph is not
                    // available, so label generically. (A "which bus" accessor
                    // is a P7 nicety; for P6 the tick falls through to no-match
                    // and the button shows the bus name only after selection.)
                    juce::ignoreUnused (bid);
                    break;
                }
                dest.currentKind = Pane::DestKind::Bus;
                dest.currentName = "Bus";
                break;
            case sirius::InputMixer::MainOutDest::HardwareOutput:
                dest.currentKind = Pane::DestKind::HardwareOutput;
                dest.currentId   = 0;
                dest.currentName = "Direct out";
                break;
        }
        perStrip.push_back (std::move (dest));
    }
    inputMixerPane_->setDestinations (choices, perStrip);
}
```

> **Note for the executor:** the `Bus` case cannot tick the *specific* bus because `InputMixer` has no "which bus is this channel's main-out" accessor (it only has `channelMainOut → MainOutDest` and the tape-specific `channelMainOutIsTape`). For P6 the button still updates correctly on selection (Step 5 sets the label), and re-opening shows the generic "Bus" current label. If exact bus tick-back is wanted, add `BusId channelMainOutBus(ChannelId)` to the engine — but that is engine work; **leave it for P7** and keep this comment so the gap is honest.

- [ ] **Step 5: Route by kind in the `onDestinationChosen` handler.**

Replace the `onDestinationChosen` handler (1585–1595):

```cpp
        inputMixerPane_->onDestinationChosen = [this] (int idx, InputMixerPane::DestChoice dest)
        {
            if (idx < 0 || idx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (idx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case InputMixerPane::DestKind::Tape:
                    inputMixer_->setChannelMainOutToTape (chId, static_cast<sirius::TapeId> (dest.id));
                    break;
                case InputMixerPane::DestKind::Bus:
                    inputMixer_->setChannelMainOutToBus (chId, static_cast<sirius::BusId> (dest.id));
                    break;
                case InputMixerPane::DestKind::HardwareOutput:
                    inputMixer_->setChannelMainOutToHardwareOutput (chId);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshInputMixer();
        };
```

> **Looper-invariant caveat (honest carry-forward):** routing a channel away from every tape (to a bus or direct-out) can in principle drop the channels-recording-to-tape count to zero, which the `project_looper_at_least_one_tape_invariant` memory forbids. Active floor-enforcement is explicitly assigned to the **input→output bridge slice (P8)**, not P6 (see todo.md / continue.md). P6 wires the picker; it does **not** add floor-enforcement. Do not claim the invariant is enforced.

- [ ] **Step 6: Compile-green gate.**

Run: `cmake --build build --target SiriusLooper`
Expected: builds clean. (Watch for the `choices_` type change rippling anywhere else — grep `TapeChoice` to confirm no stragglers: `grep -n "TapeChoice" app/MainComponent.cpp` should return nothing.)

- [ ] **Step 7: No-regression gate.**

Run: `ctest --test-dir build`
Expected: baseline unchanged (567 pass / 1 Not-Run).

- [ ] **Step 8: Commit.**

```bash
git add app/MainComponent.cpp
git commit -m "feat: P6 T3 — channel destination picker offers tape / bus / direct-out"
```

---

### Task 4: Clean rebuild, operator eyes-on, continue.md handoff

P6 is operator-verified. This task gates on a clean build and a human confirming the GUI, then refreshes the session handoff (mandatory per `feedback_update_continue_md_every_session` and the spec's per-phase handoff rule).

**Files:**
- Modify: `continue.md`

- [ ] **Step 1: Clean rebuild (CMake caches stale configs — required before eyes-on).**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiriusLooper
```

Expected: configures + builds clean from scratch.

- [ ] **Step 2: Launch the app and ask the operator to confirm.**

```bash
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Operator eyes-on checklist (the agent cannot verify GUI behavior — the operator confirms each):
- On the Input Mixer tab, two FX-return strips (**RVB**, **DLY**) appear to the right of the channel strips, after a divider gap.
- Each bus strip's fader and mute respond; its peak+LUFS meter is live.
- Right-click / long-press on blank pane area shows **Add bus / Add FX return / Add tape**; "Add bus" adds a new "Bus N" strip, "Add FX return" adds an "FX N" strip — live, no audio glitch.
- A channel strip's destination button lists the pooled tapes, the buses/FX returns, and **Direct out**; selecting a bus or Direct out updates the button label and re-routes (confirm audibly or via meters that the channel's signal moves off the tape path).
- No regression to the shipped tape picker (tape selection still works) or the split/collapse gesture.

- [ ] **Step 3: On operator sign-off, refresh `continue.md`.**

Update the top-of-file header and the `RESUME HERE` block so a cold chat resumes from "read continue.md" alone:
- Header: P6 SHIPPED — commits `<T1>..<T4>`; what landed (bus/FX-return strips + creation gesture + channel destination picker tape/bus/direct-out); clean-rebuild green; ctest 567/1; operator eyes-on confirmed.
- Record the **honest carry-forwards** in the handoff + `todo.md`: (a) exact "which bus" tick-back deferred to P7 (needs an engine `channelMainOutBus` accessor); (b) bus-node main-out picker deferred to P7 (needs an acyclic query); (c) looper floor-enforcement still assigned to P8; (d) bus solo is strip-only (no engine bus-solo).
- Set **NEXT = P7** (Input Mixer sends tab + insert mgmt ≤8 + wire P4/P5 routing-graph apparatus into production save/load) with its first concrete moves, per the long-range path.

- [ ] **Step 4: Commit + push.**

```bash
git add continue.md todo.md
git commit -m "docs: P6 shipped — Input Mixer UI (strips + creation + routing); handoff to P7"
git push origin master
```

---

## Self-review

- **Spec coverage:** Creation gesture (Task 2) ✓; bus/FX-return strips with dual peak+LUFS meter (Task 1) ✓; per-channel main-out picker tape/bus/output (Task 3) ✓; "own control, not the output combo" ✓. Deliberately deferred per scope decisions: bus-node main-out picker + exact bus tick-back (P7), bus removal (no engine API), sends tab + inserts (P7), floor-enforcement (P8). Each deferral is justified by a missing engine surface or the locked roadmap — none is a silent gap.
- **Type consistency:** `DestChoice`/`DestKind`/`StripDest` are defined once (T3 S1) and used in `setDestinations`, `showDestinationMenu`, `onDestinationChosen`, `refreshInputDestinations`. `BusInfo` defined once (T1 S1), used in `setBusStrips` + `rebuildBusStrips`. `busStripIds_` (T1) reused by the meter loop (T1 S6) and the bus relays (T1 S5). `kStripW`/`kInputLufsMaxBlock`/`linToDb` reuse existing symbols (no redeclaration).
- **Placeholder scan:** no TBD/TODO left in the plan; the two engine-accessor gaps are documented as explicit P7 deferrals with the reason, not as code stubs.
