# Capture Promotion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Mark Out auto-promote captured regions into the session Constituent tree as Loops (and Phrase wrappers when needed), with non-destructive undo that restores the capture session to AwaitingOut on revert.

**Architecture:** New pure-function module `core/ida::promotion` mirrors the existing `core/ida::arrangement` pattern. Promotion runs a multi-instance write-protect first, finds the deepest host Phrase whose LMC span contains Mark In (or mints one at the song root when none exists), and adds the Loop child via copy-on-write. UndoStack gains an optional `CaptureRestorePoint` per entry so MainComponent can restore `CaptureSession` state on undo. CaptureBanner becomes tappable for one-tap recovery.

**Tech Stack:** C++20, JUCE 7 (UI only — `core/` stays JUCE-free), Catch2 (existing test harness), CMake + Ninja, Release builds only on iOS.

**Spec:** `docs/superpowers/specs/2026-05-15-capture-promotion-design.md`

**M3 simplification (explicit):** For this milestone, promotion treats conceptual time as 1:1 with LMC seconds when constructing new Loop / Phrase boundaries. The demo session already uses identity-rate tempo maps. When non-trivial tempo maps land later, this needs revisiting (a `TempoMap::applyInverse` would land then). The plan flags this in code comments and in the user guide's Roadmap so it isn't lost.

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `core/include/ida/Promotion.h` | NEW | `ida::promotion` namespace: `PromotionResult`, `IdAllocator`, `promote()` declaration. |
| `core/src/Promotion.cpp` | NEW | `promote()` implementation: multi-instance guard, host-finding walk, mint vs join paths, boundary clamping. |
| `core/CMakeLists.txt` | MODIFY | Add `src/Promotion.cpp` to `IdaCore` sources. |
| `tests/PromotionTests.cpp` | NEW | Pure-function tests for `promote()`. Catch2. |
| `tests/CMakeLists.txt` | MODIFY | Add `PromotionTests.cpp` to `IdaTests` sources. |
| `ui/include/ida/UndoStack.h` | MODIFY | Add `CaptureRestorePoint` struct, new `push` overload, `currentEntryRestorePoint()` accessor. |
| `ui/src/UndoStack.cpp` | MODIFY | Implement the overload + accessor; carry restore point on `Entry`. |
| `tests/UndoStackTests.cpp` | MODIFY | Add tests for the restore-point round-trip. |
| `app/MainComponent.h` | MODIFY | Add `nextConstituentId_` member; remove `capturedRegions_`. Adjust `announceCapture` signature. |
| `app/MainComponent.cpp` | MODIFY | New `onMarkOut` body (promote + push + announce). New initialization of `nextConstituentId_`. New `onUndo` restore branch. New `CaptureBanner` click handler + adapted message text. |

---

## Task 1: UndoStack — `CaptureRestorePoint` struct + push overload + accessor

**Files:**
- Modify: `ui/include/ida/UndoStack.h`
- Modify: `ui/src/UndoStack.cpp`
- Test: `tests/UndoStackTests.cpp`

- [ ] **Step 1: Write the failing tests in `tests/UndoStackTests.cpp`**

Append at the end of the file:

```cpp
TEST_CASE ("UndoStack carries an optional CaptureRestorePoint per entry",
           "[undo][promotion]")
{
    using ida::CaptureRestorePoint;
    using ida::TapeId;

    UndoStack stack (makeRoot (1, "initial"));

    // A non-promotion push leaves the current entry's restore point empty.
    stack.push (makeRoot (2, "rename"), "rename phrase");
    CHECK_FALSE (stack.currentEntryRestorePoint().has_value());

    // A promotion push attaches a restore point.
    const CaptureRestorePoint rp { Rational (3, 2), TapeId (200) };
    stack.push (makeRoot (3, "after promote"), "capture phrase", rp);

    REQUIRE (stack.currentEntryRestorePoint().has_value());
    CHECK (stack.currentEntryRestorePoint()->pendingIn  == Rational (3, 2));
    CHECK (stack.currentEntryRestorePoint()->pendingTape.value() == 200);

    // Undo returns to the prior (rename) entry — no restore point there.
    stack.undo();
    CHECK_FALSE (stack.currentEntryRestorePoint().has_value());

    // Redo returns to the promotion entry — restore point reappears.
    stack.redo();
    REQUIRE (stack.currentEntryRestorePoint().has_value());
    CHECK (stack.currentEntryRestorePoint()->pendingTape.value() == 200);
}
```

Add the include at the top of the file with the other includes:

```cpp
#include "sirius/TapeId.h"
```

- [ ] **Step 2: Run tests to verify the build fails (CaptureRestorePoint not defined)**

```bash
cd /Users/larryseyer/IDA
cmake --build build --target IdaTests 2>&1 | tail -20
```

Expected: compile errors mentioning `CaptureRestorePoint`, `currentEntryRestorePoint`, the three-arg `push` overload — none of these exist yet.

- [ ] **Step 3: Add `CaptureRestorePoint`, the new push overload, and the accessor to `ui/include/ida/UndoStack.h`**

Add `#include "sirius/TapeId.h"` to the existing includes (sirius/Constituent.h is already there). After the existing `using RootPtr = ...` line (around line 29), add the struct:

```cpp
    /// Captured-state snapshot recorded with a promotion entry so that undoing
    /// the promotion can restore CaptureSession to AwaitingOut with the
    /// original in-point intact (white paper Part 14.7 — "undo is sacred",
    /// extended to capture-state for the auto-promotion flow). Non-promotion
    /// edits omit this; only entries pushed via the three-arg overload carry it.
    struct CaptureRestorePoint
    {
        Rational pendingIn;
        TapeId   pendingTape;
    };
```

Then in the public methods area, after the existing `void push (RootPtr nextRoot, std::string label = {});` declaration, add:

```cpp
    /// Records a promotion edit. Behaves exactly like the two-argument push
    /// (truncates redo, advances current) but additionally attaches a
    /// CaptureRestorePoint to the new entry, so undo can restore
    /// CaptureSession::AwaitingOut state on the way back.
    void push (RootPtr nextRoot, std::string label, CaptureRestorePoint restore);

    /// The CaptureRestorePoint of the current entry, or nullopt if the current
    /// entry was a non-promotion edit (or the initial baseline). Stable across
    /// undo/redo: the field belongs to the entry, not to the stack cursor.
    const std::optional<CaptureRestorePoint>& currentEntryRestorePoint() const noexcept;
```

In the private `Entry` struct, add the field:

```cpp
    struct Entry
    {
        RootPtr     root;
        std::string label;
        std::optional<CaptureRestorePoint> captureRestore;  // promotion entries only
    };
```

Add `#include <optional>` to includes if not already present (it is — `std::optional` is used elsewhere in the file? — check; if absent, add it).

- [ ] **Step 4: Implement the overload and accessor in `ui/src/UndoStack.cpp`**

After the existing two-argument `push` implementation, add:

```cpp
void UndoStack::push (RootPtr nextRoot, std::string label, CaptureRestorePoint restore)
{
    if (nextRoot == nullptr)
        throw std::invalid_argument ("ida::UndoStack::push: nextRoot must not be null");

    // Truncate redo branch: once a fresh edit lands, the alternate future is
    // gone (white paper 14.7). Same as the two-argument push.
    if (currentIndex_ + 1 < entries_.size())
        entries_.resize (currentIndex_ + 1);

    entries_.push_back (Entry { std::move (nextRoot), std::move (label), std::move (restore) });

    if (entries_.size() > maxDepth_)
        entries_.erase (entries_.begin());
    else
        ++currentIndex_;
}

const std::optional<CaptureRestorePoint>& UndoStack::currentEntryRestorePoint() const noexcept
{
    return entries_[currentIndex_].captureRestore;
}
```

If the existing two-arg `push` constructs an `Entry` literal, that line will need to be updated to leave `captureRestore` default-constructed (its default is `std::nullopt`, which is what we want for non-promotion entries). Verify by reading `ui/src/UndoStack.cpp` first; the most likely change is `entries_.push_back (Entry { std::move (nextRoot), std::move (label) });` becoming `entries_.push_back (Entry { std::move (nextRoot), std::move (label), std::nullopt });` — or no change at all if aggregate-init handles the omitted field.

- [ ] **Step 5: Build and run only the new tests**

```bash
cd /Users/larryseyer/IDA
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[undo][promotion]"
```

Expected: 1 test case, all assertions pass.

- [ ] **Step 6: Run the full test suite to verify no regression**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 227 tests pass (was 226 + the new one), assertion count up by ~6.

- [ ] **Step 7: Commit**

```bash
git add ui/include/ida/UndoStack.h ui/src/UndoStack.cpp tests/UndoStackTests.cpp
git commit -m "feat: UndoStack — optional CaptureRestorePoint per entry for promotion undo"
```

---

## Task 2: Promotion module skeleton + multi-instance guard (TDD)

**Files:**
- Create: `core/include/ida/Promotion.h`
- Create: `core/src/Promotion.cpp`
- Modify: `core/CMakeLists.txt`
- Create: `tests/PromotionTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test for the multi-instance guard**

Create `tests/PromotionTests.cpp`:

```cpp
// Tests for ida::promotion::promote — the auto-promotion of CaptureRegions
// into the session Constituent tree. Pure-function tests; no JUCE.
//
// Scope is single-instance promotion (see
// docs/superpowers/specs/2026-05-15-capture-promotion-design.md). The
// multi-instance write-protect is verified here so the guard cannot be
// removed silently.
#include "sirius/Promotion.h"

#include "sirius/Arrangement.h"
#include "sirius/CaptureSession.h"
#include "sirius/Constituent.h"
#include "sirius/Phrase.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

using ida::CaptureRegion;
using ida::Constituent;
using ida::ConstituentId;
using ida::PhraseMetadata;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TempoMap;
using ida::promotion::IdAllocator;
using ida::promotion::promote;

namespace
{
    /// 1:1 conceptual ↔ LMC mapping — the M3 simplification, matches the demo.
    TempoMap identityMap()
    {
        return TempoMap::constant (Rational (1));
    }

    /// A monotonic counter producing fresh ConstituentIds.
    struct Counter
    {
        std::int64_t next { 1000 };
        ConstituentId operator() () { return ConstituentId (next++); }
    };

    Constituent emptyRoot()
    {
        return Constituent (ConstituentId (1), Position(), Position (Rational (60)));
    }
}

TEST_CASE ("promote throws when any Constituent id appears more than once",
           "[promotion][guard]")
{
    // Build a root that contains two distinct Constituents sharing id 42.
    // arrangement::sequence does this naturally when the same Phrase is placed
    // multiple times — each placement is a new Constituent object that copies
    // the shared id. The guard must catch this.
    auto sharedPhrase = std::make_shared<const Constituent> (
        Constituent (ConstituentId (42), Position(), Position (Rational (4)))
            .withPhraseMetadata (PhraseMetadata { "verse", "" }));

    Constituent root = ida::arrangement::sequence (emptyRoot(),
                                                      { sharedPhrase, sharedPhrase });

    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;

    CHECK_THROWS_AS (
        promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (5),
                 IdAllocator (std::ref (counter))),
        std::logic_error);
}
```

- [ ] **Step 2: Create `core/include/ida/Promotion.h` with the public surface only**

```cpp
#pragma once

#include "sirius/CaptureSession.h"
#include "sirius/Constituent.h"
#include "sirius/ConstituentId.h"
#include "sirius/Rational.h"
#include "sirius/TempoMap.h"

#include <functional>
#include <optional>
#include <string>

namespace ida::promotion
{

/// The result of a successful promotion: the new session root, identity of
/// the added Loop, identity of any minted Phrase wrapper, and the undo label
/// the caller should attach to the UndoStack entry.
///
/// `mintedPhraseId` is present iff promote() had to mint a wrapper Phrase
/// (i.e. the playhead at Mark In was outside any existing Phrase). When the
/// playhead landed inside an existing Phrase, only a Loop is added and
/// `mintedPhraseId == nullopt`.
struct PromotionResult
{
    Constituent newRoot;
    ConstituentId addedLoopId { 0 };
    std::optional<ConstituentId> mintedPhraseId;
    std::string undoLabel;
};

/// Caller-supplied id allocator. promote() invokes it once for the host-
/// Phrase case (one fresh id for the Loop) and twice for the mint case
/// (one id for the Phrase wrapper, one for the Loop child).
using IdAllocator = std::function<ConstituentId()>;

/// Auto-promote a captured region into the session graph. Pure: returns a
/// new root with the Loop (and possibly Phrase wrapper) added; the input
/// `root` is untouched.
///
/// Behaviour (see docs/superpowers/specs/2026-05-15-capture-promotion-design.md):
///   * Throws std::logic_error if any ConstituentId appears more than once
///     anywhere in `root` — shared-placement architecture is deferred work.
///   * Throws std::invalid_argument if the region's duration is non-positive.
///   * Walks `root` to find the deepest Phrase whose LMC span contains
///     `lmcAtMarkIn`. If found, adds a Loop as a child of that Phrase
///     (clamped to the host's bounds in the straddle case). If not found,
///     mints a new Phrase at the song root, containing the Loop.
///
/// M3 simplification: conceptual time is treated as 1:1 with LMC seconds for
/// the purposes of computing new Loop / Phrase boundaries. The demo session
/// uses identity-rate tempo maps. Non-trivial tempo maps will require an
/// inverse mapping on TempoMap before they can compose with promotion.
PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         const IdAllocator&   allocateId);

} // namespace ida::promotion
```

- [ ] **Step 3: Create `core/src/Promotion.cpp` with the multi-instance guard only (other paths still throw)**

```cpp
#include "sirius/Promotion.h"

#include <stdexcept>
#include <unordered_set>

namespace ida::promotion
{

namespace
{
    /// Walk `c` and throw std::logic_error if any ConstituentId appears more
    /// than once in the subtree. The single-instance write-protect: until the
    /// shared-placement-with-overlays architecture lands (todo.md), promotion
    /// must refuse to operate on a tree containing repeated placements.
    void enforceSingleInstance (const Constituent& c,
                                std::unordered_set<std::int64_t>& seen)
    {
        const auto rawId = c.id().value();
        if (! seen.insert (rawId).second)
            throw std::logic_error (
                "ida::promotion: shared-placement architecture not yet implemented; "
                "see todo.md \"Shared-placement-with-per-instance-overlays architecture\"");
        for (const auto& child : c.children())
            enforceSingleInstance (*child, seen);
    }
}

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      /*sessionToLmc*/,
                         const CaptureRegion& region,
                         Rational             /*lmcAtMarkIn*/,
                         const IdAllocator&   /*allocateId*/)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "ida::promotion::promote: region duration must be strictly positive");

    std::unordered_set<std::int64_t> seen;
    enforceSingleInstance (root, seen);

    // TODO(Task 3+): host-finding, mint, attach. The remaining behaviour is
    // staged across follow-on tasks — every other call to promote() throws
    // until those land. This TODO is tracked in docs/superpowers/plans/.
    throw std::logic_error ("ida::promotion::promote: not yet implemented");
}

} // namespace ida::promotion
```

- [ ] **Step 4: Wire the new sources into CMake**

In `core/CMakeLists.txt`, locate the `add_library(IdaCore STATIC ...)` block and add `src/Promotion.cpp` after `src/CaptureSession.cpp` (any position works; keep the existing alphabetical-ish order):

```cmake
    src/CaptureSession.cpp
    src/Promotion.cpp
    src/EffectChain.cpp)
```

In `tests/CMakeLists.txt`, locate the `add_executable(IdaTests ...)` block and add `PromotionTests.cpp` after `CaptureSessionTests.cpp`:

```cmake
    CaptureSessionTests.cpp
    PromotionTests.cpp
    InputDescriptorTests.cpp
```

- [ ] **Step 5: Build (clean rebuild — CMake source-list change requires reconfigure)**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
```

Expected: build succeeds; new `Promotion.cpp` and `PromotionTests.cpp` are in the link.

- [ ] **Step 6: Run the new test, expect it to pass (the guard is the only thing it exercises)**

```bash
./build/tests/IdaTests "[promotion][guard]"
```

Expected: 1 test case, 1 assertion, pass.

- [ ] **Step 7: Run the full suite — should be green**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 228 tests pass.

- [ ] **Step 8: Commit**

```bash
git add core/include/ida/Promotion.h core/src/Promotion.cpp core/CMakeLists.txt tests/PromotionTests.cpp tests/CMakeLists.txt
git commit -m "feat: promotion module skeleton + multi-instance write-protect"
```

---

## Task 3: Promotion — host-Phrase-found path

**Files:**
- Modify: `core/src/Promotion.cpp`
- Modify: `tests/PromotionTests.cpp`

- [ ] **Step 1: Write the failing test for the host-Phrase case**

Append to `tests/PromotionTests.cpp`:

```cpp
TEST_CASE ("promote into an existing Phrase adds a Loop child, no Phrase mint",
           "[promotion][host]")
{
    // Build a root with a single Phrase "verse" spanning [2, 6) seconds.
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (2)), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { "verse", "" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In at LMC = 3 (inside verse), Mark Out at LMC = 5 (still inside).
    const CaptureRegion region { TapeId (200), Rational (3), Rational (5) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (3),
                           IdAllocator (std::ref (counter)));

    CHECK_FALSE (result.mintedPhraseId.has_value());
    CHECK (result.addedLoopId.value() == 1000);  // first Counter id
    CHECK (result.undoLabel == "capture loop into verse");

    // The new root should have one top-level child (the verse, copy-on-write
    // replaced) which now itself has one child (the Loop).
    REQUIRE (result.newRoot.children().size() == 1);
    const auto& placedVerse = *result.newRoot.children()[0];
    CHECK (placedVerse.id().value() == 10);
    REQUIRE (placedVerse.children().size() == 1);

    const auto& addedLoop = *placedVerse.children()[0];
    CHECK (addedLoop.id().value() == 1000);
    REQUIRE (addedLoop.tapeReference().has_value());
    CHECK (addedLoop.tapeReference()->tape.value()  == 200);
    CHECK (addedLoop.tapeReference()->tapeIn        == Rational (3));
    CHECK (addedLoop.tapeReference()->tapeOut       == Rational (5));
}
```

- [ ] **Step 2: Run the test, verify it fails (still throws "not yet implemented")**

```bash
cd /Users/larryseyer/IDA
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][host]"
```

Expected: FAIL — the implementation throws `std::logic_error: not yet implemented`.

- [ ] **Step 3: Implement host-finding + Loop attach in `core/src/Promotion.cpp`**

Replace the body of `promote` (everything after the guard call) with:

```cpp
namespace
{
    /// LMC span of `c` given its parent's `parentToLmc` mapping. Mirrors the
    /// pattern in ui/src/TimelineViewState.cpp's `walk()`.
    struct Span { Rational startLmc; Rational endLmc; };

    using ParentToLmc = std::function<Rational (Rational)>;

    Span lmcSpan (const Constituent& c, const ParentToLmc& parentToLmc)
    {
        return { parentToLmc (c.conceptualIn().wholeNotes()),
                 parentToLmc (c.conceptualOut().wholeNotes()) };
    }

    /// Compose the child's parent-to-LMC mapping from its own placement and
    /// optional local TempoMap. Same pattern as TimelineViewState::walk.
    ParentToLmc childMapping (const Constituent& parent,
                              const ParentToLmc& parentToLmc)
    {
        const Rational childOffset = parent.conceptualIn().wholeNotes();
        const auto     localMap    = parent.localTempoMap();
        return [parentToLmc, childOffset, localMap] (Rational t)
        {
            const Rational inParent =
                childOffset + (localMap ? localMap->apply (t) : t);
            return parentToLmc (inParent);
        };
    }

    /// Walk to find the deepest Phrase whose LMC span contains `lmcAtMarkIn`.
    /// Returns the index path from `root` (empty path = root itself, never
    /// applicable since root is not a Phrase by convention). Empty optional
    /// means no host.
    struct HostHit
    {
        std::vector<std::size_t> path;  // indices through children
        Rational hostStartLmc;
        Rational hostEndLmc;
        std::string hostName;
    };

    bool findHostRecursive (const Constituent& c,
                            const ParentToLmc& parentToLmc,
                            Rational           lmcAtMarkIn,
                            std::vector<std::size_t>& currentPath,
                            std::optional<HostHit>& deepestSoFar)
    {
        const Span span = lmcSpan (c, parentToLmc);

        if (c.isPhrase() && lmcAtMarkIn >= span.startLmc && lmcAtMarkIn < span.endLmc)
        {
            deepestSoFar = HostHit { currentPath, span.startLmc, span.endLmc, c.name() };
        }

        const auto childMap = childMapping (c, parentToLmc);
        for (std::size_t i = 0; i < c.children().size(); ++i)
        {
            currentPath.push_back (i);
            findHostRecursive (*c.children()[i], childMap, lmcAtMarkIn,
                               currentPath, deepestSoFar);
            currentPath.pop_back();
        }
        return deepestSoFar.has_value();
    }

    /// Replace the Constituent at `path` in `root`'s subtree with `replacement`.
    /// Returns a new root via copy-on-write down the path. Empty path means
    /// replace `root` itself, which we do not allow here.
    Constituent replaceAt (const Constituent& root,
                           const std::vector<std::size_t>& path,
                           std::size_t                     depth,
                           const Constituent&              replacement)
    {
        if (depth == path.size())
            return replacement;

        const std::size_t i = path[depth];
        auto childCopy = std::make_shared<const Constituent> (
            replaceAt (*root.children()[i], path, depth + 1, replacement));
        return root.withChildReplaced (i, childCopy);
    }
}

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         const IdAllocator&   allocateId)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "ida::promotion::promote: region duration must be strictly positive");

    std::unordered_set<std::int64_t> seen;
    enforceSingleInstance (root, seen);

    // M3 simplification: conceptual ↔ LMC is treated as 1:1 by the boundary
    // construction below. The find-host walk uses the *real* sessionToLmc so
    // host detection composes correctly through any tempo map; only the new
    // Loop's conceptual boundaries are the simplified part.
    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    std::vector<std::size_t> path;
    std::optional<HostHit> hit;
    findHostRecursive (root, rootToLmc, lmcAtMarkIn, path, hit);

    if (hit.has_value())
    {
        const auto loopId = allocateId();

        // M3 simplification: clamp to host's LMC span, then express the Loop's
        // conceptual bounds 1:1 against the host's local conceptual time.
        const Rational clampedInLmc  = std::max (region.inLmcSeconds,  hit->hostStartLmc);
        const Rational clampedOutLmc = std::min (region.outLmcSeconds, hit->hostEndLmc);

        const Position loopIn  (clampedInLmc  - hit->hostStartLmc);
        const Position loopOut (clampedOutLmc - hit->hostStartLmc);

        Constituent loop (loopId, loopIn, loopOut);
        loop = loop.withTapeReference (
            ida::TapeReference (region.tape,
                                   region.inLmcSeconds, region.outLmcSeconds));

        // Walk down the path again to splice the Loop into the host.
        std::function<Constituent (const Constituent&, std::size_t)> spliced;
        spliced = [&] (const Constituent& c, std::size_t depth) -> Constituent
        {
            if (depth == hit->path.size())
                return c.withChildAdded (std::make_shared<const Constituent> (loop));
            const std::size_t i = hit->path[depth];
            auto childCopy = std::make_shared<const Constituent> (
                spliced (*c.children()[i], depth + 1));
            return c.withChildReplaced (i, childCopy);
        };

        Constituent newRoot = spliced (root, 0);

        std::string label = hit->hostName.empty()
                            ? std::string ("capture loop")
                            : "capture loop into " + hit->hostName;

        return PromotionResult { std::move (newRoot), loopId, std::nullopt, std::move (label) };
    }

    // No host found — mint case is implemented in Task 4.
    throw std::logic_error ("ida::promotion::promote: mint path not yet implemented");
}
```

- [ ] **Step 4: Run the test, verify it passes**

```bash
cd /Users/larryseyer/IDA
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][host]"
```

Expected: PASS.

- [ ] **Step 5: Run the full suite — guard test still passes; new host test passes; nothing else broken**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 229 tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/Promotion.cpp tests/PromotionTests.cpp
git commit -m "feat: promotion — add Loop child to host Phrase when Mark In lands inside one"
```

---

## Task 4: Promotion — mint-Phrase path (no host)

**Files:**
- Modify: `core/src/Promotion.cpp`
- Modify: `tests/PromotionTests.cpp`

- [ ] **Step 1: Write the failing tests for the mint case**

Append to `tests/PromotionTests.cpp`:

```cpp
TEST_CASE ("promote on an empty root mints a Phrase containing one Loop",
           "[promotion][mint]")
{
    Constituent root = emptyRoot();
    const CaptureRegion region { TapeId (300), Rational (4), Rational (8) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (4),
                           IdAllocator (std::ref (counter)));

    REQUIRE (result.mintedPhraseId.has_value());
    CHECK (result.mintedPhraseId->value() == 1000);  // first id minted (Phrase)
    CHECK (result.addedLoopId.value()     == 1001);  // second id minted (Loop)
    CHECK (result.undoLabel == "capture phrase");

    REQUIRE (result.newRoot.children().size() == 1);
    const auto& mintedPhrase = *result.newRoot.children()[0];
    CHECK (mintedPhrase.id().value() == 1000);
    REQUIRE (mintedPhrase.isPhrase());
    CHECK (mintedPhrase.phraseMetadata()->role == "capture");
    CHECK (mintedPhrase.conceptualIn()  == Position (Rational (4)));
    CHECK (mintedPhrase.conceptualOut() == Position (Rational (8)));

    REQUIRE (mintedPhrase.children().size() == 1);
    const auto& loop = *mintedPhrase.children()[0];
    CHECK (loop.id().value() == 1001);
    REQUIRE (loop.tapeReference().has_value());
    CHECK (loop.tapeReference()->tape.value() == 300);
    CHECK (loop.conceptualIn()  == Position());                      // local-to-Phrase
    CHECK (loop.conceptualOut() == Position (Rational (4)));         // duration of region
}

TEST_CASE ("promote with playhead in a gap between Phrases mints a fresh Phrase",
           "[promotion][mint]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (0)), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { "verse", "" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In at LMC = 10, far past the verse. No host.
    const CaptureRegion region { TapeId (300), Rational (10), Rational (12) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (10),
                           IdAllocator (std::ref (counter)));

    REQUIRE (result.mintedPhraseId.has_value());
    CHECK (result.undoLabel == "capture phrase");

    // Root now has two children: the original verse and the new Phrase.
    REQUIRE (result.newRoot.children().size() == 2);
    CHECK (result.newRoot.children()[0]->id().value() == 10);
    CHECK (result.newRoot.children()[1]->id().value() == result.mintedPhraseId->value());
}
```

- [ ] **Step 2: Run the tests, verify failure (mint path throws)**

```bash
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][mint]"
```

Expected: FAIL — `std::logic_error: mint path not yet implemented`.

- [ ] **Step 3: Implement the mint path in `core/src/Promotion.cpp`**

Replace the trailing `throw std::logic_error ("ida::promotion::promote: mint path not yet implemented");` line with:

```cpp
    // Mint case — no Phrase contained Mark In. Create a fresh Phrase at the
    // song root, sized to the captured region (1:1 conceptual ↔ LMC for M3),
    // containing one Loop child whose conceptual bounds run from 0 to the
    // region's duration in the new Phrase's local time.
    const auto phraseId = allocateId();
    const auto loopId   = allocateId();

    const Position phraseIn  (region.inLmcSeconds);
    const Position phraseOut (region.outLmcSeconds);
    const Position loopIn;                                     // start of phrase-local
    const Position loopOut (region.outLmcSeconds - region.inLmcSeconds);

    Constituent loop (loopId, loopIn, loopOut);
    loop = loop.withTapeReference (
        ida::TapeReference (region.tape,
                               region.inLmcSeconds, region.outLmcSeconds));

    Constituent newPhrase (phraseId, phraseIn, phraseOut);
    newPhrase = newPhrase.withPhraseMetadata (PhraseMetadata { "capture", "" })
                         .withChildAdded (std::make_shared<const Constituent> (loop));

    Constituent newRoot = root.withChildAdded (
        std::make_shared<const Constituent> (newPhrase));

    return PromotionResult { std::move (newRoot), loopId,
                             std::optional<ConstituentId> (phraseId),
                             std::string ("capture phrase") };
```

If the compiler complains about `PhraseMetadata { "capture", "" }` (positional construction of a struct), use designated init or set fields explicitly:

```cpp
PhraseMetadata pm;
pm.role   = "capture";
pm.intent = "";
newPhrase = newPhrase.withPhraseMetadata (std::move (pm))
                     .withChildAdded (std::make_shared<const Constituent> (loop));
```

- [ ] **Step 4: Run mint tests, verify they pass**

```bash
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][mint]"
```

Expected: 2 test cases, all assertions pass.

- [ ] **Step 5: Run full suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 231 tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/Promotion.cpp tests/PromotionTests.cpp
git commit -m "feat: promotion — mint Phrase wrapper when Mark In is outside any Phrase"
```

---

## Task 5: Promotion — straddle clamp behaviour

**Files:**
- Modify: `tests/PromotionTests.cpp`

(The clamping is already in place from Task 3 — this task adds the test that locks it down so it cannot regress.)

- [ ] **Step 1: Write the straddle-clamp test**

Append to `tests/PromotionTests.cpp`:

```cpp
TEST_CASE ("promote clamps Loop bounds to the host Phrase when region extends past",
           "[promotion][straddle]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position (Rational (2)), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { "verse", "" }));

    Constituent root = emptyRoot().withChildAdded (verse);

    // Mark In = 4 (inside verse [2,6)), Mark Out = 9 (well past verse).
    // Mark In wins: host is verse; Loop must be clamped to [4,6) in LMC,
    // which is [2,4) in verse-local conceptual time.
    const CaptureRegion region { TapeId (200), Rational (4), Rational (9) };
    Counter counter;

    auto result = promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (4),
                           IdAllocator (std::ref (counter)));

    REQUIRE_FALSE (result.mintedPhraseId.has_value());
    REQUIRE (result.newRoot.children().size() == 1);
    const auto& placedVerse = *result.newRoot.children()[0];
    REQUIRE (placedVerse.children().size() == 1);
    const auto& loop = *placedVerse.children()[0];

    // Conceptual bounds are clamped to the verse's local time domain.
    CHECK (loop.conceptualIn()  == Position (Rational (2)));   // (4 - 2)
    CHECK (loop.conceptualOut() == Position (Rational (4)));   // (6 - 2), clipped from 9

    // TapeReference keeps the *unclamped* original LMC times — the audio
    // beyond the host boundary still exists on the tape and remains
    // referenceable; only the Constituent's structural placement is clipped.
    CHECK (loop.tapeReference()->tapeIn  == Rational (4));
    CHECK (loop.tapeReference()->tapeOut == Rational (9));
}
```

- [ ] **Step 2: Run the test, verify it passes (Task 3's implementation already clamps)**

```bash
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][straddle]"
```

Expected: PASS.

- [ ] **Step 3: Run full suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 232 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/PromotionTests.cpp
git commit -m "test: promotion — lock down straddle clamping to host Phrase boundary"
```

---

## Task 6: Promotion — defensive throw on degenerate region

**Files:**
- Modify: `tests/PromotionTests.cpp`

(The defensive check is already in place from Task 2's implementation.)

- [ ] **Step 1: Write the defensive-throw test**

Append to `tests/PromotionTests.cpp`:

```cpp
TEST_CASE ("promote throws on a zero-duration or reversed region",
           "[promotion][defensive]")
{
    Constituent root = emptyRoot();
    Counter counter;

    SECTION ("zero duration")
    {
        const CaptureRegion bad { TapeId (200), Rational (3), Rational (3) };
        CHECK_THROWS_AS (
            promote (root, identityMap(), bad, Rational (3),
                     IdAllocator (std::ref (counter))),
            std::invalid_argument);
    }

    SECTION ("reversed bounds")
    {
        const CaptureRegion bad { TapeId (200), Rational (5), Rational (3) };
        CHECK_THROWS_AS (
            promote (root, identityMap(), bad, Rational (5),
                     IdAllocator (std::ref (counter))),
            std::invalid_argument);
    }
}
```

- [ ] **Step 2: Run, verify pass**

```bash
cmake --build build --target IdaTests && ./build/tests/IdaTests "[promotion][defensive]"
```

Expected: PASS.

- [ ] **Step 3: Run full suite + commit**

```bash
./build/tests/IdaTests 2>&1 | tail -3
git add tests/PromotionTests.cpp
git commit -m "test: promotion — defensive throw on degenerate captured region"
```

Expected: 233 tests pass.

---

## Task 7: MainComponent wiring — `onMarkOut` calls `promote`, removes `capturedRegions_`

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Read current `onMarkOut` and `MainComponent.h` to know exactly what to replace**

```bash
sed -n '110,135p' app/MainComponent.h
sed -n '610,640p' app/MainComponent.cpp
```

This is to confirm line numbers and exact wording — they may have drifted since the spec was written.

- [ ] **Step 2: Update `app/MainComponent.h`**

Add include near the existing `#include "sirius/UndoStack.h"` line:

```cpp
#include "sirius/Promotion.h"
```

Remove the `std::vector<CaptureRegion> capturedRegions_;` member (was around line 127). Add in its place:

```cpp
std::int64_t nextConstituentId_ { 0 };
```

Change the `announceCapture` declaration from:

```cpp
void announceCapture (const CaptureRegion& region, int loopNumber);
```

to:

```cpp
void announceCapture (const CaptureRegion& region,
                      const promotion::PromotionResult& result);
```

- [ ] **Step 3: Update `app/MainComponent.cpp` — initialize `nextConstituentId_`**

In the `MainComponent::MainComponent()` constructor (around line 322 where `undoStack_` is initialised), after `undoStack_` is constructed, add a helper that walks the demo root and seeds the counter to one past the maximum existing id. Insert before any other constructor body work that uses ids:

```cpp
    // Seed nextConstituentId_ to one past the maximum id present in the
    // initial demo session, so promotion's allocateId callback never collides
    // with an existing id.
    {
        std::function<void (const Constituent&)> walk = [&] (const Constituent& c)
        {
            if (c.id().value() >= nextConstituentId_)
                nextConstituentId_ = c.id().value() + 1;
            for (const auto& child : c.children())
                walk (*child);
        };
        walk (*undoStack_.current());
    }
```

(`<functional>` is already pulled in via JUCE; if a linker error appears, add `#include <functional>` to `app/MainComponent.cpp`.)

- [ ] **Step 4: Replace the body of `MainComponent::onMarkOut`**

Locate the existing `void MainComponent::onMarkOut()` (around app/MainComponent.cpp:616). Replace its entire body with:

```cpp
void MainComponent::onMarkOut()
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    if (auto region = captureSession_.markOut (t))
    {
        const ida::CaptureRestorePoint restorePoint {
            region->inLmcSeconds, region->tape };

        auto result = promotion::promote (
            *undoStack_.current(),
            demo_.sessionToLmc,
            *region,
            region->inLmcSeconds,
            [this] { return ConstituentId (nextConstituentId_++); });

        undoStack_.push (
            std::make_shared<const Constituent> (std::move (result.newRoot)),
            result.undoLabel,
            restorePoint);

        announceCapture (*region, result);
        refreshPerformance();
        refreshPreparation();
    }

    refreshCaptureControls();
    refreshDiagnostics();
}
```

- [ ] **Step 5: Replace the body of `announceCapture`**

Locate the existing `void MainComponent::announceCapture (const CaptureRegion& region, int loopNumber)` (around app/MainComponent.cpp:629). Replace its signature and body with:

```cpp
void MainComponent::announceCapture (const CaptureRegion& region,
                                     const promotion::PromotionResult& result)
{
    const double seconds = (region.outLmcSeconds - region.inLmcSeconds).toDouble();
    juce::String msg;

    if (result.mintedPhraseId.has_value())
    {
        msg << "Phrase captured  ·  "
            << juce::String (seconds, 2) << " s  ·  tape #"
            << juce::String ((juce::int64) region.tape.value());
    }
    else
    {
        // Walk the new root to recover the host Phrase's display name for
        // the banner. Cheap — root is small at M3.
        std::string hostName;
        std::function<void (const Constituent&)> findLoop;
        findLoop = [&] (const Constituent& c)
        {
            for (const auto& child : c.children())
            {
                if (child->id() == result.addedLoopId)
                {
                    hostName = c.name();
                    return;
                }
                findLoop (*child);
                if (! hostName.empty()) return;
            }
        };
        findLoop (*undoStack_.current());

        msg << "Loop added to "
            << (hostName.empty() ? juce::String ("phrase") : juce::String (hostName))
            << "  ·  "
            << juce::String (seconds, 2) << " s  ·  tape #"
            << juce::String ((juce::int64) region.tape.value());
    }

    captureBanner_->show (msg);
}
```

- [ ] **Step 6: Update `refreshDiagnostics` to no longer reference `capturedRegions_`**

Locate the block around app/MainComponent.cpp:565 that builds the `Regions: ...` diagnostic line from `capturedRegions_.size()` and `capturedRegions_.back()`. The simplest M3 swap: count Loops in the current root tree and report the most-recent one's tape id. Replace the block with:

```cpp
    int loopCount = 0;
    std::optional<TapeId> lastTape;
    Rational lastIn  { 0 }, lastOut { 0 };
    std::function<void (const Constituent&)> count;
    count = [&] (const Constituent& c)
    {
        if (const auto& ref = c.tapeReference())
        {
            ++loopCount;
            lastTape = ref->tape;
            lastIn   = ref->tapeIn;
            lastOut  = ref->tapeOut;
        }
        for (const auto& child : c.children())
            count (*child);
    };
    count (*undoStack_.current());

    juce::String captureLine;
    captureLine << "    Regions: " << juce::String (loopCount);
    if (lastTape.has_value())
    {
        captureLine << " (last: "
                    << juce::String (lastIn.toDouble(),  2) << " s -> "
                    << juce::String (lastOut.toDouble(), 2) << " s, "
                    << juce::String ((lastOut - lastIn).toDouble(), 2)
                    << " s long  ·  tape #"
                    << juce::String ((juce::int64) lastTape->value()) << ")";
    }
```

(This preserves the operator-visible diagnostic introduced in commit `ee34812`. Adjust to match exact wording in the file.)

- [ ] **Step 7: Build and run the full test suite**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 233 tests pass (no UI tests broke; promotion tests still green).

- [ ] **Step 8: Operator-side verification — open the .app and capture**

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

In the app:
1. Arm input. Mark In, Mark Out — banner shows `Phrase captured ...`. New Pill appears on the timeline.
2. Mark In with playhead inside the new Pill. Mark Out. Banner shows `Loop added to capture ...`. No new Pill.
3. Mark In far past the new Pill. Mark Out. Banner shows `Phrase captured ...`. Second Pill appears.

If any of these fail, do not proceed — debug before committing.

- [ ] **Step 9: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: MainComponent — onMarkOut auto-promotes captures into the session tree"
```

---

## Task 8: MainComponent::onUndo — restore CaptureSession from CaptureRestorePoint

**Files:**
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Locate and update `onUndo` (around app/MainComponent.cpp:670)**

Replace its body with:

```cpp
void MainComponent::onUndo()
{
    if (undoStack_.canUndo())
    {
        // The entry we are about to leave (the current top) is the one whose
        // restore point applies — undoing a promotion entry restores the
        // CaptureSession state that existed before that promotion fired.
        const auto restoreOnLeave = undoStack_.currentEntryRestorePoint();

        undoStack_.undo();

        if (restoreOnLeave.has_value())
        {
            // Re-enter AwaitingOut with the original Mark In intact. The
            // operator can immediately Mark Out again at a different time, or
            // hit Disarm to abandon. Tape samples between the original Mark
            // In and any future Mark Out are still on the always-running tape.
            captureSession_.arm();  // ensure not Disarmed (no-op if already armed)
            captureSession_.markIn (restoreOnLeave->pendingIn,
                                    restoreOnLeave->pendingTape);
        }

        refreshPerformance();
        refreshPreparation();
        refreshCaptureControls();
        refreshDiagnostics();
    }
}
```

- [ ] **Step 2: Build, run full tests**

```bash
cmake --build build && ./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 233 tests pass — no test regressions; this change is exercised manually.

- [ ] **Step 3: Operator-side verification**

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

1. Arm. Mark In, Mark Out. Pill appears, banner shows.
2. Hit bottom-bar Undo. Pill disappears. The capture controls show `Mark Out` available again — this means CaptureSession is back in AwaitingOut. (Confirm by inspecting the bottom-bar button states and the diagnostics line.)
3. Move playhead forward. Mark Out again. New Pill at the *new* span, no re-arm needed.

- [ ] **Step 4: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: MainComponent — undo of a promotion restores AwaitingOut + original in-point"
```

---

## Task 9: CaptureBanner — tap-to-undo + adapted message text

**Files:**
- Modify: `app/MainComponent.cpp` (CaptureBanner class is nested inside MainComponent.cpp per `app/MainComponent.h:112`)

- [ ] **Step 1: Locate the CaptureBanner class definition in `app/MainComponent.cpp`**

```bash
grep -n "class MainComponent::CaptureBanner\|^class CaptureBanner\|CaptureBanner *::" app/MainComponent.cpp | head -10
```

- [ ] **Step 2: Add `mouseDown` override and an `onUndoRequested` callback**

In the CaptureBanner class declaration body, add:

```cpp
    std::function<void()> onUndoRequested;

    void mouseDown (const juce::MouseEvent&) override
    {
        if (alpha_ > 0.0f && onUndoRequested)
            onUndoRequested();
    }
```

Where `alpha_` is the current animator-driven alpha (use whatever member already tracks visibility so that clicks are ignored once the banner has fully faded). If no such member exists, gate on `isVisible()` instead:

```cpp
    void mouseDown (const juce::MouseEvent&) override
    {
        if (isVisible() && onUndoRequested)
            onUndoRequested();
    }
```

- [ ] **Step 3: Wire the banner's callback in MainComponent's constructor**

In the constructor, after `captureBanner_` is created (`std::make_unique<CaptureBanner>(...)` or equivalent — find the line that adds it to the component), add:

```cpp
captureBanner_->onUndoRequested = [this] { onUndo(); };
```

- [ ] **Step 4: Update the banner's `paint` to render an `↶ Undo` hint on the right edge**

Inside `CaptureBanner::paint`, after the existing text draw, add a smaller right-aligned label:

```cpp
g.setColour (juce::Colours::amber.withAlpha (0.65f));
g.setFont   (juce::Font (12.0f, juce::Font::bold));
g.drawText  ("↶ Undo", getLocalBounds().reduced (12, 0),
             juce::Justification::centredRight, true);
```

Adjust colour and font to whatever the existing main label uses (read the file first for the exact `setColour` / `setFont` calls and match conventions).

- [ ] **Step 5: Build and operator-verify**

```bash
cmake --build build
open "build/app/IDA_artefacts/Release/IDA.app"
```

1. Capture a phrase. Banner appears with text on the left and `↶ Undo` on the right.
2. Tap the banner within 1.5s — Pill disappears, CaptureSession returns to AwaitingOut (confirm via bottom-bar Mark Out being available without re-arming).
3. Capture another phrase. Wait 2 seconds (banner fully faded). Click where the banner was — nothing happens (the banner is no longer visible / not clickable).

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: CaptureBanner — tap to undo within the visible window"
```

---

## Task 10: End-to-end verification + final commit

**Files:** none (verification only)

- [ ] **Step 1: Clean build from scratch**

```bash
cd /Users/larryseyer/IDA
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build 2>&1 | tail -5
```

Expected: zero warnings from any source we control; build succeeds.

- [ ] **Step 2: Full test suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 233 tests pass, ~4080+ assertions, no failures.

- [ ] **Step 3: Operator-side end-to-end script**

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

Run through the user-guide chapter 1 workflow exactly:

1. **Pass 1 (single instrument, build sections):** Arm input. Mark In at 0:01, Mark Out at 0:04 → banner `Phrase captured ...`, new Pill labeled `capture`. Mark In at 0:05, Mark Out at 0:08 → second Pill. Repeat once more → third Pill.
2. **Pass 2 (overdub):** Scrub playhead to 0:02 (inside Pill 1). Switch focused tape to a different input. Arm. Mark In at 0:02, Mark Out at 0:03 → banner `Loop added to capture ...`. No new Pill, but the existing Pill 1 should reflect a new Loop child in the diagnostics `Regions: N`.
3. **Recovery:** Mark In at 0:00, Mark Out at 0:01 → banner. Tap the banner. Pill disappears, Mark Out is immediately available again (capture state restored). Mark Out at 0:04 → banner shows the *new* span, new Pill appears.
4. **Multi-instance guard (manual confirmation):** Try to load a session that contains repeated placements (none exist today; skip if not constructible). If it ever happens to hit, the operator should see a clean abort, not a corrupt tree.

- [ ] **Step 4: Update `continue.md` for next session**

Per the project's session-continuation pattern, write a fresh handoff doc capturing:
- This session shipped capture promotion end-to-end.
- Tests went from 226 → 233.
- The shared-placement architecture is now the explicit next big topic.
- Reference the design doc, the user guide, and the todo.md entries.

(Use the `continue` skill if running interactively; otherwise hand-write a similar structure to the existing `continue.md`.)

- [ ] **Step 5: Final commit (if `continue.md` was updated)**

```bash
git add continue.md
git commit -m "docs: continue.md — capture promotion shipped end-to-end"
```

---

## Self-Review

**Spec coverage:** every section of the spec has at least one task implementing it.
- §1 Promotion semantics → Tasks 3, 4 (host + mint).
- §2 promotion module / API → Task 2 (skeleton) + 3, 4, 5, 6 (behaviour).
- §3 Write-protect guard → Task 2 (test + impl).
- §4 MainComponent wiring → Task 7.
- §5 Undo restoration of capture state → Task 1 (UndoStack) + Task 8 (MainComponent::onUndo).
- §6 CaptureBanner tap-to-undo → Task 9.
- §7 Tests → covered inline in each TDD task.
- §8 User guide chapter → already shipped this session (commit `9a414fb`); referenced by Task 10's verification script.
- §9 todo.md entry → already shipped this session (commit `9a414fb`).
- Dependencies graph → reflected in the Task ordering.
- Out-of-scope → explicitly preserved (no task introduces shared-placement work).

**Placeholders:** none — every step shows the exact code or command. The two M3 simplifications (1:1 conceptual ↔ LMC; in-tree walk to find host name) are documented in code comments and in this plan's header so they cannot become silent debt.

**Type consistency:** `CaptureRestorePoint`, `PromotionResult`, `IdAllocator`, `promote()` signatures are identical across all tasks that reference them. `nextConstituentId_` is named consistently. `announceCapture`'s new signature matches between header (Task 7 Step 2) and impl (Task 7 Step 5).

**Scope check:** plan stays single-instance. Multi-instance work only ever appears as the runtime guard verifying the deferral is enforced.
