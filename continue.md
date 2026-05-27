# Session Continuation — M-OTTO-3 landed (OttoHost transport listener API + FileInputRegistry first subscriber) + lsfx OFF-passthrough fix pulled in

## ▶ 0. Read these first (60 seconds)

1. **M-OTTO-3 complete.** Three IDA commits this session pushed to
   `origin/master`:
   - `0454efb` — M-OTTO-3a: new `IOttoTransportListener` interface +
     `TransportSnapshot` POD; OttoHost subscribes to
     `otto::EventBus<TransportEvent>` and marshals snapshots from the
     audio thread to the message thread via `ida::LockFreeSpscQueue`
     + `juce::Timer` drainer at 30 Hz; `addTransportListener` /
     `removeTransportListener` message-thread API; 5 new tests under
     `tests/OttoHostTransportTests.cpp`.
   - `0c0c691` — M-OTTO-3b: relocated `IOttoTransportListener.h` from
     `otto-bridge/` to `core/` (pure dependency-inversion port,
     matches `INotificationSink` precedent). `FileInputRegistry`
     inherits the listener interface; `onOttoTransport` stub records
     the most-recent snapshot via `lastOttoTransport()`. MainComponent
     registers `fileInputRegistry_` with `ottoHost_` right after
     `prepare()`. 6th test case proves the wiring.
   - `d50e813` — submodule bumps: `lsfx_tapecolor` `09eda39 → a812670`
     (OTTO's OFF-passthrough fix for the Bypass-click regression I
     identified yesterday; all 5 `[tapecolor-adapter]` tests pass at
     the new pin) + `external/OTTO` `bed38211 → d4321510` (inbox: ack
     of OFF-passthrough fix, new IDA→OTTO entry flagging EventBus
     audio-thread alloc/lock, OTTO upstream T25 EditorHost lsfx bump
     rolled in via rebase).

2. **OTTO upstream activity this session:** OTTO's Claude landed two
   commits between my read at session start and my final push: (a) the
   OFF-passthrough fix at lsfx `a812670` consolidating the three
   needs-ack entries (Bypass-click + Phase A + Phase B+C) into one
   resolution, (b) T25 EditorHost/ParamSurface scaffolding bump. Both
   integrated cleanly.

3. **One IDA→OTTO inbox entry remains `needs-ack` from OTTO:** the
   2026-05-27 EventBus::publish audio-thread alloc/lock report I filed
   in `d4321510`. OTTO's next session needs to convert `publish` to a
   lock-free + alloc-free path (COW snapshot of subscribers suggested).
   **IDA is NOT blocked on this** — the SPSC marshal in OttoHost
   absorbs the cost. But the longer it sits, the more callers OTTO
   accumulates.

4. **Baseline.** `master` at `d50e813` (verify with
   `git log -1 --oneline`). Local == origin (all three pushes went
   through). lsfx_tapecolor pin = `a812670`; OTTO submodule pin =
   `d4321510`; sfizz pin unchanged at `f5c6e29f`.

5. **ctest: 782 / 783** (the 1 not-run is the separately-built
   `MainComponentPluginEditorTests_NOT_BUILT-b12d07c` as before — same
   number as the previous session's baseline). Six new tests added,
   30 new assertions, all green. `[tapecolor-adapter]` 5/5 green at
   the new lsfx pin. `[file-input]` 42/42 green (3820 assertions).

6. **IDA app + tests build clean** on the merged submodule pins.
   Operator eyes-on of "launch IDA, confirm boot is unchanged" is
   still pending (recommended verification step — the new timer +
   subscription shouldn't perceptibly affect boot).

---

## ▶ 1. What landed THIS chat

| Commit | Subject |
|---|---|
| `0454efb` | feat: OttoHost transport listener API (M-OTTO-3a) — IOttoTransportListener interface + TransportSnapshot POD, OttoHost subscribes to EventBus<TransportEvent>, SPSC marshal + Timer drainer |
| `0c0c691` | feat: FileInputRegistry subscribes to OttoHost transport (M-OTTO-3b) — relocate listener to core/, FileInputRegistry implements interface, MainComponent wires the registration |
| `d50e813` | chore: bump submodules — lsfx_tapecolor a812670 (OFF-passthrough fix) + external/OTTO d4321510 (inbox housekeeping + EventBus flag) |

OTTO-side commits today (visible in `git -C external/OTTO log`):
- `d4321510` — inbox: ack OFF-passthrough fix; flag EventBus::publish audio-thread alloc/lock (Ida-Origin: 0c0c691)
- `19bd8b20` — T25 EditorHost/ParamSurface lsfx bump (OTTO-originated; rolled in via rebase)
- `563b76d0` — fix: TAPECOLOR OFF-path bit-identical passthrough; bump lsfx to a812670 (OTTO-originated, acks IDA's 2026-05-27 bisection)
- `3e47fe35` — docs: continue.md TAPECOLOR T25 brainstorm (OTTO-originated)

---

## ▶ 2. Notes worth carrying forward

### Note A — IOttoTransportListener lives in `core/`, not `otto-bridge/`

First cut put the interface in `otto-bridge/include/ida/`. That forced
any IDA-side listener (FileInputRegistry, future consumers) to either
link otto-bridge or live in otto-bridge — neither acceptable.
FileInputRegistry lives in `audio/`, which deliberately stays
OTTO-free.

The fix: move the listener interface (which is pure IDA — no `otto::`
types in either header or body) to `core/include/ida/`. This matches
the `INotificationSink` precedent exactly: pure dependency-inversion
port, lives in the JUCE-free + OTTO-free `core/`, every layer that
needs to name the interface gets it transitively via `Ida::Core` →
`Ida::Engine` → consumer.

### Note B — `juce::Timer` inside OttoHost::Impl needs `juce::ScopedJuceInitialiser_GUI` in tests

The drainer is a `juce::Timer` subclass; constructing it touches the
MessageManager. Tests therefore need a `juce::ScopedJuceInitialiser_GUI`
in every TEST_CASE (same shape as `InsertChainPopupTests.cpp`). And
because IdaTests compiles without `JUCE_MODAL_LOOPS_PERMITTED`, the
test can't call `MessageManager::runDispatchLoopUntil` to spin the
timer — instead OttoHost exposes `drainForTesting()` which calls the
same drain path the Timer's callback uses. That keeps the test
synchronous and deterministic.

### Note C — OTTO's `EventBus::publish` is not RT-safe; IDA's SPSC marshal absorbs it

`otto::EventBus::publish()` takes a `std::mutex` and allocates a
`std::vector<EventHandler>` per call. `TransportTracker::update()`
calls it from `processBlock()`. Until M-OTTO-3 this was silent (zero
subscribers); now IDA is the first subscriber. The OttoHost handler
does the bare minimum required to be honest with the SPSC contract:
build a stack-local `TransportSnapshot`, push into the ring, return.
The audio-thread alloc/lock still happens inside OTTO's bus itself,
but IDA's listeners never see it. Filed for OTTO via the inbox; not
blocking for IDA.

### Note D — Member-declaration order in MainComponent makes dtor-unregister unnecessary

`MainComponent.h:318` declares `fileInputRegistry_` before
`MainComponent.h:347` declares `ottoHost_`. C++ destructor order is
reverse-declaration, so `ottoHost_` is destroyed first — its dtor
stops the drainer Timer and unsubscribes from OTTO's bus before
`fileInputRegistry_` falls out of scope. No explicit
`removeTransportListener` in MainComponent's dtor needed. Worth
checking if anyone ever reorders those members.

### Note E — lsfx_tapecolor at a812670 introduces big public-API growth (UI scaffolding)

The jump from 09eda39 → a812670 spans 16+ lsfx commits. New public
fields on `TapeColorConfig`: `inputLowShelf{Hz,Q}`,
`inputHighShelf{Hz,Q}`, `outputLowShelf{Hz,Q}`, `outputHighShelf{Hz,Q}`,
`xfmr{...}` (9 fields), `tube{...}` (4 fields). Public API additions
are additive — no IDA code change required, the new fields default
to per-machine baselines. Also: the lsfx repo now ships a full editor
Component tree under `ui/` (TapeColorEditor + tab views + factory
presets + L&F). IDA does NOT consume the UI symbols today; OTTO's
inbox offered an opt-in DSP-only target if we ever want to drop the
UI surface — declined for now, the unused symbols are free.

---

## ▶ 3. What's next

### (A) Begin M-OTTO-4 — 32 OTTO outputs as Output Mixer channels (recommended)

Per scope doc
(`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`):

- The "32 OTTO outputs into Output Mixer" slice. Per
  `project_otto_as_output_mixer_source` memory: bundled OTTO presents
  32 stereo outputs as additional Output Mixer channel strips. IDA's
  Output Mixer ALONE owns physical-output routing. Tape EXCLUDES OTTO;
  render/export INCLUDES it.
- OttoHost gains a `renderBlock(juce::AudioBuffer<float>& out, int
  numSamples)` (or similar) called from IDA's audio callback. The
  function reads 32 stereo pairs out of OTTO's MasterBus and writes
  them into per-channel buffers OutputMixer can read.
- OutputMixer needs to gain `setOttoChannelSource(...)` analogous to
  the existing `setChannelAudioSource` used by the MON path.

Risks: OTTO's PlayerManager produces audio per-Player; collapsing into
the 32 MasterBus outputs is OTTO-internal. Need to read OTTO's
PlayerManager::processBlock + MasterBus mixing logic first. Audio-thread
safety: OTTO's processBlock is itself audio-thread-safe per OTTO's
CLAUDE.md, so calling it from IDA's callback is fine — but the
boundary buffer layout must be sized at `prepare(sr, bs)` time, not
allocated in the callback.

Size: medium-large. ~4-5 commits per scope doc.

Unblocks: render/export of OTTO output; operator-visible OTTO audio
through IDA's Output Mixer.

### (B) Operator verification of M-OTTO-3 boot

The headless ctest pass is automated proof, but operator eyes-on
(launch IDA from the Desktop `IDA` alias, confirm boot, confirm no
audible artifacts or perceptible delay from the new timer +
subscription) is still pending. Quick: launch, observe, quit. The
listener path is dormant until OTTO publishes transport events
(M-OTTO-4 wires actual audio through), so no audible change is
expected on this baseline.

### (C) Wait for OTTO to fix EventBus::publish

OTTO's next session sees the new IDA→OTTO inbox entry. Once OTTO posts
the lock-free + alloc-free rewrite of `publish`, IDA bumps OTTO
submodule once more (no IDA-side code change required — OttoHost's
SPSC marshal is the same shape whether OTTO's bus is RT-clean or not).

### (D) Per-input "follow transport" UX

The engine half of transport-sync is now in place (FileInputRegistry
records snapshots). The UX half — per-file-input "follow transport"
toggle, behaviour on Started (auto-play armed inputs?), behaviour on
Stopped, interaction with arm/disarm gestures — is downstream of the
operator's mixer-first roadmap (`project_mixer_then_transport_roadmap`).
Park for a dedicated session.

Default recommendation: **(A) M-OTTO-4**. Largest remaining piece of
the OTTO integration sequencing; transport plumbing now in place
unlocks it.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `d50e813` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected) |
| lsfx_tapecolor pin | `a812670` (OFF-passthrough fix) — NEW; supersedes the Bypass-click regression that blocked us at 09eda39 |
| OTTO submodule pin | `d4321510` (inbox housekeeping + EventBus flag) — NEW |
| sfizz submodule pin | `f5c6e29f` — unchanged from M-OTTO-1 |
| ctest baseline | **782/783** (1 not-run is the separately-built MainComponentPluginEditorTests, same as before; +6 net new test cases from M-OTTO-3a/b) |
| `[tapecolor-adapter]` | 5/5 green at lsfx a812670 (regression fixed) |
| `[file-input]` | 42 cases / 3820 assertions green (FileInputRegistry's new listener inheritance is non-disruptive) |
| `[otto-host-transport]` | 6 cases / 30 assertions green |
| IDA app builds + links | yes (clean Release build) |
| Operator eyes-on (still pending) | (1) launch IDA, confirm OttoHost-instantiation + new Timer doesn't perceptibly affect boot; (2) carryover from prior session — phrase + MON strip fader survives a refresh tick |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per
   the cross-project protocol. Look for any new `[FROM OTTO → IDA]`
   entry addressing the EventBus::publish audio-thread issue. If OTTO
   has posted a fix-and-bump entry, bump OTTO submodule + ack the
   entry per protocol. The 2026-05-27 IDA→OTTO EventBus entry should
   still be `needs-ack` until OTTO fixes it.
3. Pick from §3. Default (A) M-OTTO-4.
4. If picking (A), start by reading
   `external/OTTO/src/otto-core/include/otto/manager/PlayerManager.h`
   + `external/OTTO/src/otto-core/include/otto/mixer/MasterBus.h` to
   understand the 32-output layout and the processBlock entrypoint.

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
- **OTTO integration design (4 foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md`
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching sections in both `CLAUDE.md` files
- Whitepaper: `docs/IDA_Whitepaper_V9.md`

Memory:
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer (M-OTTO-4 target)
- `project_otto_is_the_transport_source` — IDA has no engine-side transport state; OTTO supplies play/stop (M-OTTO-3 just landed the wiring)
- `project_otto_is_a_submodule_now` — submodule consumption model
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics

---

*End of session. M-OTTO-3 landed in 2 feature commits + 1 submodule
bump on IDA; 4 OTTO-side commits (one IDA-originated EventBus inbox
entry + three OTTO-originated: OFF-passthrough fix, T25, continue.md);
lsfx_tapecolor regression from 5 days of pinned-at-Phase-8 cleared
by OTTO's surgical fix at a812670; ctest 782/783, zero flakes. Next
session: M-OTTO-4 (32 OTTO outputs into Output Mixer) by default, or
any of the alternatives in §3.*
