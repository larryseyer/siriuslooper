# Per-channel record arm + ≥1-channel→tape floor (bridge slice)

**Date:** 2026-05-25
**Status:** approved — proceeding to plan
**Source:** `todo.md` entry 2026-05-22 — "input→output bridge slice"
**Memory pins:** `[[project_looper_at_least_one_tape_invariant]]`,
`[[project_io_ownership_direct_layer]]`, `[[feedback_gesture_over_ui_clutter]]`,
`[[feedback_ios_long_press_pairs_right_click]]`
**Whitepaper:** V9 §5.2, §6.6, §7.1 — direct layer + per-channel tape choice.

---

## 1. Background

IDA's looper invariant is "at least one channel records to at least one
tape at all times — otherwise it's a mixer, not a looper". The engine
already carries the machinery:

- `core/include/ida/TapeMode.h` defines
  `enum class TapeMode { CommitToTape, NonDestructive, NoTape }`.
- `engine/src/InputMixer.cpp:1140` gates direct channel→tape writes on
  `channel.tapeMode != TapeMode::NoTape` (the gate has been live for some
  time; tests pass).
- `engine/src/InputMixer.cpp:354` exposes
  `InputMixer::setChannelTapeMode(ChannelId, TapeMode)`.
- The whitepaper's "direct layer" — the mechanism by which a NoTape
  channel is still audible without writing to a tape — is **MON**.
  Per-channel MON has been live for several slices; per-bus MON landed
  2026-05-25 (commits `533537a` through `ceb3296`). A NoTape channel
  with MON On is heard via its auto-created OutputMixer MON-band strip.

What is missing:

1. **Operator UI** to set `TapeMode` per channel — today every channel is
   seeded `CommitToTape` at strip creation (`MainComponent.cpp:6425`)
   and no UI surface flips it. NoTape is unreachable from operator
   gestures.
2. **Active enforcement** that the count of channels with
   `TapeMode != NoTape` cannot drop to zero. Today an engine caller can
   `setChannelTapeMode(_, NoTape)` on every channel without complaint,
   leaving the session with no recording path — a silent looper-invariant
   violation.

This slice adds both. Persistence of `tapeMode` is verified (and added if
absent).

---

## 2. Operator UX

### 2.1 Gesture surface — strip context menu

Pro-audio convention is a per-track record-arm toggle. IDA's strips are
tight (especially on iPhone — `[[feedback_gesture_over_ui_clutter]]`) so
there will be **no new on-strip control**. The existing strip
right-click / long-press menu — `InputMixerPane::showToggleMenu`
(`app/MainComponent.cpp:1365`), today carrying the single
"Split to two mono channels / Collapse to stereo" item — gains a second
**checkable** entry:

> ☑ **Record to tape**

The check state mirrors engine state:

| Engine state                       | Menu reads |
|------------------------------------|------------|
| `TapeMode::CommitToTape`           | ☑ Record to tape |
| `TapeMode::NoTape`                 | ☐ Record to tape |
| `TapeMode::NonDestructive` (rare today; reserved) | ☑ Record to tape — preserved on toggle off-then-on path? **See §6.1.** |

Both gestures (desktop right-click, iOS long-press) target the same menu,
matching `[[feedback_ios_long_press_pairs_right_click]]`.

### 2.2 Visual feedback — dim the strip when NoTape

When a channel is NoTape, the strip's faceplate dims by
**`withMultipliedBrightness(0.7f)`** on its background colour token —
i.e. a 30 % luminance reduction. Operator sees at a glance which strips
will be silent on capture without opening any menu. The dim applies to
the channel-row strip body only; MON, INS, dest button, and
fader/mute/solo controls stay full-brightness so they remain legible.

### 2.3 Refusal banner

When the engine refuses a `NoTape` transition (§3), MainComponent
surfaces the refusal on the existing **`CaptureBanner`**
(`MainComponent.cpp:3083`) — the same on-top transient surface today's
Mark Out announcement uses. Reused via the existing `show(juce::String)`
entry point; no new banner widget. The message:

> *"At least one channel must record to a tape."*

Click-through behaviour and 1.5 s fade are inherited from CaptureBanner.
The `onUndoRequested` hook is no-op for a refusal banner (the operator
has nothing to undo — the engine rejected the gesture before any state
changed).

### 2.4 Bus rows

Out of scope. The whitepaper's TapeMode is a per-channel concept
(`Channel.h:118`). Buses always write to their main-out destination
unconditionally; per-bus opt-out is not part of the looper invariant and
is not in this slice.

### 2.5 Bus-routing wrinkle (acknowledged, not solved)

The engine's `TapeMode` gate suppresses only the **direct** channel→tape
write path. A NoTape channel routed to a bus that routes to a tape still
reaches the tape via the bus's unconditional write. This is intentional
(it's how subgroup recording works in any mixer) but it means
*"TapeMode::NoTape"* literally guarantees "no direct write", not "no
capture anywhere downstream". The floor enforcement in this slice counts
channels with `TapeMode != NoTape` directly — it does **not** perform
transitive bus-routing analysis. Operator mental model: "at least one
record-armed channel exists", which matches DAW convention.

Transitive bus-to-tape analysis can land in a later slice if the operator
ever surfaces a use case that needs it. The deferral entry's text
("≥1 channel→≥1 tape") is exactly the channel-count invariant we
enforce; the bus path is a separate concern.

---

## 3. Engine contract

### 3.1 New predicate

```cpp
// In InputMixer.h, alongside setChannelTapeMode:

/// Returns false iff disarming this channel's recording (setting its
/// TapeMode to NoTape) would drop the count of channels with
/// TapeMode != NoTape below 1. Use this to gate an operator gesture
/// BEFORE calling setChannelTapeMode; or simply call setChannelTapeMode
/// and act on its return value.
bool canDisarmChannelRecording (ChannelId) const;
```

Constant-time (linear scan over `channels_`); message-thread caller; no
audio-thread reach-through.

### 3.2 `setChannelTapeMode` returns `bool`

Today `setChannelTapeMode` returns `void`. It changes to `bool`:

- Returns `true` on a successful transition (including idempotent
  same-mode-to-same-mode, matching the `setChannelMonitorMode` pattern).
- Returns `false` in **exactly two cases**:
  1. **Floor-violating transition** — the call would set this channel
     to `NoTape` AND `canDisarmChannelRecording(id)` is false (this is
     the last channel with `TapeMode != NoTape`). Channel state
     unchanged.
  2. **Unknown `ChannelId`** — no such channel registered. No-op
     preserved; the bool surface makes the prior silent no-op visible
     to callers.

The void→bool change is breaking for callers. The only current callers
are:

- `MainComponent.cpp:6425` — channel-strip creation, sets
  `CommitToTape`. The return value can be `juce::ignoreUnused`'d (the
  default-tape-mode constructor guarantees success here).
- Test fixtures that construct `Channel`s — unchanged, they don't call
  the setter.

Subagent will sweep all call sites in the implementer task.

### 3.3 No new MixerTerminal kind

The deferral entry's phrasing "TapeMode::NoTape → the direct layer to an
output-mixer channel" reads as if NoTape requires a new destination
type. It does not — the direct layer is **MON**, which is already
implemented. A NoTape channel's main-out points wherever it already
pointed (typically primary tape); the gate at `InputMixer.cpp:1140`
prevents the write. Audibility flows through MON's auto-created
OutputMixer channel.

This keeps the slice surgical: no graph topology changes, no new
terminal nodes, no main-out migration of existing channels.

---

## 4. Persistence

The `Channel.tapeMode` field exists. Verify two surfaces round-trip it:

1. **`InputMixer::exportGraphState` / `importGraphState`** — engine-level
   graph state. Likely already round-trips, given `Channel` carries the
   field; if not, add it alongside other channel fields.
2. **`SessionFormat::channelStateToVar` / `channelStateFromVar`** — JSON
   serialization (the load/save user-visible session file). Add
   `tapeMode` JSON token (default-suppress per existing convention —
   absent JSON value reads as default `CommitToTape`). Lower-case
   string tokens: `"commit"` / `"nondestructive"` / `"no-tape"`,
   matching the lowercase pattern used by `monitorModeToken` in the same
   file.

Add an explicit E2E round-trip test if one doesn't exist: set
`NoTape` on a channel → export → JSON → deserialize → import → assert
`channelTapeMode` is still `NoTape`.

---

## 5. UI contract

### 5.1 `InputMixerPane`

New callback:

```cpp
/// Strip context-menu "Record to tape" toggle. MainComponent translates
/// to InputMixer::setChannelTapeMode(chId, on ? CommitToTape : NoTape)
/// inside the audio-callback bracket, and shows a refusal banner if the
/// engine returns false.
std::function<void (int idx, bool record)> onToggleChannelRecording;
```

New state mirror (parallel to `inputStripChannelIds_` / monitor button
state — same pattern):

```cpp
std::vector<ida::TapeMode> stripTapeModes_;
```

Push from engine after each refresh, via a new `setChannelTapeModes`
method (mirror of `setMonitorModes`).

### 5.2 `showToggleMenu` extension

The existing single-item menu becomes two items:

```
☑ Record to tape       ← new, checkable, fires onToggleChannelRecording
─────────────────
Split to two mono channels    ← existing
```

(or "Collapse to stereo" depending on current state).

Menu construction reads `stripTapeModes_[idx]` for the check state and
fires the callback with the **target** state (the inverse of current).

### 5.3 Dim-when-NoTape rendering

`CompactFaderStrip` (or the strip wrapper layer in `InputMixerPane`) gets
a new method:

```cpp
void setTapeModeDimming (bool dim);
```

When dim is true the strip applies a luminance reduction to its
background colour token. Implementation goal: read the strip's current
background colour, multiply by ~0.7 luminance, set as override. When
dim is false, restore the token's default colour.

`InputMixerPane` calls `setTapeModeDimming(mode == NoTape)` on each
strip whenever `setChannelTapeModes` is called.

### 5.4 Refresh hook in `refreshInputMixer`

After the existing channel-MON-modes push (`refreshInputDestinations` /
`refreshInputMixer`), add a TapeMode push:

```cpp
std::vector<ida::TapeMode> modes;
modes.reserve (inputStripChannelIds_.size());
for (const auto& chId : inputStripChannelIds_)
    modes.push_back (inputMixer_->channelTapeMode (chId));
inputMixerPane_->setChannelTapeModes (modes);
```

`InputMixer::channelTapeMode(ChannelId) const` likely already exists;
verify and add a thin accessor if not.

---

## 6. Edge cases

### 6.1 NonDestructive preservation across off-then-on

`NonDestructive` is reserved but not currently UI-reachable. If a session
loads with a channel in `NonDestructive` and the operator toggles
"Record to tape" off then on, today's design loses the NonDestructive
choice (off = NoTape, on = CommitToTape — `NonDestructive` would be
overwritten).

Resolution: this slice **does not** introduce a UI for NonDestructive;
the operator can only reach CommitToTape and NoTape. Sessions that
arrive with NonDestructive set programmatically remain NonDestructive
until the operator toggles off. After off-then-on, the channel becomes
CommitToTape — that's the documented behaviour for the operator-facing
toggle. A future slice that adds a third menu state for NonDestructive
can revisit.

### 6.2 Load-handler floor restoration

If a session file contains zero channels with `TapeMode != NoTape`
(externally edited file, or a bug in a prior export), the engine's
`importGraphState` must **not** silently respect the corrupt state — that
would leave the operator with an invariant-violating session. Resolution:
after `importGraphState`, MainComponent's load handler checks the count;
if zero, force the first channel back to `CommitToTape` and surface a
load-time toast: *"Session contained no record-armed channels; armed
channel 1 to satisfy looper floor."*

This is a load-time integrity guard, not an in-session enforcement —
the in-session enforcement (§3.2) prevents the count from ever reaching
zero through operator gestures.

### 6.3 Strip count vs channel count

Today strips are 1:1 with channels in `inputStripChannelIds_`. The floor
counts CHANNELS (engine-level), not strips. If a channel without a strip
exists (cross-device load, partial mirror), it still contributes to the
floor count. Strip dimming applies only to channels that have strips.

### 6.4 First-channel-is-still-armed assertion

`MainComponent.cpp:6425` sets `CommitToTape` on every newly minted strip.
On a fresh empty session (zero channels), creating the first strip
provides the floor channel. The engine's `setChannelTapeMode` allows the
transition (no opposing channels to preserve). No special-casing needed.

---

## 7. Tests

### 7.1 Engine tests

1. `canDisarmChannelRecording` — true when ≥2 CommitToTape channels;
   false when exactly 1; false on unknown id.
2. `setChannelTapeMode` floor — refuses the last-armed-channel disarm;
   returns false; state unchanged.
3. `setChannelTapeMode` idempotence — same mode → same mode returns
   true, no state change.
4. `setChannelTapeMode` happy path — CommitToTape → NoTape with ≥1
   other armed channel returns true.
5. `setChannelTapeMode` re-arm — NoTape → CommitToTape always returns
   true (re-arming is never refused).

### 7.2 Persistence tests

1. JSON round-trip — set NoTape on channel 2, export, parse, import,
   assert.
2. JSON default-suppress — CommitToTape channel's JSON omits the
   `tapeMode` token.
3. JSON load with absent token — defaults to CommitToTape.
4. Load-handler floor restoration — synthetic session with all NoTape →
   load handler arms first channel; toast posted (test asserts the
   notification queue if observable, otherwise just the engine state).

### 7.3 UI / wiring tests

GUI changes are operator-eyes-on per project convention. The wiring
(callback fires, engine call site, refusal handling) is exercised by the
manual T-last task in the plan.

---

## 8. Task breakdown (rough — full plan after this spec is approved)

1. **Engine: predicate + setter bool + tests.** `canDisarmChannelRecording`
   + `setChannelTapeMode` → `bool` + the 5 engine tests in §7.1.
2. **Engine: `channelTapeMode(ChannelId) const` accessor** (verify exists;
   add thin getter if not).
3. **Persistence: `tapeMode` JSON round-trip** — add the token,
   default-suppress, helpers, the 3 JSON tests in §7.2.
4. **Persistence: engine-graph-state `tapeMode` round-trip** — verify
   `export`/`importGraphState` carry the field; add if not + test.
5. **Load-handler floor restoration** — `chooseFileAndLoad` post-import
   guard + test.
6. **`InputMixerPane`: new callback + state mirror + `setChannelTapeModes`
   pusher + `showToggleMenu` "Record to tape" item + dim-when-NoTape
   rendering.** No unit tests for menu/painting (operator eyes-on).
7. **`MainComponent`: wire callback (audio-callback-bracketed engine call,
   refusal banner) + add `setChannelTapeModes` push in
   `refreshInputDestinations`.**
8. **Clean rebuild + ctest + operator-verify recipe written into
   `continue.md`** (gesture verify: right-click strip → see toggle;
   uncheck → strip dims; uncheck on last armed → banner; re-arm →
   restoration; save/quit/load → state survives).

Estimated 7-8 commits, subagent-driven like the bus-MON slice. ~1 day.

---

## 9. Out of scope

- Removing the legacy `MainOutDest::HardwareOutput` engine API (separate
  cleanup slice; the picker already excluded the choice on 2026-05-24).
- Operator UI for `TapeMode::NonDestructive` (third toggle state;
  reserved).
- Auto-MON-on heuristic when picking NoTape (operator's responsibility).
- Transitive bus-routing analysis for the floor invariant (§2.5).
- Per-bus opt-out (buses always write).
- The `[[project_minimal_default_mixers]]` rule that channel ctors
  shouldn't presume `CommitToTape` (also in `todo.md`).
