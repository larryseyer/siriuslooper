# Session Continuation — M-OTTO-4 slices 1, 2, 4a landed (engine plumbing complete); 4b (picker UI), 4c (persistence), 4d (transport-start) queued

## ▶ 0. Read these first (60 seconds)

1. **M-OTTO-4 engine plumbing complete.** Three feature commits this
   session pushed to `origin/master` on top of the prior session-start
   `cc9ce1d` (which had already bumped OTTO to `4cdbad3e` carrying the
   expanded EventBus brief inside the inbox):
   - `c1acfb1` — **Slice 1**: `OttoHost::renderBlock(numSamples)` +
     `getOttoOutputLeft/Right(idx)` for the 32 OTTO stereo pairs
     (24 instrument channels at 0..23 + 4 FX returns at 24..27 +
     4 player buses at 28..31). 6 new `[otto-host-render]` tests pin
     the wiring (157 assertions). Wraps OTTO's
     `PlayerManager::processGlobalMixer` (RT-safe per OTTO's
     `CLAUDE.md`); dispatches the 32-output index space across three
     `GlobalMixer` accessor families.
   - `388a9f4` — **Slice 2**: `core/IOttoRenderSource.h` new dependency-
     inversion port (matches the `IOttoTransportListener` precedent —
     pure JUCE-free + OTTO-free interface in `core/` so `audio/` can
     name it without depending on `otto-bridge/`); `OttoHost` inherits
     the interface; `AudioCallback` gains `setOttoRenderSource` and
     invokes `source->renderBlock(numSamples)` in a new "Step 2b" of
     `audioDeviceIOCallbackWithContext`, between input-graph render
     and OutputMixer dispatch. `MainComponent` moves OttoHost construct
     + prepare BEFORE `addAudioCallback` (configure-before-audio-starts
     contract) and calls `setOttoRenderSource` right after `prepare`.
     2 new `[audio-callback][otto-render]` tests.
   - `a1fd81e` — **Slice 4a**: `MainComponent::addOttoOutputStrip(int)`
     + `removeOttoOutputStrip(int)` + `hasOttoOutputStrip(int) const`
     public methods. Each `add` brackets the audio callback, creates
     one `OutputMixer` channel (SignalType::Audio + default
     `ChannelStrip<Audio>`), binds the channel's audio source via the
     existing `setChannelAudioSource(chId, ottoHost_->getOttoOutputLeft
     /Right(idx))` API — OTTO's per-output pointers are stable for the
     OttoHost's lifetime (GlobalMixer allocates buffers once in
     `prepare()`), so the bind is set-once. New
     `ottoChannelByOutputIndex_` map tracks the bindings. Idempotent
     on double-add; no-op on remove-unknown. **No UI changes** in this
     slice — the operator-on-demand picker is **slice 4b** (operator
     answered the `project_otto_as_output_mixer_source` vs
     `project_minimal_default_mixers` fork in favour of on-demand
     creation, so 4a deliberately ships engine-only).

2. **Slice 3 was deleted.** The original scope assumed
   `OutputMixer::setOttoChannelSource` would be a distinct API. The
   Explore audit of OutputMixer revealed the existing
   `setChannelAudioSource(channelId, const float*, const float*)` is
   already source-agnostic (MON / OTTO / future sources all hand in a
   stereo pointer pair). Slice 3 collapsed into slice 4a per
   `CLAUDE.md`'s "don't design for hypothetical requirements."

3. **OTTO upstream activity this session:** none — OTTO's submodule pin
   stays at `4cdbad3e` (the inbox brief-expansion commit landed at
   session start, before slice 1). The 2026-05-27 IDA→OTTO EventBus
   implementation brief in `external/OTTO/CROSS_PROJECT_INBOX.md`
   remains `needs-ack` (OTTO's Claude has not yet picked it up). IDA is
   NOT blocked — the SPSC marshal in OttoHost absorbs the cost.

4. **Baseline.** `master` at `a1fd81e` (verify with
   `git log -1 --oneline`). Local == origin (all four pushes —
   `cc9ce1d` from session start + three feature commits — went
   through). lsfx_tapecolor pin = `a812670`; OTTO submodule pin =
   `4cdbad3e`; sfizz pin unchanged at `f5c6e29f`.

5. **ctest: 790 / 791** (the 1 not-run is the separately-built
   `MainComponentPluginEditorTests_NOT_BUILT-b12d07c` as before — same
   number as the previous session's baseline + 8 new test cases from
   slices 1 & 2). `[otto-host-render]` 6 cases / 157 assertions green.
   `[audio-callback][otto-render]` 2 cases all green. Slice 4a added
   no new tests on purpose — the composition is OttoHost (slice 1
   tested) + OutputMixer (existing tests) + the same channel-create
   orchestration the phrase-channel path already exercises.

6. **IDA app + tests build clean** on the merged submodule pins.
   Operator eyes-on of M-OTTO-4 is **still pending** — slice 4b's
   picker UI is the operator-facing entry point. There is currently
   NO way for the operator to add an OTTO strip from the running app
   (the `addOttoOutputStrip` method is reachable only from C++).

---

## ▶ 1. What landed THIS chat

| Commit | Subject |
|---|---|
| `c1acfb1` | feat: OttoHost::renderBlock + 32 per-output pointer accessors (M-OTTO-4 slice 1) |
| `388a9f4` | feat: AudioCallback drives OttoHost::renderBlock via IOttoRenderSource port (M-OTTO-4 slice 2) |
| `a1fd81e` | feat: MainComponent::addOttoOutputStrip + removeOttoOutputStrip engine seam (M-OTTO-4 slice 4a) |

Plus the pre-session-start `cc9ce1d` (inbox EventBus brief expansion,
OTTO `d4321510 → 4cdbad3e`) was already on `master` when slice 1
started.

No OTTO-side commits this session.

---

## ▶ 2. Notes worth carrying forward

### Note A — OTTO's "32 outputs" topology

The Explore audit pinned OTTO's per-output surface concretely:
- 24 instrument channels (post-channel-EQ/CMP, pre-send, pre-bus) →
  output indices 0..23. Drums 0..15, Percs 16..19, Shakers 20..21,
  Hands 22..23.
- 4 FX returns (post-FX, pre-pair-assignment) → indices 24..27.
- 4 player sub-buses (post-bus-EQ/CMP, post-bus-fader/pan,
  pre-master) → indices 28..31. 28=Drums, 29=Percs, 30=Shakers,
  31=Hands.

OTTO's `static_assert` in `GlobalMixer.h` pins the total at 32.
Constants on `ida::OttoHost`: `kNumOttoOutputs = 32`,
`kOttoChannelRangeBegin = 0`, `kOttoFxReturnRangeBegin = 24`,
`kOttoPlayerBusRangeBegin = 28`.

The canonical entry point is `processGlobalMixer(numSamples)` — NOT
`processBlockWithMasterBus`, which sums everything away to stereo.
The three accessor families are exposed unified through
`OttoHost::getOttoOutputLeft/Right(int idx)` with range dispatch.

### Note B — Per-output pointer stability

OTTO's `GlobalMixer` allocates its per-channel / per-FX-return /
per-bus output buffers once in `prepare()` and never reallocates.
The pointer returned by `getOttoOutputLeft/Right(idx)` is therefore
stable for the OttoHost's lifetime; only the buffer **contents**
change each `renderBlock`. Slice 4a leans on this hard: it binds the
pointers ONCE via `setChannelAudioSource` and never re-binds.

If a future change makes OTTO's per-output buffers reallocate (e.g.
a runtime block-size change without a full host re-prepare), the
bind goes stale and the audio thread reads freed memory. There is
no safety net for this today. Two mitigations if needed: (a) re-bind
on every `prepare()` call from MainComponent, (b) move the pointer
fetch into the audio thread per-block (extra indirection per channel
per block — measure before doing).

### Note C — Operator-on-demand picker is unambiguous

Operator answered the seeding fork explicitly: **all 32 OTTO outputs
are operator-on-demand** (option (C) of the AskUserQuestion). Slice
4b's picker should NOT auto-seed any subset; the default OutputMixer
state on a fresh session has zero OTTO strips. The picker is the
ONLY path to surface an OTTO strip.

### Note D — Slice 4 needs a transport-start surface to be audible

Slice 4a's engine plumbing is correct, but on a fresh session OTTO's
transport never rolls (per `project_otto_is_the_transport_source`,
OTTO supplies transport TO IDA, not the other way around — and IDA
has no engine-side transport state). Without OTTO playing, every
OTTO strip is silent regardless of routing. **Slice 4d** is the
smallest piece of M-OTTO-6 territory needed to make slice 4 actually
audible: a minimal Play/Stop affordance wired to OTTO's
`TransportTracker::start/stop`. The current session deferred this;
without it the operator-verify path for M-OTTO-4 is "see the strip,
move the fader, observe silence." Land 4d alongside or shortly after
4b so eyes-on is meaningful.

### Note E — Member-declaration order still matters

`fileInputRegistry_` is declared in MainComponent.h BEFORE `ottoHost_`,
which means `ottoHost_` is destroyed first (reverse-declaration
order). Its dtor stops the drainer Timer and unsubscribes from
OTTO's bus before `fileInputRegistry_` falls out of scope, so no
explicit `removeTransportListener` is needed in the dtor. Same logic
holds for the new `setOttoRenderSource(nullptr)` — we don't call it
explicitly because `audioCallback_` is destroyed before `ottoHost_`
(callback declared after ottoHost_ via `audioCallback_` at line 339,
ottoHost_ at line 372). Actually wait — verify that ordering is
right. The destructor at MainComponent::~MainComponent calls
`removeAudioCallback` explicitly before any member destruction, so
the callback is detached from the device manager and no audio runs
during teardown regardless.

---

## ▶ 3. What's next

### (A) Begin slice 4b — "Add OTTO source" picker UI (recommended)

The slice 4a engine seam exists. Slice 4b is the operator-facing
trigger: a right-click + long-press gesture on the OutputMixerPane
background (per `feedback_ios_long_press_pairs_right_click`,
gestures must pair) opens a popup listing the 32 OTTO outputs by
friendly name, filtered to exclude already-added entries. Selection
calls `MainComponent::addOttoOutputStrip(idx)`.

**Friendly names** (matches OTTO's canonical layout):
- 0..15: "OTTO Drum 1" .. "OTTO Drum 16"
- 16..19: "OTTO Perc 1" .. "OTTO Perc 4"
- 20..21: "OTTO Shaker 1" .. "OTTO Shaker 2"
- 22..23: "OTTO Hand 1" .. "OTTO Hand 2"
- 24..27: "OTTO FxRet 1" .. "OTTO FxRet 4"
- 28: "OTTO Drums Bus", 29: "OTTO Percs Bus",
  30: "OTTO Shakers Bus", 31: "OTTO Hands Bus"

**Visual band placement**: probably a new `ottoStrips_` band on
OutputMixerPane (mirroring the MON-band pattern), placed between
the phrase band and the bus band. Discriminator question:
CompactFaderStrip's `ChannelType` enum has Instrument / FXReturn /
Bus / Master. Instrument is already used by phrase strips, FXReturn
by MON, Bus by aux buses + master. Three options to discriminate
OTTO strips in listener callbacks:
- Reuse `ChannelType::Master` with non-negative row IDs (master
  uses `kMasterStripId = -1`, so non-negative is free) — internal
  hack but works.
- Use `ChannelType::Instrument` with a row-ID offset (e.g. OTTO
  strips at 10000+, phrase strips at 0..N).
- File an OTTO inbox entry asking for a new ChannelType value
  (cross-project change; cleanest semantically).

Recommend: ChannelType::Master + non-negative row IDs (in-IDA only,
no OTTO change needed, no row-ID magic constants). Document the
"Master row ID -1 = real master, >= 0 = OTTO strip" convention in
the OutputMixerPane's listener dispatch.

**Size**: medium. ~1-2 commits (UI band scaffolding + listener
dispatch + picker popup).

### (B) Slice 4c — OTTO strip persistence

Once an operator adds OTTO strips via slice 4b, they should survive
session save/load. Touches IDA's session JSON envelope (adds an
`"otto_output_strips"` key holding the list of bound ottoOutputIndex
values + their fader/mute/destination state) and the OutputMixer
channel-restore path. Round-trip test pins behaviour.

**Size**: small-medium. ~1 commit.

### (C) Slice 4d — OTTO transport-start surface

Without a way to start OTTO's transport, slice 4 is operator-silent.
Add a minimal Play/Stop affordance wired to OTTO's
`TransportTracker::start/stop` (likely a method on OttoHost like
`setTransportPlaying(bool)` + a UI surface — keyboard shortcut or a
small transport bar entry). Bumps into M-OTTO-6 territory; the
minimum needed for slice 4 eyes-on is a single Play/Stop button or
hotkey.

**Size**: small. 1 commit.

### (D) Wait for OTTO to fix EventBus::publish

OTTO's next session sees the 2026-05-27 inbox brief in
`external/OTTO/CROSS_PROJECT_INBOX.md`. Until OTTO lands the
lock-free + alloc-free rewrite, IDA's SPSC marshal in OttoHost
continues to absorb the cost; no IDA-side change needed.

### Default recommendation: **(A) slice 4b, then (C) slice 4d**

Together they make M-OTTO-4 operator-verifiable end-to-end: pick an
OTTO source → see the strip → start OTTO transport → hear audio
through IDA's master output. Slice 4c (persistence) can follow once
the picker is paying its way.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `a1fd81e` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected) |
| lsfx_tapecolor pin | `a812670` (OFF-passthrough fix) — unchanged from prior session |
| OTTO submodule pin | `4cdbad3e` (inbox EventBus brief expansion) — unchanged within this session |
| sfizz submodule pin | `f5c6e29f` — unchanged |
| ctest baseline | **790/791** (1 not-run is the separately-built MainComponentPluginEditorTests, same as before; +8 net new test cases from slices 1 & 2 — slice 4a added zero tests by design) |
| `[otto-host-render]` | 6 cases / 157 assertions green |
| `[audio-callback][otto-render]` | 2 cases all green |
| `[tapecolor-adapter]` | 5/5 green at lsfx a812670 (still green from prior session) |
| `[file-input]` | 42 cases / 3820 assertions green |
| `[otto-host-transport]` | 6 cases / 30 assertions green |
| IDA app builds + links | yes (clean Release build) |
| Operator eyes-on (pending) | (1) launch IDA, confirm OttoHost-instantiation + new audio-thread renderBlock call doesn't perceptibly affect boot or steady-state CPU (slice 1 + 2); (2) M-OTTO-4 audible verification requires slice 4b + slice 4d to land first. |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per
   the cross-project protocol. The 2026-05-27 IDA→OTTO EventBus
   implementation brief should still be `needs-ack` unless OTTO's
   Claude has landed the fix between sessions. If OTTO has posted a
   fix-and-bump entry, bump OTTO submodule + ack per protocol.
3. Pick from §3. Default (A) slice 4b, then (C) slice 4d.
4. If picking (A), start by reading
   `app/MainComponent.cpp:1823-...` (the `OutputMixerPane` nested
   class) — specifically the `appendPhraseStrip` /
   `appendMonStrip` / `setPhraseStrips` / `setMonStrips` pattern as
   the template to mirror. The pane's CompactFaderStripListener
   callbacks (in the same class) are where the OTTO-vs-others
   discriminator lands.

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
- **OTTO integration design (4 foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md`
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching sections in both `CLAUDE.md` files
- Whitepaper: `docs/IDA_Whitepaper_V9.md`

Memory:
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer (slice 4 target)
- `project_minimal_default_mixers` — informed the operator-on-demand picker decision
- `project_otto_is_the_transport_source` — IDA has no engine-side transport state; OTTO supplies play/stop; the slice 4d transport-start surface lives here
- `project_otto_is_a_submodule_now` — submodule consumption model
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics
- `feedback_ios_long_press_pairs_right_click` — slice 4b's picker gesture must pair right-click + long-press
- `feedback_sirius_done_right_and_complete` — slice 4 isn't "done" until 4b + 4d are landed (operator can see + touch + hear)

---

*End of session. M-OTTO-4 engine plumbing complete (slices 1, 2, 4a)
in 3 IDA feature commits, no OTTO-side commits, no submodule bumps.
ctest 790/791, zero flakes. Next session: slice 4b (picker UI) + 4d
(transport-start surface) by default; 4c (persistence) can ride after.*
