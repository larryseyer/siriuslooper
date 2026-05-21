# Bus Channel Controls (gain / mute / dual peak+LUFS metering) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the engine `Bus` type a real fader gain, mute, and a post-fader dual peak+LUFS meter — mirroring `ChannelStrip<SignalType::Audio>` — so the Input Mixer UI (routing-graph Phase 6) can render live, non-dead bus / FX-return strips.

**Architecture:** This is the **engine prerequisite for routing-graph Phase 6** (the spec assumes bus/FX-return strips have "fader/mute/solo + the dual peak+LUFS meter like channel strips," but `Bus` currently has only an effect chain + mix scratch). All work is headless TDD in the `engine` library; no UI, no operator eyes-on. The new state lives on the shared `Bus` type (both mixers reuse it per `project_two_mixers_totally_separate` — same *type*, separate instances). `OutputMixer` is structurally untouched: its master/sub buses inherit the new members at their defaults (gain 1.0, unmuted, meter un-prepared → no-op), so its audio output stays bit-identical and its existing tests stay green. The Input Mixer UI phase that follows wires gestures/strips to these accessors.

**Tech Stack:** C++20, JUCE (engine public API stays JUCE-free), Catch2 (`SiriusTests`), CMake + Ninja. RT-safety contract `docs/RT_SAFETY_CONTRACT.md` §6 governs `Bus::process`.

---

## Why a custom move ctor + a move-enabled LufsMeter (read before Task 1)

`InputMixer` and `OutputMixer` both store buses as `std::vector<Bus> buses_`, each `reserve()`d to its cap in the constructor (`InputMixer.cpp:34` → `kMaxInputBuses`; `OutputMixer.cpp` likewise). `reserve` never grows at runtime, so existing elements are never moved during operation — **but `std::vector<Bus>` still must compile**, which requires `Bus` to be *MoveInsertable with a `noexcept` move ctor* (since the type will be non-copyable once it holds atomics).

- `std::atomic<float>`/`std::atomic<bool>` have **no move ctor** → adding them makes the implicit `Bus` move ctor deleted.
- `LufsMeter` declares `LufsMeter(const LufsMeter&) = delete` and does **not** declare a move ctor → it is currently move-deleted too.

So this plan: (1) makes `LufsMeter` move-only via a defaulted `noexcept` move ctor/assignment (its members are `std::vector` + scalars, all `noexcept`-movable), and (2) gives `Bus` a hand-written `noexcept` move ctor that `.load()`/`.store()`s the atomics and moves the meter. That keeps `std::vector<Bus>` valid in **both** mixers with zero changes to `OutputMixer`'s storage or call sites.

`ChannelStrip<Audio>` already holds a `LufsMeter` by value and is non-copyable; it is held via `unique_ptr` so it never needs to move — the new `LufsMeter` move ops are harmless to it.

## File structure

- **Modify** `engine/include/sirius/LufsMeter.h` — add `noexcept` move ctor + move assignment (keep copy deleted).
- **Modify** `engine/include/sirius/Bus.h` — add gain/mute/peak/LUFS API + members + a `noexcept` move ctor; include `<atomic>` and `"sirius/LufsMeter.h"`.
- **Modify** `engine/src/Bus.cpp` — implement the move ctor; apply gain+mute and write the post-fader meter inside `process()` (both the inline path and the effect-chain path), preserving the M8 S4 wet-capture tap point and default-config bit-equivalence.
- **Modify** `engine/include/sirius/InputMixer.h` + `engine/src/InputMixer.cpp` — add `Bus* busForId (BusId) noexcept` (message-thread accessor, mirrors `processingChainFor`).
- **Modify** `tests/BusTests.cpp` — gain/mute/peak/LUFS/move-ctor cases.
- **Modify** `tests/InputMixerTests.cpp` — `busForId` accessor cases.
- **Modify** `tests/LufsMeterTests.cpp` if present (else add cases to `tests/BusTests.cpp`) — LufsMeter move case.

## Conventions for every task

- Build: `cmake --build build --target SiriusTests` (configure once: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`).
- Run one tag: `./build/tests/SiriusTests "[tag]"`. Full suite: `ctest --test-dir build` (baseline 521/522 — the 1 non-pass is the documented `MainComponentPluginEditorTests_NOT_BUILT`, run separately by `bash/test-s7.sh`).
- Commit after each task (single-line message, `<type>: <title>`), then `git push` (authorized: memory `feedback_claude_commits_and_pushes_master`).
- Const-correctness, named constants, RAII, no magic numbers (CLAUDE.md). The peak atomics + `LufsMeter` are `mutable` because `process` is `const`.

---

### Task 1: Make `LufsMeter` move-only

**Files:**
- Modify: `engine/include/sirius/LufsMeter.h:30-33` (the special-members block)
- Test: `tests/LufsMeterTests.cpp` (if it exists; otherwise add the case to `tests/BusTests.cpp`)

- [ ] **Step 1: Confirm the test file**

Run: `ls tests/LufsMeterTests.cpp 2>/dev/null && echo HAVE || echo USE_BusTests`
If `USE_BusTests`, add the test below to `tests/BusTests.cpp` instead (it already includes Catch2). Adjust the include to `#include "sirius/LufsMeter.h"`.

- [ ] **Step 2: Write the failing test**

```cpp
TEST_CASE ("LufsMeter is move-constructible and a moved meter still measures", "[lufs][move]")
{
    static_assert (std::is_move_constructible_v<sirius::LufsMeter>,
                   "LufsMeter must be movable so Bus can live in std::vector<Bus>");

    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 512;

    sirius::LufsMeter source;
    source.prepare (kSampleRate, kBlock);

    sirius::LufsMeter moved (std::move (source));   // move-construct after prepare()

    // Feed a 1 kHz sine (NOT DC — K-weighting's high-pass would kill a constant);
    // integrated loudness must rise above the -70 LUFS absolute gate, proving the
    // moved-into meter is fully functional.
    std::array<float, static_cast<std::size_t> (kBlock)> buf {};
    double phase = 0.0;
    const double inc = 2.0 * M_PI * 1000.0 / kSampleRate;
    for (int i = 0; i < 300; ++i)
    {
        for (int n = 0; n < kBlock; ++n) { buf[static_cast<std::size_t> (n)] = 0.5f * static_cast<float> (std::sin (phase)); phase += inc; }
        moved.process (buf.data(), buf.data(), kBlock);
    }

    REQUIRE (moved.getIntegrated() > -70.0f);
}
```

Add `#include <type_traits>`, `#include <array>`, `#include <cmath>` (for `std::sin`/`M_PI`), and `#include <utility>` to the test file if not already present.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build --target SiriusTests 2>&1 | tail -20`
Expected: **compile error** — `use of deleted function 'sirius::LufsMeter::LufsMeter(sirius::LufsMeter&&)'` (move is deleted today because copy is user-deleted).

- [ ] **Step 4: Make LufsMeter move-only**

In `engine/include/sirius/LufsMeter.h`, replace the special-members block (currently lines ~30-33):

```cpp
    LufsMeter() = default;

    LufsMeter (const LufsMeter&) = delete;
    LufsMeter& operator= (const LufsMeter&) = delete;
```

with:

```cpp
    LufsMeter() = default;

    // Copy stays deleted (the meter owns large filter-state buffers and is
    // never duplicated). Move is enabled (defaulted, noexcept — members are
    // std::vector + scalars) so an owning aggregate like Bus can live in a
    // std::vector<Bus>; see docs/.../2026-05-21-mixer-bus-controls-engine.md.
    LufsMeter (const LufsMeter&)            = delete;
    LufsMeter& operator= (const LufsMeter&) = delete;
    LufsMeter (LufsMeter&&) noexcept            = default;
    LufsMeter& operator= (LufsMeter&&) noexcept = default;
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target SiriusTests && ./build/tests/SiriusTests "[lufs][move]"`
Expected: PASS (1 assertion + the static_assert compiles).

- [ ] **Step 6: Commit + push**

```bash
git add engine/include/sirius/LufsMeter.h tests/LufsMeterTests.cpp
git commit -m "refactor: make LufsMeter move-only so Bus can hold one by value"
git push
```
(If the test went into `tests/BusTests.cpp`, stage that file instead.)

---

### Task 2: `Bus` gain + mute (atomics) + `noexcept` move ctor

**Files:**
- Modify: `engine/include/sirius/Bus.h` (includes; public API after `setEffectChain`; private members after `processedBuffer_`)
- Modify: `engine/src/Bus.cpp` (move ctor; apply gain+mute in `process` — both paths)
- Test: `tests/BusTests.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tests/BusTests.cpp`:

```cpp
TEST_CASE ("Bus is movable (std::vector<Bus> requirement) and move preserves gain/mute",
           "[bus][move]")
{
    static_assert (std::is_move_constructible_v<sirius::Bus>,
                   "Bus must be MoveInsertable for std::vector<Bus>");

    sirius::Bus source (sirius::BusId { 7 }, sirius::BusConfig {});
    source.setGain (0.25f);
    source.setMuted (true);

    sirius::Bus moved (std::move (source));
    REQUIRE (moved.id().value() == 7);
    REQUIRE (moved.gain()  == 0.25f);
    REQUIRE (moved.muted() == true);
}

TEST_CASE ("Bus default gain is unity and unmuted (inline path stays bit-identical)",
           "[bus][gain]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    REQUIRE (bus.gain()  == 1.0f);
    REQUIRE (bus.muted() == false);

    // Mix a known value, default gain → output equals the mix (unchanged M5 body).
    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.5f;
    bus.mixBufferChannel (1)[0] = 0.5f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == 0.5f);
    REQUIRE (r[0] == 0.5f);
}

TEST_CASE ("Bus::setGain scales the output (inline path)", "[bus][gain]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setGain (0.5f);

    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.8f;
    bus.mixBufferChannel (1)[0] = 0.8f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == Approx (0.4f));
    REQUIRE (r[0] == Approx (0.4f));
}

TEST_CASE ("Bus::setMuted silences the output (inline path)", "[bus][mute]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setMuted (true);

    constexpr int kSamples = 4;
    bus.mixBufferChannel (0)[0] = 0.9f;
    bus.mixBufferChannel (1)[0] = 0.9f;

    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);
    REQUIRE (l[0] == 0.0f);
    REQUIRE (r[0] == 0.0f);
}
```

Ensure the test file has `#include <type_traits>`, `#include <utility>`, `#include <vector>`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target SiriusTests 2>&1 | tail -20`
Expected: compile errors — `'class sirius::Bus' has no member named 'setGain'` (and `gain`, `setMuted`, `muted`).

- [ ] **Step 3: Add the gain/mute API + members + move ctor declaration to `Bus.h`**

At the top of `Bus.h`, add to the include block (alongside the existing `<cstddef>`/`<string>`/`<vector>`):

```cpp
#include "sirius/LufsMeter.h"

#include <atomic>
```

In the `public:` section, immediately AFTER the `setEffectChain` method (the line `void setEffectChain (EffectChain chain) { effectChain_ = std::move (chain); }`), add:

```cpp
    /// Move ctor — hand-written because the atomic members are not movable.
    /// `noexcept` so std::vector<Bus> uses it on (reserve-bounded, never-hit-at-
    /// runtime) reallocation rather than refusing to compile. Copies the atomic
    /// values and moves the (move-only) loudness meter.
    Bus (Bus&& other) noexcept;
    Bus& operator= (Bus&&)      = delete;
    Bus (const Bus&)            = delete;
    Bus& operator= (const Bus&) = delete;

    /// Message-thread setter — post-effects fader gain (linear, default 1.0 =
    /// unity). Published via atomic; the audio thread loads it once per
    /// `process()`. Parity with ChannelStrip<Audio>::setGain.
    void  setGain (float linear) noexcept { gainLinear_.store (linear, std::memory_order_relaxed); }
    float gain() const noexcept           { return gainLinear_.load (std::memory_order_relaxed); }

    /// Message-thread setter — mute. When true, `process` contributes silence
    /// to the output and the meter reads silence. Solo is mixer-level policy
    /// (the UI maps solo to an effective mute), exactly as for channel strips.
    void setMuted (bool m) noexcept { muted_.store (m, std::memory_order_relaxed); }
    bool muted() const noexcept     { return muted_.load (std::memory_order_relaxed); }
```

In the `private:` section, immediately AFTER the `processedBuffer_` member declaration (`mutable std::vector<float> processedBuffer_;`), add:

```cpp
    /// Post-effects fader gain + mute (routing-graph Phase 6 prerequisite).
    /// Message-thread writes, audio-thread reads once per `process()`. Default
    /// unity/unmuted → the inline path stays bit-for-bit the M5 body.
    std::atomic<float> gainLinear_ { 1.0f };
    std::atomic<bool>  muted_      { false };
```

- [ ] **Step 4: Implement the move ctor + apply gain/mute in `Bus.cpp`**

In `engine/src/Bus.cpp`, add `#include <cmath>` to the include block (needed for `std::fabs` in Task 3; harmless now).

Add the move ctor immediately after the existing constructor (after the closing brace of `Bus::Bus (BusId id, BusConfig config)`):

```cpp
Bus::Bus (Bus&& other) noexcept
    : id_ (other.id_),
      config_ (std::move (other.config_)),
      effectChain_ (std::move (other.effectChain_)),
      host_ (other.host_),
      wetSink_ (other.wetSink_),
      wetCaptureId_ (other.wetCaptureId_),
      mixBuffer_ (std::move (other.mixBuffer_)),
      processedBuffer_ (std::move (other.processedBuffer_)),
      gainLinear_ (other.gainLinear_.load (std::memory_order_relaxed)),
      muted_ (other.muted_.load (std::memory_order_relaxed)),
      lufsMeter_ (std::move (other.lufsMeter_))
{
}
```

> NOTE on member init-list order: it MUST match the *declaration* order in `Bus.h`. After Task 4 adds the peak atomics + `lufsMeter_`, the declaration order is: `id_, config_, effectChain_, host_, wetSink_, wetCaptureId_, mixBuffer_, processedBuffer_, gainLinear_, muted_, peakLeft_, peakRight_, lufsMeter_`. Write the init-list to match; Task 4 inserts `peakLeft_`/`peakRight_` before `lufsMeter_`. (`-Werror=reorder` will catch a mismatch — fix the order if it fires.)

Now apply gain + mute at the two output-accumulate points. **Inline path** — replace the inner accumulate loop (currently):

```cpp
        for (int c = 0; c < activeChannels; ++c)
        {
            if (output[c] == nullptr) continue;
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;

            for (int s = 0; s < clampedSamples; ++s)
                output[c][s] += mix[s];

            std::memset (mix, 0,
                         static_cast<std::size_t> (clampedSamples) * sizeof (float));
        }
        return;
```

with:

```cpp
        const float inlineGain = muted_.load (std::memory_order_relaxed)
                                     ? 0.0f
                                     : gainLinear_.load (std::memory_order_relaxed);
        for (int c = 0; c < activeChannels; ++c)
        {
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;

            if (output[c] != nullptr)
                for (int s = 0; s < clampedSamples; ++s)
                    output[c][s] += mix[s] * inlineGain;

            std::memset (mix, 0,
                         static_cast<std::size_t> (clampedSamples) * sizeof (float));
        }
        return;
```

**Effect-chain path** — the final accumulate loop (currently):

```cpp
    for (int c = 0; c < activeChannels; ++c)
    {
        if (output[c] == nullptr) continue;

        const float* const proc = processedPtrs[c];
        for (int s = 0; s < clampedSamples; ++s)
            output[c][s] += proc[s];

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
```

becomes:

```cpp
    const float chainGain = muted_.load (std::memory_order_relaxed)
                                ? 0.0f
                                : gainLinear_.load (std::memory_order_relaxed);
    for (int c = 0; c < activeChannels; ++c)
    {
        const float* const proc = processedPtrs[c];
        if (output[c] != nullptr)
            for (int s = 0; s < clampedSamples; ++s)
                output[c][s] += proc[s] * chainGain;

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
```

> The wet-capture tap (the `if (wetSink_ != nullptr)` block) is left ABOVE this loop and reads `processedBuffer_` BEFORE gain is applied — preserving M8 S4's "post-effects, pre-fader" wet semantics. At default gain 1.0 the wet bytes are unchanged.

- [ ] **Step 5: Run the new tests + the existing Bus/OutputMixer suites**

Run: `cmake --build build --target SiriusTests && ./build/tests/SiriusTests "[bus]"`
Expected: PASS (new gain/mute/move cases + all pre-existing `[bus]` cases).

Run: `./build/tests/SiriusTests "[output-mixer]" && ./build/tests/SiriusTests "[input-mixer]"`
Expected: PASS — bit-identical default behavior; no regression.

- [ ] **Step 6: Commit + push**

```bash
git add engine/include/sirius/Bus.h engine/src/Bus.cpp tests/BusTests.cpp
git commit -m "feat: Bus post-fader gain + mute (parity with ChannelStrip)"
git push
```

---

### Task 3: `Bus` post-fader peak metering

**Files:**
- Modify: `engine/include/sirius/Bus.h` (public accessors after `muted()`; private mutable atomics after `muted_`)
- Modify: `engine/src/Bus.cpp` (compute peak in both accumulate loops; init-list order)
- Test: `tests/BusTests.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("Bus peak reflects the post-fader signal (inline path)", "[bus][meter]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setGain (0.5f);

    constexpr int kSamples = 8;
    for (int s = 0; s < kSamples; ++s)
    {
        bus.mixBufferChannel (0)[s] = 0.8f;   // pre-fader 0.8
        bus.mixBufferChannel (1)[s] = 0.4f;
    }
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);

    REQUIRE (bus.peakLeft()  == Approx (0.4f));   // 0.8 * 0.5
    REQUIRE (bus.peakRight() == Approx (0.2f));   // 0.4 * 0.5
}

TEST_CASE ("Bus peak reads silence when muted", "[bus][meter][mute]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    bus.setMuted (true);
    constexpr int kSamples = 8;
    for (int s = 0; s < kSamples; ++s)
    {
        bus.mixBufferChannel (0)[s] = 0.9f;
        bus.mixBufferChannel (1)[s] = 0.9f;
    }
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);

    REQUIRE (bus.peakLeft()  == 0.0f);
    REQUIRE (bus.peakRight() == 0.0f);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target SiriusTests 2>&1 | tail -20`
Expected: compile error — `'class sirius::Bus' has no member named 'peakLeft'`.

- [ ] **Step 3: Add the peak API + members to `Bus.h`**

In `public:`, immediately after `bool muted() const noexcept { ... }`, add:

```cpp
    /// Post-fader peak level for each side of the last processed block, in
    /// [0, ∞). Audio thread writes once per `process()`; the UI reads on its
    /// timer. A mono bus reports its peak on both sides (dual-mono). Parity
    /// with ChannelStrip<Audio>::peakLeft/peakRight.
    float peakLeft()  const noexcept { return peakLeft_.load (std::memory_order_relaxed); }
    float peakRight() const noexcept { return peakRight_.load (std::memory_order_relaxed); }
```

In `private:`, immediately after `std::atomic<bool> muted_ { false };`, add:

```cpp
    /// Post-fader meter — written from the const `process` (the bus is logically
    /// const there), so mutable. Audio-thread writes, UI reads.
    mutable std::atomic<float> peakLeft_  { 0.0f };
    mutable std::atomic<float> peakRight_ { 0.0f };
```

- [ ] **Step 4: Compute peak in `Bus.cpp` (both paths)**

In the **inline path** loop, track the per-channel post-fader peak and store after the loop. Replace the inline accumulate loop body from Task 2 with:

```cpp
        const float inlineGain = muted_.load (std::memory_order_relaxed)
                                     ? 0.0f
                                     : gainLinear_.load (std::memory_order_relaxed);
        float inlinePeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
        for (int c = 0; c < activeChannels; ++c)
        {
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;

            float p = 0.0f;
            for (int s = 0; s < clampedSamples; ++s)
            {
                const float v = mix[s] * inlineGain;
                p = std::max (p, std::fabs (v));
                if (output[c] != nullptr) output[c][s] += v;
            }
            inlinePeak[c] = p;

            std::memset (mix, 0,
                         static_cast<std::size_t> (clampedSamples) * sizeof (float));
        }
        peakLeft_.store  (inlinePeak[0], std::memory_order_relaxed);
        peakRight_.store (activeChannels > 1 ? inlinePeak[1] : inlinePeak[0],
                          std::memory_order_relaxed);
        return;
```

In the **effect-chain path** final loop, do the same against `proc`:

```cpp
    const float chainGain = muted_.load (std::memory_order_relaxed)
                                ? 0.0f
                                : gainLinear_.load (std::memory_order_relaxed);
    float chainPeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
    for (int c = 0; c < activeChannels; ++c)
    {
        const float* const proc = processedPtrs[c];
        float p = 0.0f;
        for (int s = 0; s < clampedSamples; ++s)
        {
            const float v = proc[s] * chainGain;
            p = std::max (p, std::fabs (v));
            if (output[c] != nullptr) output[c][s] += v;
        }
        chainPeak[c] = p;

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
    peakLeft_.store  (chainPeak[0], std::memory_order_relaxed);
    peakRight_.store (activeChannels > 1 ? chainPeak[1] : chainPeak[0],
                      std::memory_order_relaxed);
```

Update the move ctor init-list to include the peak atomics in declaration order (before `lufsMeter_`, which arrives in Task 4 — for now place them last):

```cpp
      gainLinear_ (other.gainLinear_.load (std::memory_order_relaxed)),
      muted_ (other.muted_.load (std::memory_order_relaxed)),
      peakLeft_ (other.peakLeft_.load (std::memory_order_relaxed)),
      peakRight_ (other.peakRight_.load (std::memory_order_relaxed))
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target SiriusTests && ./build/tests/SiriusTests "[bus]"`
Expected: PASS (peak cases + all prior). Also re-run `[output-mixer]` and `[input-mixer]` → PASS (output bits unchanged; only the meter side-effect is new).

- [ ] **Step 6: Commit + push**

```bash
git add engine/include/sirius/Bus.h engine/src/Bus.cpp tests/BusTests.cpp
git commit -m "feat: Bus post-fader peak metering"
git push
```

---

### Task 4: `Bus` LUFS meter + `prepare()`

**Files:**
- Modify: `engine/include/sirius/Bus.h` (`prepare` + `lufsIntegrated` API; `mutable LufsMeter lufsMeter_` member)
- Modify: `engine/src/Bus.cpp` (feed the meter in both paths; init-list)
- Test: `tests/BusTests.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("Bus LUFS reads silence floor before prepare()", "[bus][lufs]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    constexpr int kSamples = 64;
    for (int s = 0; s < kSamples; ++s)
    {
        bus.mixBufferChannel (0)[s] = 0.5f;
        bus.mixBufferChannel (1)[s] = 0.5f;
    }
    std::vector<float> l (kSamples, 0.0f), r (kSamples, 0.0f);
    float* out[2] = { l.data(), r.data() };
    bus.process (out, 2, kSamples);

    // Un-prepared meter self-no-ops → integrated stays at the silence floor.
    REQUIRE (bus.lufsIntegrated() <= -70.0f);
}

TEST_CASE ("Bus LUFS rises with a 1 kHz post-fader signal after prepare()", "[bus][lufs]")
{
    sirius::Bus bus (sirius::BusId { 1 }, sirius::BusConfig {});
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 512;
    bus.prepare (kSampleRate, kBlock);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float* out[2] = { l.data(), r.data() };
    double phase = 0.0;
    const double inc = 2.0 * M_PI * 1000.0 / kSampleRate;
    for (int i = 0; i < 300; ++i)   // ~3.2 s — past the integration ramp
    {
        // 1 kHz sine, NOT DC — K-weighting's high-pass would kill a constant.
        for (int s = 0; s < kBlock; ++s)
        {
            const float v = 0.5f * static_cast<float> (std::sin (phase));
            phase += inc;
            bus.mixBufferChannel (0)[s] = v;
            bus.mixBufferChannel (1)[s] = v;
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        bus.process (out, 2, kBlock);
    }
    REQUIRE (bus.lufsIntegrated() > -70.0f);
}
```

Add `#include <algorithm>` (`std::fill`) and `#include <cmath>` (`std::sin`/`M_PI`) to the test file if not present.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target SiriusTests 2>&1 | tail -20`
Expected: compile error — `'class sirius::Bus' has no member named 'prepare'` / `'lufsIntegrated'`.

- [ ] **Step 3: Add the LUFS API + member to `Bus.h`**

In `public:`, after the `peakRight()` accessor from Task 3, add:

```cpp
    /// Message-thread (off the audio thread) — prepares the loudness meter for
    /// the device sample rate / max block. Until called, the meter no-ops:
    /// `lufsIntegrated()` reads the silence floor and `process` skips the LUFS
    /// work. Parity with ChannelStrip<Audio>::prepare.
    void prepare (double sampleRate, int maxBlockSize) { lufsMeter_.prepare (sampleRate, maxBlockSize); }

    /// Integrated EBU R128 loudness (LUFS) — the LUFS half of the dual meter.
    /// UI reads on its timer.
    float lufsIntegrated() const noexcept { return lufsMeter_.getIntegrated(); }
```

In `private:`, after `peakRight_`, add:

```cpp
    /// LUFS half of the dual meter (OTTO parity). Fed the post-fader signal;
    /// self-no-ops until prepare() runs. Mutable because `process` is const.
    mutable LufsMeter lufsMeter_;
```

- [ ] **Step 4: Feed the meter in `Bus.cpp` (both paths)**

In the **inline path**, after the `peakRight_.store(...)` and before `return;`, add the LUFS feed over the post-fader mix channels. Because `mix` is zeroed at the end of each channel's loop, feed the meter from a captured planar pointer pair BEFORE the memset, OR re-order so the meter is fed before zeroing. Simplest: hoist the channel-0/1 base pointers and feed after the per-channel loop but BEFORE the memset. Restructure the inline path's per-channel loop to defer the memset:

```cpp
        const float inlineGain = muted_.load (std::memory_order_relaxed)
                                     ? 0.0f
                                     : gainLinear_.load (std::memory_order_relaxed);
        float inlinePeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
        for (int c = 0; c < activeChannels; ++c)
        {
            float* const mix = mixBuffer_.data()
                             + static_cast<std::size_t> (c) * kMaxBusMixSamples;
            float p = 0.0f;
            for (int s = 0; s < clampedSamples; ++s)
            {
                const float v = mix[s] * inlineGain;
                mix[s] = v;                              // post-fader, in place for the LUFS feed
                p = std::max (p, std::fabs (v));
                if (output[c] != nullptr) output[c][s] += v;
            }
            inlinePeak[c] = p;
        }
        peakLeft_.store  (inlinePeak[0], std::memory_order_relaxed);
        peakRight_.store (activeChannels > 1 ? inlinePeak[1] : inlinePeak[0],
                          std::memory_order_relaxed);

        {
            const float* const lufsL = mixBuffer_.data();
            const float* const lufsR = activeChannels > 1
                                           ? mixBuffer_.data() + kMaxBusMixSamples
                                           : lufsL;
            lufsMeter_.process (lufsL, lufsR, clampedSamples);   // mono → dual-mono
        }
        for (int c = 0; c < activeChannels; ++c)
            std::memset (mixBuffer_.data() + static_cast<std::size_t> (c) * kMaxBusMixSamples,
                         0, static_cast<std::size_t> (clampedSamples) * sizeof (float));
        return;
```

In the **effect-chain path**, `processedPtrs` already holds the post-fader signal once Task 3's loop multiplies in place — but Task 3 multiplied into a local `v` without writing back to `proc`. Update that loop to write `v` back to `proc` (so the LUFS feed sees post-fader), then feed the meter after the loop:

```cpp
    const float chainGain = muted_.load (std::memory_order_relaxed)
                                ? 0.0f
                                : gainLinear_.load (std::memory_order_relaxed);
    float chainPeak[kMaxBusChannelsHard] = { 0.0f, 0.0f };
    for (int c = 0; c < activeChannels; ++c)
    {
        float* const proc = processedPtrs[c];
        float p = 0.0f;
        for (int s = 0; s < clampedSamples; ++s)
        {
            const float v = proc[s] * chainGain;
            proc[s] = v;                                 // post-fader, in place for the LUFS feed
            p = std::max (p, std::fabs (v));
            if (output[c] != nullptr) output[c][s] += v;
        }
        chainPeak[c] = p;

        float* const mix = mixBuffer_.data()
                         + static_cast<std::size_t> (c) * kMaxBusMixSamples;
        std::memset (mix, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
    }
    peakLeft_.store  (chainPeak[0], std::memory_order_relaxed);
    peakRight_.store (activeChannels > 1 ? chainPeak[1] : chainPeak[0],
                      std::memory_order_relaxed);
    {
        const float* const lufsL = processedPtrs[0];
        const float* const lufsR = activeChannels > 1 ? processedPtrs[1] : lufsL;
        if (lufsL != nullptr) lufsMeter_.process (lufsL, lufsR, clampedSamples);
    }
```

> The wet-capture tap reads `processedBuffer_` BEFORE this loop, so it still captures the pre-fader post-effects signal (M8 S4 contract). Mutating `proc` to post-fader happens only after the wet tap.

Finally, append `lufsMeter_` to the move ctor init-list (LAST, matching declaration order):

```cpp
      peakLeft_ (other.peakLeft_.load (std::memory_order_relaxed)),
      peakRight_ (other.peakRight_.load (std::memory_order_relaxed)),
      lufsMeter_ (std::move (other.lufsMeter_))
```

- [ ] **Step 5: Run tests + the noexcept static_assert sanity**

Run: `cmake --build build --target SiriusTests && ./build/tests/SiriusTests "[bus]"`
Expected: PASS (LUFS cases + all prior `[bus]`).

Run: `./build/tests/SiriusTests "[output-mixer]" "[input-mixer]" "[mixer-graph]"`
Expected: PASS — no regression (default gain 1.0; un-prepared OutputMixer bus meters no-op).

- [ ] **Step 6: Commit + push**

```bash
git add engine/include/sirius/Bus.h engine/src/Bus.cpp tests/BusTests.cpp
git commit -m "feat: Bus post-fader LUFS meter + prepare() (dual-meter parity)"
git push
```

---

### Task 5: `InputMixer::busForId` accessor for the UI

**Files:**
- Modify: `engine/include/sirius/InputMixer.h` (declare after `setBusEffectChain`)
- Modify: `engine/src/InputMixer.cpp` (define near `setBusEffectChain`)
- Test: `tests/InputMixerTests.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/InputMixerTests.cpp`:

```cpp
TEST_CASE ("InputMixer::busForId returns the bus for a known id, nullptr otherwise",
           "[input-mixer][bus-access]")
{
    sirius::InputMixer mixer;
    const sirius::BusId id = mixer.addBus (sirius::BusConfig { 2, "Drum Bus", sirius::BusKind::Bus });

    sirius::Bus* bus = mixer.busForId (id);
    REQUIRE (bus != nullptr);
    REQUIRE (bus->id() == id);
    REQUIRE (bus->config().name == "Drum Bus");

    // Round-trip a control through the accessor (what the UI does).
    bus->setGain (0.3f);
    REQUIRE (mixer.busForId (id)->gain() == Approx (0.3f));

    REQUIRE (mixer.busForId (sirius::BusId { 9999 }) == nullptr);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target SiriusTests 2>&1 | tail -20`
Expected: compile error — `'class sirius::InputMixer' has no member named 'busForId'`.

- [ ] **Step 3: Declare + define `busForId`**

In `engine/include/sirius/InputMixer.h`, immediately after the `setBusEffectChain` declaration, add:

```cpp
    /// Message-thread accessor — the live Bus for `id`, or nullptr if unknown.
    /// Mirrors `processingChainFor(ChannelId)`. The Input Mixer UI uses this to
    /// drive a bus/FX-return strip's fader/mute and read its peak/LUFS meter.
    /// NOT for the audio thread (the bus is held by value in a reserved vector;
    /// the pointer is stable for the bus's lifetime within this mixer).
    Bus* busForId (BusId id) noexcept;
```

In `engine/src/InputMixer.cpp`, after the `setBusEffectChain` definition, add:

```cpp
Bus* InputMixer::busForId (BusId id) noexcept
{
    for (auto& bus : buses_)
        if (bus.id() == id)
            return &bus;
    return nullptr;
}
```

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target SiriusTests && ./build/tests/SiriusTests "[input-mixer][bus-access]"`
Expected: PASS.

- [ ] **Step 5: Commit + push**

```bash
git add engine/include/sirius/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::busForId accessor for the bus-strip UI"
git push
```

---

### Task 6: Full-suite verification + handoff

- [ ] **Step 1: Clean rebuild + full suite**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiriusTests
ctest --test-dir build
```
Expected: the new `[bus]`/`[lufs]`/`[input-mixer]` cases pass; total green except the documented `MainComponentPluginEditorTests_NOT_BUILT` (run separately by `bash/test-s7.sh`). Record the new count.

- [ ] **Step 2: Update `continue.md`**

Refresh the RESUME-HERE block: this engine slice shipped (Bus gain/mute/dual-meter + `InputMixer::busForId`), it is the **prerequisite for routing-graph Phase 6 UI**, and the next move is the Phase 6 UI plan (`writing-plans` → `subagent-driven-development`, operator-verified): blank-area long-press creation menu (Add bus / Add FX return / Add tape), Bus/FXReturn `CompactFaderStrip`s wired to `busForId(...)` (fader→`setGain`, mute→`setMuted`, meter←`peakLeft/peakRight`+`lufsIntegrated`, solo-in-place across all strips), and the per-strip destination picker rendered by `InputMixerPane` (NOT the vendored strip's hardware-output combo) wired to `setChannelMainOutTo{Bus,Tape,HardwareOutput}`. Note the bus-creation gesture must bracket the `addBus`/`addFxReturn` + graph mutation with `removeAudioCallback`/`addAudioCallback` (same pattern as `rebuildInputStrips`) and call `bus->prepare(sampleRate, maxBlock)` for the new bus's meter inside the bracket.

- [ ] **Step 3: Commit + push the handoff**

```bash
git add continue.md
git commit -m "docs: Bus controls engine slice shipped — handoff to Phase 6 UI"
git push
```

---

## Self-review notes

- **Spec coverage:** the spec line "Bus/FX-return strips get fader/mute/solo + the dual peak+LUFS meter like channel strips" is the requirement this plan satisfies at the engine layer (gain, mute, peak, LUFS, `prepare`). Solo is mixer/UI policy (effective-mute), not an engine concept — same as channel strips — so it's correctly absent here.
- **Type consistency:** accessor names match `ChannelStrip<Audio>` (`setGain`/`gain`/`setMuted`/`muted`/`peakLeft`/`peakRight`/`prepare`/`lufsIntegrated`) so the UI binds buses and channels through the same vocabulary. `busForId` mirrors `processingChainFor`.
- **OutputMixer untouched:** no change to `OutputMixer.{h,cpp}` storage or call sites; its buses inherit defaults (unity/unmuted/un-prepared) → audio output bit-identical, meter no-ops. Re-run `[output-mixer]` each task to keep that honest.
- **RT-safety:** `process` stays `const noexcept`, allocation-free, lock-free. New work is atomic load/store + a peak max-loop + `LufsMeter::process` (already audio-thread-safe in `ChannelStrip`). The existing noexcept `static_assert` on `Bus::process` (if present in `BusTests.cpp`) continues to hold; if absent, the build's `-Werror` + the suite cover it.
- **Move-ctor init-list order** is the one easy footgun — it MUST track declaration order across Tasks 2→3→4. `-Werror=reorder` catches a mismatch; the per-task code blocks already give the correct cumulative order.
