# Session Continuation — M-OTTO-4 slice 4b landed (picker UI + visible OTTO band); 4c (persistence + routing) and 4d (transport-start surface so it's actually audible) queued

## ▶ 0. Read these first (60 seconds)

1. **Slice 4b is in.** One feature commit this session pushed to
   `origin/master`:
   - `9075c85` — **Slice 4b**: `OutputMixerPane` gets an `ottoStrips_`
     band between phrase (left) and bus group (right). Blank-area menu
     has a new "Add OTTO source ▶" submenu listing the 32 OTTO outputs
     by friendly name (OTTO Drum 1..16, Perc 1..4, Shaker 1..2,
     Hand 1..2, FxRet 1..4, Drums/Percs/Shakers/Hands Bus), filtered
     to exclude already-added entries. Selection routes to
     `MainComponent::addOttoOutputStrip` (the slice-4a engine seam),
     then `appendOttoStrip` pushes a `CompactFaderStrip` into the band.
     Strip `id` = `ottoOutputIndex` (0..31, distinct from
     `kMasterStripId == -1`), so listener callbacks receive the OTTO
     output index directly — no row→index translation. `ChannelType::
     Master` tags the band for listener dispatch; the real master keeps
     `ChannelType::Bus` + sentinel id -1. Each OTTO strip carries a
     `StripContextOverlay` over its name band that catches right-click
     + long-press and surfaces "Remove" (paired gesture per
     `feedback_ios_long_press_pairs_right_click`). No INS / dest /
     detail-panel binding in 4b — gain + mute + select-highlight +
     remove only.

2. **OTTO upstream activity this session:** none — OTTO's submodule pin
   stays at `4cdbad3e`. The 2026-05-27 IDA→OTTO EventBus implementation
   brief in `external/OTTO/CROSS_PROJECT_INBOX.md` remains
   `needs-ack` (OTTO's Claude has not yet picked it up). IDA is NOT
   blocked — the SPSC marshal in OttoHost absorbs the cost.

3. **Baseline.** `master` at `9075c85` (verify with
   `git log -1 --oneline`). Local == origin. lsfx_tapecolor pin =
   `a812670`; OTTO submodule pin = `4cdbad3e`; sfizz pin unchanged at
   `f5c6e29f`.

4. **ctest: 790 / 791** (the 1 not-run is the separately-built
   `MainComponentPluginEditorTests_NOT_BUILT-b12d07c` as before — same
   number as the prior session's baseline; slice 4b adds zero tests
   intentionally — UI-only change on top of the slice-1/2 engine
   surface). Clean rebuild (`rm -rf build && cmake -B build ...`)
   succeeded; full test suite green except #791 not-run.

5. **Known load-sensitive flake (NOT new):** test #52 (`permanent
   bypass: kill every generation, slot bypasses after
   kMaxRestartAttempts`, in
   `tests/OutOfProcessEffectChainHostSupervisorTests.cpp:268`) is
   load-sensitive — its own comment explicitly says "(which flakes on
   loaded CI)". It passes in isolation (`ctest -R "permanent bypass"`)
   and on a quiet machine. Not from slice 4b's changes (MainComponent.cpp
   isn't linked into IdaTests). If you see it red in a concurrent run,
   re-run alone before assuming damage.

6. **IDA app + tests build clean.** Operator eyes-on of slice 4b is
   **pending**:
   - Right-click (or long-press on touch) on an empty area of the
     OutputMixerPane → confirm "Add OTTO source ▶" submenu opens
     listing the 32 friendly names.
   - Pick a few entries — confirm an OTTO strip appears between
     phrases and the bus group, fader + mute work.
   - Already-picked entries vanish from the submenu on next open.
   - Right-click (or long-press) on an OTTO strip's name band →
     "Remove" pops up; selecting Remove drops the strip and re-offers
     that index in the picker.

7. **M-OTTO-4 still not audibly verifiable** (per Note D from prior
   session) — strip exists, fader moves, but OTTO's transport never
   rolls on a fresh session (`project_otto_is_the_transport_source`).
   **Slice 4d** is still the smallest piece that makes M-OTTO-4
   audible end-to-end.

---

## ▶ 1. What landed THIS chat

| Commit | Subject |
|---|---|
| `9075c85` | feat: OutputMixerPane "Add OTTO source" picker + visible OTTO band (M-OTTO-4 slice 4b) |

No OTTO-side commits this session. No submodule bumps.

---

## ▶ 2. Notes worth carrying forward

### Note A — Discriminator decision is locked

The handoff's recommended discriminator (use `ChannelType::Master` +
non-negative row IDs to identify OTTO strips, leaving the real master
on `ChannelType::Bus` + `kMasterStripId == -1`) is now in code at
`stripGainChanged` / `stripMuteChanged` / `stripChannelSelected`. The
row-ID-AS-ottoOutputIndex pattern means listener callbacks dispatch
straight to the OTTO output index without any translation table —
slice 4c's routing / persistence work can lean on the same identity.

### Note B — OTTO strips bypass the INS / DEST / detail-panel infrastructure

Slice 4b deliberately ships strips with no INS button, no DEST picker,
and no detail-panel binding (select fires `stripChannelSelected`,
clears other selections, highlights the OTTO strip, hides the detail
panel). The INS row + picker row reserved in `resized()` are placed
but left empty for OTTO strips — slice 4c wires them. Three concrete
deferrals:

- **DEST**: OTTO strips currently route to the OutputMixer's default
  destination (whatever `outputMixer_->setChannelAudioSource` set up;
  audit on the slice-4a path). To get per-strip routing to bus /
  hardware-output pairs, slice 4c needs to add OTTO strips into the
  `refreshOutputDestinations` enumeration so they collect the same
  `DestChoice` lists phrase / MON strips do, plus an
  `onOttoDestinationChosen` callback that calls
  `outputMixer_->setChannelMainOutToHardwareOutput` or
  `routeChannelToBus` with the bound `OutputChannelId`.
- **INS**: `onOttoInsertChainClicked` + `openInsertChainPopupForOttoOutputStrip`
  with the same shape as `openInsertChainPopupForOutputBus`.
  Insert-chain UI ships internal-FX-only until the plugin scanner is
  fixed (`project_plugin_scanner_broken`).
- **Detail panel**: EQ + CMP at minimum (Pan/Width is meaningful too
  since OTTO outputs are stereo pairs). Mirror the
  `selectedOtto_ : int` pattern — non-negative when an OTTO strip is
  active, drives `onMasterEqConfigChanged`-style callbacks.

### Note C — Persistence is required for "complete"

OTTO strips don't survive session save/load yet. Per
`feedback_sirius_done_right_and_complete`, the operator add-then-remove
loop works in one session but reload-after-add silently drops the
strips. Slice 4c must serialise the `ottoChannelByOutputIndex_` set
(as an "otto_output_strips" list of indices + their current strip
gain/mute/destination) into the session JSON envelope and rebind on
load. Round-trip test pins behaviour.

### Note D — Slice 4 STILL needs a transport-start surface to be audible

Re-stating from prior session: on a fresh session OTTO's transport
never rolls (`project_otto_is_the_transport_source`). Without OTTO
playing, every OTTO strip is silent regardless of routing. Slice 4d
remains the smallest piece of M-OTTO-6 territory needed to make
slice 4 actually audible — minimal Play/Stop affordance wired to
OTTO's `TransportTracker::start/stop`. Without it the operator-verify
path for M-OTTO-4 is "see the strip, move the fader, observe silence."

### Note E — Friendly-name helper lives on the pane

`OutputMixerPane::ottoFriendlyName(int idx)` is a `static juce::String`
helper. Adding it to `OttoHost` would force a JUCE dependency on the
otto-bridge public header (intentionally JUCE-free per
`project_otto_is_a_submodule_now`), so it sits on the pane and
MainComponent's `onAddOttoOutputStrip` lambda calls into the pane to
get the name when appending. Slice 4c's destination labels can reuse
the same helper.

---

## ▶ 3. What's next

### (A) Slice 4d — OTTO transport-start surface (recommended FIRST)

Without it slice 4b ships dark. The minimum: a Play/Stop button on
the transport bar (or a keyboard shortcut) wired to a new
`OttoHost::setTransportPlaying(bool)` that calls into OTTO's
`TransportTracker::start/stop`. Small commit. Lands enough of M-OTTO-6
that operator can hear an OTTO strip route to the master.

**Size**: small. 1 commit.

### (B) Slice 4c — OTTO strip routing + persistence

After 4d gives audible verification, layer in:
- Per-OTTO-strip DEST picker (bus / hardware-output stereo pair)
  via `setOttoDestinations` + `onOttoDestinationChosen` (mirror of
  `setPhraseDestinations`).
- INS button + insert-chain popup
  (`openInsertChainPopupForOttoOutputStrip`).
- Detail-panel binding (EQ + CMP + Pan + Width). Adds
  `selectedOtto_` to the mutual-exclusion logic and dispatches the
  tab callbacks to the bound OutputChannelId's `ChannelStrip<Audio>`.
- Session JSON: serialise the `ottoChannelByOutputIndex_` set with
  per-strip gain/mute/destination state; rebind on load.

**Size**: medium-large. 2-3 commits.

### (C) Wait for OTTO to fix EventBus::publish

OTTO's next session sees the 2026-05-27 inbox brief. Until OTTO lands
the lock-free + alloc-free rewrite, IDA's SPSC marshal absorbs the
cost; no IDA-side change needed.

### Default recommendation: **(A) slice 4d, then (B) slice 4c**

Get audible end-to-end first (4d), then deepen the UX (4c). Together
they finish M-OTTO-4 properly.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `9075c85` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected) |
| lsfx_tapecolor pin | `a812670` (OFF-passthrough fix) — unchanged from prior session |
| OTTO submodule pin | `4cdbad3e` (inbox EventBus brief expansion) — unchanged within this session |
| sfizz submodule pin | `f5c6e29f` — unchanged |
| ctest baseline | **790/791** (1 not-run is the separately-built MainComponentPluginEditorTests, same as before; slice 4b adds zero new tests) |
| `[otto-host-render]` | 6 cases / 157 assertions green |
| `[audio-callback][otto-render]` | 2 cases all green |
| `[otto-host-transport]` | 6 cases / 30 assertions green |
| `[tapecolor-adapter]` | 5/5 green |
| IDA app builds + links | yes (clean Release build) |
| Operator eyes-on (pending) | (1) right-click / long-press OutputMixerPane blank area → "Add OTTO source ▶" submenu lists 32 friendly names, already-added entries vanish; (2) pick a few → OTTO strips appear in band between phrase and bus, fader + mute work; (3) right-click / long-press strip name band → "Remove" pops up, removing returns the index to the picker; (4) audible verification still blocked on slice 4d (no transport-start). |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`**
   per the cross-project protocol. The 2026-05-27 IDA→OTTO EventBus
   brief should still be `needs-ack` unless OTTO's Claude has landed
   the fix between sessions. If OTTO has posted a fix-and-bump entry,
   bump the OTTO submodule + ack per protocol.
3. Pick from §3. Default (A) slice 4d, then (B) slice 4c.
4. If picking (A), the engine surface to look at is
   `external/OTTO/src/otto-core/include/otto/transport/
   TransportTracker.h` (for `start()` / `stop()`) and OTTO's
   `PluginProcessor::processBlock` for the lifecycle. The IDA-side
   surface goes on `OttoHost` (a `setTransportPlaying(bool)` method
   that forwards to OTTO's tracker) plus a small transport-bar
   button (likely in `MainComponent`'s top toolbar — see the existing
   `transportBar_` if it exists, otherwise mint one).
5. If picking (B), the templates to mirror are:
   - `setPhraseDestinations` / `setMonDestinations` →
     `setOttoDestinations` (per-strip `DestChoice` list +
     `StripDest` current value).
   - `onPhraseDestinationChosen` → `onOttoDestinationChosen` on the
     pane; MainComponent looks up the bound `OutputChannelId` from
     `ottoChannelByOutputIndex_` and calls
     `outputMixer_->setChannelMainOutToHardwareOutput` /
     `routeChannelToBus`.
   - `selectedPhrase_` / `selectedMon_` etc. add a `selectedOtto_`
     sibling; the four (now five) mutual-exclusion guards extend in
     `stripChannelSelected`.
   - Session JSON: see how phrase strips' bus routing serialises
     (search for "phrase_channels" or similar in the persistence
     code) and mirror.

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
- **OTTO integration design (4 foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md`
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + matching `CLAUDE.md` sections
- Whitepaper: `docs/IDA_Whitepaper_V10.md`

Memory:
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer (slice 4 target)
- `project_otto_is_the_transport_source` — IDA has no engine-side transport state; OTTO supplies play/stop; slice 4d lives here
- `project_otto_is_a_submodule_now` — submodule consumption model + friendly-name helper rationale
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics
- `feedback_ios_long_press_pairs_right_click` — paired gesture rule (Remove popup uses StripContextOverlay so it works on iOS too)
- `feedback_sirius_done_right_and_complete` — slice 4 is "done" when 4b + 4c + 4d are landed; 4b on its own is engine+UI but silent + non-persistent
- `feedback_clean_builds` — clean rebuild done before this handoff

---

*End of session. M-OTTO-4 slice 4b landed (picker UI + visible OTTO
band + select-highlight + remove gesture) in 1 IDA feature commit, no
OTTO-side commits, no submodule bumps. ctest 790/791, zero flakes
(once the load-sensitive #52 is excluded). Next session: slice 4d
(transport-start) by default to make slice 4 audible, then slice 4c
(routing + persistence) to close M-OTTO-4 properly.*
