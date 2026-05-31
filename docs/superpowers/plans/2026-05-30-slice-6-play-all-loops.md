# Slice 6 — Play All Loops of a Phrase — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** A phrase's Output-Mixer channel must mix **every** leaf-loop the phrase owns, played simultaneously (a phrase is a stack of loops), not just its first leaf-loop. This lifts the T0b known limit recorded in the design spec §8.5 ("today the Output Mixer phrase strip prefetches only the phrase's *first* leaf-loop tape"). Operator-visible success: a phrase with two layered loops (e.g. the demo verse: rhythm tape 2 + lead tape 3) plays BOTH.

**Architecture:** The outbound engine is **already loop-centric**. `RenderPipeline::activeReadsAt` (`engine/src/RenderPipeline.cpp:50-102`) walks the Constituent tree depth-first and emits **one `ActiveRead` per sounding leaf-loop**, each keyed by that loop's own `ConstituentId` (`ar.loop`). `PlaybackResolver::resolveOnce` (`engine/src/PlaybackResolver.cpp:27-36`) maps each `ar.loop → slot` via the injected `slotFor_` callback and publishes one active snapshot entry per loop. The audio callback's `renderPlaybackStep` (`audio/src/AudioCallback.cpp:36-72`) pulls each active slot's prefetcher into a destination scratch.

The bottleneck is **entirely in `MainComponent`**, in two places:

1. **Wrong key + single loop (the bug).** `refreshOutputMixerPhraseChannels` (`app/MainComponent.cpp:7105`) opens **one** `TapePrefetcher` per *phrase pill*, using `resolveLoopTapeInfo(pillId)` which descends to the **first** leaf-loop only (`app/MainComponent.cpp:6065-6084`), and registers the slot under the **pill (phrase-wrapper) ConstituentId**: `slotByConstituent_[cid.value()] = slot` (line 7249). But the resolver looks up `slotFor_(ar.loop)` where `ar.loop` is the **leaf-loop** id (`app/MainComponent.cpp:4462-4467`). A pill id (a phrase wrapper, e.g. demo verse wrapper id 51, or a bare phrase id 10) is **never** equal to a leaf-loop id (21, 22, 11). So `slotFor_(loopId)` returns -1 and the bound slot is never marked active. This is the raw form of the T0b limit: the slot map is keyed by the wrong id *and* only one loop is opened.

2. **One scratch per channel, overwrite semantics.** `TapePrefetcher::pull` (`audio/src/TapePrefetcher.cpp:55-71`) **overwrites** its destination (it zero-fills underrun, it does not accumulate). `renderPlaybackStep` calls `ps.pre->pull(ps.l, ps.r, n)` straight into the channel's phrase scratch. Two loops cannot share one scratch under overwrite.

**The fix (decided after reading the code, not hand-waved):** keep **one OutputMixer channel per phrase** (preserves the pill↔strip 1:1 UI contract and the 32-channel `kMaxOutputChannels` cap — N-channels-per-phrase would multiply strips and exhaust the cap), but:

- Allocate **one prefetcher + one slot per leaf-loop**, keyed in `slotByConstituent_` by the **leaf-loop's** `ConstituentId` (fixes bug #1: the resolver now matches). All loops of a phrase point their slot at the **same** phrase-channel scratch (the channel the resolver-independent `phraseChannelByConstituent_` already owns).
- Change `renderPlaybackStep` to be **additive within a destination scratch** (fixes bug #2): zero every distinct bound destination once per block, then for each active slot pull into a small **per-slot temp** and **add** into the slot's destination. The per-slot temp is pre-allocated (`Bus::kMaxBusMixSamples = 8192` per side, `kMaxPhraseSlots = 64` slots) so there is no hot-path allocation. Stereo-only throughout (L/R parallel buffers).

A pure tree→loop-list selector (`core`) is extracted so the multi-loop enumeration is headless-TDD'd; the audible end-to-end is operator-verified.

**Tech Stack:** C++/JUCE; `core` (JUCE-free) for the pure loop-enumeration selector; `audio` (`AudioCallback`, `TapePrefetcher`); `app` (`MainComponent`); Catch2 (`IdaTests`, flat `tests/`, `tests/CMakeLists.txt`); CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §8.5.

**Dependencies:** Slices 1–5 (phrases exist with potentially multiple loops; the per-phrase state machine + command layer create them). This slice consumes the resulting Constituent tree. No new external libs. RT-safety contract: `docs/RT_SAFETY_CONTRACT.md` (no alloc/lock/IO/throw on `renderPlaybackStep`, which runs on the audio thread; lock-free hand-off via the existing `ActiveReadsPublisher` seqlock is unchanged).

---

## Cross-cutting rules (this slice)

- **Engine/pure logic is TDD'd headless** in `IdaTests`. The multi-loop *enumeration* and the *additive playback step* are pure/unit-testable and MUST have failing-test-first coverage. The actual audible multi-loop playback in the running `.app` is **operator-verified** — never fake it as a unit test; use the numbered operator step.
- **RT-safety:** `renderPlaybackStep` stays `noexcept`, no alloc/lock/IO. The new per-slot temp buffers are pre-allocated in the `AudioCallback` ctor. Verify with the existing `[rt-safety]` allocation-counting test pattern (`tests/TapePlaybackTests.cpp:575`).
- **Stereo only** (`docs/IDA_Whitepaper_V10.md` §6.1 hard invariant) — every buffer is an L/R pair; no mono path is introduced.
- **Clean build (`rm -rf build`) before the operator hand-off** (`[[feedback_clean_builds_only_for_testing]]`). The implementing agent builds AND launches the app for the operator and gives terse numbered test steps.
- **Single-line commits**, trailer `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`. Each task commits. (Coordinator handles push per this session's protocol — do not push from inside the plan unless told.)
- **Surgical:** do not refactor the resolver, the prefetcher ring, or the OutputMixer routing graph. They are correct as-is. Touch only the loop-enumeration source-construction in `MainComponent`, the `renderPlaybackStep` summing, and the new `core` selector + its tests.

---

## Task 1 — Pure leaf-loop enumeration selector (`core`)

Extract the "given a phrase/pill ConstituentId, find every leaf-loop under it (with its tape reference)" logic into a **pure, JUCE-free** function in `core` so it is headless-testable and reusable. Today this logic is buried in `MainComponent::resolveLoopTapeInfo` (a private member that returns only the *first* leaf and depends on `juce::File` — untestable).

**Files:**
- Create: `core/include/ida/LeafLoopSelector.h`
- Create: `core/src/LeafLoopSelector.cpp`
- Create: `tests/LeafLoopSelectorTests.cpp`
- Modify: `tests/CMakeLists.txt` (add `LeafLoopSelectorTests.cpp` to the `IdaTests` sources list, alphabetical-ish near `RenderPipelineTests.cpp`)
- Modify: `core/CMakeLists.txt` (add `src/LeafLoopSelector.cpp` to the `core` target sources)

- [ ] **Step 1: Write the failing tests** in `tests/LeafLoopSelectorTests.cpp`.

```cpp
// Tests for ida::collectLeafLoops — the pure tree walk that lifts the T0b
// "first leaf-loop only" limit. Given a phrase (or any) Constituent id, it
// returns EVERY leaf-loop in that subtree (a phrase is a stack of loops),
// each with its tape reference, in depth-first order. JUCE-free.
#include "ida/LeafLoopSelector.h"

#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TapeReference.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::collectLeafLoops;
using ida::Constituent;
using ida::ConstituentId;
using ida::LeafLoop;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TapeReference;

namespace
{
    std::shared_ptr<const Constituent> makeLoop (std::int64_t id, std::int64_t tape,
                                                 Rational length)
    {
        const Constituent loop { ConstituentId (id), Position(), Position (length) };
        return std::make_shared<const Constituent> (
            loop.withTapeReference (TapeReference (TapeId (tape), Rational (0), length)));
    }
}

TEST_CASE ("collectLeafLoops on a missing id returns empty", "[leaf-loops]")
{
    const Constituent root { ConstituentId (1), Position(), Position (Rational (4)) };
    CHECK (collectLeafLoops (root, ConstituentId (999)).empty());
}

TEST_CASE ("a bare loop is its own single leaf", "[leaf-loops]")
{
    // Phrase wrapper (id 10) containing one loop (id 11, tape 1).
    const Constituent shell =
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withChildAdded (makeLoop (11, 1, Rational (3)));
    const Constituent root =
        Constituent (ConstituentId (1), Position(), Position (Rational (3)))
            .withChildAdded (std::make_shared<const Constituent> (shell));

    const auto loops = collectLeafLoops (root, ConstituentId (10));
    REQUIRE (loops.size() == 1);
    CHECK (loops[0].loopId == ConstituentId (11));
    CHECK (loops[0].tape == TapeId (1));
    CHECK (loops[0].sliceLengthWholeNotes == Rational (3));
}

TEST_CASE ("a phrase with two layered loops yields BOTH (the slice-6 case)",
           "[leaf-loops]")
{
    // The demo verse shape: phrase id 20 = layer of loop 21 (tape 2) + loop 22 (tape 3).
    const Constituent verse =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withChildAdded (makeLoop (21, 2, Rational (6)))
            .withChildAdded (makeLoop (22, 3, Rational (3)));
    const Constituent root =
        Constituent (ConstituentId (1), Position(), Position (Rational (6)))
            .withChildAdded (std::make_shared<const Constituent> (verse));

    const auto loops = collectLeafLoops (root, ConstituentId (20));
    REQUIRE (loops.size() == 2);
    CHECK (loops[0].loopId == ConstituentId (21));   // depth-first order
    CHECK (loops[0].tape   == TapeId (2));
    CHECK (loops[1].loopId == ConstituentId (22));
    CHECK (loops[1].tape   == TapeId (3));
    CHECK (loops[1].sliceLengthWholeNotes == Rational (3));
}

TEST_CASE ("querying a loop id directly returns that loop", "[leaf-loops]")
{
    const Constituent verse =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withChildAdded (makeLoop (21, 2, Rational (6)))
            .withChildAdded (makeLoop (22, 3, Rational (3)));
    const auto loops = collectLeafLoops (verse, ConstituentId (21));
    REQUIRE (loops.size() == 1);
    CHECK (loops[0].loopId == ConstituentId (21));
}

TEST_CASE ("nested wrappers are walked to their leaves", "[leaf-loops]")
{
    // wrapper(51) -> sharedPhrase(20) -> { loop21, loop22 }. Querying the
    // wrapper must reach both leaves through the shared phrase.
    const Constituent shared =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withChildAdded (makeLoop (21, 2, Rational (6)))
            .withChildAdded (makeLoop (22, 3, Rational (3)));
    const Constituent wrapper =
        Constituent (ConstituentId (51), Position(), Position (Rational (6)))
            .withChildAdded (std::make_shared<const Constituent> (shared));

    const auto loops = collectLeafLoops (wrapper, ConstituentId (51));
    REQUIRE (loops.size() == 2);
    CHECK (loops[0].loopId == ConstituentId (21));
    CHECK (loops[1].loopId == ConstituentId (22));
}
```

- [ ] **Step 2: Run the tests, verify they FAIL.**
  Run: `cmake --build build --target IdaTests` — expected: **compile failure** (`LeafLoopSelector.h` does not exist). That is the failing state for a new pure module.

- [ ] **Step 3: Write the header** `core/include/ida/LeafLoopSelector.h` (minimal, JUCE-free; mirrors the `ActiveRead`/`TapeReference` style already in `core`).

```cpp
#pragma once

#include "ida/ConstituentId.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"

#include <vector>

namespace ida
{

class Constituent;

/// One leaf-loop discovered under a queried phrase: which loop Constituent,
/// which tape it reads, and its slice length (whole notes). Lifts the T0b
/// "first leaf-loop only" limit — `collectLeafLoops` returns ALL of them so a
/// phrase channel can mix every loop the phrase owns (design spec §8.5).
struct LeafLoop
{
    ConstituentId loopId;                 ///< the leaf-loop Constituent's id
    TapeId        tape;                   ///< the tape it reads from
    Rational      sliceLengthWholeNotes;  ///< loop length in whole notes (tapeOut - tapeIn)
};

/// Find the Constituent with id == phraseId anywhere in `root`, then return
/// every leaf-loop in that subtree (depth-first order). A leaf-loop is a
/// Constituent carrying a TapeReference. Returns empty if `phraseId` is not
/// found or has no loops. Pure: no I/O, no JUCE, no allocation beyond the
/// returned vector — safe to call on any thread (used off the audio thread).
std::vector<LeafLoop> collectLeafLoops (const Constituent& root, ConstituentId phraseId);

} // namespace ida
```

- [ ] **Step 4: Write the implementation** `core/src/LeafLoopSelector.cpp`.

```cpp
#include "ida/LeafLoopSelector.h"

#include "ida/Constituent.h"
#include "ida/TapeReference.h"

namespace ida
{
namespace
{
    const Constituent* findById (const Constituent& c, ConstituentId id)
    {
        if (c.id() == id) return &c;
        for (const auto& child : c.children())
            if (const Constituent* hit = findById (*child, id))
                return hit;
        return nullptr;
    }

    void collectLeaves (const Constituent& c, std::vector<LeafLoop>& out)
    {
        // A leaf-loop carries a TapeReference. Emit it; do not descend past a
        // loop (loops are leaves in practice, and a loop has no loop children).
        if (c.isLoop())
        {
            const TapeReference& ref = *c.tapeReference();
            out.push_back ({ c.id(), ref.tape, ref.sliceLength() });
            return;
        }
        for (const auto& child : c.children())
            collectLeaves (*child, out);
    }
}

std::vector<LeafLoop> collectLeafLoops (const Constituent& root, ConstituentId phraseId)
{
    std::vector<LeafLoop> out;
    if (const Constituent* phrase = findById (root, phraseId))
        collectLeaves (*phrase, out);
    return out;
}

} // namespace ida
```

- [ ] **Step 5: Wire CMake.**
  - In `core/CMakeLists.txt`, add `src/LeafLoopSelector.cpp` to the `core` library's source list (find the existing `src/*.cpp` enumeration and append in the same style).
  - In `tests/CMakeLists.txt`, add `LeafLoopSelectorTests.cpp` to the `IdaTests` sources (near `RenderPipelineTests.cpp` at line 34).

- [ ] **Step 6: Run the tests, verify PASS.**
  Run: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests && ctest --test-dir build -R leaf-loops`
  Expected: all `[leaf-loops]` cases PASS.

- [ ] **Step 7: Commit.**
  ```bash
  git add core/include/ida/LeafLoopSelector.h core/src/LeafLoopSelector.cpp \
          tests/LeafLoopSelectorTests.cpp core/CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat: pure collectLeafLoops selector enumerates all leaf-loops of a phrase"
  ```

---

## Task 2 — Additive multi-slot playback step (`AudioCallback`)

Make `renderPlaybackStep` sum every active slot **into** its destination scratch instead of overwriting, so multiple leaf-loops sharing one phrase-channel scratch mix together. Pre-allocate a per-slot stereo temp so the additive path stays allocation-free on the audio thread.

**Files:**
- Modify: `audio/include/ida/AudioCallback.h` (anchor: `struct PlaybackSlot` ~249; the playback-step members ~248-259)
- Modify: `audio/src/AudioCallback.cpp` (anchor: `renderPlaybackStep` 36-72; the ctor for temp allocation)
- Modify: `tests/TapePlaybackTests.cpp` (add the additive-summing case; the existing `[callback]` cases at 541, 575 must still pass unchanged)

- [ ] **Step 1: Write the failing test** — two slots writing into the **same** destination must SUM. Append to `tests/TapePlaybackTests.cpp` near the existing "fills multiple active slots independently" case (line 541).

```cpp
TEST_CASE ("playback step SUMS slots that share one destination scratch (multi-loop phrase)",
           "[tape-playback][callback]")
{
    // Two ramp tapes; both slots target the SAME L/R scratch — the phrase
    // channel for a phrase that owns two layered loops. The step must add,
    // not overwrite (slice 6: a phrase plays ALL its loops).
    juce::TemporaryFile tmpA (".idatape"), tmpB (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    writeRampTape (tmpA.getFile(), registry, 4, 256);   // ramp 0,1,2,...
    writeRampTape (tmpB.getFile(), registry, 4, 256);   // identical ramp

    TapePrefetcher preA, preB;
    REQUIRE (preA.open (tmpA.getFile(), registry, 48000.0, 0));
    REQUIRE (preB.open (tmpB.getFile(), registry, 48000.0, 0));
    preA.prepare (4096); preA.setTargetSample (0); preA.serviceForTest();
    preB.prepare (4096); preB.setTargetSample (0); preB.serviceForTest();

    std::vector<float> sharedL (256, 0.0f), sharedR (256, 0.0f);

    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    // BOTH slots point at the SAME destination buffers.
    cb.bindPlaybackSlotForTest (0, &preA, sharedL.data(), sharedR.data());
    cb.bindPlaybackSlotForTest (1, &preB, sharedL.data(), sharedR.data());

    ActiveReadsSnapshot snap;
    snap.add ({ 0, 0, true });
    snap.add ({ 1, 0, true });
    publisher.publish (snap);

    cb.runPlaybackStepForTest (64);

    // Each tape contributes sample i at index i; summed => 2*i.
    CHECK (sharedL[1] == Catch::Approx (2.0f));
    CHECK (sharedL[2] == Catch::Approx (4.0f));
    CHECK (sharedR[3] == Catch::Approx (6.0f));
}

TEST_CASE ("playback step zeroes a shared destination once even with one active slot",
           "[tape-playback][callback]")
{
    // Regression: additive path must zero the destination before adding, so a
    // single active slot still reads as its own ramp (not ramp + stale data).
    juce::TemporaryFile tmp (".idatape");
    TapeCodecRegistry registry = makePlaybackRegistry();
    writeRampTape (tmp.getFile(), registry, 4, 256);
    TapePrefetcher pre;
    REQUIRE (pre.open (tmp.getFile(), registry, 48000.0, 0));
    pre.prepare (4096); pre.setTargetSample (0); pre.serviceForTest();

    std::vector<float> l (256, 999.0f), r (256, 999.0f);   // pre-dirtied
    ActiveReadsPublisher publisher;
    ida::AudioCallback cb { ida::EngineConfig {} };
    cb.setActiveReadsPublisher (&publisher);
    cb.bindPlaybackSlotForTest (0, &pre, l.data(), r.data());
    ActiveReadsSnapshot snap; snap.add ({ 0, 0, true }); publisher.publish (snap);

    cb.runPlaybackStepForTest (64);
    CHECK (l[0] == Catch::Approx (0.0f));   // ramp sample 0, NOT 999 + 0
    CHECK (l[5] == Catch::Approx (5.0f));
}
```

- [ ] **Step 2: Run the tests, verify they FAIL.**
  Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "tape-playback"`
  Expected: the new SUM case FAILS — current code overwrites, so `sharedL[1]` is `1.0` (second slot clobbers first), not `2.0`. The zero-shared case may pass or fail depending on slot order; both must be green after the impl.

- [ ] **Step 3: Add the per-slot temp + change the header.** In `audio/include/ida/AudioCallback.h`, after the `PlaybackSlot` struct / `playbackSnapshot_` member (~257), add pre-allocated per-slot stereo temp storage and document the additive contract. Replace the `renderPlaybackStep` doc to state additive-into-destination.

```cpp
    // Slice 6 — per-slot stereo temp for the ADDITIVE playback step. Each
    // active slot pulls into its own temp, then adds into its destination
    // scratch, so multiple leaf-loops of one phrase (which share a single
    // phrase-channel destination) SUM instead of overwriting. Pre-sized to
    // kMaxBusMixSamples in the ctor so the audio thread never allocates.
    std::array<std::array<float, Bus::kMaxBusMixSamples>, kMaxPhraseSlots> slotTempL_ {};
    std::array<std::array<float, Bus::kMaxBusMixSamples>, kMaxPhraseSlots> slotTempR_ {};
```

  Note: `kMaxPhraseSlots * kMaxBusMixSamples * 4 bytes * 2 = 64 * 8192 * 4 * 2 = 4 MB`. That is a value member of `AudioCallback`; confirm `AudioCallback` is heap-allocated (`MainComponent` holds `std::unique_ptr<ida::AudioCallback> audioCallback_` — it is). `Bus.h` is already included via `AudioCallback.h`'s existing includes (it references `Bus::kMaxBusMixSamples` at line 45 in the `.cpp`); verify the header include is present and add `#include "ida/Bus.h"` to `AudioCallback.h` if only the `.cpp` had it.

- [ ] **Step 4: Rewrite `renderPlaybackStep`** in `audio/src/AudioCallback.cpp` (replace the body at 36-72). Zero every distinct bound destination once, then add each active slot's pulled temp into its destination. Inactive (and previously-active) destinations are zeroed by the same first pass, so a phrase going silent reads as silence with no per-slot `wasActive` bookkeeping.

```cpp
void AudioCallback::renderPlaybackStep (int numSamples) noexcept
{
    if (activeReads_ == nullptr) return;
    activeReads_->read (playbackSnapshot_);   // lock-free seqlock read into reused member

    // Clamp to the phrase-scratch capacity (Bus::kMaxBusMixSamples): the pull /
    // add destinations are fixed-size buffers, so an oversized device block must
    // never write past them on the audio thread.
    const int n = juce::jmin (numSamples, static_cast<int> (Bus::kMaxBusMixSamples));

    // Pass 1: zero every distinct destination that has a bound slot, exactly
    // once per block. Multiple slots (leaf-loops) can share one destination
    // (their phrase channel); zeroing per-destination — not per-slot — lets the
    // next pass ADD without double-clearing or clobbering a sibling loop. A
    // destination with no active slot stays zero => the phrase reads silent.
    for (int slot = 0; slot < kMaxPhraseSlots; ++slot)
    {
        auto& ps = playbackSlots_[static_cast<std::size_t> (slot)];
        if (ps.l == nullptr) continue;
        // Only the FIRST slot that owns a given destination clears it. Detect
        // "first" by scanning earlier slots for the same pointer.
        bool alreadyCleared = false;
        for (int prev = 0; prev < slot; ++prev)
        {
            if (playbackSlots_[static_cast<std::size_t> (prev)].l == ps.l)
            {
                alreadyCleared = true;
                break;
            }
        }
        if (! alreadyCleared)
        {
            std::fill (ps.l, ps.l + n, 0.0f);
            std::fill (ps.r, ps.r + n, 0.0f);
        }
    }

    // Mark which slots are active this block.
    std::array<bool, kMaxPhraseSlots> active {};
    for (int i = 0; i < playbackSnapshot_.count; ++i)
    {
        const auto& s = playbackSnapshot_.slots[static_cast<std::size_t> (i)];
        if (s.active && s.slot >= 0 && s.slot < kMaxPhraseSlots)
            active[static_cast<std::size_t> (s.slot)] = true;
    }

    // Pass 2: each active slot pulls into its own temp, then ADDS into its
    // destination. Destinations shared by sibling loops accumulate the sum.
    for (int slot = 0; slot < kMaxPhraseSlots; ++slot)
    {
        auto& ps = playbackSlots_[static_cast<std::size_t> (slot)];
        if (ps.l == nullptr || ps.pre == nullptr) continue;
        if (! active[static_cast<std::size_t> (slot)]) continue;

        float* tmpL = slotTempL_[static_cast<std::size_t> (slot)].data();
        float* tmpR = slotTempR_[static_cast<std::size_t> (slot)].data();
        ps.pre->pull (tmpL, tmpR, n);  // fills + zero-fills underrun; wait-free
        for (int i = 0; i < n; ++i)
        {
            ps.l[i] += tmpL[i];
            ps.r[i] += tmpR[i];
        }
    }
}
```

  This makes `PlaybackSlot::wasActive` dead — **remove that field** from the struct in `AudioCallback.h` (zero-tolerance dead code). Confirm no other reader: `grep -rn "wasActive" audio/ tests/` → only the struct + the old body. The `bindPlaybackSlot` initializer (`audio/src/AudioCallback.cpp:33`) drops the trailing `false`.

- [ ] **Step 5: Run the playback tests, verify PASS.**
  Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "tape-playback"`
  Expected: the two new cases PASS; **all pre-existing `[callback]` and `[e2e]` cases still PASS** (single-slot additive-into-zeroed == overwrite; the independent-slots case has distinct destinations, each zeroed then filled).

- [ ] **Step 6: Verify RT-safety (no new allocations).**
  The `[rt-safety]` test at `tests/TapePlaybackTests.cpp:575` already counts allocations across 1000 `runPlaybackStepForTest` calls. Confirm it still reports zero (the temps are ctor-allocated value members; the `std::fill`/add loops allocate nothing).
  Run: `ctest --test-dir build -R "rt-safety"` — expected: PASS.

- [ ] **Step 7: Commit.**
  ```bash
  git add audio/include/ida/AudioCallback.h audio/src/AudioCallback.cpp tests/TapePlaybackTests.cpp
  git commit -m "feat: playback step sums slots sharing a destination so a phrase plays all its loops"
  ```

---

## Task 3 — Wire one prefetcher/slot per leaf-loop into the phrase channel (`MainComponent`)

Replace the single-first-leaf prefetcher per pill with **one prefetcher + slot per leaf-loop**, keyed by the **leaf-loop's** ConstituentId (so the resolver's `slotFor_(ar.loop)` matches), all pointing at the phrase's one channel scratch. This is the integration that makes the engine's already-multi-loop `activeReadsAt` actually sound.

**Files:**
- Modify: `app/MainComponent.cpp` — `refreshOutputMixerPhraseChannels` add-path (anchor: 7186-7251) and remove-path (anchor: 7152-7172); add a small helper `resolveLeafLoopTape` (next to `resolveLoopTapeInfo`, anchor 6036) or fold the file-path build inline using `collectLeafLoops`.
- Modify: `app/MainComponent.h` — `slotByConstituent_` is keyed by leaf-loop id now (update the comment at ~419-425); add a `phraseLoopSlots_` map so the remove-path can tear down all of a phrase's loop slots. (`resolveLoopTapeInfo`/`PillTapeInfo` may stay for any other caller; if grep shows none, delete them — zero dead code.)
- No change to `PlaybackResolver`, `setSlotForConstituent`, or `setSteerPrefetcher` (lines 4462-4476) — they already key on the loop id and steer by slot; once the slot map is loop-keyed, they work unchanged.

- [ ] **Step 1: Confirm the resolver wiring is already loop-keyed (read-only check, no test).**
  Re-read `app/MainComponent.cpp:4462-4476`: `setSlotForConstituent` looks up `slotByConstituent_.find(c.value())` where `c == ar.loop`; `setSteerPrefetcher` indexes `phrasePrefetchers_[slot]`. Both are correct for a loop-keyed map. The only required change is *what we put in the map*. (This step is a documented verification, not code — record the line numbers in the commit body.)

- [ ] **Step 2: Add the per-loop teardown map** to `app/MainComponent.h`, near `slotByConstituent_` (~425):

```cpp
    /// Slice 6 — for each phrase pill (keyed by its ConstituentId.value()), the
    /// list of leaf-loop ids whose prefetcher slots feed that phrase's channel.
    /// The remove-path tears every one of them down; slotByConstituent_ is keyed
    /// by leaf-loop id (the id the PlaybackResolver resolves), NOT by pill id.
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> phraseLoopIds_;
```

  Update the `slotByConstituent_` comment to: "Keyed by **leaf-loop** ConstituentId (the id `PlaybackResolver` resolves via `ar.loop`). One slot per sounding leaf-loop; sibling loops of one phrase share that phrase's channel scratch."

- [ ] **Step 3: Add the leaf-loop tape-file resolver** (a thin wrapper over `collectLeafLoops` that adds the `juce::File` path). Place next to `resolveLoopTapeInfo` (anchor 6036). Include `"ida/LeafLoopSelector.h"` at the top of `MainComponent.cpp`.

```cpp
// Slice 6 — resolve EVERY leaf-loop of `phraseId` to (loopId, tape file, loop
// length samples). Lifts the first-leaf-only limit of resolveLoopTapeInfo by
// delegating enumeration to the pure core selector, then mapping each tape to
// its on-disk file. Message thread.
std::vector<MainComponent::LeafLoopTape>
MainComponent::resolveLeafLoopTapes (ida::ConstituentId phraseId, double sampleRate) const
{
    std::vector<LeafLoopTape> out;
    if (demo_.root == nullptr) return out;

    const double effectiveSr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (const auto& leaf : ida::collectLeafLoops (*demo_.root, phraseId))
    {
        const auto loopLen =
            std::llround (leaf.sliceLengthWholeNotes.toDouble() * effectiveSr);
        out.push_back ({ leaf.loopId,
                         tapesDirectory().getChildFile (
                             "tape-" + juce::String (leaf.tape.value()) + ".idatape"),
                         loopLen });
    }
    return out;
}
```

  Add the struct + declaration to `app/MainComponent.h` near `PillTapeInfo` (~165):

```cpp
    /// Slice 6 — one leaf-loop resolved to its on-disk tape + loop length.
    struct LeafLoopTape
    {
        ida::ConstituentId loopId;
        juce::File         tapeFile;
        std::int64_t       loopLengthSamples { 0 };
    };

    /// Resolve EVERY leaf-loop of `phraseId` to a tape file + loop length, so a
    /// phrase channel can mix all of its loops (design spec §8.5). Empty when
    /// the phrase has no recorded loops yet.
    [[nodiscard]] std::vector<LeafLoopTape>
    resolveLeafLoopTapes (ida::ConstituentId phraseId, double sampleRate) const;
```

- [ ] **Step 4: Rewrite the add-path** in `refreshOutputMixerPhraseChannels` (replace 7203-7250, the block from `// Resolve the pill's first leaf loop tape file.` through `phrasePrefetchers_[slot] = std::move (pre);`). The channel + scratch + source wiring (7188-7201) is unchanged — one channel per pill. Inside, loop over **all** leaf-loops, allocate a slot each, bind each to the **same** channel scratch, and key the map by the **loop** id.

```cpp
            // Slice 6 — open a prefetcher per leaf-loop the phrase owns and bind
            // each to THIS phrase channel's scratch. The playback step (Task 2)
            // sums slots sharing one destination, so the phrase plays ALL its
            // loops. slotByConstituent_ is keyed by the leaf-loop id — the id
            // the PlaybackResolver resolves via ar.loop.
            const auto leafTapes = resolveLeafLoopTapes (cid, sampleRate);
            std::vector<std::int64_t> boundLoopIds;
            boundLoopIds.reserve (leafTapes.size());

            for (const auto& leaf : leafTapes)
            {
                // Find a free slot (null entry) or append one (capped).
                int slot = -1;
                bool slotWasAppended = false;
                for (int s = 0; s < static_cast<int> (phrasePrefetchers_.size()); ++s)
                    if (phrasePrefetchers_[static_cast<std::size_t> (s)] == nullptr)
                    { slot = s; break; }
                if (slot < 0)
                {
                    if (static_cast<int> (phrasePrefetchers_.size()) >= kMaxPhraseSlots)
                        continue; // at slot cap — this loop stays silent
                    phrasePrefetchers_.push_back (nullptr);
                    slot = static_cast<int> (phrasePrefetchers_.size()) - 1;
                    slotWasAppended = true;
                }

                auto pre = std::make_unique<ida::TapePrefetcher>();
                if (! pre->open (leaf.tapeFile, tapeCodecRegistry_,
                                 sampleRate, leaf.loopLengthSamples))
                {
                    if (slotWasAppended) phrasePrefetchers_.pop_back();
                    continue; // tape not recorded yet / unreadable — this loop stays silent
                }

                pre->prepare (kRingFrames);
                pre->start();

                // EVERY loop of this phrase targets the same channel scratch;
                // the additive playback step mixes them.
                audioCallback_->bindPlaybackSlot (
                    slot,
                    pre.get(),
                    outputMixer_->mutablePhraseScratch (chId, 0),
                    outputMixer_->mutablePhraseScratch (chId, 1));

                slotByConstituent_[leaf.loopId.value()] = slot;   // KEYED BY LOOP ID
                phrasePrefetchers_[static_cast<std::size_t> (slot)] = std::move (pre);
                boundLoopIds.push_back (leaf.loopId.value());
            }

            phraseLoopIds_[cid.value()] = std::move (boundLoopIds);
```

  Remove the now-dead single-`tapeInfo` lines (the old `resolveLoopTapeInfo(cid…)` call and its `if (! tapeInfo.ok) continue;`). The early `continue` on an unrecorded *single* tape is gone — a phrase with zero recorded loops simply binds zero slots and stays silent, which is correct.

- [ ] **Step 5: Rewrite the remove-path** (replace 7157-7171, the single-slot teardown). Tear down **all** of the phrase's loop slots.

```cpp
            // Tear down EVERY prefetcher slot this phrase owns (one per loop).
            const auto loopsIt = phraseLoopIds_.find (cidValue);
            if (loopsIt != phraseLoopIds_.end())
            {
                for (const auto loopIdValue : loopsIt->second)
                {
                    const auto slotIt = slotByConstituent_.find (loopIdValue);
                    if (slotIt == slotByConstituent_.end()) continue;
                    const int slot = slotIt->second;
                    // Unbind from the audio callback BEFORE stopping — the
                    // callback must not pull from a stopped prefetcher.
                    audioCallback_->bindPlaybackSlot (slot, nullptr, nullptr, nullptr);
                    if (phrasePrefetchers_[static_cast<std::size_t> (slot)])
                    {
                        phrasePrefetchers_[static_cast<std::size_t> (slot)]->stop();
                        phrasePrefetchers_[static_cast<std::size_t> (slot)].reset();
                    }
                    slotByConstituent_.erase (slotIt);
                }
                phraseLoopIds_.erase (loopsIt);
            }
```

  (The `phraseChannelByConstituent_.erase(cidValue)` + `outputMixer_->removeChannel(...)` lines just above stay — still one channel per pill.)

- [ ] **Step 6: Remove dead code.** Grep for the old single-leaf path:
  ```bash
  grep -rn "resolveLoopTapeInfo\|PillTapeInfo" app/ tests/
  ```
  If `refreshOutputMixerPhraseChannels` was the only caller (it was), delete `resolveLoopTapeInfo` (`app/MainComponent.cpp:6036-6085`) and the `PillTapeInfo` struct (`app/MainComponent.h:165-176`). If a test or other site uses them, keep and note why in `todo.md`. Per `[[feedback_no_deferral_get_it_done]]`, deleting now is preferred when truly unused.

- [ ] **Step 7: Build the app + the tests; verify green.**
  Run: `cmake --build build --target IdaTests && ctest --test-dir build` (expect baseline 449/450 — the lone non-pass is the separately-run `MainComponentPluginEditorTests` exe, unrelated).
  Run: `cmake --build build --target IDA` — expect links green.
  There is **no headless test of `refreshOutputMixerPhraseChannels`** itself (it is GUI/audio-device wiring — `MainComponent` is operator-verified per repo convention). The pure parts it depends on (`collectLeafLoops`, the additive step) are covered by Tasks 1–2. Do **not** fabricate a unit test for the message-thread wiring.

- [ ] **Step 8: Commit.**
  ```bash
  git add app/MainComponent.cpp app/MainComponent.h
  git commit -m "feat: phrase channel opens one prefetcher per leaf-loop keyed by loop id (play all loops)"
  ```

---

## Task 4 — Clean build + operator verification (audible multi-loop playback)

The audible result is operator-verified, not unit-tested. Build clean and hand off with numbered steps. The demo verse phrase (id 20: rhythm tape 2 + lead tape 3) is the ready-made two-layered-loops fixture — but note Slices 1–5 retire the demo boot; if the demo is gone at execution time, the operator records a phrase, overdubs a second loop into it (the §8.1 "playhead inside an existing phrase → new loop" path), then plays it.

**Files:** none (verification only).

- [ ] **Step 1: Clean build** (`[[feedback_clean_builds_only_for_testing]]`).
  ```bash
  rm -rf build
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target IDA
  ```

- [ ] **Step 2: Launch the app for the operator.**
  ```bash
  open "build/app/IDA_artefacts/Release/IDA.app"
  ```

- [ ] **Step 3: Give the operator these numbered steps** (terse, point-by-point):
  1. Create a channel, pick an input, and record a first phrase (Record from stopped → coinit; Stop). One loop now exists.
  2. With the transport running and the playhead **inside** that phrase, press Record again to overdub — this creates a **second** loop in the same phrase. Stop.
  3. The phrase shows **two loops** (the pill's loop count = 2 / its Output-Mixer strip).
  4. Play the phrase (its phrase-trigger button / transport over it). **Confirm you hear BOTH loops layered**, not just the first.
  5. (If the demo is still present at execution time, the verse pill is a pre-made two-loop phrase — playing it must sound rhythm + lead together.)
  6. Confirm no crackle/dropout while both loops play (RT-safety sanity).

- [ ] **Step 4: On operator confirmation, the slice is done.** If the operator reports only one loop audible, debug with `superpowers:systematic-debugging` — likely suspects in order: (a) `slotByConstituent_` not keyed by loop id, (b) the additive zero-pass clobbering a sibling (shared-pointer detection), (c) `collectLeafLoops` not reaching nested wrappers. Do NOT flip the slice complete without the operator's "I hear both."

---

## Self-review

- **Honors "play = all loops layered."** Task 1 enumerates every leaf-loop; Task 3 opens a prefetcher per loop into one phrase channel; Task 2 sums them. The demo verse (2 loops) and any operator overdub are covered. ✓
- **Identified the real bottleneck the roadmap under-specified.** The roadmap (Slice 6) said only "a phrase channel must mix every loop … not just the first leaf-loop." Reading the code surfaced a deeper, load-bearing defect the roadmap missed: **`slotByConstituent_` is keyed by the phrase/pill ConstituentId, but `PlaybackResolver` resolves the leaf-loop id (`ar.loop`)** — so today even the *single* first-leaf phrase channel is wired to a slot the resolver never matches (pill id ≠ loop id for every wrapped or bare phrase; demo `TapePlaybackTests` confirm the engine model is loop-keyed). Slice 6 must re-key the map by loop id; "play all loops" and "play at all through this path" are the same fix. This is flagged as the cross-slice risk. ✓
- **OutputMixer/Bus change needed? NO — and that is the decided answer.** I read `OutputMixer.h` (`setChannelAudioSource` = one source per channel; `ensurePhraseScratch`/`mutablePhraseScratch` = one stereo scratch per channel) and `Bus.h`. Summing N loops does **not** require an OutputMixer or Bus change, a per-phrase summing node, or N channels: the summation happens in `AudioCallback::renderPlaybackStep` by making multiple slots **add** into the **shared** phrase-channel scratch. One channel per phrase is preserved (UI pill↔strip 1:1 and the 32-channel cap intact). The alternative (N output channels per phrase) was rejected in the Architecture section for multiplying strips and exhausting `kMaxOutputChannels`. ✓
- **RT-safety.** `renderPlaybackStep` stays `noexcept`/alloc-free: per-slot temps are ctor-allocated value members (`kMaxPhraseSlots × kMaxBusMixSamples`, ~4 MB), the prefetch hand-off is the unchanged lock-free seqlock, and the additive loops do only `std::fill`/`+=`. Verified by the existing `[rt-safety]` allocation-counting test (Task 2 Step 6). ✓
- **Stereo-only.** Every buffer is an L/R pair end to end (`slotTempL_/slotTempR_`, `mutablePhraseScratch(chId, 0/1)`). No mono path introduced. ✓
- **TDD where pure, operator-verified where not.** `collectLeafLoops` (Task 1) and the additive step (Task 2) are headless Catch2 with failing-test-first. The message-thread `refreshOutputMixerPhraseChannels` wiring and the audible playback are operator-verified (Task 4) — no fabricated GUI/audio unit test. ✓
- **Zero dead code.** `PlaybackSlot::wasActive` is removed (Task 2); `resolveLoopTapeInfo`/`PillTapeInfo` are deleted if unused (Task 3 Step 6), with a `todo.md` fallback only if a real caller remains. ✓
- **Surgical.** No change to `RenderPipeline`, `PlaybackResolver`, the prefetcher ring, or OutputMixer routing — only the loop-enumeration source-construction, the playback-step summation, and the new `core` selector. ✓
- **Build/commit discipline.** Clean `rm -rf build` before the operator hand-off; single-line commits with the required trailer; coordinator pushes. ✓
