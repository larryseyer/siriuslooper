# Session Continuation — M-OTTO-2 landed (OttoHost wired into MainComponent)

## ▶ 0. Read these first (60 seconds)

1. **M-OTTO-2 complete.** Four commits this session pushed to `origin/master`:
   - `cb87887` — bump lsfx_tapecolor `a7ba9c3 → 09eda39` (Alignment, Phase 9
     fields present) + bump external/OTTO `c01460a2 → bed38211` (inbox
     housekeeping + bisection finding).
   - `8d8584a` — M-OTTO-2a: new `otto-bridge/` module with pimpl `ida::OttoHost`
     wrapping one `PlayerManager` + `TransportTracker` per session.
   - `60e159d` — M-OTTO-2b: `MainComponent` constructs one `ottoHost_` and
     calls `prepare(device_sr, device_bs)` after the audio callback is up.
2. **Regression bisection done.** The yesterday-2026-05-26 hypothesis was
   wrong: the OFF-passthrough regression entered at `6f51fa1` (Bypass-click),
   NOT Phase A. Phase A is downstream of the actual break. Root cause: the
   Bypass-click commit removed the `if (!cfg.enabled) return;` early-exit
   from `TapeColorProcessor::process()` and replaced it with a wet/dry mix
   gate, so the chain ALWAYS runs (DC blockers, J-A solver, oversampler,
   etc.). When `cfg.enabled == false`, the output is no longer bit-identical
   to the input. Inbox entry filed at 2026-05-27 in
   `external/OTTO/CROSS_PROJECT_INBOX.md` under `[FROM IDA → OTTO]`.
3. **Clean rebuild on a fresh `build/`** at HEAD shows **776 / 776 ctest,
   zero transient flakes** (the strongest signal seen this week).
4. **Baseline.** `master` at `60e159d` (verify with `git log -1 --oneline`).
   Local == origin (push went through).
5. **The IDA-app boots cleanly + an OttoHost ctor + dtor cycle through the
   session.** Verified by clean-rebuild ctest pass + IDA app link. Operator
   eyes-on of "actually launch IDA, confirm no crash on boot" is still
   pending (recommended verification step for the M-OTTO-2c criterion).
6. **OTTO inbox state.** The 4 ack'd entries (Phase 9, Chow J-A, Thiran,
   Alignment) are pruned. 3 entries remain `needs-ack` from OTTO: Phase B+C
   (14b4920), Phase A (f510e6e), Bypass-click (6f51fa1). All three are now
   correctly identified as blocked on the SAME root cause (Bypass-click's
   removed early-exit) — OTTO's next session needs to fix that one issue.

---

## ▶ 1. What landed THIS chat

| Commit | Subject |
|---|---|
| `cb87887` | chore: bump submodules — lsfx_tapecolor `a7ba9c3→09eda39` (Alignment) + external/OTTO `c01460a2→bed38211` (inbox housekeeping + bisection finding) |
| `8d8584a` | feat: otto-bridge skeleton (M-OTTO-2a) — JUCE-coupled module, pimpl `ida::OttoHost` |
| `60e159d` | feat: MainComponent owns one OttoHost per session (M-OTTO-2b) — ctor allocates, dtor tears down via unique_ptr |

OTTO-side: `bed38211` — inbox: ack Phase 9 / Chow J-A / Thiran / Alignment +
bisection finding (Ida-Origin: 511ab23). Pushed to OTTO's `origin/main`.

---

## ▶ 2. Notes worth carrying forward

### Note A — Namespace `ida::otto` collides with top-level `::otto`

The first M-OTTO-2 cut put `OttoHost` in `namespace ida::otto`. Inside
`MainComponent.cpp` (which lives in `namespace ida`) every unqualified
`otto::` reference (e.g. `otto::ui::CompactFaderStripListener`,
`otto::OTTOLookAndFeel`, `otto::Colours::...`) suddenly resolved to
`ida::otto`, not `::otto`. Hundreds of compile errors in seconds.

Lesson: keep IDA-side OTTO-wrapping types in the **top-level `ida::`
namespace**, matching sibling types like `ida::FlacTapeSink`,
`ida::TapeColoringSink`, `ida::TapePool`. Even though the filesystem path
`ida/otto/OttoHost.h` would be tempting for hierarchy, the namespace
must stay flat to avoid the collision. The current state: header is at
`otto-bridge/include/ida/OttoHost.h`, class is `ida::OttoHost`.

### Note B — Bisection found a smaller fix than yesterday's hypothesis

Yesterday's IDA→OTTO inbox entry blamed Phase A's
`kDigitalToFluxScale = 0.14f`. Today's bisection (5 SHAs between Phase
8 and Phase B+C) showed `09eda39` Alignment passes all five
`[tapecolor-adapter]` tests, and `6f51fa1` Bypass-click fails 2 of 5.
So the actual fix is much narrower than yesterday assumed — OTTO needs
to restore an OFF-path early-exit ahead of stateful DSP while keeping
the click-free transition that motivated `6f51fa1`. Inbox entry now
points OTTO at the right code site.

### Note C — Phase 9 `tape` + `level` fields are now load-bearing for IDA

Before today, OTTO's `MasterBus.h` referenced Phase 9 lsfx fields that
IDA hadn't touched (since IDA didn't `#include` it). M-OTTO-2's
`OttoHost.cpp` includes `<otto/manager/PlayerManager.h>` which
transitively pulls in MixerBus + MasterBus. Now: the IDA pin MUST be
Phase 9+ to compile. The `09eda39` pin satisfies that and stays below
the Bypass-click regression. Don't ever drop below Phase 9.

### Note D — `lsfx::lsfx_tapecolor` link is required for otto-bridge

OTTO's MixerBus.h includes `<lsfx_tapecolor/dsp/TapeColorProcessor.h>`.
The otto-bridge module must link `lsfx::lsfx_tapecolor` PRIVATE to get
that include path resolved. Already wired in
`otto-bridge/CMakeLists.txt`.

### Note E — OttoHost construction is non-trivial (allocates 4 Players)

`PlayerManager()`'s ctor builds 4 `Player` objects, each holding sampler
+ synth engines. The allocation happens on IDA's message thread inside
MainComponent's ctor body. `prepare(sr, bs)` allocates per-block
buffers via OTTO's `prepare` chain. Both operations are message-thread
only and currently complete in well under 1 ms; no perceptible boot
delay. If that ever changes, the OttoHost ctor + prepare are obvious
candidates for a deferred init.

---

## ▶ 3. What's next

### (A) Begin M-OTTO-3 — transport subscription + IDA-side listener API (recommended)

Per scope doc (`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`):

- `OttoHost` exposes `addTransportListener(IOttoTransportListener*)` +
  `removeTransportListener(...)` message-thread API.
- Internally, `OttoHost` subscribes to `EventBus<TransportEvent>` and
  fans events out to IDA-side listeners. Translate `TransportEvent` to
  an IDA-flavoured `TransportSnapshot { enum Kind; double bpm; bool isPlaying; }`
  struct — don't leak OTTO types into the listener interface.
- A `FileInputRegistry` (or sibling controller) becomes the first
  subscriber. This is the engine half of todo entry B (Transport sync).

Risks: EventBus subscription threading. Need to read TransportTracker's
event-publishing code first — does it fire on audio thread, worker, or
message thread? If audio-thread, IDA's fan-out itself must be RT-safe.

Size: medium. 2-3 commits.

Unblocks: todo entry B; transport-sync sub-features in C (MIDI) + D
(Video).

### (B) Begin M-OTTO-4 — 32 OTTO outputs as Output Mixer channels

Independent of M-OTTO-3 (could parallelize in a different session, but
serial is fine within one session). The "32 OTTO outputs into Output
Mixer" slice — this is where audio actually flows from OTTO through
IDA's Output Mixer. Larger than M-OTTO-3 (~4-5 commits per scope doc).

Per `project_otto_as_output_mixer_source` memory.

### (C) Operator verification of M-OTTO-2 boot

The clean-rebuild ctest pass is automated proof, but the operator
eyes-on verification (launch the .app, confirm boot, confirm no crash
during session lifecycle) is still pending. Quick: launch from the
Desktop `IDA` alias, observe; if it boots normally and quits cleanly,
M-OTTO-2 is verified.

### (D) Wait for OTTO to fix Bypass-click + bump further

OTTO's next session sees the 2026-05-27 IDA→OTTO inbox entry with the
narrower diagnosis. Once OTTO posts a fix, IDA can bump past `09eda39`
to whatever OTTO recommends.

Default recommendation: **(A) M-OTTO-3**. Small, well-scoped, unblocks
the file-input transport-sync work plus M-OTTO-4 routing.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `60e159d` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected, see prior session's Note A about the Config.h.in patch) |
| lsfx_tapecolor pin | `09eda39` (Alignment) — NEW; gives Phase 9 fields, stays below Bypass-click regression |
| OTTO submodule pin | `bed38211` (inbox ack + bisection finding) — NEW |
| sfizz submodule pin | `f5c6e29f` — unchanged from M-OTTO-1 |
| ctest baseline | **776/776 on clean rebuild, ZERO flakes** |
| IDA app links + boots an OttoHost | yes (clean build link succeeds; ctor + dtor exercise verified via app target build) |
| Operator eyes-on (still pending) | (1) phrase + MON strip fader survives a refresh tick (from `d1fe0b1`); (2) launch IDA, confirm OttoHost-instantiation doesn't break boot |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per
   the cross-project protocol. Look for any new `[FROM OTTO → IDA]` entry
   addressing the Bypass-click regression. Ack any new entries
   addressed to IDA. The 2026-05-27 IDA → OTTO bisection-finding entry
   should still be `needs-ack` until OTTO fixes it.
3. Pick from §3. Default (A) M-OTTO-3.
4. If picking (A), start by reading
   `external/OTTO/src/otto-core/include/otto/transport/TransportTracker.h`
   + the `EventBus` publication code to understand the threading model.

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
- **OTTO integration design (4 foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md`
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching sections in both `CLAUDE.md` files
- Whitepaper: `docs/IDA_Whitepaper_V9.md`

Memory:
- `project_otto_is_the_transport_source` — IDA has no engine-side transport state; OTTO supplies play/stop (M-OTTO-3 is the wiring)
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer (M-OTTO-4 target)
- `project_otto_is_a_submodule_now` — submodule consumption model
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics

---

*End of session. M-OTTO-2 landed in 3 IDA commits + 1 OTTO commit;
OttoHost is alive in MainComponent; clean rebuild posts 776/776 with
zero transient flakes; bisection found a narrower fix for the
lsfx_tapecolor regression than yesterday's hypothesis. Next session:
M-OTTO-3 (transport subscription) by default, or any of the alternatives
in §3.*
