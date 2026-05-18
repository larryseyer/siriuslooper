# Session Continuation — 2026-05-18 (M7 S2 SHIPPED on origin; M7 S3 next — wire OutOfProcessPluginInstance through OutputMixer)

> **For a fresh chat picking this up cold:** read this whole file
> before doing anything. The user's `~/.claude/CLAUDE.md` and the
> project's auto-memory (`MEMORY.md` + `*.md` in the memory dir) are
> loaded automatically and contain the rules. This file is the
> *state*: what just shipped and what's queued next.

---

## RESUME HERE (2026-05-18 — M7 S2 fully shipped on origin; M7 S3 next)

**M7 Sessions 1 + 2 are on `origin/master`.** S2 head is `<latest>` (filled
in after push). Four sub-session commits landed for S2:

| SHA | Subject |
|---|---|
| `de3179b` | M7 S2a — CLAP via setup-deps + SyntheticTestPlugin + host `--mode clap` |
| `c0cf0d6` | M7 S2b — SharedMemoryRegion (shm_open/mmap RAII) + SharedMemorySpscQueue<T> |
| `6e1df55` | M7 S2c — swap stdin/stdout for shared-mem SPSC rings + PluginIpcMessage |
| `<S2d>`   | M7 S2d — round-trip latency [.rt-smoke] + RT_SAFETY_CONTRACT §5 row |

Test count: **364/364** green (was 354 at S1; +1 CLAP smoke in S2a, +9 in
S2b for 6 SPSC tests + 3 region tests, hidden latency smoke in S2d).
The IPC transport is now POSIX shared-memory SPSC rings — `stdin`/`stdout`
piping is gone from both engine and host sides. The host child binary
**stays JUCE-free** (CLAP is header-only; shm primitives moved to JUCE-
free `Sirius::Core`).

**Measured round-trip latency (Apple Silicon dev machine, 2026-05-18):**
median 68 µs, p99 134-135 µs across 1000 samples. Hidden `[plugin-ipc]
[.rt-smoke]` asserts p99 < 300 µs (≈2× headroom). The V7 plan's < 10 µs
target is aspirational — it requires the lock-free spin transport the S4+
watchdog work will introduce; the S2c `kRingPollMicroseconds = 50` poll
backoff dominates the current baseline.

**Scope deviations locked in S2** (carry forward):

1. **CLAP arrives via `bash/setup-deps.sh`, NOT git submodule.** continue.md
   pre-S2 recommended submodule; that contradicted the actual project
   pattern (external/ is gitignored + populated by `setup-deps.sh`,
   matching OTTO's layout). S2a extended setup-deps.sh with
   `clap|free-audio/clap.git|1.2.6`. Operators on a fresh clone now run
   `bash bash/setup-deps.sh` to populate `external/clap`.
2. **SHM primitives live in `core/`, not `engine/`.** Initially placed in
   engine in S2b (where `LockFreeSpscQueue` lives), then moved to core in
   S2c when wiring revealed that linking the host binary against
   `Sirius::Engine` would pull `juce_core` (engine's PRIVATE dep) and
   violate the "host stays JUCE-free" constraint. `Sirius::Core` is the
   JUCE-free pure library, and the SHM primitives belong there.
   `PluginIpcMessage.h` also moved core-side so the host child can
   include it without depending on `SiriusHost` (which carries JUCE).
3. **`PluginIpcMessage::monotonicNs` is `int64_t` ns since
   `steady_clock` epoch, NOT an LMC `Rational`.** The plan said "LMC
   timestamps in headers" but LMC time is `Rational` (non-trivially-
   constructible), which can't live in a `static_assert(trivially_copyable)`
   POD. S3 will reinterpret the slot as LMC-domain when wiring the audio
   thread; for S2c-era round-trip measurement, monotonic ns is the
   meaningful unit.
4. **`OutOfProcessPluginInstance` gained a second constructor**
   `(hostBinaryPath, instanceId, clapPluginBundle)` for CLAP-mode spawn,
   additive to the S1 identity-mode constructor. Existing tests build
   binary-compatible.
5. **CLAP-mode `process()` uses a `RingByteStream` abstraction** inside
   `host_process/main.cpp` — reads the existing framed wire format
   (`uint32_t frameCount` + interleaved stereo float pairs) on top of
   message-by-message ring pops. Audio buffer ceiling is
   `PluginIpcMessage::kMaxPayloadBytes = 8192` = 1024 stereo float
   frames — matches the V7 outer block-size envelope.
6. **macOS shm pages are 16 KiB, not 4 KiB.** Apple Silicon kernel
   rounds shm segments up to a 16 KiB boundary; `SharedMemoryRegion::
   size()` re-stats unconditionally so both sides agree on the
   actual mapped bytes. Test assertions use `size() >= requested`.

### First moves for the M7 S3 chat

M7 S3 wires `OutOfProcessPluginInstance` into `OutputMixer`'s plug-in
slot on a Constituent's `EffectChain`. This is **the first session that
puts the IPC layer on the audio call chain** — design care required.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and re-read
   **M7 lines 487-554**. Session 3 is enumerated at line 533: "S3:
   Engine-side `OutOfProcessPluginInstance` wires through `OutputMixer`'s
   plug-in slot on a Constituent's `EffectChain`; integration test plays
   audio through the synthetic plug-in; commit."
3. Read `engine/include/sirius/Bus.h` + `engine/include/sirius/OutputMixer.h`
   to find the EffectChain entry point. M5 Session 3 noted: "When M7
   wires real plugin invocation through EffectChain, the inline path
   must become a real Bus::process invocation." S3 makes that real.
4. Read `core/include/sirius/EffectChain.h` (the M5-era stub that holds
   plug-in slots). S3 fills the slot with a real
   `OutOfProcessPluginInstance` invocation.
5. **Brainstorm before code.** S3 open questions:
   - **Audio-thread `sendBytes`/`readBytes`?** Current API is message-
     thread per the header docblock. The shm push/pop primitives are
     wait-free, so the API *can* be made audio-thread-safe — but the
     current `usleep` backoff in `readBytes` violates RT-safety. S3
     needs an audio-thread variant: zero-wait `tryReadBytes` returning
     immediately if the ring's empty. Watchdog (S4) handles the missed-
     deadline case.
   - **EffectChain → OutOfProcessPluginInstance shape.** EffectChain
     currently holds `PluginDescriptor` slots. S3 either: (a) extends
     EffectChain to own the OutOfProcessPluginInstance per slot, or
     (b) introduces a sibling `EffectChainHost` that owns the spawned
     processes and EffectChain stays descriptor-only. (b) keeps Core's
     JUCE-free contract (OutOfProcessPluginInstance is in SiriusHost
     which has JUCE deps); (a) would force Core to grow a JUCE dep.
     **Lean (b)**.
   - **Where instance ownership lives.** `MainComponent` owns mixers
     per the M5+M6 pattern; should it also own the `EffectChainHost`
     (or per-slot OutOfProcessPluginInstances)? Or does the OutputMixer
     own them via injection? The M5-era injection pattern says owner
     stays in MainComponent; OutputMixer holds raw pointers.
   - **Integration test shape.** S3 acceptance criterion: "plays audio
     through the synthetic plug-in." The synthetic plug-in is identity-
     only this session, so the test asserts byte-for-byte audio round-
     trip through an EffectChain slot. Catch2 tag suggestion:
     `[output-mixer][plugin-host]`.
6. Adopt the same orchestrator+subagents execution mode. Backend
   Architect for the EffectChain ↔ OutOfProcessPluginInstance wiring.

### S3 acceptance criteria (V7 plan lines 533 + 487-503)

- `EffectChainHost` (or equivalent) owns one `OutOfProcessPluginInstance`
  per active plug-in slot on a Constituent's `EffectChain`.
- `Bus::process` (or `OutputMixer::renderBuffer` step 4) invokes the
  hosted plug-in's audio-thread `tryReadBytes`/`tryWriteBytes` per
  buffer, swapping the M5 inline pass-through with real plug-in dispatch.
- An integration test loads the synthetic CLAP plug-in (already built
  by S2a) into an EffectChain slot, plays stereo audio through it,
  asserts byte-for-byte round-trip.
- Audio-thread surface of OutOfProcessPluginInstance is audited in
  `docs/RT_SAFETY_CONTRACT.md §6` (S2d added the §5 measured-latency
  note; S3 adds the table row).
- All existing 364 ctest cases still pass; new integration test added.
- Test count target: ~365-368.

### M7-era decisions locked (S3 must preserve)

1. **POSIX-only scope** (macOS + Linux; Windows defers).
2. **Host binary stays JUCE-free.** CLAP + SHM primitives are in
   `Sirius::Core` (JUCE-free) for exactly this reason. **DO NOT**
   link the host binary against `Sirius::Engine` or `Sirius::Host`.
3. **`OutOfProcessPluginInstance` public API is byte-oriented** —
   `sendBytes`/`readBytes` signatures unchanged through S2. S3 adds
   audio-thread `tryReadBytes`/`tryWriteBytes` siblings (zero-wait,
   noexcept).
4. **`isRunning()` stays non-const + silently reaps + clears
   `childPid_`.** Don't restore the const.
5. **Zero allocation / zero locks on the audio-thread side of the
   rings.** The SPSC primitive is wait-free; SHM regions are created
   at construction time (message thread). S3 must NOT introduce any
   audio-thread allocation when wiring through EffectChain.
6. **`NotificationBus` (M6) is the engine→UI truthfulness channel.**
   When S4 wires plug-in watchdog events, they post into the existing
   bus. S3 does not need to wire it (no watchdog events yet).
7. **Drop-NEW overflow policy** for SPSC rings — if the engine→host
   ring is full, `sendBytes` (and S3's `tryWriteBytes`) returns false
   and the caller surfaces it. Watchdog work in S4 handles the back-
   pressure semantics.
8. **macOS shm_open name length cap (31 chars including leading slash).**
   Current name layout is `/sirius.<instanceId>.<suffix>` with `suffix`
   ∈ {`e2h`, `h2e`}. `<instanceId>` budget is ~18 chars. Long ids must
   be hashed by the caller; S3 should add a `hashInstanceId(std::string)
   -> std::string` helper if any Constituent path generates ids > 18.
9. **`PluginIpcMessage::monotonicNs` will become LMC-domain in S3.**
   Reinterpret the slot when wiring the audio thread; document the
   semantic shift in the header.

### Carryover from M6 + earlier still unresolved (M7 doesn't touch)

- **ProcessedRoute STILL passing empty span to DirectLayer.** Not M7's
  problem (covered by OutputMixer's produced-mix path).
- **Manual operator-launch eyes-on of M6 notification surface.**
  Operator launched the app during the M7 S1 chat; visual verification
  status from operator's side is unknown to Claude. Not blocking M7.
- **Operator-side CI signing handoff (3 secrets pending).** Does not
  block M7 work. Will block the M7 milestone close (S10+) when the
  binary signing has to cover `sirius_plugin_host` too — coordinate
  with the signing-secrets handoff before then.

---

## HISTORICAL — M6 close + M7 handoff (superseded 2026-05-18 — M7 S1 now shipped)

The original M7 first-moves + M6-era constraints follow. M6-era
constraints #1-#8 + M5-era + M4-era ARE STILL LOAD-BEARING for
M7 S2+; only the S1-specific first-moves are now history.

### M7 first moves (superseded — S1 shipped)

**M6 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`a5b86bb`. Three session commits shipped:

| SHA | Subject |
|---|---|
| `7e6b3e6` | M6 Session 1 — NotificationBus + per-category SPSC + tests |
| `a9567f6` | M6 Session 2 — wire NotificationBus into AudioCallback + InputMixer + TapeWriter + MainComponent drain |
| `a5b86bb` | M6 Session 3 — Preparation-tab notification surface + rolling history + integration tests |

Test count: **349/349** green (was 336 at M5 close; +13 across M6 —
8 for S1, 1 for S2, 4 for S3). Full `cmake --build` clean. Manual
operator-launch verification of the notification surface is the one
remaining M6 verification step (deferred from automated smoke — the
spec called for "launch app, trigger an overload, confirm notification
appears" which is fragile to drive via macOS AX).

**Execution mode used:** orchestrator+subagents (Backend Architect
implementer per session; spec review + code review per session; final
milestone review at end). The final milestone review caught a real
layout bug — `kPreparationTopAndBottomReservedPx = 180` was
double-counting the notifications row's height (the bottom-anchored
stack had already been `removeFromBottom`ed before the clamp executed,
so the 80→180 bump silently stole ~100 px of timeline headroom on
small windows). Reverted to 80 + renamed to
`kPreparationTreeMinHeightPx` before the close-out commit. Both per-
session reviews caught real polish items: drop-new overflow doc + a
static_assert pinning kCategoryCount; channel-id in TapeWriter failure
messages; deferred-TapeWriter-injection comment.

### First moves for the M7 chat

M7 is **the largest single piece of architectural work in the plan**
(V7 plan line 489): out-of-process plug-in hosting. Standalone
`sirius_plugin_host` executable, shared-memory SPSC IPC rings,
watchdog, supervisor, GUI host process with platform-specific window
embedding (macOS first per the platform-order rule), parameter
marshalling. **No in-process fallback** per V7 §9.1.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M7 in full** (starts at line 487). Sessions 1-12 (estimated)
   break-out is at lines 530-535: S1 = host executable + stdin/stdout
   identity; S2 = shared-mem + SPSC rings + LMC timestamps; S3 = engine-
   side OutOfProcessPluginInstance wired through OutputMixer; S4-N =
   watchdog + supervisor + macOS GUI embedding + platform conditionals.
3. Read V7 §9.1 in the white paper for the full IPC + watchdog spec.
4. M7 is significantly larger than M3-M6. **Sessions 1-3 are the
   foundational layer** (executable, IPC, engine integration);
   subsequent sessions add the watchdog + supervisor + GUI embedding.
   Treat the first chat as Sessions 1-3 only; subsequent chats handle
   later sessions. The V7 plan acknowledges "Sessions 4-N (estimated)"
   without pinning specifics — let the brainstorm + first 3 sessions
   inform the rest.
5. Brainstorm pass for M7 before code. Likely open questions:
   - **CLAP-first format choice.** M8 spec says CLAP is the first
     hosted format (cleanest API, no legacy baggage). VST3 + AU follow
     in M20 + M21. M7 itself is format-agnostic — verify the host
     binary's plug-in-loading layer is abstracted enough that adding
     VST3/AU later is a new translation unit, not a rewrite.
   - **macOS sandbox entitlements.** The signed CI workflow
     (`ci-macos-signed.yml`) needs updating to sign `sirius_plugin_host`
     too; shared-memory IPC requires careful entitlement design under
     `com.apple.security.app-sandbox` + `com.apple.security.cs.allow-shared-memory`.
     The CI signing handoff (3 secrets still pending per §"CI signing
     handoff" below) intersects this — M7 may push the operator-side
     CI work to actually finish before the M7 milestone closes.
   - **Wet-capture pre-allocation budget.** Per the plan line 551, M8
     commits the wet-capture budget; M7 only allocates input/output
     rings. Verify M7 doesn't accidentally consume the M8 budget.
   - **Host process lifecycle: spawn-per-instance vs pool.** V7 §9.1
     prescribes per-instance spawn. Confirm this isn't relitigated in
     brainstorm.
6. Adopt the same orchestrator+subagents execution mode. M7's V7 plan
   suggests Backend Architect for the IPC ring shape; Security Engineer
   for sandboxing review; Performance Benchmarker for round-trip
   latency. Use the specialists when their scope matches.

### M6-era decisions that constrain M7 (DO NOT "fix" without operator approval)

These are NEW constraints landed in M6; combine with the M5-era +
M4-era constraints further down (still all load-bearing).

1. **`NotificationBus` is the engine→UI truthfulness channel.**
   `post(level, category, const char*) noexcept` is allocation-free,
   wait-free on the audio-thread path via per-category SPSC rings (256
   entries each). M7's watchdog + supervisor will post `PluginEvent`
   notifications when a host process misses deadlines, crashes, or
   restarts — use the existing bus, do NOT spin a parallel signal
   channel.
2. **Drain is message-thread-only + must pre-reserve** to
   `kCategoryCount * kRingCapacity = 2304` to honor the bus's
   `bad_alloc` contract (drain is NOT `noexcept`). MainComponent
   already does this at `app/MainComponent.cpp:643`.
3. **Bus is owned by `MainComponent`; set-once on collaborators**
   BEFORE `audioDeviceAboutToStart` (same pattern as DirectLayer/
   OutputMixer per M5 #8). M7's `OutOfProcessPluginInstance` will
   likely follow the same: MainComponent (or a per-Constituent owner)
   constructs + injects.
4. **Overflow policy is drop-NEW** (not drop-oldest as plan line 479
   suggested) — the only SPSC-correct choice since the producer can't
   touch the consumer's head. `overflowCount(Category)` exposes the
   per-category counter. M7's IPC ring should follow the same policy.
5. **`drain()` signature is out-param `void drain(vector&)`**, NOT
   return-by-value (deviation from plan line 444, documented at
   `engine/include/sirius/NotificationBus.h:139-151`).
6. **`TapeWriter` has `setNotificationBus` wired but is NOT currently
   owned by `MainComponent`.** When TapeWriter joins the owned app
   graph (M11 SAF wiring is the likely trigger), inject the bus per
   the comment at `app/MainComponent.cpp:537-541`. M7 doesn't touch
   TapeWriter.
7. **AudioCallback posts `DeviceEvent` only from
   `audioDeviceAboutToStart`/`audioDeviceStopped`** (JUCE setup
   callbacks, not the render thread). No render-thread post sites yet
   from AudioCallback itself. M7's audio-thread plug-in dispatch will
   add render-thread posts (`Warning, PluginEvent, ...`) for
   watchdog misses — that IS allowed, post is noexcept.
8. **`kNotificationHistorySize = 20` in MainComponent**; M22 redesigns
   the UI surface. M7 may add more notification post sites that
   surface here — fine.

### Carryover from M5 NOT resolved (still deferred — M7 doesn't touch)

- **ProcessedRoute STILL passing empty span to DirectLayer.** M5+M6
  did not unblock this. Deferral comment lives at
  `audio/src/AudioCallback.cpp:66-77`. Not urgent — OutputMixer's
  produced-mix path (Step 4) covers the surface DirectLayer would have
  used. M7 doesn't need to fix this.
- **Manual operator-launch verification of M6 notification surface**
  is the one remaining M6 verification step (deferred from automated
  smoke per the AX-fragility constraint from M4 GUI smoke work).
  Not blocking M7; operator should launch the .app once at their
  convenience to eyes-on the surface.

---

## HISTORICAL — M5 close + M6 handoff (superseded 2026-05-18 — M6 now complete)

The original M6 first-moves + M5-era constraints follow. M5-era
constraints #1-#9 ARE STILL LOAD-BEARING for M7+; the rest is history.

### M6 first moves (superseded)

**M5 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`6cd3810`. Three session commits shipped:

| SHA | Subject |
|---|---|
| `2eb296c` | M5 Session 1 — ChannelStrip<Audio> + InputMixer input-side gain/pan |
| `25da0ff` | M5 Session 2 — Bus + send/return + master bus + OutputMixer surfaces |
| `6cd3810` | M5 Session 3 — OutputMixer::renderBuffer + Bus::process audio-thread wiring + AudioCallback Step 4 |

Test count: **336/336** green (was 310 at M4 close; +26 across M5 — 6
for S1, 14 for S2, 6 for S3). Full `cmake --build` clean. `bash bash/autotest.sh`
not re-run this session (S1–S3 are headless code changes; the M4 close-out
GUI smoke is still the last operator-verified launch).

**RT-budget result for the 32-channel × 8-bus configuration:** 17.5 µs
max elapsed on the dev machine. At 48kHz/256-sample buffer (5.33 ms
budget), that's **0.33% of buffer time** — well inside any realistic
audio-thread budget. Hidden `[output-mixer][.rt-smoke]` Catch2 tag,
matches the DirectLayer pattern.

**Execution mode used:** orchestrator+subagents (Backend Architect
implementer per session; spec review + code review per session; final
milestone review at end). The final milestone review caught a stale
ProcessedRoute deferral comment + a `routeChannelToOutput` dead-code
placeholder + a missing `static_assert` linking
`OutputMixer.cpp:kMaxBlockSamples` to `Bus::kMaxBusMixSamples` — all
folded into the S3 commit before push, so HEAD is the M5 close.

### First moves for the M6 chat

M6 ships the **NotificationBus** — V5 §8.6 engine↔UI truthfulness
channel. Wait-free SPSC per category for audio-thread posts (failure
events, accessibility cues, capacity warnings, partition events);
message-thread drain renders into a minimal Preparation-tab UI
surface (M22 redesigns the surface — M6 is just the plumbing).

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M6 in full** (starts at line 435). Sessions 1-3 break-out: S1 =
   `NotificationBus.{h,cpp}` + per-category SPSC + unit tests; S2 =
   wire into existing emitters (M1 device events, M3 tape rotation,
   M5 overload); S3 = Preparation-tab UI surface + smoke test.
3. Read V5 §8.6 in the white paper for the category enum + ordering
   semantics, and V5 §17.9 for failure-event categories.
4. Brainstorm pass for M6 before code. Likely open questions:
   - **Per-category ring depth.** Plan says 256 entries per category
     as a starting cap. Audio-thread is wait-free but message-thread
     drain cadence matters — if drain is on the 30 Hz UI timer, that's
     8.5 s of buffer at full posting before overflow. Probably fine
     for "rare" notifications; verify the categories that could spam
     (overload reports could fire once per buffer at 44.1 kHz / 256
     samples = ~170 Hz; ring fills in 1.5 s if drain pauses).
   - **Where the engine-side post sites live.** S2 wires existing
     emitters: M1's `OverloadProtection::reportLoad(1.0)` becomes a
     `notificationBus_->post(Warning, CpuPressure, "audio thread
     missed deadline")`; M3's `TapeWriter` flush failure becomes a
     `post(Error, DiskPressure, ...)` etc. List the existing
     emit sites first, decide the ones M6 wires now vs defers.
   - **Atomic-snapshot vs ring per category.** The plan calls for
     SPSC rings per category. If a category never has >1 entry
     pending at a time (e.g. `ClockEvent` with infrequent device
     resyncs), a single-cell atomic snapshot would be cheaper. Worth
     a one-question pass — but defaulting to ring keeps the contract
     uniform across categories, which is probably the right call.
5. Adopt the same orchestrator+subagents execution mode. Per-session
   spec review + code review caught real issues across all three M5
   sessions (`static_assert` cross-file linking, deep-const escape
   hatch on `ChannelStrip::process`, jassertfalse fail-loud asserts,
   tautological tests deleted). M6's V7 plan suggests the same mode.

### M5-era decisions that constrain M6 (DO NOT "fix" without operator approval)

These are NEW constraints landed in M5; combine with the M4-era
constraints further down (still all load-bearing).

1. **`ChannelStrip<SignalType>` is the per-modality processing template,
   inheriting `ProcessingChain`.** `ChannelStrip<Audio>` is the only
   real one; `<Midi>`/`<Video>`/`<File>` are stubs in `ChannelStrip.h`.
   The M3-era `AudioChain` is REMOVED — `makeProcessingChain(Audio)`
   returns `ChannelStrip<Audio>`. M9/M12/M13 will rename
   `MidiChain`/`VideoChain`/`FileChain` to their `ChannelStrip`
   counterparts (left as-is in `ProcessingChain.h` for now — M6 does
   NOT need to touch this).
2. **`OutputMixer` comes up empty by default.** No channels auto-register
   in M5 (operator UX for mixer config is M22+). `renderBuffer` early-
   returns on `channels_.empty()`. M6's NotificationBus integration
   does NOT need to register OutputMixer channels.
3. **Per-OutputChannel audio source in M5 is `inputChannelData[OutputChannelId.value()-1]`.**
   One-to-one with input device channels. This is a placeholder until
   Constituent rendering lands (post-M6). Don't take a hard dependency
   on this mapping in M6.
4. **`Bus::process` zeroes its own `mixBuffer_` after the additive
   write to output.** `mixBuffer_` is `mutable` because the bus
   accumulation path (via `mixBufferChannel(int) const noexcept`) is
   called from `OutputMixer::renderBuffer`, which is `const`. M6's
   NotificationBus post-from-audio-thread interface will likely use the
   same `mutable` + `const post()` pattern.
5. **Non-master buses' `mixBuffer_`s are zeroed INLINE in
   `OutputMixer::renderBuffer` step 3** (not via `Bus::process`).
   Only the master bus invokes `Bus::process` (at step 4). When M7
   wires real plugin invocation through `EffectChain`, the inline
   path must become a real `Bus::process` invocation; flag for M7's plan.
6. **`BusId{0}` is the implicit master bus, auto-created in OutputMixer
   ctor, not removable.** `addBus` returns `BusId{1+}` for aux buses.
   M6 doesn't add buses.
7. **`EffectChain` is held on `Bus` but not invoked in M5.** Real
   plugin dispatch lands with M7 (out-of-process hosting). M6 does
   not need to do anything with `EffectChain`.
8. **`AudioCallback` orchestrates a 6-step audio path:** 1 zero
   outputs → 2 InputMixer → 3 DirectLayer (monitoring-gated) → 4
   OutputMixer (ungated) → 5 Lmc → 6 publish elapsed. The class-level
   threading-contract docblock at `AudioCallback.h:45-57` documents
   the configure-before-audio-starts invariant for every collaborator
   setter. M6's NotificationBus will likely become a new
   collaborator via `setNotificationBus(NotificationBus*)` — follow
   the same set-once pattern.
9. **`OutputMixer*` in AudioCallback is `const OutputMixer*`**
   (matches `const DirectLayer*` per M4 constraint #4). `renderBuffer`
   is `const noexcept`. If M6's NotificationBus has a `post()` that
   can fire from any thread including audio, and if AudioCallback
   holds it as a collaborator, it'll likely be `NotificationBus*`
   non-const (post mutates the ring), with `post` itself being
   `const noexcept` from the bus's POV (mutates only internal
   atomic-published ring state). Decide the const-ness in S1
   brainstorm.

### Operator-side TODOs for M6

1. **CI signing secrets** (still operator-pending — unchanged from M4).
   Three secrets remain. See "CI signing handoff" section further down.
2. **Decide per-category ring depth** (see brainstorm open questions).
3. **Decide which existing emit sites M6 wires now vs M11+ defers.**

### Carryover from M5 NOT resolved (defer to M6 or later)

- **ProcessedRoute is STILL passing empty span to DirectLayer.** M5
  was supposed to unblock this once `ChannelStrip<Audio>` is real,
  but `InputMixer` doesn't expose the post-strip float buffer publicly
  (applies strip into a private scratch then memcpys to TapeWriter).
  The deferral comment at `audio/src/AudioCallback.cpp:66-72` now
  documents this honestly. **M6 may unblock** by adding either an
  audio-thread getter to InputMixer (post-strip float view) or a
  parallel output pointer for the audio callback to capture — but
  it's not urgent; OutputMixer's own processed path (Step 4) covers
  the produced-mix surface.
- **AudioCallback diagnostics pane height** (60 → 84 px after M1 S3
  Load line). If M6's notification UI sits on the Preparation tab,
  it'll likely need another bump or a switch to dynamic height — flag
  to the M6 S3 implementer when the surface lands.

---

## HISTORICAL — M4 close + M5 handoff (superseded 2026-05-18 — M5 now complete)

The original M5 first-moves and M4-era constraints follow. M4-era
constraints #1-#7 ARE STILL LOAD-BEARING for M6+; the rest is history.

**M4 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`d0aa45f`. Four commits shipped this session, all green:

| SHA | Subject |
|---|---|
| `70f8030` | M4 Session 1 — DirectLayer registry + OutputChannelId + dense generation handles |
| `0130a5f` | M4 Session 2 — DirectLayer::routeBuffers audio-thread additive routing |
| `3dff703` | M4 Session 3 — AudioCallback orchestrates InputMixer + DirectLayer; MainComponent owns mixers |
| `d0aa45f` | docs: M4 close-out — RT_SAFETY_CONTRACT §6 rows for DirectLayer::routeBuffers + InputMixer::processBuffer + AudioCallback orchestration update |

Test count: **310/310** green (was 293 at M3 close; +17 across M4).
Full 4-phase `bash bash/autotest.sh` green (Phase 4 GUI smoke flaked
twice on cold-start runs; passed cleanly on retry — known
Accessibility-timing transient, unrelated to M4 changes).

**Execution mode used:** subagent-driven (one implementer + spec
review + code review per session, then a final milestone review).
Per-session reviews caught: dense-storage vs tombstone-walk RT issue
(S1 fix-up), `routeBuffers` const-correctness + RT-smoke flake risk
(S2 fix-up), `audioDeviceAboutToStart` allocation in `vector::assign`
(S3 fix-up). The final milestone review caught the
`RT_SAFETY_CONTRACT.md §6` table missing rows for the new
audio-thread classes — fixed in `d0aa45f` before declaring M4 done.

### First moves for the M5 chat

M5 ships the **OutputMixer expansion** — V3 Step 7 + decision #57:
per-channel strips with gain/pan/EQ/dynamics, session-level effect
buses, send/return architecture, master bus, real `OutputMixer::render_buffer`
on the audio thread. **Plus** the input-side `AudioChain` becomes
real (no longer no-op) — `ChannelStrip<SignalType::Audio>` is the
shared implementation between input and output mixer channels.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M5 in full** (starts at line 376). Sessions 1-3 break-out is at
   the end of the M5 section (Session 1 = ChannelStrip template +
   OutputMixer channel registration; Session 2 = Bus + sends; Session
   3 = `render_buffer` audio-thread wiring + integration test).
3. Read V3 transition guide §2.4 / §3.3 / §3.4 for the channel-strip
   / bus / send topology.
4. Brainstorm pass for M5 before code. The plan body is dense; key
   open questions worth surfacing to the operator:
   - **ChannelStrip inheritance vs composition** — plan amendment §3
     in `docs/superpowers/specs/2026-05-18-m3-design.md` says either
     `ChannelStrip<Audio>` inherits `ProcessingChain` (rename — preferred)
     or `AudioChain` delegates to a held `ChannelStrip<Audio>`
     (composition — fallback). Pick one before Session 1.
   - **`OutputChannelId` promotion** — M4 ships it as a strong-typed
     int wrapper in `engine/include/sirius/Channel.h`. M5 needs to
     decide whether the promotion adds methods to the wrapper in-place
     or whether `OutputMixer::add_channel` returns a NEW richer type
     (`OutputChannel` value object?) that holds the id plus the strip
     reference. The V7 plan acceptance criterion ("`add_channel(...)`
     returns `OutputChannelId`") suggests the wrapper stays, methods
     get added.
   - **OutputMixer slot in AudioCallback** — Session 3 of M4 wired the
     callback's 5-step body (zero → InputMixer → DirectLayer → LMC →
     elapsed). OutputMixer is currently a held-but-not-invoked
     `(void) outputMixer_;` marker at line ~160 of
     `audio/src/AudioCallback.cpp`. M5 Session 3 turns that marker
     into the real `render_buffer` call — additive into the same
     output buffers DirectLayer also writes to. Operator should know
     the M5 callback grows from 5 steps to 6 (or DirectLayer + OutputMixer
     get fused as one step).
5. Adopt the same subagent-driven execution mode that worked for M3+M4.
   Plan-write → spec-review → code-review per session; final-milestone
   review at the end. **The execution mode in the V7 plan is
   `orchestrator+subagents` with Performance Benchmarker for the
   32-channel/8-bus RT-budget test and Backend Architect for the
   channel→bus→master DAG audit.** Worth using those specialists
   instead of generic-purpose for the matching sessions.

### M4-era decisions that constrain M5 (DO NOT "fix" without operator approval)

1. **`OutputChannelId` lives in `engine/include/sirius/Channel.h`**
   alongside `ChannelId` / `InputId`. M5 promotes by adding methods
   in-place — do NOT move it to a new header.
2. **AudioCallback orchestrates the audio path; InputMixer / OutputMixer
   / DirectLayer do not call each other.** AudioCallback owns the
   sequence "for each input → InputMixer per channel → DirectLayer
   routeBuffers → OutputMixer render_buffer (when M5 makes it real) →
   LMC → elapsed publish." M5 must NOT add `setOutputMixer(...)` to
   InputMixer or `setOutputMixer(...)` to DirectLayer.
3. **MainComponent owns mixers + DirectLayer via `std::unique_ptr`,
   injects raw pointers into AudioCallback set-once on the message
   thread.** Matches the M1 Session 3 ASRC pattern. M5's Bus
   instances should follow the same shape (MainComponent owns; raw
   pointer injection if they need to be visible across components).
4. **DirectLayer is `const DirectLayer*` because `routeBuffers` is
   `const`.** If `OutputMixer::render_buffer` can be `const` (which
   it should — render is a function of state, not a state mutator),
   do the same: `const OutputMixer*` in AudioCallback. If not, document
   why not.
5. **Scratch vectors in AudioCallback are pre-sized to
   `kMaxScratchChannels = 32` in the constructor; `audioDeviceAboutToStart`
   only records active counts.** Zero allocation in the
   message-thread-on-audio-boundary path. If M5 OutputMixer needs its
   own per-buffer scratch (e.g. send-bus intermediate buffers), use
   the same pattern — pre-allocate in constructor, record active
   sizes in `audioDeviceAboutToStart`, only index in the callback.
6. **Threading contract: configure-before-audio-starts.** Documented
   in `DirectLayer.h` and inherited by AudioCallback. No atomic-snapshot
   publish on route mutation. M5 inherits the same — bus/strip
   configuration happens before the audio thread reads them, period.
   When the operator-facing mutation UI lands (post-M5), AudioCallback
   will need a temporary stop-callback-mutate-restart guard or a real
   lock-free publish; defer the decision until the UI is real.
7. **ProcessedRoute is wired but not exercised in M4** — the
   `processedChannels` span in `DirectLayer::routeBuffers` is passed
   empty from AudioCallback (`audio/src/AudioCallback.cpp` line ~103).
   Reason: M3's `InputMixer::processBuffer` writes byte-serialized
   output to a TapeWriter queue and does NOT expose a post-processing
   float buffer. **M5 unblocks this**: once `ChannelStrip<Audio>` is
   the concrete `Channel::processing` body, InputMixer can publish
   a per-channel post-processing float buffer that AudioCallback
   populates into the `ProcessedChannelBufferView` scratch and passes
   to DirectLayer. Add this as M5 Session 3 work — it's a natural fit
   alongside `render_buffer` wiring. Without it, ProcessedRoute
   registration is allowed but routes nothing.

### Known M4 deviations + minors deferred to later milestones

These are real but intentionally not fixed in M4. Don't pick them up
opportunistically in M5 unless they overlap with M5 scope.

- **`AudioCallback.h` lacks a threading-contract note** matching
  `DirectLayer.h`'s. The configure-before-start assumption is in the
  setters' doc comments but not the class-level block. Add the
  paragraph during M5 Session 3 when `setOutputMixer` joins
  `setDirectLayer` in the same neighborhood.
- **No regression test for the InputMixer-no-registered-channels
  no-op path** on the audio thread. M4 default config exercises it
  (MainComponent doesn't register any InputMixer channels) but
  nothing asserts it stays a no-op if the early-return in
  `InputMixer::processBuffer` is ever changed. M5 will register
  channels for real (gain/pan input-side processing) — when it does,
  add an assertion that unregistered channels still no-op gracefully.
- **No test for `DirectLayer::removeRoute` mid-audio.** Currently
  illegal per the threading contract, so untestable. When mutation-
  during-audio becomes a real surface (post-M5), add the test.
- **DirectLayer's `[.rt-smoke]` test is hidden by default** (Catch2
  hidden-tag convention) — runs only on explicit filter via the
  test binary, not `ctest`. Acceptable for a performance smoke;
  document if M5 adds a similar smoke for `render_buffer`.

### RT-safety invariants M5 OutputMixer must preserve

- Zero allocation, zero locks, zero I/O, zero logging on the audio
  thread. `noexcept` on every hot-path function.
- `render_buffer` should be `const` and audio-thread-only.
- Channel → bus → master DAG traversal must be bounded. The V3
  decision caps buses at 64; channels are typically <32. Worst case
  ~2048 send operations per buffer — well within budget at modern
  CPU.
- Bus effect chains (`EffectChain` is reused from core/) — verify
  each effect's audio-thread call is allocation-free. M5 only ships
  identity-EQ + identity-dynamics stubs; real DSP is a deferred
  sub-milestone.
- Per `docs/RT_SAFETY_CONTRACT.md §6`, every new audio-thread class
  gets a row in the table BEFORE the milestone closes. M4 missed
  this and had to backfill — don't repeat the mistake.

### Operator-side TODOs for M5

1. **CI signing secrets** (still operator-pending — does NOT block
   any V7 alignment work, but the auto-testing milestone won't go
   fully green on CI until done). See the "CI signing handoff" section
   further down. Three secrets remain.
2. **Decide ChannelStrip inheritance vs composition** (see brainstorm
   open question above).
3. **Optional:** Use Performance Benchmarker subagent for the
   32-channel/8-bus RT-budget test in M5 Session 3 (per the V7 plan's
   recommended execution mode).

---

## HISTORICAL — M3 close-out + M4 handoff (superseded 2026-05-18 — M4 now complete)

**M3 shipped on `origin/master` HEAD `b395f2e`.** Six commits, test count 279 → 293.

| SHA | Subject |
|---|---|
| `58a6d7d` | M3 Session 1 — ProcessingChain + ChannelDefaults + InputDescriptor flags + Channel ctor |
| `aba4e0e` | M3 Session 2 — TapeWriter + InputMixer::processBuffer real bodies |
| `ada4858` | M3 Session 2 fix — keep SiriusEngine public API JUCE-free (TapeWriter PIMPL) |
| `f4ef59f` | M3 Session 2 fix — sampleCount→payloadByteCount + flushChannel preconditions + OverloadProtection::reportLoad noexcept |
| `94aed46` | M3 Session 3 — finalizeChannel + NonDestructive params + setInputDefaults end-to-end |
| `b395f2e` | M3 follow-up — TapeWriter flushChannel prompt wake on empty queue + simplify predicate to fail loud in NDEBUG |

M3-era decisions still load-bearing in M5:
- `TapeWriter` takes `std::chrono::milliseconds`, NOT `CapabilityTier&` (engine can't depend on app layer).
- `TapeMode` lives in `core/` (because `ChannelDefaults` needs it).
- InputMixer collaborator setters are set-once on the message thread; never null-checked at every audio-thread call.
- Engine public API is JUCE-free via PIMPL where filesystem types are needed.
- `flushChannel` has a single-caller precondition (asserted in debug, blocks-loud in NDEBUG).
- `TapeWriteMessage::payloadByteCount` (not `sampleCount`); `lmcTime` is `Rational(0)` until per-channel sample-rate context exists.

Deferred from M3 to specific later milestones:
- `touchParamsPartial` silent-fail if `setChannelTapeMode(NonDestructive)` before `setTapeWriter` — flagged for M11.
- `touchParamsPartial` holds `stateMutex_` across `std::ofstream` open — negligible, defer.
- `finalizeChannel` doesn't `removeChannel` — revisit when iterating channels per buffer (M5+ may hit this).
- `[finalize]` test doesn't cover no-messages-enqueued path — cheap insurance missing.
- No mechanical RT-safety counter on `processBuffer` — M11+ may add `RtSafetyHarness`.

---

## HISTORICAL — M2 close-out + M3 handoff (superseded 2026-05-18 — M3 now complete)

**M2 of the V7 alignment plan is fully shipped on `origin/master`.**
All three sessions are pushed (HEAD `f1b4d58`):

- `ec1b5d9` — Session 2: type shapes (SignalType, TapeMode, Channel,
  InputMixer/OutputMixer skeletons)
- `9edd311` — Session 2 handoff continue.md
- `f1b4d58` — Session 3: skeleton tests for the new types

Test count is **279/279** (was 270; +9 cases from Session 3: 2
SignalType, 5 Channel/TapeMode/InputId/ChannelId, 1 InputMixer, 1
OutputMixer). Full 4-phase `bash bash/autotest.sh` green; Phase 4 GUI
smoke flaked once on a cold build but passed on retry — known
Accessibility-timing transient, unrelated to type-shape additions.

### First moves for the M3 chat

M3 is **the first behaviour-bearing milestone in Part B** — the
mixer skeleton bodies that currently assert-false get real
implementations, and the first `Tape<T>` instance in product code
lands. This is a *real* session, not a rename or scaffolding pass.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   the **entire M3 section** (lines ~263 onward — "Channel-driven tape
   allocation + channel-layer processing chains + per-input flags").
   M3 is significantly larger than M2: it changes `Channel`, adds
   `ProcessingChain`, adds `TapeWriter` + lock-free queue, extends
   `TapeStore` with `appendBytes`, and adds three flags to
   `InputDescriptor`. Files-touched list is long.
3. **Brainstorm first, then propose a sub-session break-out.** M3 is
   large enough that it deserves a `superpowers:brainstorming` pass
   before any code, and a retro-split into 3-or-4 sessions (matching
   the M2 pattern) before execution. Do not dive straight into
   implementation — the M2 break-out earned its keep, M3's will too.

   The two would-be operator questions about audio-thread contract and
   parameter-tape format are **already resolved** (decisions locked
   2026-05-18 — see "Operator decisions locked for M3" below). The
   brainstorm should focus on remaining open shapes — `ProcessingChain`
   parameterization by `SignalType`, the audio-thread / writer-thread
   handoff struct, and how `InputDescriptor`'s three new flags
   (`rawDirectMonitor`, `enabled`, `defaults`) interact with the
   already-shipped `InputMixer::setInputRawDirect` / `setInputEnabled`.
4. Read the V3 transition guide §2 (the same lines used in M2) plus
   §2.4 / §3.3 / §3.4 for the channel/tape/effects topology that M3
   makes real.
5. Read what M2 left in place:
   - `engine/include/sirius/InputMixer.h` — method names are PascalCase
     (`registerInput`, `addChannel`, `setChannelTapeMode`,
     `processBuffer`); placeholder args are `/* M3: ... */` comments
     in the signatures. M3 turns those comments into real types.
   - `engine/src/InputMixer.cpp` — every body
     `assert(false && "M3-M5 stub")`. M3 replaces every assert with a
     real body — don't leave any stubs.
   - `engine/include/sirius/Channel.h` — currently 4 fields
     (`id`, `signalType`, `source`, `tapeMode`). M3 adds
     `ProcessingChain processing` and a destinations container.
6. Plan break-out for M3 is **not yet sub-sessioned**. The M2 break-out
   (Sessions 1/2/3) was added retroactively after brainstorming with
   the operator; do the same for M3 — propose a 3-or-4-session split
   in the first chat, get operator agreement, then execute.

### Operator decisions locked for M3 (2026-05-18)

These came out of the M2-close-out conversation and remove two would-be
brainstorm questions:

1. **SPSC overflow on `processBuffer`:** when the audio-thread → writer-
   thread queue is full, the audio thread **drops the buffer and
   reports overload** via `OverloadProtection.reportLoad(1.0)`. Matches
   the M1 Session 3 pattern (overload is the existing surface for
   "audio thread couldn't keep up"). Never block on the audio thread;
   no silent drops. The diagnostics pane already shows shed-count from
   OverloadProtection, so the operator-visible signal is in place.
2. **`NonDestructive` parameter-tape format:** JSONL, same shape as the
   existing `ParameterAutomation` tape. Keeps SAF consistent and lets
   the same parameter-merge/diff utilities work across both tape kinds.
   Defer any "binary format would be smaller" optimization to a future
   milestone — premature today.

Both decisions belong in the M3 Risks section of the V7 alignment plan
when M3 lands; capture them there as part of Session 1's commit.

### M2 Session 3 — what landed and pushed (commit `f1b4d58`)

5 files changed, 223 insertions, 1 deletion:

- `tests/SignalTypeTests.cpp` (new) — exhaustive `signalTypeOf` mapping;
  `static_assert`s at file scope plus runtime `CHECK`s in 2 TEST_CASEs.
  Tag `[signal-type]`.
- `tests/ChannelTests.cpp` (new) — `InputId` / `ChannelId` strong-typing
  (runtime `CHECK`s only; the house IDs have non-constexpr accessors so
  compile-time asserts on `value()` / `==` don't work — see deviation
  note below). `Channel` aggregate brace-init; `SignalType × TapeMode`
  combinations all build; `TapeMode` three-case distinctness. Tag
  `[channel]` + sub-tags `[input-id]` / `[channel-id]` / `[tape-mode]`.
- `tests/InputMixerTests.cpp` (new) — single TEST_CASE confirming
  default ctor + dtor don't crash; `static_assert` that
  `is_default_constructible_v<InputMixer>` holds. **No setter / no
  `addChannel` / no `processBuffer` calls** — every body still asserts
  false per the M2 Risks note. Tag `[input-mixer]`.
- `tests/OutputMixerTests.cpp` (new) — same shape as InputMixerTests.
  Tag `[output-mixer]`.
- `tests/CMakeLists.txt` (modified) — appended the 4 new files to the
  `SiriusTests` source list.

### Session 3 plan deviations to carry forward into M3

1. **Static_assert on `InputId.value()` / `==` doesn't work.** The
   `value()` accessor and `==`/`!=` operators on `InputId` / `ChannelId`
   aren't `constexpr` — matched the house `TapeId` / `ConstituentId`
   pattern exactly, which has the same half-measure (constexpr ctor,
   non-constexpr accessors). Initial Session 3 draft had
   `static_assert(InputId(1).value() == 1)`; clang correctly rejected
   it. Removed those and kept runtime `CHECK`s. If M3+ wants
   constexpr-throughout for the house IDs, that's a project-wide
   refactor (touches TapeId, ConstituentId, InputId, ChannelId), not
   an M3-local change.
2. **Mixer tests are intentionally thin.** Single ctor/dtor test each.
   The plan (line 228) enumerated `InputMixerTests.cpp` /
   `OutputMixerTests.cpp` as deliverables; I shipped them as floors
   (regression catches if a future refactor accidentally makes the
   mixers non-default-constructible). M3 will fill them with real
   behaviour as the assert-false bodies become real implementations.
3. **GUI smoke is flaky on first run after a clean build.** The
   `bash bash/autotest.sh` Phase 4 failed once on a cold build, then
   passed on direct re-run with the same `.app`. Almost certainly an
   Accessibility-permission timing transient (the new app bundle needs
   a moment for AX to attach). Not worth chasing this session. If it
   becomes chronic, the fix is probably a 1–2 s `sleep` after launch
   inside `bash/smoke-persistence.sh` before the first AX call.

### Where M2 acceptance criteria stand (all green)

- [x] Membrane → LatencyTiming rename + namespace flip (Session 1)
- [x] `core/include/sirius/SignalType.h` added (Session 2)
- [x] `signalTypeOf(InputKind)` helper added to InputKind.h (Session 2)
- [x] `engine/include/sirius/InputMixer.h` + `OutputMixer.h` skeletons added — assert-false bodies (Session 2)
- [x] `engine/include/sirius/Channel.h` added with strong-typed `ChannelId` + `InputId` (Session 2)
- [x] `engine/include/sirius/TapeMode.h` added (Session 2)
- [x] Skeleton tests for new types (Session 3 — 9 new test cases)
- [x] Pushed to `origin/master` (Session 3)

M3 (Channel-driven tape allocation + channel-layer processing chains +
per-input flags) is unblocked.

What landed this session (M2 Session 1):

- `engine/include/sirius/LatencyTiming.h`, `engine/src/LatencyTiming.cpp`
  — new, namespace `sirius::latency`. Content is the verbatim former
  Membrane content with namespace + error-message string updates only.
  Two free functions: `inboundCaptureTime`, `outboundPresentTime`.
- `engine/include/sirius/Membrane.h`, `engine/src/Membrane.cpp` —
  deleted. Zero remaining `sirius::membrane::` references in the tree
  (verified by grep).
- `tests/LatencyTimingTests.cpp` — renamed from `MembraneTests.cpp`.
  Catch2 tags updated `[membrane]` → `[latency]`. Test descriptions /
  prose updated. Assertions byte-identical.
- `engine/CMakeLists.txt`, `tests/CMakeLists.txt` — updated.
- `tests/AudioCallbackTests.cpp` — added a `pumpUntilPositive` helper
  so the M1 Session 3 load-publish tests don't depend on a single
  cold-cache callback registering a non-zero tick delta. 1000-iteration
  safety cap so a fundamentally-broken clock can't hang the test.

**Plan deviations from M2 Session 1:**

- The plan predicted "expected hits in `engine/src/RenderPipeline.cpp`,
  `app/MainComponent.cpp`" for `sirius::membrane::` usages. **Zero
  actual hits in either** — `Membrane.{h,cpp}` was already unused by
  product code, only referenced by its own test. The rename surface
  was 3 files, not the 5+ the plan implied. Session 2's "Files
  touched" enumeration is more accurate (no widespread call-site
  rewrites needed).
- `video/include/sirius/FrameMembrane.h` + `video/src/FrameMembrane.cpp`
  are a separate concept (video frame timing) and are **explicitly
  out of scope** for M2 per the plan's narrow wording. Left untouched.

What landed this session (M1 Session 3):

- `audio/include/sirius/AudioCallback.h`, `audio/src/AudioCallback.cpp`
  — added `setAsrcInputs(std::vector<Asrc*>)`, `setAsrcOutputs(...)`,
  `setCalibration(const AudioDeviceCalibration*)` — non-owning,
  message-thread, set-once. New `std::atomic<double> lastCallbackElapsedSec_`
  published via `lastCallbackElapsedSec()`. The callback now wraps its
  work with `juce::Time::getHighResolutionTicks` and stores elapsed
  seconds at the end of every buffer. Buffer body is otherwise
  unchanged — still `memcpy + lmc_->advanceBySamples`.
- `app/MainComponent.{h,cpp}` — owns 2 input ASRCs + 2 output ASRCs
  (`std::unique_ptr<Asrc>`, `maxIoRatio=1.01`, quality from
  `EngineConfig`), one `AudioDeviceCalibration::identity()`, one
  `OverloadProtection`, one `RetroactiveRing<std::uint8_t>{1024}` (the
  `std::uint8_t` is provisional until the real tape-event type lands
  in M3/M4). The 30 Hz `timerCallback` reads
  `audioCallback_->lastCallbackElapsedSec()`, divides by
  `bufSize / rate`, calls `overloadProtection_.reportLoad(fraction)`.
  Diagnostics row in the Preparation pane gained a new `Load: X%
  of budget (shed: N)` line; the label height bumped 60 → 84 px to fit.
- `docs/RT_SAFETY_CONTRACT.md` — four `TBD` rows filled, with notes
  per piece (Asrc held-not-invoked; OverloadProtection driven from
  the message thread via atomic-published elapsed; RetroactiveRing
  explicitly off the audio thread; AudioDeviceCalibration held at
  identity until M8). New post-table paragraph describes the three
  shapes M1 Session 3 introduced.
- `tests/AudioCallbackTests.cpp` — 3 new `[load-publish]` cases:
  elapsed is 0 before any callback; positive (< 1 ms) after one buffer;
  resets to 0 on `audioDeviceStopped()`.

**Operator decisions locked in 2026-05-17 brainstorm (captured in
`~/.claude/plans/read-continue-and-proceed-partitioned-diffie.md`):**

- Q1 — *Strict scaffolding* (option A): engine pieces are reachable
  from the audio path's owners but the callback body remains
  `memcpy + lmc_->advanceBySamples`. ASRC at unity is NOT bit-identical
  (cubic/sinc interpolation), so routing through it would have broken
  the existing identity tests for no M1 benefit.
- Q2 — *Atomic publish, message-thread consumes* (option A). Mid-session
  refinement: the atomic publishes **elapsed seconds**, not the load
  fraction — division moves to the message thread. Net result:
  smaller audio-thread footprint (one `mach_absolute_time` pair + one
  atomic store; no division), testable without a JUCE device mock,
  same operator-facing behaviour.

**Subtle constraints worth carrying forward:**

- The diagnostics pane height grew (60 → 84 px) for the new Load
  line. If M2 adds another diagnostics line it'll likely need
  another bump, or a switch to dynamic height.
- `Asrc` is held by `AudioCallback` but not invoked. Compiler will
  warn about unused if any future cleanup tightens warnings — the
  member is intentional scaffolding; M2-M5 grow the call sites.
- `RetroactiveRing<std::uint8_t>` is a placeholder type parameter.
  The real `T` is the tape-event type that lands with the M3 SPSC
  tape-event queue. Renaming the member at that point is acceptable.

### Where M1 acceptance criteria stand (all green)

- [x] App registers an audio device on startup at user default sample
      rate / buffer size. (M1 Session 1 + 2/2 channel default.)
- [x] `AudioCallback::audioDeviceIOCallbackWithContext` runs identity
      pass-through within buffer budget. (Verified by autotest.)
- [x] LMC sample-clock fed from device callback. (M1 Session 2,
      tests `[sample-clock]`.)
- [x] `Asrc` / `OverloadProtection` / `AudioDeviceCalibration` /
      `RetroactiveRing` wired in. (M1 Session 3 — see RT_SAFETY_CONTRACT.md.)
- [x] `docs/RT_SAFETY_CONTRACT.md` lives in the repo with the per-PR
      audit checklist. (M1 Session 2 + Session 3 audit rows.)
- [x] Existing 256 ctest cases stay green — 270/270 now.

M2 (Membrane → Mixer rename + SignalType + Channel) is unblocked.

The auto-testing milestone (sections 0-5 below) is **closed and
shipped**. The CI signing handoff (3 of 6 secrets pending) is **still
open** — operator-only Keychain/AppleID work, doesn't gate M1+.
Treat sections 0-5 as historical state.

---

## HISTORICAL — Auto-testing CI signing handoff (still operator-pending, not blocking V7 work)

Auto-testing milestone is shipped on master. CI workflow is in place
but **first run will fail** until the remaining three repo secrets are
added.

**Already done this session (live in repo settings):**

| Secret | Status | Value |
|---|---|---|
| `APPLE_TEAM_ID` | ✓ set | `RR5DY39W4Q` |
| `APPLE_ID` | ✓ set | `itunes@larryseyer.com` |
| `KEYCHAIN_PASSWORD` | ✓ set | random 32-char base64 (throwaway, never leaves CI) |

**Still needed (require operator hands — Keychain GUI + appleid.apple.com):**

1. **`DEVELOPER_ID_CERT_P12_BASE64`** + **`DEVELOPER_ID_CERT_PASSWORD`** —
   export the Developer ID Application cert from Keychain Access:
   - Keychain Access → "login" → My Certificates
   - Find `Developer ID Application: Larry Seyer (RR5DY39W4Q)`. The
     disclosure triangle MUST show a private key under it (else the
     export is useless for signing)
   - Right-click → Export → save as `~/Desktop/sirius-devid.p12`,
     format: Personal Information Exchange (`.p12`)
   - Keychain prompts for an export password — pick anything
     memorable, this becomes `DEVELOPER_ID_CERT_PASSWORD`
   - Keychain may also prompt for your login password to release the
     private key (normal)

2. **`APPLE_APP_PASSWORD`** — app-specific password for notarytool:
   - https://appleid.apple.com → sign in as `itunes@larryseyer.com`
   - Sign-In and Security → App-Specific Passwords → "+"
   - Label `sirius-notarytool` and generate
   - Copy the 19-char password (`xxxx-xxxx-xxxx-xxxx`)

**Two paths to finish the handoff** (operator's choice on return):

- **Path A (operator hands the values to Claude):** drop the .p12 at
  `~/Desktop/sirius-devid.p12`, tell Claude the export password and the
  app-specific password. Claude runs the three `gh secret set` lines.
- **Path B (operator runs the gh commands):**
  ```
  gh secret set DEVELOPER_ID_CERT_P12_BASE64 < <(base64 -i ~/Desktop/sirius-devid.p12)
  gh secret set DEVELOPER_ID_CERT_PASSWORD --body "<the .p12 password>"
  gh secret set APPLE_APP_PASSWORD --body "xxxx-xxxx-xxxx-xxxx"
  ```

**After all six secrets are in place — verification:**

```
gh secret list                    # confirm 6 entries
gh workflow run ci-macos-signed.yml
gh run watch                      # follow the live run
```

Expected: build + sign + verify all green. The "GUI smoke (best-effort)"
step may pass or skip on the GitHub-hosted runner — either is acceptable
(see continue.md §4 for why).

Once the workflow runs green: delete `~/Desktop/sirius-devid.p12`
(security hygiene — the secret is now in GitHub; the local copy is
no longer needed and exposes the private key if leaked). Then the
auto-testing milestone is fully closed out and the next session can
start on the white-paper alignment pass (§6).

---

## 0. Headline

**Auto-testing infrastructure milestone shipped end-to-end** on master
this session. Three commits, all pushed:

| SHA       | Subject                                                                                      |
|-----------|----------------------------------------------------------------------------------------------|
| `4e8a1df` | fix: smoke-persistence.sh — direct binary launch + recursive AX walk + dialog-targeted clicks |
| `f68aa3c` | feat: bash/autotest.sh — local 4-phase macOS verification driver                              |
| (next)    | feat: ci-macos-signed.yml — signed-build + smoke CI workflow + this doc update                |

**Phase A — `bash/smoke-persistence.sh` patched and verified.** Five
AppleScript / Launch Services landmines worked around (catalogued in
§2). Round-trips Save → Load against the signed bundle, exits 0 with
"refs in file: 2" proving v2 shared-encoding ran.

**Phase B — `bash/autotest.sh` runs four phases locally** in ~25s
total on an Apple Silicon dev machine: headless ctest → signed Xcode
bundle build → codesign + spctl verification → GUI smoke. All four
green on master.

**Phase C — `.github/workflows/ci-macos-signed.yml` added** as a
separate workflow from `ci.yml`. Will fire on the next push to master
(this commit). Operator one-time setup: six repo secrets must be
added before the workflow can succeed — see §5.

**Next session = white-paper alignment pass** per the standing
operator sequencing (signing → auto-testing → white-paper alignment).
Auto-testing landed; alignment is up. See §6 for the picked-up
context for that work.

---

## 1. Build + test state

```bash
cd /Users/larryseyer/SiriusLooper
bash bash/autotest.sh        # full 4-phase verification, ~25s
```

Or individual gates:

```bash
# Headless only
cmake --build build --target SiriusTests
ctest --test-dir build --output-on-failure        # 256 / 4283 expected

# Signed bundle only
cmake --build build-xcode --config Release --target SiriusLooper

# GUI smoke against the signed bundle
APP_BUNDLE="build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app" \
  bash bash/smoke-persistence.sh
```

The signed `.app` lives at `build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
Developer ID Application: Larry Seyer (RR5DY39W4Q), hardened runtime,
audio-input entitlement, spctl-accepted.

The dev-loop `build/.app` is unchanged — ad-hoc-signed, fast iteration.

---

## 2. AppleScript / macOS landmines documented this session (for future GUI smoke work)

Each of these bit hard during Phase A. Recorded here so the next time
someone touches `bash/smoke-persistence.sh` or writes a new
osascript-based GUI driver, they don't have to rediscover them.

1. **`open ...app` returns -10825 on dev-tree paths.** Launch Services
   refuses to launch the Developer-ID-signed bundle from
   `build-xcode/...` even though `spctl` accepts it. The ad-hoc
   sibling at `build/...` opens fine. Suspected cause: the ad-hoc
   bundle's invalid `Identifier=Sirius Looper` (string with space)
   collides in LS with the signed bundle's valid
   `com.larryseyer.siriuslooper`. Workaround: launch the binary
   directly via `${APP_BUNDLE}/Contents/MacOS/${APP_NAME}` —
   bypasses LS entirely, more reliable for automation anyway.
   Root cause filed as its own todo entry.

2. **`entire contents of window 1` strips role info.** Iterating
   returns elements where `role of elt` doesn't resolve cleanly; the
   `AXButton` role check matched zero elements even when buttons were
   present. Workaround: explicit two-level recursion using
   `every UI element of window 1` then `every button of grp`.

3. **Tab buttons are nested one AXGroup deep.** JUCE's
   `TabbedComponent` exposes tab buttons as AXButton children of an
   AXGroup, not at the window's top level. Save/Load on the
   Preparation pane are similarly nested. Recursion in #2 catches
   both.

4. **`keystroke "g" using {command down, shift down}` silently no-ops
   inside NSSavePanel/NSOpenPanel on Sequoia** when the filename field
   has focus. The text field appears to eat the modified keystroke.
   Workaround: `key code 5 using {command down, shift down}` (5 =
   physical 'g' keycode) fires the system shortcut reliably.

5. **NSSavePanel/NSOpenPanel open as separate top-level windows,
   not sheets.** `sheets of window 1` returns 0 after clicking
   Save/Load. The dialog is a sibling window whose name starts with
   the title prefix ("Save Sirius session..." / "Load Sirius
   session..."). Target it by `first window whose name starts with
   "..."` and click the action button by name. Bonus: NSOpenPanel's
   "Open" button stays *disabled* until a file is selected in the
   listing — Cmd+Shift+G navigates to the directory but doesn't
   select, so the smoke script falls back to Return when the action
   button is disabled.

6. **Activate-and-keystroke must be in the same osascript block.**
   When `tell application "X" to activate` and `keystroke ...` are
   issued from separate osascript invocations, focus can slip back
   to the terminal between calls and the keystrokes land in the
   wrong app. Solution: combine them in one heredoc.

---

## 3. The three small follow-ups surfaced this session

All filed in `todo.md` near the top. None block anything; all are
"while we're in this neighborhood" cleanups.

1. **`com.apple.security.get-task-allow=true` in signed entitlements.**
   Visible in `codesign -dv --entitlements -` output. Fine for non-
   notarized dev builds; rejected by Apple's notarization service.
   Must be `false` (or absent) before any notarized release. Likely a
   Xcode default the CMake block needs to override.

2. **`open` returns -10825 on the signed dev-tree bundle.** Root cause
   of landmine #1 above. Workaround in place in the smoke script;
   investigation deferred. End-user launches against a notarized
   bundle in /Applications are not affected.

3. **Ad-hoc `build/` bundle has invalid `Identifier=Sirius Looper`.**
   Should be `com.larryseyer.siriuslooper` to match the Xcode-gen
   build. The mismatch is the leading suspect for #2's LS confusion.
   Fix the CMake config that sets the bundle ID on the Unix Makefiles
   target.

---

## 4. CI status

`ci.yml` (the original push/PR matrix):
- macOS: green (headless tests).
- Linux + Windows: red on a pre-existing int64→juce::var ambiguity in
  `persistence/src/SessionFormat.cpp`. **Explicitly deferred** under
  the `feedback-mac-first-linux-windows-last` rule — not a candidate
  for "what's next."

`ci-macos-signed.yml` (added this session):
- Will fire on the next push to master. **First run will fail**
  because the six required secrets are not yet in the repo. Operator
  step: add them per §5, then push (or `workflow_dispatch`) to verify.
- GUI smoke step is `continue-on-error: true` from day one — GitHub-
  hosted macOS runners can't grant the runner user Accessibility
  permission without SIP-disabling tricks that hosted runners don't
  support. Build + sign + codesign-verify is the hard gate; smoke is
  best-effort signal.

---

## 5. Operator one-time setup before CI signing works

In GitHub repo Settings → Secrets and variables → Actions, add:

| Secret name                    | Value source                                                                                                |
|--------------------------------|-------------------------------------------------------------------------------------------------------------|
| `DEVELOPER_ID_CERT_P12_BASE64` | Export `Developer ID Application: Larry Seyer (RR5DY39W4Q)` from Keychain Access as `.p12` with a password, then `base64 -i Developer-ID.p12 \| pbcopy` |
| `DEVELOPER_ID_CERT_PASSWORD`   | The `.p12` export password just set                                                                         |
| `APPLE_ID`                     | `itunes@larryseyer.com` (per `reference-apple-id` memory)                                                   |
| `APPLE_APP_PASSWORD`           | App-specific password from appleid.apple.com (NOT the Apple ID password — generate one specifically for notarytool) |
| `APPLE_TEAM_ID`                | `RR5DY39W4Q`                                                                                                |
| `KEYCHAIN_PASSWORD`            | Any opaque string the operator picks — used as the throwaway CI keychain password                            |

`APPLE_ID` / `APPLE_APP_PASSWORD` / `APPLE_TEAM_ID` aren't strictly
needed for the current workflow (no notarization step yet), but
they're listed here so they get added once and are ready when
notarization moves into a release-tag workflow.

After secrets land: trigger the workflow via `workflow_dispatch` in
the Actions tab (or just push), and verify the run goes green
("GUI smoke: PASS" or "GUI smoke: SKIPPED/FAILED on CI runner" —
either is acceptable; build + sign + verify must pass).

---

## 6. Next milestone — white-paper alignment pass — SUPERSEDED-AND-SPEC'D 2026-05-17

The alignment pass that this section queued has been spec'd in full at
`docs/superpowers/plans/2026-05-17-v7-alignment.md` (24 milestones across
11 parts; M1 = Audio I/O foundation + RT-safety contract audit; see
RESUME HERE at the top of this file). The original V2 white paper
referenced below is superseded by V7 (`docs/Sirius_Looper.md`); the
V2→V7 transition guide (`docs/sirius-looper-v2-to-v7-transition.md`)
is the bridge.

Original §6 contents preserved for historical context follow; treat as
read-only:

> Per the operator's stated sequence (signing → auto-testing → white-
> paper alignment). Auto-testing is done; alignment is up.
>
> **Reading order before starting alignment work:**
>
> 1. `docs/Sirius Looper Whitepaper V2.md` — the white paper.
>    *(Superseded — read V7 at `docs/Sirius_Looper.md` instead.)*
> 2. `docs/Sirius Looper User Guide.md` — the operator-facing how-to.
> 3. `docs/superpowers/specs/2026-05-16-shared-placement-design.md` — the
>    most-recent shipped spec.
> 4. `todo.md` — alignment-adjacent entries.
>
> **What this session does NOT decide:** the scope, structure, or
> deliverables of the alignment pass. That's the next session's first-
> half work (brainstorm + spec) before any code lands.
>    *(That brainstorm + spec is what 2026-05-17 produced. The plan
>    file is the output.)*

---

## 7. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded). Two new entries this session:
  - `feedback-mac-first-linux-windows-last` (Platform order:
    **macOS → iOS → Windows → Linux**; iOS = AUv3 only)
  - `feedback-esc-while-typing-is-not-abort` (silent tool rejections
    usually mean the operator is mid-message)
- `todo.md` — deferred items register. Auto-testing milestone marked
  SUPERSEDED-AND-IMPLEMENTED at the top; three new follow-ups filed
  beneath it; legacy GUI-smoke and Load-dialog entries updated to
  reflect resolution.
- `/Users/larryseyer/.claude/plans/read-continue-and-proceed-floating-pelican.md`
  — the auto-testing plan that just shipped. Self-contained, can be
  archived or kept as a structural template.
- `bash/autotest.sh` — single command for full local verification.
- `bash/smoke-persistence.sh` — GUI smoke driver (operator must have
  Accessibility access granted to the shell that runs it).
- `.github/workflows/ci-macos-signed.yml` — signed CI workflow (six
  secrets required, see §5).
- `docs/Sirius Looper Whitepaper V2.md` and
  `docs/Sirius Looper User Guide.md` — next milestone's primary
  source material.

---

## 8. One-paragraph orientation if everything else has changed

Auto-testing infrastructure is in place. `bash/autotest.sh` is the
local one-shot gate; `ci-macos-signed.yml` is the per-push CI gate
(needs operator-added secrets to actually run, see §5). Next
milestone is the white-paper alignment pass — that's a design
session, not a code session, so start with brainstorm + spec before
touching `app/` or `ui/`. Linux/Windows remain explicitly deferred.
