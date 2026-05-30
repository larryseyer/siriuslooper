# RT-Safety Contract

This is the rule every audio-thread code path in IDA lives
under. It is the codification of the V7 white paper §5.6 commitments
for our codebase, intended to outlast individual milestones — every
PR that touches the audio thread self-certifies against the checklist
at the bottom of this file before it lands.

The architectural claim is the white paper's:

> The architecture makes claims about latency budgets that are
> achievable only if the engine never blocks the audio thread.

These commitments make those claims real. Violating any of them
violates the architecture, regardless of how clever the workaround.

---

## The six commitments (white paper §5.6)

### 1. The audio thread runs unbounded-latency-free

No operation on the audio thread may have unbounded execution time.
Memory allocation, lock acquisition, file I/O, synchronous device I/O,
blocking on plug-in code, and any operation whose worst-case runtime
cannot be bounded statically are forbidden. Parameter changes arrive
via lock-free message queues.

In practice this rules out: `new` / `delete`, `std::make_*`,
`std::mutex`, `std::condition_variable`, `std::lock_guard`, any
`std::vector` mutation (push_back/insert/resize), `std::string` that
might allocate, `printf`/`std::cout`/`std::ostream`, any blocking
syscall, any call that *might* take a lock inside an opaque framework
(suspect everything in `juce::File`, `juce::String` mutation,
`juce::MessageManager`).

### 2. Constituent graph mutation is non-blocking from the audio thread's perspective

Mutations happen on a non-realtime thread; the audio thread reads the
graph through an atomically-swapped snapshot pointer. The audio thread
never sees a partially-mutated graph and never blocks on a mutation
in progress.

Pattern: `std::atomic<std::shared_ptr<const Snapshot>>` (C++20) on the
publishing side; `std::shared_ptr<const Snapshot> local = snap.load()`
once per buffer on the audio side, then read through `*local` for the
rest of the buffer.

### 3. Tape append is wait-free

Tape events are written through a single-producer single-consumer
queue from the audio thread to a non-realtime writer thread. The audio
thread's contribution to a tape write is a fixed, bounded operation —
typically a couple of `std::atomic` stores and a memcpy into a
pre-allocated slot.

### 4. ASRC operates on a bounded ring

The ASRC's working memory is fixed at session startup based on the
capability tier (M11). The audio thread never allocates inside the
ASRC. Resampling state — coefficient tables, history buffers, polyphase
banks — is owned by the ASRC instance and sized at construction.

### 5. Plug-ins run in dedicated host processes, separate from the engine

Each plug-in instance is loaded into its own out-of-process host (the
*plug-in host process*), communicating with the engine via lock-free
shared-memory ring buffers. The audio thread reads input from a
single-producer single-consumer ring filled by the engine and writes
output to a single-producer single-consumer ring read by the engine;
both rings carry LMC timestamps so the engine knows precisely when
each block of plug-in output was produced. A watchdog bounds the
plug-in's execution time per buffer. **No plug-in failure mode can
produce an audio-thread glitch in the engine, because the engine and
the plug-in never share a process boundary that a crash could cross.**

**Measured IPC latency (M7 S2c, 2026-05-18, Apple Silicon dev machine):**
identity-mode round-trip over the shared-memory SPSC rings — median
68 µs, p99 134 µs across 1000 samples. The hidden Catch2 case
`[plugin-ipc][.rt-smoke]` asserts p99 < 300 µs (≈2× headroom over
the observed baseline). The V7 plan's <10 µs aspiration assumes a
spin-wait transport; the S2c baseline uses a 50 µs poll backoff to
keep idle CPU usage low. The S4+ watchdog work will revisit the
trade-off when sub-buffer-deadline behaviour becomes load-bearing.
This row is **not** on the audio-thread call chain in M7 — the
engine-side `OutOfProcessPluginInstance::sendBytes`/`readBytes` are
message-thread only. **S3 promoted the producer side onto the audio
thread via `OutOfProcessEffectChainHost::pumpSlot`; the row in §6
below is now the load-bearing audit.** The S2c `sendBytes`/`readBytes`
path remains message-thread-only and continues to back the existing
identity-mode round-trip tests + the `[plugin-ipc][.rt-smoke]` latency
case.

### 6. The direct layer is audio-thread-exclusive and stateless

Direct routing is a pure function of input buffers. No allocation, no
locks, no graph traversal. The direct layer's responsibility is
sub-millisecond live monitoring; it cannot afford even a snapshot
pointer load on the hot path beyond reading a couple of gain atomics.

### Aux: the graph is shallow at audio-thread depth

The Constituent hierarchy can be deep (set → song → section → phrase
→ loop → cycle), but the audio thread sees a flattened, pre-computed
schedule of active tape reads for the current buffer. Tree traversal
happens on the non-realtime thread.

---

## Subsystem audit

One row per class on the audio thread's call chain. Every PR that
introduces a new such class adds a row before merging. Every PR that
changes an existing row's hot path re-verifies it.

| Class / function | Allocation-free? | Lock-free? | Bounded? | Notes |
|---|---|---|---|---|
| `ida::AudioCallback::audioDeviceIOCallbackWithContext` | yes | yes | yes (O(numSamples * numChannels)) | M5 Session 3 promoted the body from a five-step to a six-step orchestrator: (1) zero outputs, (2) `dispatchInputMixer` per channel, (3) `dispatchDirectLayer` gated by `monitoringEnabled_`, (4) `dispatchOutputMixer` (produced-mix path, NOT gated — distinct from DirectLayer's monitoring-gated bypass), (5) LMC advance, (6) `juce::Time::getHighResolutionTicks` pair + `std::atomic<double>` elapsed publish. Scratch vectors for the DirectLayer span are pre-sized in the constructor to `kMaxScratchChannels = 32` and only indexed (never resized) in the callback. `audioDeviceAboutToStart` records `activeRawScratchCount_` / `activeOutputScratchCount_` from the device's active-channel masks; no allocation on the audio-thread side. Steps 3 and 4 both write ADDITIVELY into the same physical output buffers — DirectLayer is the bypass path, OutputMixer is the produced-mix path; step 1's zero-fill is the common foundation. `outputMixer_` is `const OutputMixer*` because `renderBuffer` is `const noexcept` (state-function shape, matches the M4 `const DirectLayer*` pattern). The class-level threading-contract docblock in AudioCallback.h documents the configure-before-audio-starts invariant for all collaborator setters. M1 Sessions 1+2+3, M4 Session 3, M5 Session 3 — verified by inspection. |
| `ida::InputMixer::processBuffer` | yes | yes | yes (O(1) map lookup + O(byteCount) memcpy + O(numSamples) DSP) | M3 Session 2 wiring + M5 Session 1 extension + M6 Session 2 extension: one `std::unordered_map<int64_t, Channel>::find` per call (early-return if the channel isn't registered — the M4 default config exercises this path); for `SignalType::Audio` channels, `std::memcpy` the byte stream into the constructor-pre-allocated `processingScratch_` float vector (sized to file-scope `kMaxScratchSamples = 8192`, byteCount clamped to that ceiling), invoke `ChannelStrip<SignalType::Audio>::process` on the scratch (gain + equal-power pan, allocation-free, lock-free, `noexcept`), then on tape-bearing channels `std::memcpy` the processed scratch into a stack-allocated `TapeWriteMessage` and a wait-free `boost::lockfree::spsc_queue::push` to the TapeWriter. Non-Audio channels skip the DSP path entirely (their chains are stubs until M9/M12/M13). The source `bytes` pointer is read but never mutated — DirectLayer's raw routes read the same float buffers and a write through would break the raw-monitor contract. Overload path: queue-full triggers `OverloadProtection::reportLoad(1.0)` via the atomic-publish pattern (the `noexcept reportLoad` overload added in M3 — never throws on the audio thread). **M6 Session 2** — when a `NotificationBus*` is bound via `setNotificationBus`, the queue-full branch ALSO posts a `Warning/CpuPressure` notification (`"audio thread missed deadline — tape buffer dropped"`) BEFORE the `reportLoad` call. `NotificationBus::post` is `noexcept`, allocation-free, and lock-free (see the row below), preserving the audio-thread contract on this method. Both collaborators may be unbound (tests exercise the partial-wiring case) and each leg no-ops independently. M3 Sessions 1+2+3, M5 Session 1, M6 Session 2 — verified by inspection plus `[input-mixer][audio-dsp]` test asserting a 0.5-gain strip on an all-1.0f buffer lands as all-0.5f bytes on tape, and `[input-mixer][notification]` test asserting overflow → bus post. |
| `ida::InputMixer::renderInputGraph` | yes | yes | yes (O(numChannels * numSamples * numTapes), capped at kMaxScratchSamples = 8192 samples) | Tape subsystem slice 2 (2026-05-21). Audio-thread entry point; `noexcept` per signature. All working storage is pre-allocated in the constructor: `tapeMixLeft_` / `tapeMixRight_` (kMaxTapes × kMaxScratchSamples floats each), `tapeTouched_` (kMaxTapes ints), and the stereo scratch pair `scratchLeft_` / `scratchRight_` (kMaxScratchSamples floats each) — no allocation on the audio path. Body: (1) zero the active tape-slot buffers and clear `tapeTouched_` flags via `std::memset` (bounded by `tapeTerminals_.size() ≤ kMaxTapes`); (2) for each channel with a source descriptor, `std::memcpy` the device input into the stereo scratch and invoke `ChannelStrip<SignalType::Audio>::process` on it, then route the post-strip block to its main-out destination (tape slot via `accumulateIntoTape`, hardware output via additive loop, or bus via `accumulateIntoBus`) and to any active sends via `accumulateIntoBus`; (3) for each bus node in `graph_.evaluationOrder()` (a `const&` to a pre-built vector — no allocation), invoke `Bus::process` into the destination (tape, hardware output, or upstream bus); (4) deliver each tape slot whose `tapeTouched_` flag is set once per block via `ITapeSink::deliverTapeBlock` — the contract on `ITapeSink::deliverTapeBlock` is `noexcept` and allocation-free (documented in `ITapeSink.h`). `accumulateIntoTape` guards the per-slot buffer size with a `jassert` (fires only on a future contract violation — callers pass the already-clamped `n`). No locks, no I/O, no `throw`. 2026-05-21 — verified by inspection plus `[input-mixer][multi-tape][render]` test suite (tape-routed delivery, two-channel sum, parallel-tape, bus→tape, no-sink crash-free). |
| `ida::ChannelStrip<SignalType::Audio>::process` | yes | yes | yes (O(numChannels * numSamples)) | M5 Session 1 — the per-channel gain/pan DSP behind `Channel::processing` (supersedes the M3-era no-op `AudioChain`). Loads `gainLinear_` and `panNormalized_` from `std::atomic<float>` once each per call (acquire), computes equal-power pan coefficients via `std::cos`/`std::sin` on a stack-resident float, then runs a tight `*=` loop over the caller-provided non-interleaved `float* const* channelData` for the supplied `numSamples`. Mono buffers apply gain only; stereo (or wider) apply equal-power pan to the first two channels and leave any additional channels untouched (M5 does not spec surround panning). No allocation, no locks, no I/O, no logging. `noexcept` enforced by `static_assert` in `ChannelStrip.h`. Called from `InputMixer::processBuffer` on the audio thread; will also be called from `OutputMixer::renderBuffer` once Sessions 2+3 wire that path. |
| `ida::Bus::process` | yes | yes | yes (O(numSlots × numChannels * numSamples), clamped to `kMaxBusChannelsHard = 2`) | M5 Session 3 / M7 Session 3 — the session-level effect-bus audio-thread entry point per V3 Step 7 / V7 alignment plan M5 (lines 384-388) + M7 line 533. `const noexcept` body, two paths: (M5 inline path, taken when `host_ == nullptr` OR `effectChain_` is empty / all bypassed — the default config) for each active channel (clamped to `kMaxBusChannelsHard = 2`), ADDITIVELY accumulate `mixBuffer_[c]` into `output[c]` via a tight `+=` loop, then `std::memset` the `mixBuffer_` slice to zero so the next buffer starts fresh. (M7 S3 chain path, taken when `host_ != nullptr` AND at least one non-bypassed slot exists) `std::memcpy mixBuffer_[c] → processedBuffer_[c]` for active channels, then for each non-bypassed slot call `host_->pumpSlot(busId, slotIdx, processedPtrs, processedPtrs, …)` IN-PLACE — pumpSlot's miss case leaves `processedBuffer_` unchanged (= dry signal), the hit case writes the previous buffer's response (pipelined 1-buffer delay); then ADDITIVELY accumulate `processedBuffer_[c]` into `output[c]` and zero `mixBuffer_[c]`. Pre-allocated `mixBuffer_` + `processedBuffer_` (each sized `kMaxBusMixSamples × kMaxBusChannelsHard = 8192 × 2 floats = 64 KB` in the constructor — well above any realistic device buffer envelope) are `mutable` because `mixBufferChannel(int) const noexcept` returns a writable pointer that audio-thread callers (typically `OutputMixer::renderBuffer`) populate BEFORE invoking `process`. `effectChain_` is set-once on the message thread via `setEffectChain`; `host_` is set-once via `setEffectChainHost`; the audio thread reads both through `const` reference and never mutates. Defensive guards on null pointers + non-positive counts. No allocation, no locks, no I/O, no logging. `noexcept` enforced by `static_assert` in `BusTests.cpp`. M5 Sessions 2+3 + M7 Session 3 — verified by inspection plus the `[bus][rt-safety]` test asserting additive-plus-zero semantics on the inline path and the `[output-mixer][plugin-host]` integration test asserting pipelined 1-buffer-delay round-trip on the chain path. |
| `ida::OutputMixer::renderBuffer` | yes | yes | yes (O(numChannels_registered * numBuses * numSamples), capped at 32 × 64 × kMaxBlockSamples) | M5 Session 3 — the produced-mix audio-thread entry point per V3 §2.2 / V7 alignment plan M5 (lines 376-432). `const noexcept` body implementing the four-step traversal: (1) for each registered output channel, scratch-mix its source from the corresponding input device channel (M5 pre-Constituent proxy; M6+ replaces with Constituent renders) through `ChannelStrip<SignalType::Audio>::process` into a pre-allocated per-channel scratch slot; (2) for each (channel, bus) send level > 0, accumulate the scaled scratch into the target bus's `mixBuffer_` via `Bus::mixBufferChannel`; (3) for each non-master bus, accumulate its mixBuffer into the master bus's mixBuffer at unity, then zero the source bus's mixBuffer; (4) `Bus::process` on the master writes its accumulated mixBuffer additively into the physical output channels. All scratch is pre-allocated in the constructor: `sendMatrix_` (`kMaxOutputChannels × kMaxBuses = 8 KB`), `channelScratch_` (`kMaxOutputChannels × kMaxBlockSamples × 2 = 2 MB` for the stereo per-channel post-strip buffers); both `mutable` because the function is `const`. Each `Bus`'s `mixBuffer_` is also `mutable` per the row above. M5 default: OutputMixer comes up EMPTY (no auto-registered channels), so the early-return on `channels_.empty()` makes this a hot-path no-op in the default app config — tests + M6+ Constituent rendering activate the path. Const-pointer in AudioCallback (`const OutputMixer*`) matches `const DirectLayer*` from M4. No allocation, no locks, no I/O, no logging. `noexcept` per signature. RT-budget: measured 17.5µs max at 32 channels × 8 buses × 256 samples on dev machine (0.33% of 5.33ms buffer @ 48kHz/256), via the `[output-mixer][.rt-smoke]` hidden Catch2 tag. M5 Sessions 1+2+3 — verified by inspection plus `[output-mixer][render-buffer]` integration tests covering strip+pan, bus sends, additive output, and the empty-registry no-op fast path. |
| `ida::DirectLayer::routeBuffers` | yes | yes | yes (O(routes * buffers)) | Stricter than InputMixer per contract §6 — "stateless, audio-thread-exclusive." Two flat loops over dense `std::vector<RawRoute>` / `std::vector<ProcessedRoute>` (no tombstones; swap-and-pop removal preserves dense iteration). For each route: linear scan of the caller-provided spans for matching `InputId` / `ChannelId` / `OutputChannelId` (bounded by buffer count — typically <16 per the audio device), then `std::min(srcCount, dstCount)` clamp and an additive `dst[i] += src[i]` loop. No map lookup, no allocation, no lock, no atomic — `const` member function, called via `const DirectLayer*` from AudioCallback. M4 Sessions 1+2+3 — verified by inspection plus the `[.rt-smoke]` Catch2 test (8 routes × 256 samples, median under 100µs after 100 warmup iterations). |
| `ida::Lmc::advanceBySamples` | yes | yes | yes (constant time) | One fetch_add and one store on `std::atomic<int64_t>`. M1 Session 2 — verified by inspection. |
| `ida::Asrc::process` | yes | yes | yes (O(inputCount + outputCapacity)) | soxr handle created with `runtimeSpec=1` (single-threaded — no internal locking) and the variable-rate path; per soxr docs `soxr_process` does not allocate once the handle is initialised. **M1 Session 3 — held by `AudioCallback` as scaffolding but not invoked from the buffer body. Audit established now so M2 routing can call it without further audit work.** Note: `setIoRatio` throws on out-of-range and is for message-thread use only; the audio thread must not call it. |
| `ida::OverloadProtection::reportLoad` | yes | yes | yes (constant time) | Three independent state-machine updates over fixed hysteresis bands; no loops, no allocation. **Throws `std::invalid_argument` on negative input — never called from the audio thread.** M1 Session 3 wiring: audio thread publishes the per-buffer elapsed time via `AudioCallback::lastCallbackElapsedSec()` (`std::atomic<double>` store); message thread (30 Hz timer in `MainComponent`) reads, divides by the buffer-time budget, and calls `reportLoad` with a guaranteed-non-negative fraction. |
| `ida::RetroactiveRing::push` | yes | yes | yes (O(1)) | Single modulo + one assignment into a pre-allocated buffer. **Not on the audio thread per the class doc** — lives engine-side, consumer of the (M3/M4) SPSC tape-event queue. `snapshot()` *does* allocate (`reserve`/`push_back`) and is explicitly off the audio path. M1 Session 3 — held by `MainComponent` as scaffolding sized at construction; first writer is the M3 tape-event drain thread. |
| `ida::AudioDeviceCalibration::deviceToLmc` / `lmcToDevice` | yes | yes | yes (constant time) | Pure `Rational` arithmetic on an immutable value object — one multiply / add / subtract / divide on `Rational` (which is itself allocation-free for `int64_t` operands). **M1 Session 3 — held by `AudioCallback` as scaffolding (identity calibration) but not invoked from the buffer body. M8 fills the first call site once a measured calibration replaces the identity default.** |
| `ida::OutOfProcessEffectChainHost::pumpSlot` | yes | yes | yes (O(1) unordered_map find + 2 acquire-load atomic reads + 1 SPSC push + 1 SPSC pop + 1 relaxed atomic store-or-fetch_add + O(numSamples × kPumpChannels) interleave/de-interleave; total payload bounded at `PluginIpcMessage::kMaxPayloadBytes = 8192` bytes) | M7 Session 3 + S4 — the audio-thread surface that promotes the engine-side producer of the M7 S2c shared-memory IPC onto the audio thread AND drives the S4 watchdog miss counter. `noexcept` per the `IEffectChainHost::pumpSlot` interface contract. Body shape: (1) defensive argument guards (null pointers / non-positive counts → return false); (2) `instances_.find((busId, slotIndex))` — `std::unordered_map` find is O(1) average, allocation-free, and noexcept on libc++/libstdc++ for trivially-copyable keys; (3) **S4 bypass-flag fence** — acquire-load `state.permanentlyBypassed` and `state.bypassed`; either set ⇒ return false without touching the instance (the supervisor's protocol for swapping instances safely is "raise bypass, wait kRestartGraceMs, swap"); (4) interleave `inChannels` into `wireScratch_` (pre-allocated in the constructor); (5) one `tryWriteBytes` — wait-free SPSC push; (6) one `tryReadBytes` — wait-free SPSC pop; (7) de-interleave the response into `outChannels`. **S4 miss counter**: on success ⇒ relaxed-store `consecutiveMisses = 0`; on push-failure (full ring), empty-pop, or zero-frame payload ⇒ relaxed `fetch_add(1)`. The supervisor thread polls this counter at `kSupervisorPollMs = 50 ms` and triggers a restart at `kConsecutiveMissThreshold = 16` misses. The audio thread NEVER reads the counter — the atomic exists for cross-thread visibility, not coordination. Map ordering: the bucket pointer to the SlotState is stable across `configureBus` and supervisor `attemptRestart` (the supervisor mutates `state->instance` IN PLACE under the bypass fence — the map's key set is only mutated by configureBus, which runs before audio starts). **S4 ownership note (B1 fix)**: map values are `std::shared_ptr<SlotState>`, but the audio thread reads `it->second` as a `const shared_ptr&` and dereferences via `.get()` — NO refcount bump, NO atomic refcount manipulation on the audio path. The shared_ptr layout exists so the supervisor thread can snapshot strong references under `instancesMutex_` and safely outlive a future concurrent `configureBus` erase across the supervisor's unlocked grace/spawn window; that snapshot copy + drop is the only place the refcount moves, and it happens on the supervisor thread. **Pipelined 1-buffer delay model**: on a miss, `outChannels` is left untouched (the caller — `Bus::process` — ensures `outChannels` holds the dry signal before invoking, so a miss = dry passthrough). **Single message per buffer**: at the 1024-frame envelope, `4 + 1024 × kPumpChannels × float = 8196` bytes — one byte over `kMaxPayloadBytes`. Realistic engine block sizes (64..512) fit with headroom. `JUCE-free interface` (`IEffectChainHost` lives in `core/`, no `juce::` types in the audio-thread surface). M7 Sessions 3+4 — verified by inspection plus `[output-mixer][plugin-host]` integration test (pipelined round-trip, 1040 sample assertions) AND `[plugin-supervisor]` integration tests (healthy / restart / permanent-bypass lifecycle through SIGKILL). Measured baseline: inherits the 68 µs median / 134 µs p99 round-trip from the S2c `[plugin-ipc][.rt-smoke]` audit (the S4 atomic ops add ≤ 5 ns each on Apple Silicon — well below measurement noise). |
| `ida::NotificationBus::post` | yes | yes | yes (constant time + bounded 128-byte copy) | M6 Session 1 — V5 §8.6 engine→UI truthfulness channel post path. RT-safe-by-construction: builds a stack-resident `Notification` POD (trivially copyable, ~144 bytes, `static_assert` enforced in the header), snapshots `juce::Time::getHighResolutionTicks` (Apple Silicon VDSO userspace call, bounded), copies the caller's `const char*` into the 128-byte fixed array via a tight character loop with forced null termination at the last byte, then pushes onto the per-category `LockFreeSpscQueue<Notification>` (capacity 256, pre-allocated in the bus constructor). On overflow, drops the NEW entry (the only SPSC-correct policy — see the deviation note in `NotificationBus.h` against M6 plan line 479) and bumps the per-category `std::atomic<std::uint64_t>` counter via `fetch_add` relaxed. No allocation, no locks, no I/O, no logging on the post path. `noexcept` enforced by signature; the SPSC `push` itself is `noexcept`-callable (returns false on full instead of throwing). Nine per-category rings each have independent head/tail atomics — posts to different categories do not serialize, eliminating the priority-inversion shape called out in the M6 spec. Held but not yet invoked by `AudioCallback` in M6 Session 1 — Session 2 wires the audio-thread post sites (overload, tape rotation, device events). |
| `ida::TapeRecordWriter::deliverTapeBlock` | yes | yes | yes (O(numSamples) interleave copy + 1 SPSC push + 1 relaxed atomic increment on overflow; bounded by `kTapeRecordWriterMaxFramesPerMessage`) | T0a Task 4 (2026-05-30) — the audio-thread `ITapeSink` implementation for the `.idatape` worker. `noexcept` per the `ITapeSink` contract and enforced by signature. Body: (1) early-return on non-positive count or null pointers; (2) drop (with relaxed `fetch_add` on `droppedBlocks_`) if `numSamples > kTapeRecordWriterMaxFramesPerMessage` — the ceiling is 4096 stereo frames (32 KB), well above any realistic device buffer; (3) construct a stack-resident `Message` POD (trivially copyable; `samples` array not zero-initialised — body fills exactly `[0, numFrames*2)` before push); (4) interleave `left[]` / `right[]` into `msg.samples` via a tight loop bounded by the clamped frame count; (5) one wait-free `LockFreeSpscQueue::push` — returns `false` on full, overflow path bumps `droppedBlocks_` via relaxed `fetch_add`. Call chain on overflow: stack POD `Message` construction → `queue_.push` → relaxed `fetch_add`. No `wakeCv_` notify on the audio thread. Worker thread owns all codec calls, `encodeRecord`, and `juce::FileOutputStream::write`. T0a Task 4 — verified by inspection plus `[tape-record]` test suite and `[tape-record][rt-safety]` alloc-free proof (operator-new override, zero allocations asserted). |
| `ida::MasterMeter::publish` | yes | yes | yes (O(numSamples)) | T2 (2026-05-28) — lock-free master-mix metering publisher. `noexcept` per signature. Takes raw planar stereo pointers (`const float* left`, `const float* right`, `int numSamples`); early-returns on `numSamples ≤ 0`. Body: single tight loop accumulating squared samples for RMS (L + R) and absolute values for peak, then three `std::sqrt` / `std::log10` calls on stack-resident floats, then one `std::atomic<Snapshot>::store(release)`. `static_assert(is_always_lock_free)` in `MasterMeter.cpp` compile-fails on any platform where the 4-float struct atomic falls back to a mutex. No allocation, no lock, no I/O, no `throw`. T2 — verified by inspection plus `[ida-master-meter]` test suite (peak value bounds, 10 000-iteration alloc-free proof via operator-new override). |
| `ida::MasterSpectrum::publish` | yes | yes | yes (O(numSamples) per call, O(fftSize log fftSize) on the once-per-fftSize-samples FFT trigger) | T4 (2026-05-28) — lock-free master-mix FFT spectrum publisher. `noexcept` per signature. Takes raw planar stereo pointers (`const float* left`, `const float* right`, `int numSamples`); early-returns on `numSamples ≤ 0`, null FFT, or null pointers. Body: a single while-loop fills the pre-allocated `scratch_` accumulator with the windowed mono'd ((L+R)/2 × Hann) signal — bounded by `fftSize_` per FFT cycle (constructed in `prepare()` on the message thread, sized once at device-start, never reallocated). When the accumulator fills, `juce::dsp::FFT::performFrequencyOnlyForwardTransform` runs in place on `scratch_` (JUCE's FFT is allocation-free post-construction — the FFT plan is built in `prepare()`), then per-bin magnitudes are normalized, converted to dB on stack-resident floats, and `std::atomic<float>::store(release)`'d into the pre-sized `bins_` vector. `static_assert(std::atomic<float>::is_always_lock_free)` in `MasterSpectrum.cpp` compile-fails on any platform where `std::atomic<float>` falls back to a mutex. The bin vector and FFT plan are constructed exclusively in `prepare()`; the audio thread never resizes either. No allocation, no lock, no I/O, no `throw`. T4 — verified by inspection plus `[ida-master-spectrum]` test suite (sine-energy assertion, 1 000-iteration alloc-free proof via the shared operator-new override owned by `IdaMasterSpectrumTests.cpp`). |

The four Session 3 rows close M1's RT-safety audit. Two distinct shapes
emerged from the M1-Session-3 brainstorm:

1. **Held-but-not-invoked scaffolding** (Asrc, AudioDeviceCalibration).
   The audio callback holds non-owning references so the routing path
   exists structurally; M2-M8 grow real calls into them. The audit
   verifies they would be RT-safe when invoked, so later milestones
   can wire them without re-auditing.
2. **Atomic-publish, message-thread consumes** (OverloadProtection).
   The audio thread does the minimal RT-safe work (a single elapsed-time
   measurement and `std::atomic<double>` store); the message thread
   reads the atomic and calls the throwing `reportLoad` API. This is
   the canonical pattern for any future class whose API straddles the
   RT/non-RT boundary — keep the throwing/allocating side off-thread
   and bridge with a lock-free atomic.

`RetroactiveRing` is a third shape: explicitly off the audio thread by
design, consumer of an SPSC queue the audio thread will write to in M3/M4.
The producer side (the queue's `push`) will land its own audit row when
the queue lands; the ring's own `push` stays a single-threaded
non-RT operation.

---

## How to audit a class for audio-thread safety

A read-the-code heuristic, in order of severity. Treat any hit as a
diagnostic — not all hits are violations, but each one needs an
explicit verdict in the row.

1. **Grep for allocation.** Run on every `.cpp` reachable from the
   audio call path:

   ```
   grep -nE '\bnew \b|\bdelete\b|std::make_(unique|shared|optional)|push_back|emplace_back|resize|insert|reserve|operator\s*\+\s*=\s*\".*\"' <files>
   ```

   Any hit on the audio thread's call chain is a violation unless it
   is provably on a path the audio thread doesn't take.

2. **Grep for locking.**

   ```
   grep -nE 'std::mutex|std::lock_guard|std::unique_lock|std::condition_variable|juce::CriticalSection|juce::ScopedLock' <files>
   ```

   Even "I'll just take a quick lock" is forbidden — a quick lock is
   still an unbounded wait if the contending thread is preempted.

3. **Grep for I/O.**

   ```
   grep -nE 'printf|std::cout|std::cerr|std::ostream|fopen|fwrite|fprintf|juce::Logger|juce::FileLogger|juce::FileOutputStream' <files>
   ```

   Logging from the audio thread is the most common violation; it
   looks harmless and is catastrophic. If you need a diagnostic from
   the audio thread, push the message onto a lock-free queue and let
   a non-RT thread log it.

4. **Grep for opaque framework calls** that *might* allocate or lock.
   `juce::String` operator+, `juce::File` anything, `juce::MessageManager`
   anything, anything that takes a `juce::Time`. Look at each one and
   either verify it's safe (rare) or rewrite the path to avoid it.

5. **Walk the call graph.** A safe function that calls an unsafe
   helper is unsafe. The audit terminates at functions whose entire
   call graph is RT-safe.

6. **Confirm bounded execution time.** For each loop on the audio
   call path, identify what bounds the iteration count: `numSamples`
   from JUCE, `numChannels` from JUCE, or a fixed capacity from
   construction. Unbounded loops — `while (queue.pop(...))` without
   a max-iteration guard — fail this rule.

A class that passes all six gets a `yes / yes / yes` row in the table.
Anything less needs either a fix or a documented exception.

---

## What this contract is not

- **Not a profiler.** Sub-microsecond paths can still blow buffer
  budgets if there are thousands of them per buffer. Profiling
  is a separate concern (M11 capability-tier tuning, M15 SLO
  measurement).
- **Not a substitute for testing.** A class can be lock-free in
  isolation and deadlock-prone in composition. Integration tests
  under real audio load are still required.
- **Not negotiable.** The latency claims of the white paper §16.8
  rest entirely on these commitments. A violation that "works in
  practice" is still a violation — it makes the architecture's
  guarantees aspirational rather than load-bearing.
