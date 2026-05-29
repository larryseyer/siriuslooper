# S6 — OTTO output-strip DEST picker + save/load persistence (design)

**Date:** 2026-05-29
**Status:** design synthesis; ready for plan-writing.
**Parent:** `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` §7 row S6 (M-OTTO-4 polish, original 4c part 2).
**Sister:** §7 row S5 (OTTO strip detail-panel binding — orthogonal, lands separately).
**Sibling:** `2026-05-27-otto-stereo-mix-output.md` (the future stereo-mix sentinel `-2` shares this persistence shape).

---

## 1. Goal

Land per-OTTO-strip routing for IDA's Output Mixer:

1. **Runtime DEST picker** on every OTTO strip — operator chooses target bus or hardware-output pair, mirror of the phrase-channel and bus-strip DEST pickers already on the pane.
2. **Save/load persistence** — the route round-trips through the session file, AND the OTTO strip itself is re-minted on import (binds the right OTTO output pointer, not silently re-spawned as a phrase channel).

Goal is operator-visible parity: an OTTO strip is just as routable as a phrase strip, and survives a Save → Load → audible-replay cycle without operator re-clicking the DEST picker.

## 2. Architecture decisions (locked)

### 2.1 OTTO strip = first-class OutputMixer channel (already shipped 4b)

`MainComponent::addOttoOutputStrip(ottoOutputIndex)` (`app/MainComponent.cpp:6832`) already mints an `OutputChannelId` via `outputMixer_->addChannel(SignalType::Audio)`, installs a `ChannelStrip<Audio>`, and binds the OTTO output L/R pointers via `setChannelAudioSource`. The OTTO-specific bit is **only** the buffer-pointer binding (step 3); the channel is otherwise indistinguishable from a phrase channel at the engine level.

Consequence: OTTO strips reuse the existing channel-routing surface (`outputMixer_->setChannelMainOutToHardwareOutput(chId, pairIndex)`, `routeChannelToBus(chId, busId)`). S6 adds **no new engine routing primitive**.

### 2.2 DEST option set = identical to phrase-channel

Per `project_otto_as_output_mixer_source` (locked memory) the Output Mixer owns all physical-output routing; tape excludes OTTO. So the OTTO strip's DEST options are exactly the phrase-channel DEST options:

- `DestKind::Bus` (any existing aux bus — master defaults at bus 0, plus any operator-created Bus or FxReturn)
- `DestKind::HardwareOutput` (any stereo pair the device exposes)

Cycle-exclusion is identical (a channel can't route to a bus that routes back through it; OTTO strips are leaf-input-style, so no cycles exist in practice — but the existing exclusion code reads from the live graph and applies uniformly).

### 2.3 Persistence shape — `int ottoSource { -1 };` on `OutputChannelState`

Operator-approved option A (2026-05-29). Add one field:

```cpp
struct OutputChannelState
{
    // ...existing fields...
    int ottoSource { -1 };   // -1 = phrase channel; 0..31 = OTTO output index;
                             // -2 reserved for future OTTO Stereo Mix sentinel (S7).
};
```

The engine treats `ottoSource` as pure transport metadata — it round-trips through `exportGraphState`/`importGraphState` but is **never read at runtime**. The OTTO-specific buffer-pointer binding is the responsibility of MainComponent's post-import pass (§3.3).

`operator==` is extended to compare `ottoSource` so the existing equality-based round-trip tests continue to enforce field-by-field round-trip.

### 2.4 Engine receives a thin setter — `OutputMixer::setOttoSource`

```cpp
// OutputMixer — engine never reads this; it round-trips it through export/import.
void setOttoSource (OutputChannelId, int ottoSource);
int  getOttoSource (OutputChannelId) const noexcept;
```

`addOttoOutputStrip` calls `setOttoSource(chId, ottoOutputIndex)` immediately after binding the buffer pointers. `exportGraphState` populates `OutputChannelState.ottoSource` from it; `importGraphState` writes it back into the engine after minting the channel.

Rationale for living in the engine vs. MainComponent decoration: the engine already owns the per-channel `OutputChannelState` projection, so adding one transported field is one-line bookkeeping. Decorating the exported state in MainComponent would require a reverse-map walk on every save and an equivalent unwind on every load — same outcome, more surface area, easier to drift from the engine's serialization conventions.

### 2.5 Post-import rebind in MainComponent

After `outputMixer_->importGraphState(state)`:

```text
for each channel in state.channels with ottoSource >= 0:
    chId = the OutputChannelId minted by importGraphState for this entry
           (ids preserved verbatim — existing E3 invariant)
    src  = ottoHost_->getOttoOutputLeft/Right (ottoSource)
    outputMixer_->setChannelAudioSource (chId, srcL, srcR)
    ottoChannelByOutputIndex_[ottoSource] = chId
    outputMixerPane_->appendOttoStrip ({ ottoSource, friendlyName(ottoSource) })
```

The rebind is bracketed by the standard `removeAudioCallback` / `addAudioCallback` pair (no new bracket required — fits inside the existing import bracket).

Routing persistence is automatic because the channel id is preserved and `mainOutKind` / `mainOutBus` / `hardwareOutPair` are already serialized for `OutputChannelState` (they apply uniformly to phrase and OTTO channels alike).

### 2.6 UI surface — mirror of phrase row

`OutputMixerPane` (the OutputMixerPane class inside `app/MainComponent.cpp`) gets the parallel-to-phrase-row scaffolding for OTTO:

```cpp
std::vector<std::unique_ptr<juce::TextButton>>  ottoDestButtons_;
std::vector<StripDest>                          ottoStripDests_;
std::vector<std::vector<DestChoice>>            ottoChoices_;

std::function<void(int, DestChoice)>            onOttoDestinationChosen;
void showOttoDestinationMenu (int idx);
```

`appendOttoStripImpl` (currently at `app/MainComponent.cpp:3449`) extends to also construct a `juce::TextButton` paired with the strip, push it into `ottoDestButtons_`, and bind its `onClick` to `showOttoDestinationMenu(idx)` — the literal pattern from `phraseDestButtons_` (line ~822).

`showOttoDestinationMenu` is a copy of `showDestinationMenu` (line 1596) parametrised on `ottoChoices_[idx]` / `ottoStripDests_[idx]` / `ottoDestButtons_[idx]` / `onOttoDestinationChosen`.

`refreshOutputDestinations()` is extended to populate `ottoChoices_` and sync `ottoStripDests_` labels alongside the phrase, bus, and master pickers it already handles.

Layout: the OTTO row already has its bounds computed (`ottoStrips_` band in `resized()` around line 2995); DEST buttons line up underneath each OTTO strip, exactly the way phrase DEST buttons line up under each phrase strip.

### 2.7 MainComponent wiring

```cpp
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
                outputMixer_->routeChannelToBus (chId, ida::BusId{ dest.id });
                break;
            case OutputMixerPane::DestKind::HardwareOutput:
                outputMixer_->setChannelMainOutToHardwareOutput (chId, dest.pairIndex);
                break;
        }
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
        refreshOutputDestinations();
    };
```

Mirror of `onBusDestinationChosen` (`app/MainComponent.cpp:5037`) — same bracket discipline, same `refreshOutputDestinations()` call after.

### 2.8 iOS long-press — no extra work

DEST is a `juce::TextButton` with an `onClick` handler. Touch fires click on iOS without any additional gesture wiring. The per-`feedback_ios_long_press_pairs_right_click` rule applies to right-click-only gestures, not to button clicks. Confirmed: no change to existing pattern.

### 2.9 Whitepaper

`docs/IDA_Whitepaper_V10.md` already documents OTTO output strips as routable Output Mixer channels (M-OTTO-4 scope). S6 implements an already-promised capability; no whitepaper edit required. If a clarifying sentence is wanted, append to §6.6 noting that OTTO strips share the channel-DEST surface — but a doc-only follow-on is fine, not a S6 deliverable.

## 3. Migration / back-compat

**Existing sessions**: `OutputChannelState::ottoSource` defaults to `-1`. Sessions saved before S6 deserialize with every channel ottoSource=`-1`, i.e. as phrase channels. No migration needed.

**Forward**: post-S6 sessions saved with OTTO strips populate `ottoSource >= 0` for those entries. An older IDA build loading such a file would silently drop the field at deserialize, mint OTTO entries as plain phrase channels with no audio source (silent strips), and on the next Save lose the `ottoSource` value permanently. Sole-developer machine; no shipped builds; not a real risk in practice, but worth naming so the cross-version mistake isn't made by accident.

## 4. Test plan

Four layers, all headless TDD:

1. **`OutputChannelState` round-trip** — extend existing equality / round-trip tests in `tests/MixerGraphPersistenceTests.cpp` to cover `ottoSource` (default -1, set to 0..31, set to -2). Tag: `[output-channel-state][round-trip]`.
2. **`OutputMixer::setOttoSource` / `getOttoSource`** — pure getter/setter test in `tests/OutputMixerTests.cpp`. Default after `addChannel` is -1; setter writes; getter reads; exportGraphState carries it; importGraphState restores it. Tag: `[output-mixer][otto-source]`.
3. **`SessionFormat` JSON round-trip** — extend `tests/SessionFormatTests.cpp` to assert `ottoSource` survives JSON serialize → deserialize at non-default values. Tag: `[session-format][otto-source]`.
4. **OttoHost + OutputMixer integration** — new test file `tests/OttoStripDestPersistenceTests.cpp`, modeled on `OttoHostRenderTests.cpp` (prepared `OttoHost` + freshly-constructed `OutputMixer`, no GUI). The MainComponent post-import rebind logic is exercised by lifting it into a small free helper (`ida::app::rebindOttoChannelsAfterImport(OutputMixer&, OttoHost&)` declared next to `addOttoOutputStrip`'s helpers) so the test can call it directly without instantiating MainComponent. Cases: (a) `addOttoOutputStrip(0)` → `setChannelMainOutToHardwareOutput(chId, 1)` → `exportGraphState` → fresh OutputMixer → `importGraphState` → `rebindOttoChannelsAfterImport` → assert rebound channel's source pointers equal `OttoHost::getOttoOutputLeft/Right(0)` AND `getOttoSource(chId) == 0` AND main-out is HardwareOutput pair 1. (b) Same chain for DEST=Bus. (c) Idempotent re-import: import twice in a row, assert the second import is a no-op (no duplicate strips, no double-bind). Tag: `[otto-strip][persistence][end-to-end]`.

## 5. Out of scope (not S6)

- **S5 — detail panel binding** (EQ/CMP/Pan/Width for selected OTTO strip). Orthogonal — lands separately.
- **S7 — OTTO Stereo Mix sentinel `-2`**. The schema reserves the value; S6 does not create the strip. S7 lands its own picker entry + sum routine per `2026-05-27-otto-stereo-mix-output.md`.
- **P7 wiring** — broader save/load envelope that wraps the existing `exportGraphState`/`importGraphState` calls. S6 assumes MainComponent already calls those in `chooseFileAndSave`/`chooseFileAndLoad` (verified at lines 8118 + 8336). If P7 hasn't landed those by S6 time, S6 lands the per-channel field independently and P7 picks up an already-aware envelope.
- **Insert chain on OTTO strips** (`ottoInsButtons_`). Same S5 territory; not S6.
- **Renaming OTTO strips**. Out of scope (OTTO outputs are named by role).

## 6. Risks

| Risk | Mitigation |
|---|---|
| Re-mint on import binds wrong OTTO pointer if `OttoHost::prepare()` hasn't fired yet | The existing `addOttoOutputStrip` already fails-loud when `getOttoOutputLeft/Right` returns null. Mirror that check in the rebind pass; if null, log + skip (the channel remains in the graph with a null source, silent — operator sees the gap and can re-add manually). Pre-import: ensure `OttoHost::prepare()` is called BEFORE `outputMixer_->importGraphState()`. |
| `ottoSource` field forgotten in a future `OutputChannelState` field-add | The round-trip test in §4.1 asserts byte-identical equality; any add that forgets to extend the equality/serialize will trip a test. |
| Channel id collision on import (phrase channel id == OTTO channel id) | Cannot happen by construction — `OutputMixer::nextChannelId_` increments monotonically across all `addChannel` calls, and `importGraphState` preserves persisted ids without re-allocating. Existing E3 invariant. |
| Post-import strip-pane rebuild order: pane has stale ottoStrips_ when MainComponent calls `appendOttoStrip` | The post-import pass calls `setOttoStrips({})` first to clear the band before iterating ottoSource>=0 channels. Mirror of the input-strip rebuild already used on load. |

## 7. Files (anticipated)

| File | Change |
|---|---|
| `core/include/ida/MixerGraphState.h` | `OutputChannelState::ottoSource` field + `operator==` extension |
| `engine/include/ida/OutputMixer.h` | `setOttoSource` / `getOttoSource` declarations |
| `engine/src/OutputMixer.cpp` | Setter/getter bodies; per-channel `ottoSource_` member in the internal channel struct; export/import wire the field through `OutputChannelState` |
| `persistence/src/SessionFormat.cpp` | Serialize/deserialize `ottoSource` (one-line per direction; default `-1` on missing) |
| `app/MainComponent.cpp` | `addOttoOutputStrip` calls `setOttoSource`; post-import rebind pass; `OutputMixerPane` gets `ottoDestButtons_`/`ottoChoices_`/`ottoStripDests_`/`showOttoDestinationMenu`/`onOttoDestinationChosen`; wiring lambda; `refreshOutputDestinations` extension; `appendOttoStripImpl` constructs the DEST button; `resized` lays it out |
| `tests/MixerGraphPersistenceTests.cpp` | `ottoSource` round-trip cases |
| `tests/OutputMixerTests.cpp` | `setOttoSource`/`getOttoSource` cases |
| `tests/SessionFormatTests.cpp` | JSON round-trip case |
| `tests/OttoStripDestPersistenceTests.cpp` | New — end-to-end |
| `tests/CMakeLists.txt` | Register the new test |

## 8. Verification

- `ctest --test-dir build` baseline is 813/1; S6 adds ~5-7 new cases. End baseline ≥818/1 with zero regressions.
- Operator-verified GUI: clean rebuild → launch IDA → add OTTO strip 0 → click its DEST button → choose a hardware pair → audio at that pair → Save → quit → relaunch → Load → audio at the same pair without re-clicking. (Mirror of the S3c T17 protocol.)

## 9. Open items (none — all forks resolved)

All design forks closed during 2026-05-29 brainstorm:
- DEST option set: identical to phrase-channel (§2.2).
- Persistence shape: `int ottoSource { -1 };` on `OutputChannelState`, engine-transported (§2.3 / §2.4).
- Post-import rebind: MainComponent-side after `importGraphState` (§2.5).
- iOS long-press: no-op (button click already handles touch — §2.8).
- Whitepaper: no edit required (§2.9).
- S7 stereo-mix sentinel: schema reserves `-2`, S7 lands the picker (§5).

Ready for `superpowers:writing-plans`.
