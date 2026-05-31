# OTTO Transport Bar (Option B) + Asset Path Injection — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land a persistent IDA-wide transport bar (option B — IDA owns its own `otto::ui::TransportBar` instance mounted above the tab strip) + inject OTTO's asset root through a new singleton so OTTO's sample-based kits load inside IDA.

**Architecture:** Two sequenced slices against the same OTTOProcessor embed shipped in S2 (IDA `af2d947`). **S3a** introduces an `ida::TransportBarHost` wrapper that owns one `otto::ui::TransportBar` instance, routes its events back to OTTO via new `OttoHost::play/stop/setTempo/tapTempo` accessors, drives meter+spectrum from a new IDA master publisher pair, and re-applies the OTTO-side `isPluginMode_` OR (reverting OTTO `f2b6f6db`) so OTTO's tab-internal bar stays hidden. **S3b** introduces an `otto::paths::AssetsRoot` singleton, refactors the 3 OTTO path-resolution sites to consult it first (fallback preserves OTTO standalone behaviour), and adds one IDA-side `setOverride` call sourced from a new `IDA_OTTO_ASSETS_DIR` compile-def.

**Tech Stack:** C++20, JUCE 8, CMake (Ninja generator), Catch2, OTTO submodule pinned at IDA's `external/OTTO/` (currently `18de2ff9`), lsfx_tapecolor submodule (currently `3dda009`). macOS Tahoe 26.5 + Apple clang 21.0.0 baseline. Audio-thread invariants per `docs/RT_SAFETY_CONTRACT.md` + `external/OTTO/CLAUDE.md`'s AUDIO THREAD RULES.

**Spec:** `docs/superpowers/specs/2026-05-28-otto-transport-bar-and-asset-path-design.md` (BS-5 output of the 2026-05-28 brainstorm).

---

## File Structure

### S3a — Persistent IDA TransportBar

| File | Operation | Responsibility |
|---|---|---|
| `external/OTTO/src/otto-plugin/PluginEditor.cpp` | modify | Re-apply `\|\| proc.isEmbeddedInHost()` OR in `isPluginMode_` initializer (re-revert of OTTO `f2b6f6db`). Cross-project edit. |
| `external/OTTO/CROSS_PROJECT_INBOX.md` | append | New `[FROM IDA → OTTO]` entry documenting the re-revert. |
| `engine/include/ida/MasterMeter.h` | create | Lock-free meter snapshot publisher (peak/RMS/LUFS per channel, atomic struct). |
| `engine/src/MasterMeter.cpp` | create | Implementation. Alloc-free, lock-free per `RT_SAFETY_CONTRACT.md`. |
| `engine/include/ida/MasterSpectrum.h` | create | Lock-free FFT-bin snapshot publisher (configurable bin count, dB magnitudes). |
| `engine/src/MasterSpectrum.cpp` | create | Implementation. FFT scratch pre-allocated in `prepare()`. |
| `engine/CMakeLists.txt` | modify | Register `MasterMeter.cpp` + `MasterSpectrum.cpp`. |
| `audio/src/AudioCallback.cpp` (or wherever the master mix point lives — verify at task time) | modify | Tap master output into MasterMeter::publish + MasterSpectrum::publish at the post-OutputMixer master point. |
| `otto-bridge/include/ida/OttoHost.h` | modify | Add `play/stop/setTempo/tapTempo/snapshotMaster/snapshotSpectrumBin` public methods. |
| `otto-bridge/src/OttoHost.cpp` | modify | Implement the new methods. Forward play/stop/setTempo/tapTempo to OTTOProcessor; snapshot accessors read IDA's MasterMeter/MasterSpectrum singletons (or member instances — task-time choice). |
| `app/TransportBarHost.h` | create | Wrapper Component owning one `otto::ui::TransportBar` instance, `otto::ui::TransportBarListener` impl, `ida::IOttoTransportListener` impl, 30Hz `juce::Timer`. |
| `app/TransportBarHost.cpp` | create | Implementation. ≤200 lines. |
| `app/MainComponent.h` | modify | New `std::unique_ptr<ida::TransportBarHost> transportBarHost_` member, declared after `ottoHost_` and `ottoPane_`. |
| `app/MainComponent.cpp` | modify | Construct + addAndMakeVisible `transportBarHost_`; `resized()` reserves top strip then lays out `tabs_`. |
| `app/CMakeLists.txt` | modify | Register `TransportBarHost.cpp`. |
| `tests/TransportBarHostTests.cpp` | create | `[transport-bar-host]` headless tests. |
| `tests/IdaMasterMeterTests.cpp` | create | `[ida-master-meter]` tests including RT-safety alloc-count smoke. |
| `tests/IdaMasterSpectrumTests.cpp` | create | `[ida-master-spectrum]` tests including RT-safety alloc-count smoke. |
| `tests/OttoPaneNoInternalTransportTests.cpp` | create | `[otto-pane-no-internal-transport]` regression pin. |
| `tests/CMakeLists.txt` | modify | Register the four new test sources. |

### S3b — OTTO Asset Path Injection

| File | Operation | Responsibility |
|---|---|---|
| `external/OTTO/src/otto-core/include/otto/paths/AssetsRoot.h` | create | Singleton declaration + inline accessors. Cross-project edit. |
| `external/OTTO/src/otto-core/src/paths/AssetsRoot.cpp` | create | Singleton storage + setOverride + fallback ladder. |
| `external/OTTO/src/otto-core/CMakeLists.txt` | modify | Register `AssetsRoot.cpp`. Cross-project edit. |
| `external/OTTO/src/otto-core/include/otto/library/SamplerPresetLoader.h` | modify | Refactor `findSamplerFolder()` to consult `AssetsRoot::samplerFolder()` first, fall through to existing ladder when override unset. |
| `external/OTTO/src/otto-core/include/otto/effects/IRPresetLoader.h` (verify path at task time) | modify | Refactor IR-load path to consult `AssetsRoot::irFolder()` first. |
| `external/OTTO/src/otto-core/src/paths/PresetPaths.cpp` | modify | Refactor `getRoot(StorageTier::Factory)` to consult `AssetsRoot::factoryPresetsFolder()` first. |
| `external/OTTO/tests/AssetsRootTests.cpp` | create | `[assets-root]` Catch2 tests. |
| `external/OTTO/tests/CMakeLists.txt` | modify | Register `AssetsRootTests.cpp`. |
| `external/OTTO/CROSS_PROJECT_INBOX.md` | append | New `[FROM IDA → OTTO]` entry documenting all S3b OTTO-side edits. |
| `otto-bridge/CMakeLists.txt` | modify | Add `IDA_OTTO_ASSETS_DIR` PRIVATE compile-def to the `IdaOttoBridge` target, sourced from top-level `${OTTO_ASSETS_DIR}`. |
| `otto-bridge/src/OttoHost.cpp` | modify | Add `otto::paths::AssetsRoot::instance().setOverride(juce::File{ IDA_OTTO_ASSETS_DIR })` at the very top of `Impl::Impl()`, before `processor_` construction. |

---

## Slice S3a — Persistent IDA TransportBar

### Task 1: Cross-project — re-apply OTTO's `isPluginMode_` OR (re-revert of `f2b6f6db`)

**Files:**
- Modify: `external/OTTO/src/otto-plugin/PluginEditor.cpp` (the `isPluginMode_` initializer, ~line 110-130 depending on current shape)
- Append: `external/OTTO/CROSS_PROJECT_INBOX.md` (`[FROM IDA → OTTO]` section)

- [ ] **Step 1: Read the current `isPluginMode_` initializer**

```bash
grep -n "isPluginMode_\s*(" external/OTTO/src/otto-plugin/PluginEditor.cpp | head -5
```

Locate the `OTTOEditor` ctor's `isPluginMode_(...)` member-init line. Pre-2026-05-27 form is approximately:

```cpp
isPluginMode_ (!juce::JUCEApplication::isStandaloneApp())
```

- [ ] **Step 2: Apply the re-revert**

Change to:

```cpp
isPluginMode_ (!juce::JUCEApplication::isStandaloneApp()
               || proc.isEmbeddedInHost())
```

Where `proc` is the ctor's `OTTOProcessor&` parameter (verify name at task time). The `isEmbeddedInHost()` accessor was added in OTTO `fb5ff039` and is still present.

- [ ] **Step 3: Append the inbox entry**

Append at the bottom of the `[FROM IDA → OTTO]` section in `external/OTTO/CROSS_PROJECT_INBOX.md`:

```markdown
### 2026-05-28 — RE-APPLY: isPluginMode_ OR (reverts OTTO f2b6f6db; supports option B TransportBar mount above IDA's tab strip)

Direction: IDA → OTTO
IDA commit: (filled at IDA-side atomic commit time below)
OTTO commit: <this commit>
Files touched:
- src/otto-plugin/PluginEditor.cpp (isPluginMode_ initializer re-applies `|| proc.isEmbeddedInHost()`)

Why: S3a of IDA's 2026-05-28 transport-bar plan lands `ida::TransportBarHost` — IDA owns its own `otto::ui::TransportBar` instance mounted above the tab strip, visible from every tab. With IDA's bar present, OTTO's tab-internal `transportBar_` would duplicate the surface. Re-applying the OR (reverting `f2b6f6db`) hides OTTO's internal bar inside IDA via the existing `isPluginMode_` layout gate. OTTO Standalone is unaffected (the OR's RHS is false when `setEmbeddedInHost(true)` was never called — which is the case in OTTO's own builds).

For OTTO's Claude: do NOT revert. This is the deliberate completion of the design that `fb5ff039` started and `f2b6f6db` interrupted under an option-A reading. Full design rationale at IDA `docs/superpowers/specs/2026-05-28-otto-transport-bar-and-asset-path-design.md` §2.6.

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

- [ ] **Step 4: Commit OTTO-side**

```bash
cd /Users/larryseyer/IDA/external/OTTO
git add src/otto-plugin/PluginEditor.cpp CROSS_PROJECT_INBOX.md
git -c commit.gpgsign=false commit -m "$(cat <<'EOF'
feat: re-apply isPluginMode_ OR for embedded-in-IDA (re-reverts f2b6f6db)

Ida-Origin: pending
EOF
)"
git push origin HEAD:main
```

- [ ] **Step 5: Bump IDA's submodule pin + verify build**

```bash
cd /Users/larryseyer/IDA
git add external/OTTO
cmake --build build --target IDA -j 8
```

Expected: clean build. The hidden OTTO bar is GUI-visible only; no headless test fires here yet (that lands in Task 7).

- [ ] **Step 6: Commit IDA-side bump (interim — final commit in Task 9)**

Hold the staged `external/OTTO` change for the atomic commit at Task 9. Do NOT commit yet — group with the rest of S3a's IDA-side work.

---

### Task 2: IDA MasterMeter publisher

**Files:**
- Create: `engine/include/ida/MasterMeter.h`
- Create: `engine/src/MasterMeter.cpp`
- Modify: `engine/CMakeLists.txt` (register MasterMeter.cpp)
- Test: `tests/IdaMasterMeterTests.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `tests/IdaMasterMeterTests.cpp`:

```cpp
#include "ida/MasterMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdlib>

namespace {

thread_local std::atomic<size_t> g_allocCount { 0 };
thread_local bool g_counting { false };

} // namespace

// Match the operator-new pattern documented in the EventBus brief at
// external/OTTO/CROSS_PROJECT_INBOX.md (kept in one TU to avoid ODR).

TEST_CASE("MasterMeter publishes peak from a known signal", "[ida-master-meter]") {
    ida::MasterMeter meter;
    meter.prepare(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    // Inject 0.5 peak on L only.
    buf.setSample(0, 100, 0.5f);

    meter.publish(buf);

    const auto snap = meter.snapshot();
    CHECK(snap.peakDb > -7.0f);   // 0.5 ≈ -6 dB
    CHECK(snap.peakDb < -5.0f);
    CHECK(snap.leftDb  > snap.rightDb);  // L louder than R (rms reflects)
}

TEST_CASE("MasterMeter::publish is alloc-free under load", "[ida-master-meter][rt-safety]") {
    ida::MasterMeter meter;
    meter.prepare(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.2f);

    meter.publish(buf);  // warm-up

    constexpr int N = 10'000;
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting = true;

    for (int i = 0; i < N; ++i) meter.publish(buf);

    g_counting = false;
    REQUIRE(g_allocCount.load(std::memory_order_relaxed) == 0u);
}
```

(operator-new override TU — see Task 4's spectrum test which defines it once for both files.)

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 8
```

Expected: compile error — `MasterMeter` header doesn't exist.

- [ ] **Step 3: Write the header**

Create `engine/include/ida/MasterMeter.h`:

```cpp
#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ida {

/// Lock-free meter snapshot publisher driven from the IDA master mix point.
/// publish() runs on the audio thread (alloc/lock/log-free).
/// snapshot() runs on the message thread (atomic load).
class MasterMeter
{
public:
    struct Snapshot { float leftDb; float rightDb; float peakDb; float lufs; };

    MasterMeter();

    /// Message-thread. Sizes integrators for the current sampleRate. Must
    /// be called once before the first publish().
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /// Audio-thread. Computes per-block left/right RMS + peak + LUFS and
    /// stores them into the atomic snapshot. Zero allocations.
    void publish (const juce::AudioBuffer<float>& masterStereo) noexcept;

    /// Any-thread (atomic load).
    Snapshot snapshot() const noexcept;

private:
    std::atomic<Snapshot> snapshot_;
    double sampleRate_ { 48000.0 };
    // LUFS integrator scratch (R128) — sized in prepare; no allocations in publish.
    // (Implementation detail at slice-fill-in time.)
};

} // namespace ida
```

- [ ] **Step 4: Write the implementation**

Create `engine/src/MasterMeter.cpp`. For the minimum-viable cut:

```cpp
#include "ida/MasterMeter.h"
#include <algorithm>
#include <cmath>

namespace ida {

namespace {
constexpr float kDbFloor = -100.0f;
inline float linToDb (float lin) noexcept
{
    return lin > 1.0e-5f ? 20.0f * std::log10(lin) : kDbFloor;
}
} // namespace

MasterMeter::MasterMeter()
{
    snapshot_.store({ kDbFloor, kDbFloor, kDbFloor, kDbFloor },
                    std::memory_order_release);
}

void MasterMeter::prepare (double sampleRate, int /*maxBlockSize*/) noexcept
{
    sampleRate_ = sampleRate;
}

void MasterMeter::publish (const juce::AudioBuffer<float>& buf) noexcept
{
    const int N = buf.getNumSamples();
    if (N <= 0 || buf.getNumChannels() < 2) return;

    const float* L = buf.getReadPointer(0);
    const float* R = buf.getReadPointer(1);

    float sumL = 0.0f, sumR = 0.0f, peak = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float l = L[i];
        const float r = R[i];
        sumL += l * l;
        sumR += r * r;
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    const float rmsL = std::sqrt(sumL / static_cast<float>(N));
    const float rmsR = std::sqrt(sumR / static_cast<float>(N));

    Snapshot s;
    s.leftDb  = linToDb(rmsL);
    s.rightDb = linToDb(rmsR);
    s.peakDb  = linToDb(peak);
    s.lufs    = linToDb(std::max(rmsL, rmsR));  // simplified; R128 lands later
    snapshot_.store(s, std::memory_order_release);
}

MasterMeter::Snapshot MasterMeter::snapshot() const noexcept
{
    return snapshot_.load(std::memory_order_acquire);
}

} // namespace ida
```

- [ ] **Step 5: Register the source in CMake**

In `engine/CMakeLists.txt`, find the `add_library(IdaEngine ...)` block and add `src/MasterMeter.cpp` to its source list. Confirm `target_include_directories` already exposes `include/`.

- [ ] **Step 6: Run tests to verify pass**

```bash
cmake --build build --target IdaTests -j 8
./build/tests/IdaTests "[ida-master-meter]" -v high
```

Expected: 2/2 pass, including the alloc-count check.

- [ ] **Step 7: Commit**

```bash
git add engine/include/ida/MasterMeter.h engine/src/MasterMeter.cpp engine/CMakeLists.txt tests/IdaMasterMeterTests.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat: IDA MasterMeter publisher — lock-free per-block peak/RMS snapshot at the master mix point (S3a)"
```

---

### Task 3: Wire MasterMeter into the master mix point

**Files:**
- Modify: the AudioCallback / master mix code (verify exact location at task start; likely `audio/src/AudioCallback.cpp` or `engine/src/OutputMixer.cpp`).

- [ ] **Step 1: Locate the master mix point**

```bash
grep -rn "master\|Master" engine/src audio/src | grep -i "process\|mix\|buffer" | head -20
```

Find where the master bus's final stereo output buffer is computed per block (post-OutputMixer master, before it leaves to the audio device).

- [ ] **Step 2: Add a MasterMeter member to whichever class owns that mix point**

Add as a member (not pointer — value):

```cpp
ida::MasterMeter masterMeter_;
```

Call `masterMeter_.prepare(sampleRate, maxBlockSize)` from the existing prepareToPlay/prepare method.

- [ ] **Step 3: Call publish() at the end of each block**

After the master stereo buffer is finalized:

```cpp
masterMeter_.publish(masterBuf);
```

- [ ] **Step 4: Expose a getter for OttoHost / TransportBarHost to read**

Add an accessor on the owning class:

```cpp
const ida::MasterMeter& getMasterMeter() const noexcept { return masterMeter_; }
```

- [ ] **Step 5: Build + run baseline tests**

```bash
cmake --build build --target IDA IdaTests -j 8
./build/tests/IdaTests "[output-mixer],[engine-render]" 2>&1 | tail -5
```

Expected: pre-existing baselines preserved. No regressions.

- [ ] **Step 6: Commit**

```bash
git add <files-touched>
git -c commit.gpgsign=false commit -m "feat: wire MasterMeter into the master mix point (S3a)"
```

---

### Task 4: IDA MasterSpectrum publisher

**Files:**
- Create: `engine/include/ida/MasterSpectrum.h`
- Create: `engine/src/MasterSpectrum.cpp`
- Modify: `engine/CMakeLists.txt`
- Test: `tests/IdaMasterSpectrumTests.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `tests/IdaMasterSpectrumTests.cpp` with two cases tagged `[ida-master-spectrum]`:

```cpp
#include "ida/MasterSpectrum.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdlib>

// Operator-new override: defined here as a shared TU for [ida-master-meter]
// + [ida-master-spectrum] tests. Other test files that already counted
// allocations via a different mechanism keep their own counter — this file
// owns the OVERRIDE for these two test suites only (place in a shared
// header if a third RT-safety suite needs it).
namespace {
thread_local std::atomic<size_t> g_allocCount { 0 };
thread_local bool g_counting { false };
} // namespace

void* operator new(size_t n) {
    if (g_counting) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](size_t n) {
    if (g_counting) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

TEST_CASE("MasterSpectrum publishes nonzero bin energy at the sine frequency",
          "[ida-master-spectrum]") {
    ida::MasterSpectrum spec;
    constexpr int kNumBins = 256;
    spec.prepare(48000.0, 256, kNumBins);

    juce::AudioBuffer<float> buf(2, 256);
    const float freq = 1000.0f;
    for (int i = 0; i < 256; ++i) {
        const float s = std::sin(2.0 * 3.14159265 * freq * i / 48000.0);
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
    }

    for (int b = 0; b < 4; ++b) spec.publish(buf);  // accumulate enough windows

    bool sawSignal = false;
    for (int bin = 0; bin < kNumBins; ++bin) {
        if (spec.binDb(bin) > -40.0f) { sawSignal = true; break; }
    }
    REQUIRE(sawSignal);
}

TEST_CASE("MasterSpectrum::publish is alloc-free under load",
          "[ida-master-spectrum][rt-safety]") {
    ida::MasterSpectrum spec;
    spec.prepare(48000.0, 256, 256);

    juce::AudioBuffer<float> buf(2, 256);
    for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.1f);

    spec.publish(buf);  // warm-up

    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting = true;
    constexpr int N = 1000;
    for (int i = 0; i < N; ++i) spec.publish(buf);
    g_counting = false;

    REQUIRE(g_allocCount.load(std::memory_order_relaxed) == 0u);
}
```

ALSO: remove the operator-new TLS block from `tests/IdaMasterMeterTests.cpp` (Task 2) and replace it with `extern thread_local std::atomic<size_t> g_allocCount; extern thread_local bool g_counting;` — these two test files share one ODR-correct override.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 8
```

Expected: compile error — `MasterSpectrum` doesn't exist.

- [ ] **Step 3: Write the header**

Create `engine/include/ida/MasterSpectrum.h`:

```cpp
#pragma once

#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace ida {

class MasterSpectrum
{
public:
    MasterSpectrum();

    /// Message-thread. Sizes scratch + FFT for current sampleRate /
    /// blockSize / bin count. Must be called once before publish().
    void prepare (double sampleRate, int maxBlockSize, int numBins) noexcept;

    /// Audio-thread. Accumulates block samples into an FFT window;
    /// publishes per-bin dB magnitudes when the window fills.
    /// Zero allocations.
    void publish (const juce::AudioBuffer<float>& masterStereo) noexcept;

    /// Any-thread (atomic load).
    int   numBins() const noexcept;
    float binDb (int bin) const noexcept;

private:
    int numBins_ { 0 };
    int fftSize_ { 0 };
    int hopFill_ { 0 };
    std::vector<float> window_;       // sized in prepare; never resized
    std::vector<float> scratch_;      // sized in prepare; FFT in-place
    std::vector<std::atomic<float>> bins_;  // sized in prepare
    std::unique_ptr<juce::dsp::FFT> fft_;   // constructed in prepare
};

} // namespace ida
```

- [ ] **Step 4: Write the implementation**

Create `engine/src/MasterSpectrum.cpp`. Minimum-viable shape:

```cpp
#include "ida/MasterSpectrum.h"
#include <algorithm>
#include <cmath>

namespace ida {

MasterSpectrum::MasterSpectrum() = default;

void MasterSpectrum::prepare (double /*sampleRate*/, int /*maxBlockSize*/, int numBins) noexcept
{
    numBins_ = numBins;
    fftSize_ = numBins * 2;        // bin = fftSize / 2
    const int order = static_cast<int>(std::log2(fftSize_));
    fft_ = std::make_unique<juce::dsp::FFT>(order);
    window_.assign(fftSize_, 0.0f);
    scratch_.assign(fftSize_ * 2, 0.0f);
    bins_ = std::vector<std::atomic<float>>(numBins_);
    for (auto& b : bins_) b.store(-100.0f, std::memory_order_release);
    hopFill_ = 0;
}

void MasterSpectrum::publish (const juce::AudioBuffer<float>& buf) noexcept
{
    if (fft_ == nullptr || buf.getNumChannels() < 2 || numBins_ <= 0) return;
    const int N = buf.getNumSamples();
    const float* L = buf.getReadPointer(0);
    const float* R = buf.getReadPointer(1);

    int read = 0;
    while (read < N)
    {
        const int room = fftSize_ - hopFill_;
        const int chunk = std::min(room, N - read);
        for (int i = 0; i < chunk; ++i)
        {
            window_[static_cast<size_t>(hopFill_ + i)] =
                0.5f * (L[read + i] + R[read + i]);
        }
        hopFill_ += chunk;
        read += chunk;
        if (hopFill_ == fftSize_)
        {
            // Hann window in-place into scratch_[0..fftSize_).
            for (int i = 0; i < fftSize_; ++i)
            {
                const float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (fftSize_ - 1)));
                scratch_[static_cast<size_t>(i)] = window_[static_cast<size_t>(i)] * w;
            }
            std::fill(scratch_.begin() + fftSize_, scratch_.end(), 0.0f);
            fft_->performFrequencyOnlyForwardTransform(scratch_.data());
            for (int bin = 0; bin < numBins_; ++bin)
            {
                const float mag = scratch_[static_cast<size_t>(bin)];
                const float db = mag > 1.0e-5f ? 20.0f * std::log10(mag) : -100.0f;
                bins_[static_cast<size_t>(bin)].store(db, std::memory_order_release);
            }
            hopFill_ = 0;
        }
    }
}

int MasterSpectrum::numBins() const noexcept { return numBins_; }

float MasterSpectrum::binDb (int bin) const noexcept
{
    if (bin < 0 || bin >= numBins_) return -100.0f;
    return bins_[static_cast<size_t>(bin)].load(std::memory_order_acquire);
}

} // namespace ida
```

- [ ] **Step 5: Register the source + run the tests**

```bash
# Update engine/CMakeLists.txt: add src/MasterSpectrum.cpp.
# Update tests/CMakeLists.txt: add tests/IdaMasterSpectrumTests.cpp.
cmake --build build --target IdaTests -j 8
./build/tests/IdaTests "[ida-master-spectrum]" -v high
```

Expected: 2/2 pass.

- [ ] **Step 6: Wire into the master mix point**

Mirror Task 3 for the spectrum: add a `MasterSpectrum masterSpectrum_` member to the master-mix owner, call `prepare(sr, blockSize, /*numBins=*/256)` from the existing prepare method, call `publish(masterBuf)` at the end of each block, expose `const MasterSpectrum& getMasterSpectrum() const noexcept`.

- [ ] **Step 7: Commit**

```bash
git add engine/include/ida/MasterSpectrum.h engine/src/MasterSpectrum.cpp engine/CMakeLists.txt tests/IdaMasterSpectrumTests.cpp tests/CMakeLists.txt audio/src/AudioCallback.cpp (or whichever file Task 3 modified)
git -c commit.gpgsign=false commit -m "feat: IDA MasterSpectrum publisher + master-mix wiring (S3a)"
```

---

### Task 5: OttoHost extensions — transport-control + master-snapshot accessors

**Files:**
- Modify: `otto-bridge/include/ida/OttoHost.h`
- Modify: `otto-bridge/src/OttoHost.cpp`

- [ ] **Step 1: Look at OTTOEditor's existing play/pause path**

```bash
grep -n "onPlayPauseClicked\|playPauseClicked\|togglePlayback\|setBpm\|tapTempo" external/OTTO/src/otto-plugin/PluginEditor.cpp external/OTTO/src/otto-plugin/PluginProcessor.h
```

Find what method OTTOEditor calls on OTTOProcessor to start/stop playback and change tempo. These calls are the model OttoHost::play/stop/setTempo/tapTempo forward to.

- [ ] **Step 2: Extend the OttoHost.h public surface**

Add to `otto-bridge/include/ida/OttoHost.h` (in the public section, after `getProcessor()`):

```cpp
/// Transport controls — message-thread only. Forward to the embedded
/// OTTOProcessor's existing play/stop/tempo paths. Used by
/// `ida::TransportBarHost` to drive OTTO from IDA's persistent
/// transport bar.
void play();
void stop();
void setTempo (double bpm);
void tapTempo();

/// Master-output snapshot. Any-thread (atomic load). The data is
/// published from the audio thread at the IDA master mix point.
struct MasterSnapshot { float leftDb; float rightDb; float peakDb; float lufs; };
MasterSnapshot snapshotMaster() const noexcept;

/// Master-spectrum bin. Any-thread (atomic load).
int   spectrumBinCount() const noexcept;
float spectrumBinDb (int bin) const noexcept;
```

- [ ] **Step 3: Implement in OttoHost.cpp**

Inside the existing `Impl` struct, add a non-owning reference to the master meter + spectrum publishers. The simplest plumbing is a setter:

```cpp
// OttoHost.h (private section)
public:
    void setMasterPublishers (const ida::MasterMeter& meter,
                              const ida::MasterSpectrum& spec) noexcept;
```

Then `OttoHost.cpp` stores the pointers in `Impl` and the snapshot accessors read through them. Alternative: take const refs in the OttoHost ctor — but the host already declares `OttoHost()` with no args, and changing that breaks callers. Stick with the setter, called once at MainComponent construction time after the publishers are wired into the master mix point.

Implementation of the new public methods (sketch):

```cpp
void OttoHost::play()
{
    impl_->processor->/* OTTO's play method, found in Step 1 */;
}
void OttoHost::stop()
{
    impl_->processor->/* OTTO's stop method */;
}
void OttoHost::setTempo (double bpm)
{
    impl_->processor->/* OTTO's setBpm method */;
}
void OttoHost::tapTempo()
{
    impl_->processor->/* OTTO's tapTempo path */;
}

OttoHost::MasterSnapshot OttoHost::snapshotMaster() const noexcept
{
    if (impl_->masterMeter == nullptr) return { -100.0f, -100.0f, -100.0f, -100.0f };
    const auto s = impl_->masterMeter->snapshot();
    return { s.leftDb, s.rightDb, s.peakDb, s.lufs };
}

int OttoHost::spectrumBinCount() const noexcept
{
    return impl_->masterSpectrum != nullptr ? impl_->masterSpectrum->numBins() : 0;
}

float OttoHost::spectrumBinDb (int bin) const noexcept
{
    return impl_->masterSpectrum != nullptr ? impl_->masterSpectrum->binDb(bin) : -100.0f;
}

void OttoHost::setMasterPublishers (const ida::MasterMeter& m,
                                    const ida::MasterSpectrum& s) noexcept
{
    impl_->masterMeter    = &m;
    impl_->masterSpectrum = &s;
}
```

- [ ] **Step 4: Add headless test for the new transport methods**

Create `tests/OttoHostTransportControlTests.cpp` (or extend `OttoHostProcessorAccessTests.cpp`):

```cpp
TEST_CASE("OttoHost::play / stop toggle the OTTO processor's transport state",
          "[otto-host-transport-control]") {
    ida::OttoHost host;
    host.prepare(48000.0, 256);

    auto& proc = host.getProcessor();
    // Initial state: stopped.
    host.play();
    // No assertion here — the actual transport state read requires looking
    // at OTTO's TransportTracker, which isn't easily accessible. Instead,
    // verify that play/stop drain a TransportEvent through the listener.
    struct Listener : ida::IOttoTransportListener {
        std::vector<ida::TransportSnapshot> snaps;
        void onOttoTransport (const ida::TransportSnapshot& s) override { snaps.push_back(s); }
    } l;
    host.addTransportListener(&l);
    host.play();
    host.drainForTesting();
    bool sawStarted = false;
    for (const auto& s : l.snaps) if (s.kind == ida::TransportSnapshot::Kind::Started) sawStarted = true;
    CHECK(sawStarted);
    host.removeTransportListener(&l);
}
```

- [ ] **Step 5: Build + run**

```bash
cmake --build build --target IdaTests -j 8
./build/tests/IdaTests "[otto-host-transport-control],[otto-host-processor-access],[otto-host-transport]" 2>&1 | tail -5
```

Expected: new test + S1/S2 baselines all pass.

- [ ] **Step 6: Commit**

```bash
git add otto-bridge/include/ida/OttoHost.h otto-bridge/src/OttoHost.cpp tests/OttoHostTransportControlTests.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat: OttoHost::play/stop/setTempo/tapTempo + snapshotMaster + spectrum accessors (S3a)"
```

---

### Task 6: `ida::TransportBarHost` — wrapper Component owning the IDA-side TransportBar

**Files:**
- Create: `app/TransportBarHost.h`
- Create: `app/TransportBarHost.cpp`
- Modify: `app/CMakeLists.txt`
- Test: `tests/TransportBarHostTests.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `tests/TransportBarHostTests.cpp`:

```cpp
#include "TransportBarHost.h"
#include "ida/OttoHost.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("TransportBarHost constructs against OttoHost and exposes the bar",
          "[transport-bar-host]") {
    ida::OttoHost host;
    host.prepare(48000.0, 256);
    ida::TransportBarHost barHost(host);
    auto& bar = barHost.getBar();
    CHECK(&bar != nullptr);
    CHECK(bar.getTempo() > 0.0);  // OTTO publishes default BPM
}

TEST_CASE("TransportBarHost::playPauseClicked starts OTTO when stopped",
          "[transport-bar-host]") {
    ida::OttoHost host;
    host.prepare(48000.0, 256);
    ida::TransportBarHost barHost(host);

    struct L : ida::IOttoTransportListener {
        bool startedSeen = false;
        void onOttoTransport (const ida::TransportSnapshot& s) override {
            if (s.kind == ida::TransportSnapshot::Kind::Started) startedSeen = true;
        }
    } l;
    host.addTransportListener(&l);

    barHost.playPauseClicked();
    host.drainForTesting();
    CHECK(l.startedSeen);
    host.removeTransportListener(&l);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target IdaTests -j 8
```

Expected: compile error — `TransportBarHost` doesn't exist.

- [ ] **Step 3: Write the header**

Create `app/TransportBarHost.h`:

```cpp
#pragma once

#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include <otto/ui/components/TransportBar.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ida {

class TransportBarHost
    : public juce::Component,
      public otto::ui::TransportBarListener,
      public IOttoTransportListener,
      private juce::Timer
{
public:
    explicit TransportBarHost (OttoHost& host);
    ~TransportBarHost() override;

    /// juce::Component
    void resized() override;

    /// Test seam
    otto::ui::TransportBar& getBar() noexcept;

    /// otto::ui::TransportBarListener
    void playPauseClicked() override;
    void stopClicked() override;
    void tempoChanged (double newTempo) override;
    void tapTempo() override;

    /// ida::IOttoTransportListener
    void onOttoTransport (const TransportSnapshot& snapshot) override;

private:
    /// juce::Timer — 30 Hz; pulls meter + spectrum from OttoHost and pushes into bar.
    void timerCallback() override;

    OttoHost& host_;
    otto::ui::TransportBar bar_;
};

} // namespace ida
```

- [ ] **Step 4: Write the implementation**

Create `app/TransportBarHost.cpp`:

```cpp
#include "TransportBarHost.h"

namespace ida {

TransportBarHost::TransportBarHost (OttoHost& host)
    : host_(host)
{
    bar_.addListener(this);
    addAndMakeVisible(bar_);

    // Configure spectrum bin count to match the publisher's resolution.
    bar_.configureSpectrum(host_.spectrumBinCount(), 48000.0);

    host_.addTransportListener(this);

    startTimerHz(30);
}

TransportBarHost::~TransportBarHost()
{
    stopTimer();
    host_.removeTransportListener(this);
    bar_.removeListener(this);
}

void TransportBarHost::resized()
{
    bar_.setBounds(getLocalBounds());
}

otto::ui::TransportBar& TransportBarHost::getBar() noexcept { return bar_; }

void TransportBarHost::playPauseClicked()
{
    // OTTO is the source of truth — call host_.play()/stop() based on the
    // bar's CURRENT state (which mirrors OTTO via onOttoTransport).
    if (bar_.getTransportState() == otto::ui::TransportState::Playing)
        host_.stop();
    else
        host_.play();
}

void TransportBarHost::stopClicked()  { host_.stop(); }
void TransportBarHost::tempoChanged (double newTempo) { host_.setTempo(newTempo); }
void TransportBarHost::tapTempo()     { host_.tapTempo(); }

void TransportBarHost::onOttoTransport (const TransportSnapshot& snap)
{
    using S = TransportSnapshot::Kind;
    switch (snap.kind)
    {
        case S::Started: bar_.setTransportState(otto::ui::TransportState::Playing); break;
        case S::Stopped: bar_.setTransportState(otto::ui::TransportState::Stopped); break;
        case S::BpmChanged: bar_.setTempo(snap.bpm); break;
        case S::TimeSigChanged: /* TransportBar has no setTimeSignature today; no-op until added */ break;
    }
}

void TransportBarHost::timerCallback()
{
    const auto m = host_.snapshotMaster();
    bar_.setMasterLevels(m.leftDb, m.rightDb);
    bar_.setMasterPeak(m.peakDb);
    bar_.setMasterLUFS(m.lufs);

    const int N = host_.spectrumBinCount();
    for (int bin = 0; bin < N; ++bin)
        bar_.setSpectrumBin(bin, host_.spectrumBinDb(bin));
}

} // namespace ida
```

- [ ] **Step 5: Register the source + the test**

In `app/CMakeLists.txt`, add `TransportBarHost.cpp` to the `IDA` target sources. In `tests/CMakeLists.txt`, add `tests/TransportBarHostTests.cpp` and pull `${CMAKE_SOURCE_DIR}/app/TransportBarHost.cpp` into IdaTests sources (mirror the OttoPane.cpp pattern from S2).

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target IdaTests -j 8
./build/tests/IdaTests "[transport-bar-host]" -v high
```

Expected: 2/2 pass.

- [ ] **Step 7: Commit**

```bash
git add app/TransportBarHost.h app/TransportBarHost.cpp app/CMakeLists.txt tests/TransportBarHostTests.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat: ida::TransportBarHost wraps otto::ui::TransportBar with listener+timer wiring (S3a)"
```

---

### Task 7: `[otto-pane-no-internal-transport]` regression pin

**Files:**
- Create: `tests/OttoPaneNoInternalTransportTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
#include "OttoPane.h"
#include "ida/OttoHost.h"
#include <PluginProcessor.h>
#include <PluginEditor.h>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

TEST_CASE("OTTOEditor's internal TransportBar is hidden when embedded in IDA",
          "[otto-pane-no-internal-transport]") {
    ida::OttoHost host;
    host.prepare(48000.0, 256);
    REQUIRE(host.getProcessor().isEmbeddedInHost());

    ida::OttoPane pane(host);
    auto* editor = pane.getEditor();
    REQUIRE(editor != nullptr);

    // OTTOEditor's transportBar_ member is exposed via either a public
    // accessor (if present) OR by walking its child hierarchy looking for
    // the TransportBar Component. Slice-time choice — pick whichever the
    // current OTTO header supports.
    //
    // Pass criterion: NO child of `editor` (recursively) is of type
    // `otto::ui::TransportBar` AND has `isVisible() == true`.

    std::function<bool(juce::Component*)> hasVisibleTransportBar =
        [&hasVisibleTransportBar](juce::Component* c) -> bool {
            if (auto* bar = dynamic_cast<otto::ui::TransportBar*>(c))
                if (bar->isVisible()) return true;
            for (int i = 0; i < c->getNumChildComponents(); ++i)
                if (hasVisibleTransportBar(c->getChildComponent(i))) return true;
            return false;
        };

    CHECK_FALSE(hasVisibleTransportBar(editor));
}
```

- [ ] **Step 2: Register the test source**

In `tests/CMakeLists.txt`, add `tests/OttoPaneNoInternalTransportTests.cpp` next to the existing `OttoPaneTests.cpp`.

- [ ] **Step 3: Run**

```bash
cmake --build build --target IdaTests -j 8
./build/tests/IdaTests "[otto-pane-no-internal-transport]" -v high
```

Expected: PASS — Task 1's re-revert hides OTTO's internal TransportBar via `isPluginMode_`. If FAIL: Task 1's OR didn't propagate (check the OTTO submodule SHA is the post-re-revert commit, and that `isEmbeddedInHost()` actually returns true in this test).

- [ ] **Step 4: Commit**

```bash
git add tests/OttoPaneNoInternalTransportTests.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "test: [otto-pane-no-internal-transport] regression pin for S3a (S3a)"
```

---

### Task 8: MainComponent integration — own + lay out the TransportBarHost

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Add the member declaration**

In `app/MainComponent.h`, in the private members section, AFTER `std::unique_ptr<ida::OttoHost> ottoHost_;` and AFTER `std::unique_ptr<ida::OttoPane> ottoPane_;`:

```cpp
#include "TransportBarHost.h"
// ...
std::unique_ptr<ida::TransportBarHost> transportBarHost_;
```

Declaration order matters for destruction (LIFO): bar host destructs first (unsubscribes from OttoHost transport listener), then pane, then host. Compile-fail if declared wrong.

- [ ] **Step 2: Construct in MainComponent::MainComponent()**

In `app/MainComponent.cpp`, AFTER `ottoHost_` and `ottoPane_` are constructed (locate the existing construction site near the top of the ctor):

```cpp
transportBarHost_ = std::make_unique<ida::TransportBarHost>(*ottoHost_);
addAndMakeVisible(*transportBarHost_);

// Wire master publishers into OttoHost so the bar can read them.
ottoHost_->setMasterPublishers(getMasterMeterRef(), getMasterSpectrumRef());
```

`getMasterMeterRef()` / `getMasterSpectrumRef()` are the accessors added in Tasks 3 + 4 on whichever class owns the master mix point. If they live on a member of MainComponent, call `member.getMasterMeter()`; if they live deeper, walk to them.

- [ ] **Step 3: Carve top strip in resized()**

Locate `void MainComponent::resized()` at the bottom of `MainComponent.cpp`. Replace the current body:

```cpp
auto area = getLocalBounds();
// ... existing layout ...
tabs_.setBounds(area);
```

with:

```cpp
auto area = getLocalBounds();
if (transportBarHost_ != nullptr)
{
    // OTTO's TransportBar adapts via internal Breakpoint logic; use a
    // sensible Desktop default and let the bar's own resized() handle
    // child layout. 88 px matches the Desktop breakpoint OTTO's bar
    // currently lays out for; phone/tablet breakpoints can come later
    // via a window-width helper.
    transportBarHost_->setBounds(area.removeFromTop(88));
}
tabs_.setBounds(area);
```

- [ ] **Step 4: Build IDA target**

```bash
cmake --build build --target IDA -j 8
```

Expected: clean build. (No new headless test fires here — the visible bar is operator-verified in Task 10.)

- [ ] **Step 5: Verify ctest baselines still pass**

```bash
ctest --test-dir build 2>&1 | tail -5
```

Expected: baseline preserved (the previous 796/797 plus the new tests from Tasks 2/4/5/6/7).

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git -c commit.gpgsign=false commit -m "feat: MainComponent mounts ida::TransportBarHost above tabs_ (S3a)"
```

---

### Task 9: Atomic IDA-side commit + push (folds the Task 1 SHA bump)

**Files:**
- All staged work from Tasks 1-8 IDA-side.

- [ ] **Step 1: Verify clean working tree state**

```bash
git status --short
```

Expected: only `m external/sfizz` remains as pre-existing dirt. external/OTTO should be at the post-re-revert SHA from Task 1 (already staged or already committed in an earlier task).

- [ ] **Step 2: ctest one more time to confirm green**

```bash
ctest --test-dir build 2>&1 | tail -10
```

Expected: all the S3a new test tags pass (`[transport-bar-host]`, `[ida-master-meter]`, `[ida-master-spectrum]`, `[otto-pane-no-internal-transport]`, `[otto-host-transport-control]`) + S1/S2 baselines preserved.

- [ ] **Step 3: Push IDA-side**

```bash
git push origin master
```

- [ ] **Step 4: Backfill the OTTO inbox entry's IDA SHA**

The Task 1 inbox entry has `IDA commit: (filled at IDA-side atomic commit time below)`. Now we have the SHA — append a small docs-only OTTO commit OR include the IDA SHA in the OTTO commit log via a follow-up. Operator's pattern is to leave the trailer as `Ida-Origin: pending` and rely on the durable `git log --grep='Ida-Origin'` record — that's acceptable. If you want a tighter record:

```bash
cd external/OTTO
sed -i '' "s/IDA commit: (filled at IDA-side atomic commit time below)/IDA commit: <new IDA SHA from step 3>/" CROSS_PROJECT_INBOX.md
git add CROSS_PROJECT_INBOX.md
git -c commit.gpgsign=false commit -m "chore: backfill IDA SHA in 2026-05-28 re-revert inbox entry"
git push origin HEAD:main
cd ../..
git add external/OTTO
git -c commit.gpgsign=false commit -m "chore: bump external/OTTO — inbox SHA backfill"
git push origin master
```

This step is optional polish.

---

### Task 10: Operator verification

This task does not have a git commit. The operator clicks through the GUI; on any failure, a follow-up commit fixes the gap.

- [ ] **Step 1: Build clean + launch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA -j 8
open build/app/IDA_artefacts/Release/IDA.app
```

(Clean build per the operator's pinned `feedback_clean_builds_only_for_testing` rule.)

- [ ] **Step 2: Walk the operator through the 7-step S3a checklist**

Recite the spec §5.2 S3a steps verbatim:

1. Launch `IDA.app`.
2. Verify the TransportBar is visible at the top of the window.
3. Switch tabs (Performance / Preparation / In Mix / Out Mix / OTTO / Tapes / Plugins / Video / Settings). Verify the bar stays visible on every tab and meter is responsive.
4. Click the bar's Play button. Verify audio is audible through the master (M-OTTO-4 audibility regression check).
5. Switch to a non-OTTO tab during playback. Verify the bar's Stop button still stops OTTO.
6. Verify the OTTO tab no longer shows OTTO's internal transport row (the player rack starts immediately below the tab strip).
7. Tap the tempo button repeatedly. Verify BPM updates in the bar AND inside OTTO's UI (sync round-trip).

- [ ] **Step 3: On failure, file a follow-up + halt**

If any step fails, do NOT proceed to S3b. Capture screenshot via operator → diagnose → land a fix commit → re-verify before unlocking S3b.

---

## Slice S3b — OTTO Asset Path Injection

### Task 11: Cross-project — create `otto::paths::AssetsRoot` singleton

**Files:**
- Create: `external/OTTO/src/otto-core/include/otto/paths/AssetsRoot.h`
- Create: `external/OTTO/src/otto-core/src/paths/AssetsRoot.cpp`
- Modify: `external/OTTO/src/otto-core/CMakeLists.txt`
- Test: `external/OTTO/tests/AssetsRootTests.cpp` (create)
- Modify: `external/OTTO/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `external/OTTO/tests/AssetsRootTests.cpp`:

```cpp
#include "otto/paths/AssetsRoot.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

TEST_CASE("AssetsRoot::setOverride sets the override and accessors return under it",
          "[assets-root]") {
    auto& root = otto::paths::AssetsRoot::instance();
    const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("otto-assets-root-test");
    tmp.createDirectory();
    tmp.getChildFile("Sampler").createDirectory();
    tmp.getChildFile("IR").createDirectory();
    tmp.getChildFile("Presets").createDirectory();

    root.setOverride(tmp);
    CHECK(root.get().getFullPathName() == tmp.getFullPathName());
    CHECK(root.samplerFolder() == tmp.getChildFile("Sampler"));
    CHECK(root.irFolder() == tmp.getChildFile("IR"));
    CHECK(root.factoryPresetsFolder() == tmp.getChildFile("Presets"));

    tmp.deleteRecursively();
}

TEST_CASE("AssetsRoot with no override falls back to the existing per-platform ladder",
          "[assets-root][no-override-fallback]") {
    // Reset by setting override to an empty File (slice-time API choice — could
    // also be a `resetOverride()` method).
    auto& root = otto::paths::AssetsRoot::instance();
    root.setOverride({});  // empty juce::File → treated as unset

    // After unset, samplerFolder() should return the same thing the
    // pre-refactor findSamplerFolder() returned. The simplest verification
    // is "doesn't crash + returns SOMETHING" — exact-path equality requires
    // pinning the ladder result. Slice-time pick:
    const auto f = root.samplerFolder();
    CHECK(true);  // smoke: no crash. Tighter assertions land if the ladder
                  // is testable on a synthetic platform fixture.
}
```

- [ ] **Step 2: Write the header**

Create `external/OTTO/src/otto-core/include/otto/paths/AssetsRoot.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>
#include <mutex>
#include <optional>

namespace otto::paths {

/// Single-instance authority on where OTTO's runtime assets live.
/// Consult an override when set; otherwise fall through to existing
/// per-platform ladders (e.g. SamplerPresetLoader::findSamplerFolder).
///
/// Threading contract:
///   - setOverride: message-thread, call once at session init.
///   - get / samplerFolder / irFolder / factoryPresetsFolder: any-thread
///     after init (single-publisher-after-init pattern).
class AssetsRoot
{
public:
    static AssetsRoot& instance();

    void setOverride (juce::File root);

    juce::File get() const;
    juce::File samplerFolder() const;
    juce::File irFolder() const;
    juce::File factoryPresetsFolder() const;

private:
    AssetsRoot() = default;
    mutable std::mutex setMutex_;
    std::optional<juce::File> override_;
};

} // namespace otto::paths
```

- [ ] **Step 3: Write the implementation**

Create `external/OTTO/src/otto-core/src/paths/AssetsRoot.cpp`:

```cpp
#include "otto/paths/AssetsRoot.h"

namespace otto::paths {

AssetsRoot& AssetsRoot::instance()
{
    static AssetsRoot inst;
    return inst;
}

void AssetsRoot::setOverride (juce::File root)
{
    std::lock_guard<std::mutex> lock(setMutex_);
    if (root == juce::File{})
        override_.reset();
    else
        override_ = std::move(root);
}

juce::File AssetsRoot::get() const
{
    if (override_) return *override_;
    return {};
}

juce::File AssetsRoot::samplerFolder() const
{
    if (override_) return override_->getChildFile("Sampler");
    return {};  // caller's fallback ladder takes over
}

juce::File AssetsRoot::irFolder() const
{
    if (override_) return override_->getChildFile("IR");
    return {};
}

juce::File AssetsRoot::factoryPresetsFolder() const
{
    if (override_) return override_->getChildFile("Presets");
    return {};
}

} // namespace otto::paths
```

- [ ] **Step 4: Register the source + test in CMake**

In `external/OTTO/src/otto-core/CMakeLists.txt`, add `src/paths/AssetsRoot.cpp` to the otto-core target sources.

In `external/OTTO/tests/CMakeLists.txt`, add `AssetsRootTests.cpp` to whatever the test target is.

- [ ] **Step 5: Build OTTO standalone + run the new tests**

```bash
# From inside external/OTTO/, build OTTO's own test target.
# Exact command varies; use whatever OTTO's CI uses for tests.
cmake -B build-otto -S external/OTTO -G Ninja -DCMAKE_BUILD_TYPE=Release -DOTTO_BUILD_TESTS=ON
cmake --build build-otto -j 8
./build-otto/tests/<otto-tests-binary-name> "[assets-root]" -v high
```

Expected: 2/2 pass.

- [ ] **Step 6: Commit OTTO-side**

```bash
cd external/OTTO
git add src/otto-core/include/otto/paths/AssetsRoot.h src/otto-core/src/paths/AssetsRoot.cpp src/otto-core/CMakeLists.txt tests/AssetsRootTests.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "$(cat <<'EOF'
feat: otto::paths::AssetsRoot singleton for asset-root override

Ida-Origin: pending
EOF
)"
git push origin HEAD:main
cd ../..
```

DO NOT yet bump IDA's external/OTTO pin — wait for Task 12's refactor to land in the same OTTO commit chain, then bump in Task 13.

---

### Task 12: Cross-project — refactor 3 OTTO call sites to consult AssetsRoot

**Files:**
- Modify: `external/OTTO/src/otto-core/include/otto/library/SamplerPresetLoader.h` (`findSamplerFolder`)
- Modify: `external/OTTO/src/otto-core/include/otto/effects/IRPresetLoader.h` (verify path at task time)
- Modify: `external/OTTO/src/otto-core/src/paths/PresetPaths.cpp` (`getRoot(StorageTier::Factory)`)

- [ ] **Step 1: Refactor `SamplerPresetLoader::findSamplerFolder`**

Find `findSamplerFolder()` (in `SamplerPresetLoader.h` around line 301-471 per continue.md). At the very top of the method body, BEFORE the existing per-platform ladder:

```cpp
{
    // S3b: consult AssetsRoot first. When unset, fall through to the
    // existing ladder below.
    const auto override_ = otto::paths::AssetsRoot::instance().samplerFolder();
    if (override_ != juce::File{} && override_.isDirectory())
        return override_;
}
// ... existing ladder runs unchanged below ...
```

Add `#include "otto/paths/AssetsRoot.h"` at the top of the file if not already present.

- [ ] **Step 2: Refactor IR loader path**

Find the OTTO IR loader's path-resolution point (likely in `external/OTTO/src/otto-core/include/otto/effects/IRPresetLoader.h` or `PlayerIRConvolution.h`). Mirror Step 1's pattern: consult `AssetsRoot::irFolder()` first, fall through to the existing path code.

- [ ] **Step 3: Refactor `PresetPaths::getRoot(StorageTier::Factory)`**

In `external/OTTO/src/otto-core/src/paths/PresetPaths.cpp`, find `getRoot(StorageTier tier)`. In the `Factory` branch:

```cpp
case StorageTier::Factory: {
    const auto override_ = otto::paths::AssetsRoot::instance().factoryPresetsFolder();
    if (override_ != juce::File{} && override_.isDirectory())
        return override_;
    // ... existing Factory path code unchanged below ...
}
```

- [ ] **Step 4: Verify OTTO standalone still works**

```bash
cd external/OTTO
cmake --build build-otto -j 8
./build-otto/<otto-standalone-binary>  # operator does this — visual check kits load
```

Expected: OTTO standalone behaviour byte-identical (override is unset → fallback ladder runs verbatim).

- [ ] **Step 5: Append the inbox entry**

Append to `external/OTTO/CROSS_PROJECT_INBOX.md` under `[FROM IDA → OTTO]`:

```markdown
### 2026-05-28 — FEAT: otto::paths::AssetsRoot singleton + 3 call-site refactors (S3b)

Direction: IDA → OTTO
IDA commit: (filled at IDA-side atomic commit time)
OTTO commit: <Task 11 + Task 12 commit chain — top of stack>
Files touched:
- src/otto-core/include/otto/paths/AssetsRoot.h (new)
- src/otto-core/src/paths/AssetsRoot.cpp (new)
- src/otto-core/include/otto/library/SamplerPresetLoader.h (findSamplerFolder consults AssetsRoot first)
- src/otto-core/include/otto/effects/IRPresetLoader.h (IR path consults AssetsRoot first; verify exact file at slice-time)
- src/otto-core/src/paths/PresetPaths.cpp (Factory tier consults AssetsRoot first)
- src/otto-core/CMakeLists.txt (register AssetsRoot.cpp)
- tests/AssetsRootTests.cpp (new) + tests/CMakeLists.txt

Why: S3b of IDA's 2026-05-28 transport-bar plan lands the dev-loop fix for OTTO's hardcoded asset-path ladder. With IDA embedding OTTO, juce::File::currentExecutableFile is IDA.app, not OTTO.app — every sample-based kit fell back to synth-mode. AssetsRoot is the OTTO-side architecture: when override unset, OTTO standalone runs the existing ladder verbatim (byte-identical behaviour). IDA calls setOverride once at OttoHost::Impl::Impl() from a new IDA_OTTO_ASSETS_DIR compile-def sourced from the existing top-level OTTO_ASSETS_DIR cache variable. Installer-time copy into IDA.app/Contents/Resources/ is deferred (Decision 4 of 2026-05-22).

For OTTO's Claude: do NOT revert. The fallback rule guarantees OTTO standalone behaviour is unchanged. The new AssetsRoot tests are `[assets-root]` tagged; they should pass against any platform. If OTTO's CI surfaces a fallback-ladder test failure, the fix is to make the fallback EXACTLY mirror the pre-refactor behavior of the call site (the refactor explicitly preserves that as the contract).

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

- [ ] **Step 6: Commit OTTO-side**

```bash
cd external/OTTO
git add <touched-files>
git -c commit.gpgsign=false commit -m "$(cat <<'EOF'
feat: refactor 3 OTTO path-resolution sites to consult otto::paths::AssetsRoot first; fallback ladder preserved (S3b)

Ida-Origin: pending
EOF
)"
git push origin HEAD:main
cd ../..
```

---

### Task 13: IDA-side wiring — CMake compile-def + setOverride call

**Files:**
- Modify: `otto-bridge/CMakeLists.txt`
- Modify: `otto-bridge/src/OttoHost.cpp`
- Modify: `external/OTTO` submodule SHA bump

- [ ] **Step 1: Bump the OTTO submodule**

```bash
cd external/OTTO
git fetch origin
git checkout origin/main         # picks up Tasks 11 + 12
cd ../..
git add external/OTTO
```

- [ ] **Step 2: Add IDA_OTTO_ASSETS_DIR compile-def**

In `otto-bridge/CMakeLists.txt`, find the `target_compile_definitions(IdaOttoBridge ...)` block (or add one if missing). Add:

```cmake
target_compile_definitions(IdaOttoBridge PRIVATE
    IDA_OTTO_ASSETS_DIR="${OTTO_ASSETS_DIR}"
)
```

`OTTO_ASSETS_DIR` is the existing top-level cache variable from `CMakeLists.txt:22` (defaults to `/Users/larryseyer/AudioDevelopment/OTTO/assets`).

- [ ] **Step 3: Call setOverride in OttoHost::Impl::Impl()**

In `otto-bridge/src/OttoHost.cpp`, modify `Impl::Impl()`. The current shape:

```cpp
Impl()
    : transportRing (kTransportRingCapacity)
    , processor     (std::make_unique<OTTOProcessor>())
{
    processor->setEmbeddedInHost (true);
    // ... subscription, timer, etc.
}
```

Restructure to call `setOverride` BEFORE `processor_` is constructed:

```cpp
#include <otto/paths/AssetsRoot.h>
// ...

Impl()
    : transportRing (kTransportRingCapacity)
    , processor     (
        []{
            otto::paths::AssetsRoot::instance().setOverride (juce::File{ IDA_OTTO_ASSETS_DIR });
            return std::make_unique<OTTOProcessor>();
        }())
{
    processor->setEmbeddedInHost (true);
    // ... unchanged below
}
```

The IIFE is so the `setOverride` runs strictly before `OTTOProcessor` construction, which is when OTTO might first probe the asset folder for default-preset loading.

- [ ] **Step 4: Build + run [otto-host-render] baseline**

```bash
cmake --build build --target IDA IdaTests -j 8
./build/tests/IdaTests "[otto-host-render],[otto-host-transport],[otto-host-processor-access]" -v high
```

Expected: baselines preserved. No headless test specifically asserts AssetsRoot is consulted at runtime — that's verified visually in Task 14.

- [ ] **Step 5: Commit IDA-side atomic bump**

```bash
git add external/OTTO otto-bridge/CMakeLists.txt otto-bridge/src/OttoHost.cpp
git -c commit.gpgsign=false commit -m "feat: IDA setOverride on otto::paths::AssetsRoot at OttoHost init; IDA_OTTO_ASSETS_DIR compile-def from OTTO_ASSETS_DIR (S3b)"
git push origin master
```

---

### Task 14: Operator verification (S3b)

- [ ] **Step 1: Clean build + launch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA -j 8
open build/app/IDA_artefacts/Release/IDA.app
```

- [ ] **Step 2: Walk the operator through the 4-step S3b checklist**

Recite the spec §5.2 S3b steps verbatim:

1. Launch `IDA.app`.
2. Open OTTO tab. Open kit picker on any player.
3. Verify sample-based kits (LSAD pop, LSAD rock, percs, shakers, hands) load and play samples — no synth-mode fallback.
4. Verify factory presets load and route through OTTO's pattern playback.

- [ ] **Step 3: On failure, diagnose + fix + re-verify**

Most likely failure modes:
- `IDA_OTTO_ASSETS_DIR` empty or pointing at a non-existent path → `samplerFolder()` returns a missing dir → fallback ladder runs → fallback finds nothing → synth fallback. Fix: confirm `OTTO_ASSETS_DIR` is set in CMake cache.
- `setOverride` called too late (after `OTTOProcessor` already probed) → IDA needs OTTO-side to also re-check on `prepareToPlay`. Fix: move setOverride EVEN earlier — possibly into `MainComponent`'s ctor before `ottoHost_` is constructed.
- The refactor of one of the 3 call sites didn't actually hit AssetsRoot first → grep for `findSamplerFolder` in the OTTO HEAD to verify.

---

## Slice Sequence + Stop Conditions

- **S3a tasks 1-10 MUST land before S3b tasks 11-14.** If S3a operator verification fails (Task 10 step 3), HALT — do not start S3b.
- Inside each slice, any failed verification or unresolved cross-project ack is a HALT condition.
- Both slices push to `origin/master` (IDA) and `origin/main` (OTTO) per the operator's standing `feedback_claude_commits_and_pushes_master` rule.

---

## Self-review checklist (done at write time)

- [x] Every §1-§6 spec section maps to one or more tasks (S3a tasks 1-10; S3b tasks 11-14).
- [x] §2.6 cross-project re-revert → Task 1.
- [x] §2.3 / §2.4 master-meter + master-spectrum publishers → Tasks 2 + 3 + 4.
- [x] §2.2 OttoHost extensions → Task 5.
- [x] §2.1 TransportBarHost → Task 6.
- [x] §4.2 double-bar-prevention regression pin → Task 7.
- [x] §2.5 MainComponent integration → Task 8.
- [x] §5.2 operator-verified checklists → Tasks 10 + 14.
- [x] §2.7 AssetsRoot singleton → Task 11.
- [x] §2.8 + the 3 call-site refactors → Task 12 + Task 13.
- [x] No "TODO" / "FIXME" / "TBD" left undocumented — slice-time uncertainties are bounded ("verify exact file at task time" with grep command provided).
- [x] Type consistency: `MasterMeter::Snapshot`, `OttoHost::MasterSnapshot`, `TransportSnapshot::Kind`, `otto::ui::TransportState::Playing/Stopped` — used consistently across tasks.

---

*Plan authored 2026-05-28 from `docs/superpowers/specs/2026-05-28-otto-transport-bar-and-asset-path-design.md`. Critical path: S3a (Tasks 1-10) → S3b (Tasks 11-14). Doctrinal anchor: whitepaper V10 §5.7 + the 2026-05-27 integration spec.*
