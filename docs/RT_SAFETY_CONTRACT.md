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
| `sirius::AudioCallback::audioDeviceIOCallbackWithContext` | yes | yes | yes (O(numSamples * numChannels)) | Identity pass-through (memcpy/memset) + LMC advance. M1 Session 1+2 — verified by inspection. |
| `sirius::Lmc::advanceBySamples` | yes | yes | yes (constant time) | One fetch_add and one store on `std::atomic<int64_t>`. M1 Session 2 — verified by inspection. |
| `sirius::Asrc::process` | TBD | TBD | TBD | Session 3 wires it. Pre-existing implementation in `engine/src/Asrc.cpp` — audit before hooking up. |
| `sirius::OverloadProtection::tick` | TBD | TBD | TBD | Session 3 wires it. |
| `sirius::RetroactiveRing::push` | TBD | TBD | TBD | Session 3 wires it. |
| `sirius::AudioDeviceCalibration::apply` | TBD | TBD | TBD | Session 3 wires it. |

`TBD` rows must be resolved before the class is invoked from the audio
thread. The Session 3 commit that lands the wiring also lands the
audit row in this table.

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
