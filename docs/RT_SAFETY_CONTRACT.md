# RT-Safety Contract

This is the rule every audio-thread code path in Sirius Looper lives
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
| `sirius::AudioCallback::audioDeviceIOCallbackWithContext` | yes | yes | yes (O(numSamples * numChannels)) | M4 Session 3 promoted the body from identity pass-through to a five-step orchestrator: (1) zero outputs, (2) `dispatchInputMixer` per channel, (3) `dispatchDirectLayer` gated by `monitoringEnabled_`, (4) LMC advance, (5) `juce::Time::getHighResolutionTicks` pair + `std::atomic<double>` elapsed publish. Scratch vectors for the DirectLayer span are pre-sized in the constructor to `kMaxScratchChannels = 32` and only indexed (never resized) in the callback. `audioDeviceAboutToStart` records `activeRawScratchCount_` / `activeOutputScratchCount_` from the device's active-channel masks; no allocation on the audio-thread side. M1 Sessions 1+2+3, M4 Session 3 — verified by inspection. |
| `sirius::InputMixer::processBuffer` | yes | yes | yes (O(1) map lookup + O(byteCount) memcpy) | M3 Session 2 wiring: one `std::unordered_map<int64_t, Channel>::find` per call (early-return if the channel isn't registered — the M4 default config exercises this path), runs the per-channel `ProcessingChain` (no-op until M5), then on tape-bearing channels `std::memcpy` into a stack-allocated `TapeWriteMessage` and a wait-free `boost::lockfree::spsc_queue::push` to the TapeWriter. Overload path: queue-full triggers `OverloadProtection::reportLoad(1.0)` via the atomic-publish pattern (the `noexcept reportLoad` overload added in M3 — never throws on the audio thread). M3 Sessions 1+2+3 — verified by inspection; M4 Session 3 added the call site in `AudioCallback::audioDeviceIOCallbackWithContext` step 2. |
| `sirius::DirectLayer::routeBuffers` | yes | yes | yes (O(routes * buffers)) | Stricter than InputMixer per contract §6 — "stateless, audio-thread-exclusive." Two flat loops over dense `std::vector<RawRoute>` / `std::vector<ProcessedRoute>` (no tombstones; swap-and-pop removal preserves dense iteration). For each route: linear scan of the caller-provided spans for matching `InputId` / `ChannelId` / `OutputChannelId` (bounded by buffer count — typically <16 per the audio device), then `std::min(srcCount, dstCount)` clamp and an additive `dst[i] += src[i]` loop. No map lookup, no allocation, no lock, no atomic — `const` member function, called via `const DirectLayer*` from AudioCallback. M4 Sessions 1+2+3 — verified by inspection plus the `[.rt-smoke]` Catch2 test (8 routes × 256 samples, median under 100µs after 100 warmup iterations). |
| `sirius::Lmc::advanceBySamples` | yes | yes | yes (constant time) | One fetch_add and one store on `std::atomic<int64_t>`. M1 Session 2 — verified by inspection. |
| `sirius::Asrc::process` | yes | yes | yes (O(inputCount + outputCapacity)) | soxr handle created with `runtimeSpec=1` (single-threaded — no internal locking) and the variable-rate path; per soxr docs `soxr_process` does not allocate once the handle is initialised. **M1 Session 3 — held by `AudioCallback` as scaffolding but not invoked from the buffer body. Audit established now so M2 routing can call it without further audit work.** Note: `setIoRatio` throws on out-of-range and is for message-thread use only; the audio thread must not call it. |
| `sirius::OverloadProtection::reportLoad` | yes | yes | yes (constant time) | Three independent state-machine updates over fixed hysteresis bands; no loops, no allocation. **Throws `std::invalid_argument` on negative input — never called from the audio thread.** M1 Session 3 wiring: audio thread publishes the per-buffer elapsed time via `AudioCallback::lastCallbackElapsedSec()` (`std::atomic<double>` store); message thread (30 Hz timer in `MainComponent`) reads, divides by the buffer-time budget, and calls `reportLoad` with a guaranteed-non-negative fraction. |
| `sirius::RetroactiveRing::push` | yes | yes | yes (O(1)) | Single modulo + one assignment into a pre-allocated buffer. **Not on the audio thread per the class doc** — lives engine-side, consumer of the (M3/M4) SPSC tape-event queue. `snapshot()` *does* allocate (`reserve`/`push_back`) and is explicitly off the audio path. M1 Session 3 — held by `MainComponent` as scaffolding sized at construction; first writer is the M3 tape-event drain thread. |
| `sirius::AudioDeviceCalibration::deviceToLmc` / `lmcToDevice` | yes | yes | yes (constant time) | Pure `Rational` arithmetic on an immutable value object — one multiply / add / subtract / divide on `Rational` (which is itself allocation-free for `int64_t` operands). **M1 Session 3 — held by `AudioCallback` as scaffolding (identity calibration) but not invoked from the buffer body. M8 fills the first call site once a measured calibration replaces the identity default.** |

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
