# Shared-Placement Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single-instance Constituent guard with shared-by-reference Phrase placements that carry per-instance overlay Loops, give the operator a long-press capture gesture for overlays and a "Vary this one" gesture for forks, and migrate the demo so the verse plays three times sharing one Phrase.

**Architecture:** Wrapper Constituents (`PhraseMetadata::role == "placement"`) carry one shared canonical Phrase as their first child and zero-or-more overlay Loops as subsequent children. A single inline predicate `isPlacementWrapper` in `Constituent.h` is the source of truth for "is this a wrapper." A new `arrangement::sequenceShared` primitive places the same `ChildPtr` at multiple offsets via the existing `IdAllocator` pattern. `promotion::promote` gains an `AttachmentMode` parameter and descends through wrappers when finding hosts; the runtime guard becomes pointer-aware (`enforceSharedInstancesAreShared`) so duplicate ids are legal iff every occurrence resolves to the same `shared_ptr`. The selector emits one Pill per wrapper, computes `sharedSiblings` by pointer-identity grouping, and the renderer surfaces sharing as a tie-bar, overlays as a dot, and forks as a prime mark. Fork is an irreversible deep-copy of the shared subtree onto one wrapper.

**Tech Stack:** C++20, JUCE 7 (UI only — `core/` stays JUCE-free), Catch2 (existing test harness), CMake + Ninja, Release builds only on iOS.

**Spec:** `docs/superpowers/specs/2026-05-16-shared-placement-design.md`

**M3 simplification (carry-over):** Promotion still treats conceptual time as 1:1 with LMC seconds when constructing new Loop / Phrase / wrapper boundaries. The demo's identity-rate tempo map preserves this. Non-trivial tempo maps remain future work; the inverse-mapping TODO survives unchanged.

**§15 operator-vocabulary review (every UI string in this plan):** *Would a musician understand this string without learning IDA's data model?* If no, the string fails. Reject any banner/menu/tooltip text containing `Wrapper`, `Placement`, `AttachmentMode`, `Fork`, `tape #<n>`, raw `ConstituentId`, or attachment-mode names. The four banner templates and the `"Vary this one"` menu label are frozen from spec §11 + §15 and reproduced inline in every task that uses them.

**Hidden-string scrub:** the legacy banner from the capture-promotion milestone shows `"Phrase captured  ·  3.42 s  ·  tape #200"` — that string is plumbing and must be replaced by the §11 templates in Task 7. Audit checks for `tape #` and `s  ·` patterns inside `MainComponent::announceCapture`.

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `core/include/ida/Constituent.h` | MODIFY | Add `inline bool isPlacementWrapper (const Constituent&)` free function (convention-not-type). |
| `core/include/ida/Arrangement.h` | MODIFY | Declare `sequenceShared (parent, phrase, offsets, allocateId)`. |
| `core/src/Arrangement.cpp` | MODIFY | Implement `sequenceShared` using existing `placedAt` neighbour pattern. |
| `core/include/ida/Promotion.h` | MODIFY | Add `enum class AttachmentMode { Shared, Overlay }`; extend `PromotionResult` with `resolvedMode` + `overlayPlacementIndex`; new `promote` signature taking `AttachmentMode`. |
| `core/src/Promotion.cpp` | MODIFY | Replace `enforceSingleInstance` with `enforceSharedInstancesAreShared` (pointer-aware); make `findHostRecursive` descend through wrappers; implement Overlay-attach path + downgrade rule. |
| `ui/include/ida/TimelineViewState.h` | MODIFY | Add `sharedSiblings`, `hasOverlays`, `isForked` fields to `PillState`. |
| `ui/src/TimelineViewState.cpp` | MODIFY | Wrapper-aware walk: emit one Pill per wrapper (suppress shared child); aggregate from shared child; second-pass pointer-identity grouping for `sharedSiblings`. |
| `ui/src/TimelineView.cpp` | MODIFY | Render tie-bar across `sharedSiblings`; overlay-dot when `hasOverlays`; prime mark when `isForked`. |
| `app/DemoSession.cpp` | MODIFY | Grow session to 24 whole notes; verse becomes one shared Phrase (id 20) placed at offsets 3/9/15 via `sequenceShared`, minting wrappers 51/52/53. |
| `app/MainComponent.h` | MODIFY | Add long-press timer member + pending-overlay-flag for Mark In gesture; declare `onPillContextMenu`. |
| `app/MainComponent.cpp` | MODIFY | Wire 500 ms long-press on `markInButton_` to upgrade pending `AttachmentMode`; rewrite `announceCapture` to use the four §11 templates; pass `requestedMode` into `promote`; wire right-click / long-press on Pills to a one-item "Vary this one" popup that runs the fork edit through `UndoStack`. |
| `docs/IDA User Guide.md` | MODIFY | Roadmap line: "repeating song sections" lands; dedicated chapter follows after operator verification. |
| `tests/ArrangementTests.cpp` | MODIFY | New cases: `sequenceShared` shape, pointer-equal sharing across three offsets, null/empty rejections, mixed bare+wrapped sequence. |
| `tests/ConstituentTests.cpp` | MODIFY | New case: `isPlacementWrapper` predicate truth-table (bare Phrase, wrapper, hybrid, Loop, role-mismatched Phrase). |
| `tests/PromotionTests.cpp` | MODIFY | Replace the multi-instance-throws case; new cases: pointer-aware guard accepts true sharing + rejects aliased mistakes; host descends through wrapper; Overlay attaches to wrapper with correct `overlayPlacementIndex`; Overlay outside wrapper downgrades. |
| `tests/TimelineViewStateTests.cpp` | MODIFY | New cases: wrapper emits one Pill; Pill content delegates to shared child; `sharedSiblings` populated for shared placements + empty for bare; `hasOverlays` flips on overlay; `isForked` flips on `forked-placement` role. |
| `tests/DemoSessionTests.cpp` | MODIFY | Update `[demoSession][shape]` to 24-whole-note span + intro/3 verse wrappers/outro structure; add `[demoSession][shared]` pointer-equality assertion. |

---

## Task 1: `isPlacementWrapper` predicate + `arrangement::sequenceShared`

**Files:**
- Modify: `core/include/ida/Constituent.h`
- Modify: `core/include/ida/Arrangement.h`
- Modify: `core/src/Arrangement.cpp`
- Modify: `tests/ConstituentTests.cpp`
- Modify: `tests/ArrangementTests.cpp`

- [ ] **Step 1: Write the failing predicate truth-table test in `tests/ConstituentTests.cpp`**

Append at the end of the file (above the closing namespace if there is one — match the file's existing convention):

```cpp
TEST_CASE ("isPlacementWrapper recognises wrappers and rejects non-wrappers",
           "[constituent][placementWrapper]")
{
    using ida::Constituent;
    using ida::ConstituentId;
    using ida::PhraseMetadata;
    using ida::Position;
    using ida::Rational;
    using ida::TapeId;
    using ida::TapeReference;

    const Constituent emptyShell (ConstituentId (1), Position(),
                                  Position (Rational (4)));

    SECTION ("bare Phrase is NOT a wrapper")
    {
        const auto bare = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "verse" });
        CHECK_FALSE (ida::isPlacementWrapper (bare));
    }

    SECTION ("Loop is NOT a wrapper")
    {
        const auto leaf = emptyShell.withTapeReference (
            TapeReference (TapeId (1), Rational (0), Rational (4)));
        CHECK_FALSE (ida::isPlacementWrapper (leaf));
    }

    SECTION ("Phrase whose role is not 'placement' is NOT a wrapper, even with a Phrase child")
    {
        auto child = std::make_shared<const Constituent> (
            emptyShell.withPhraseMetadata (PhraseMetadata { .role = "verse" }));
        const auto outer = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "verse" }).withChildAdded (child);
        CHECK_FALSE (ida::isPlacementWrapper (outer));
    }

    SECTION ("role=='placement' Phrase whose first child is a Phrase IS a wrapper")
    {
        auto sharedPhrase = std::make_shared<const Constituent> (
            emptyShell.withPhraseMetadata (PhraseMetadata { .role = "verse" }));
        const auto wrapper = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "placement" }).withChildAdded (sharedPhrase);
        CHECK (ida::isPlacementWrapper (wrapper));
    }

    SECTION ("role=='placement' Phrase whose first child is a Loop is NOT a wrapper")
    {
        auto leaf = std::make_shared<const Constituent> (
            emptyShell.withTapeReference (
                TapeReference (TapeId (1), Rational (0), Rational (4))));
        const auto bogus = emptyShell.withPhraseMetadata (
            PhraseMetadata { .role = "placement" }).withChildAdded (leaf);
        CHECK_FALSE (ida::isPlacementWrapper (bogus));
    }
}
```

If `tests/ConstituentTests.cpp` does not already `#include <memory>` and the relevant sirius headers (`Constituent.h`, `Phrase.h`, `TapeReference.h`, `Position.h`, `Rational.h`, `TapeId.h`), add them.

- [ ] **Step 2: Run tests to verify the build fails (predicate not declared)**

```bash
cd /Users/larryseyer/IDA
cmake --build build --target IdaTests 2>&1 | tail -20
```

Expected: compile error mentioning `isPlacementWrapper` — the symbol does not exist yet.

- [ ] **Step 3: Add the predicate to `core/include/ida/Constituent.h`**

Immediately after the closing `}` of the `class Constituent` and before the `} // namespace sirius`, insert:

```cpp
/// Wrapper recognition predicate — the single source of truth for the
/// "is this Constituent a shared-placement wrapper?" question. A wrapper is a
/// Phrase whose role is "placement" and whose first child is itself a Phrase
/// (the shared canonical Phrase the wrapper points at). Overlay Loops live
/// at children[i>=1]; the wrapper itself is never a hybrid (no TapeReference).
/// Convention, not a new type — every tree walker (selector, promotion,
/// renderer) goes through this one function so a future migration to a
/// dedicated `bool is_placement` field on Constituent is a one-file refactor.
inline bool isPlacementWrapper (const Constituent& c) noexcept
{
    return c.isPhrase()
        && c.phraseMetadata()->role == "placement"
        && ! c.children().empty()
        && c.children()[0]->isPhrase();
}
```

- [ ] **Step 4: Run the predicate test, verify it passes**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[constituent][placementWrapper]"
```

Expected: 1 test case, 5 SECTIONs, all pass.

- [ ] **Step 5: Write the failing `sequenceShared` tests in `tests/ArrangementTests.cpp`**

Append at the end of the file (after the existing RoleSlot tests):

```cpp
TEST_CASE ("sequenceShared places one wrapper per offset, all sharing the same ChildPtr",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (24)));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextId = 50;
    auto allocate = [&nextId] { return ConstituentId (nextId++); };

    const Constituent arranged = ida::arrangement::sequenceShared (
        parent, verse,
        { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
        allocate);

    REQUIRE (arranged.children().size() == 3);

    // Each wrapper has wrapper-shape: role="placement", first child is the
    // shared verse, no TapeReference, conceptualIn/Out is [offset, offset+6).
    for (std::size_t i = 0; i < arranged.children().size(); ++i)
    {
        const auto& wrapper = *arranged.children()[i];
        REQUIRE (ida::isPlacementWrapper (wrapper));
        CHECK (wrapper.phraseMetadata()->role == "placement");
        CHECK_FALSE (wrapper.tapeReference().has_value());
        CHECK (wrapper.children().size() == 1);
    }

    // Pointer-identity equality — the canary that proves real sharing.
    CHECK (arranged.children()[0]->children()[0].get()
           == arranged.children()[1]->children()[0].get());
    CHECK (arranged.children()[1]->children()[0].get()
           == arranged.children()[2]->children()[0].get());

    // Wrapper ids minted in offset order from the allocator.
    CHECK (arranged.children()[0]->id().value() == 50);
    CHECK (arranged.children()[1]->id().value() == 51);
    CHECK (arranged.children()[2]->id().value() == 52);

    // Wrapper spans cover [offset, offset + verse->duration()).
    CHECK (arranged.children()[0]->conceptualIn()  == Position (Rational (3)));
    CHECK (arranged.children()[0]->conceptualOut() == Position (Rational (9)));
    CHECK (arranged.children()[1]->conceptualIn()  == Position (Rational (9)));
    CHECK (arranged.children()[1]->conceptualOut() == Position (Rational (15)));
    CHECK (arranged.children()[2]->conceptualIn()  == Position (Rational (15)));
    CHECK (arranged.children()[2]->conceptualOut() == Position (Rational (21)));
}

TEST_CASE ("sequenceShared rejects a null phrase and an empty offset list",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (12)));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    auto allocate = [] { return ConstituentId (99); };

    CHECK_THROWS_AS (
        ida::arrangement::sequenceShared (
            parent, nullptr,
            { Position (Rational (0)) }, allocate),
        std::invalid_argument);

    CHECK_THROWS_AS (
        ida::arrangement::sequenceShared (parent, verse, {}, allocate),
        std::invalid_argument);
}

TEST_CASE ("sequenceShared composes with the existing bare sequence",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (24)));

    const auto intro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withPhraseMetadata (PhraseMetadata { .role = "intro" }));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    const auto outro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (30), Position(), Position (Rational (3)))
            .withPhraseMetadata (PhraseMetadata { .role = "outro" }));

    std::int64_t nextId = 50;
    auto allocate = [&nextId] { return ConstituentId (nextId++); };

    // intro (bare) + verse×3 (wrapped) + outro (bare).
    const Constituent withIntro = ida::arrangement::sequence (parent, { intro });
    const Constituent withVerses = ida::arrangement::sequenceShared (
        withIntro, verse,
        { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
        allocate);
    const Constituent full = ida::arrangement::sequence (
        withVerses, { outro });

    REQUIRE (full.children().size() == 5);
    CHECK (full.children()[0]->id().value() == 10);                       // intro
    CHECK (ida::isPlacementWrapper (*full.children()[1]));             // verse wrapper A
    CHECK (ida::isPlacementWrapper (*full.children()[2]));             // wrapper B
    CHECK (ida::isPlacementWrapper (*full.children()[3]));             // wrapper C
    // arrangement::sequence places its `outro` argument at childrenEnd, which
    // is the last wrapper's conceptualOut = Rational (21).
    CHECK (full.children()[4]->id().value() == 30);
    CHECK (full.children()[4]->conceptualIn()  == Position (Rational (21)));
    CHECK (full.children()[4]->conceptualOut() == Position (Rational (24)));
}
```

If `tests/ArrangementTests.cpp` does not already `#include "sirius/Phrase.h"`, `#include "sirius/Constituent.h"` (for `isPlacementWrapper`), or `#include <stdexcept>`, add them.

- [ ] **Step 6: Run the new tests to verify they fail (sequenceShared not declared)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -10
```

Expected: compile error mentioning `sequenceShared`.

- [ ] **Step 7: Declare `sequenceShared` in `core/include/ida/Arrangement.h`**

Add `#include "sirius/Promotion.h"` is NOT what we want (avoids dependency cycles). The `IdAllocator` typedef should be referenced as a `std::function<ConstituentId()>` without pulling in promotion's header. The cleanest path: define an arrangement-local typedef with the same shape.

In the `namespace arrangement` block, immediately after the `layer` declaration, add:

```cpp
/// Callable that mints a fresh ConstituentId on each call. Same shape as
/// `ida::promotion::IdAllocator`; defined locally here so Arrangement.h
/// does not depend on Promotion.h.
using IdAllocator = std::function<ConstituentId()>;

/// Places `phrase` at each offset in `offsets`, producing one wrapper
/// Constituent per offset. All wrappers share `phrase` by reference (the
/// same shared_ptr). Each wrapper's conceptualIn/Out spans
/// [offset, offset + phrase->duration()) in `parent`'s local time, carries
/// PhraseMetadata{ role = "placement" }, and holds the shared `phrase` as
/// its only child. The wrappers are appended to `parent` in the supplied
/// order.
///
/// `allocateId` is called once per offset to mint the wrapper id, in offset
/// order, matching the IdAllocator pattern used by `promotion::promote`.
///
/// Throws std::invalid_argument if `phrase` is null or `offsets` is empty.
Constituent sequenceShared (const Constituent&             parent,
                            const Constituent::ChildPtr&   phrase,
                            const std::vector<Position>&   offsets,
                            const IdAllocator&             allocateId);
```

Add `#include <functional>` at the top of `Arrangement.h` if not already present.

- [ ] **Step 8: Implement `sequenceShared` in `core/src/Arrangement.cpp`**

Inside `namespace arrangement`, after `layer`, add:

```cpp
Constituent sequenceShared (const Constituent&             parent,
                            const Constituent::ChildPtr&   phrase,
                            const std::vector<Position>&   offsets,
                            const IdAllocator&             allocateId)
{
    if (phrase == nullptr)
        throw std::invalid_argument (
            "ida::arrangement::sequenceShared: phrase must not be null");
    if (offsets.empty())
        throw std::invalid_argument (
            "ida::arrangement::sequenceShared: offsets must not be empty");

    const Rational phraseDuration = phrase->duration();

    Constituent result (parent);
    for (const auto& offset : offsets)
    {
        const Position wrapperIn  (offset);
        const Position wrapperOut (offset.wholeNotes() + phraseDuration);

        // The wrapper's id is freshly minted; its first (and at this stage
        // only) child is the shared phrase ChildPtr — passed through, never
        // copied, so pointer-identity equality across all wrappers holds.
        PhraseMetadata wrapperMeta;
        wrapperMeta.role = "placement";

        Constituent wrapper (allocateId(), wrapperIn, wrapperOut);
        wrapper = wrapper.withPhraseMetadata (std::move (wrapperMeta))
                         .withChildAdded (phrase);

        result = result.withChildAdded (
            std::make_shared<const Constituent> (std::move (wrapper)));
    }

    return result;
}
```

- [ ] **Step 9: Build and run the new arrangement tests, verify they pass**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[arrangement][sequenceShared]"
```

Expected: 3 test cases, all assertions pass.

- [ ] **Step 10: Run the full suite to verify no regression**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 235 + 4 new test cases = 239 tests pass (5 SECTIONs in the predicate test still count as 1 test case; sequenceShared adds 3 test cases; total +4).

- [ ] **Step 11: Commit**

```bash
git add core/include/ida/Constituent.h core/include/ida/Arrangement.h core/src/Arrangement.cpp tests/ConstituentTests.cpp tests/ArrangementTests.cpp
git commit -m "feat: arrangement — sequenceShared + isPlacementWrapper predicate"
git push origin master
```

---

## Task 2: Pointer-aware runtime guard

**Files:**
- Modify: `core/src/Promotion.cpp`
- Modify: `tests/PromotionTests.cpp`

The existing `enforceSingleInstance` throws on *any* repeated id. Replace it with `enforceSharedInstancesAreShared`: a tree walk that maps `id → shared_ptr` on first sight and, on every subsequent encounter of the same id, throws only when the encountered `shared_ptr` is pointer-distinct from the first one seen. True sharing (same `shared_ptr` at multiple positions) passes silently — that is the legitimate `sequenceShared` shape.

- [ ] **Step 1: Write the failing tests in `tests/PromotionTests.cpp`**

Replace the existing `TEST_CASE ("promote throws when any Constituent id appears more than once" …` block (currently around line 58) entirely with the two cases below. Keep all other test cases in the file unchanged.

```cpp
TEST_CASE ("promote accepts a tree containing shared placements (pointer-aware guard)",
           "[promotion][guard]")
{
    // Build a root whose verse is shared into three wrappers via sequenceShared.
    // Each wrapper has a distinct id; the inner verse Phrase is one shared
    // ChildPtr referenced three times. The pointer-aware guard must allow this.
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    Constituent root = ida::arrangement::sequenceShared (
        emptyRoot(), verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    // A capture into one of the wrappers: Mark In = 1 (inside wrapper A's
    // shared verse). promote() must not throw; the host walk lands in the
    // shared verse and adds the Loop there. Tested fully in Task 3 — here we
    // only assert the guard does not reject the shape.
    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;

    CHECK_NOTHROW (
        promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (1),
                 ida::promotion::AttachmentMode::Shared,
                 IdAllocator (std::ref (counter))));
}

TEST_CASE ("promote rejects aliased-id-by-mistake (pointer-distinct, same id)",
           "[promotion][guard]")
{
    // Build a root whose two top-level Phrases happen to share id 42 but are
    // distinct `Constituent` allocations — exactly the bug shape the old guard
    // caught. The pointer-aware guard must still throw on this.
    auto phraseA = std::make_shared<const Constituent> (
        Constituent (ConstituentId (42), Position(), Position (Rational (4)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    auto phraseB = std::make_shared<const Constituent> (
        Constituent (ConstituentId (42), Position (Rational (4)), Position (Rational (8)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    Constituent root = emptyRoot().withChildAdded (phraseA)
                                  .withChildAdded (phraseB);

    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;

    CHECK_THROWS_AS (
        promote (root, identityMap(), region, /*lmcAtMarkIn*/ Rational (1),
                 ida::promotion::AttachmentMode::Shared,
                 IdAllocator (std::ref (counter))),
        std::logic_error);
}
```

The first case uses an `AttachmentMode` argument that does not yet exist in `promote`; both will fail to compile until Task 3 adds it. **This is intentional** — we wedge the two changes against each other so neither can land alone. To make this task's TDD cycle complete on its own, temporarily add a stub `AttachmentMode` and a `promote` overload here, then unify in Task 3. The simpler course: skip the temporary stub, accept that this task's tests will not compile until Task 3 lands, and run the full suite at the end of Task 3 to verify both tasks together. Pick this latter approach to keep the diff small.

Therefore: after writing these two test cases, do **not** run them yet — Step 4 below builds and the build will not be green until Task 3 lands the new `promote` signature. Proceed with the implementation steps and let Task 3 close the loop.

- [ ] **Step 2: Replace `enforceSingleInstance` with `enforceSharedInstancesAreShared` in `core/src/Promotion.cpp`**

Delete the existing `enforceSingleInstance` function (lines 17-27 of the current file). Replace it with:

```cpp
    /// Walk `c` and throw std::logic_error on any aliased ConstituentId — that
    /// is, two Constituents with the same id that are *not* the same shared_ptr.
    /// Genuine sharing (one shared_ptr referenced from multiple positions in
    /// the tree, e.g. via sequenceShared) is allowed and walked exactly once
    /// per unique pointer. Replaces the strict single-instance guard.
    void enforceSharedInstancesAreShared (
        const Constituent::ChildPtr& cPtr,
        std::unordered_map<std::int64_t, const Constituent*>& firstSeenPointer)
    {
        const auto rawId = cPtr->id().value();
        auto [it, inserted] = firstSeenPointer.insert ({ rawId, cPtr.get() });
        if (! inserted)
        {
            if (it->second != cPtr.get())
                throw std::logic_error (
                    "ida::promotion: ConstituentId aliased across distinct allocations "
                    "(same id reached via different shared_ptr); shared placements must "
                    "share the same ChildPtr, not duplicate it");
            // Same id, same pointer → genuine sharing, already walked.
            return;
        }
        for (const auto& child : cPtr->children())
            enforceSharedInstancesAreShared (child, firstSeenPointer);
    }

    /// Convenience overload: walks the root by reference. The root itself has
    /// no enclosing shared_ptr so it cannot participate in aliasing; only its
    /// descendants need the pointer-aware check.
    void enforceSharedInstancesAreShared (const Constituent& root)
    {
        std::unordered_map<std::int64_t, const Constituent*> firstSeenPointer;
        firstSeenPointer.insert ({ root.id().value(), &root });
        for (const auto& child : root.children())
            enforceSharedInstancesAreShared (child, firstSeenPointer);
    }
```

Update the `#include` block at the top: replace `#include <unordered_set>` with `#include <unordered_map>`.

Update the call site inside `promote` (currently `std::unordered_set<std::int64_t> seen; enforceSingleInstance (root, seen);`) to:

```cpp
    enforceSharedInstancesAreShared (root);
```

- [ ] **Step 3: Build (will not link successfully until Task 3 lands, because the new test cases reference `AttachmentMode`)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -10
```

Expected: compile errors in `tests/PromotionTests.cpp` mentioning `AttachmentMode` and the new four-arg `promote`. The guard change itself compiles cleanly inside `core/src/Promotion.cpp`. **Do not commit yet** — Task 3 closes the loop.

---

## Task 3: Promotion `AttachmentMode` + new fields + wrapper-aware host walk

**Files:**
- Modify: `core/include/ida/Promotion.h`
- Modify: `core/src/Promotion.cpp`
- Modify: `tests/PromotionTests.cpp`
- Modify: `app/MainComponent.cpp` (one call-site update)

- [ ] **Step 1: Extend `core/include/ida/Promotion.h`**

Add the enum + fields + new signature. Inside `namespace ida::promotion`, immediately above `struct PromotionResult`, add:

```cpp
/// What the operator's gesture asked for. The default capture (a tap on Mark
/// In) is `Shared`: the captured Loop joins the shared Phrase so all
/// placements gain it. A long-press on Mark In requests `Overlay`: the
/// captured Loop attaches to the specific wrapper instance under the
/// playhead, peer to (not child of) the shared Phrase. `Overlay` with no
/// wrapper covering Mark In silently downgrades to `Shared` — see
/// `PromotionResult::resolvedMode`.
enum class AttachmentMode
{
    Shared,
    Overlay
};
```

Inside `struct PromotionResult`, after the existing `hostPhraseName` field and before `undoLabel`, add the two new fields (keep aggregate-initialisation order in mind — designated initialisers in the implementation make this safe):

```cpp
    /// What promote() actually did. Equal to the requested mode except in the
    /// Overlay-outside-any-wrapper case, where it downgrades to Shared.
    AttachmentMode resolvedMode { AttachmentMode::Shared };

    /// 1-based left-to-right placement index of the wrapper the Overlay was
    /// attached to. Present only when `resolvedMode == Overlay`.
    std::optional<std::size_t> overlayPlacementIndex;
```

Replace the `promote` declaration with the new signature:

```cpp
PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         AttachmentMode       requestedMode,
                         const IdAllocator&   allocateId);
```

Update the doc-comment paragraph above `promote` to mention the new parameter and the downgrade rule. Replace "shared-placement architecture is deferred work" with "the pointer-aware guard catches aliased ids — distinct Constituents sharing one id but not one ChildPtr".

- [ ] **Step 2: Update `core/src/Promotion.cpp` — host walk descends through wrappers + Overlay path + downgrade**

In `findHostRecursive` (currently lines 55-80 of the file), change the host-candidate check so a wrapper is **never** itself a host candidate; the walk simply descends into the wrapper's first child. Replace the body of `findHostRecursive` with:

```cpp
    void findHostRecursive (const Constituent& c,
                            const ParentToLmc& parentToLmc,
                            Rational           lmcAtMarkIn,
                            std::vector<std::size_t>& currentPath,
                            std::optional<HostHit>& deepestSoFar)
    {
        const Rational startLmc = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational endLmc   = parentToLmc (c.conceptualOut().wholeNotes());

        // Wrappers are placement carriers, never musical hosts — the host the
        // operator means is the shared Phrase below. The walk descends into
        // the wrapper's children but does not record the wrapper itself.
        // Hybrid Phrase+Loop Constituents are also forbidden hosts: a Loop is
        // a leaf, so its parent must be a pure Phrase container. Falling back
        // to mint when a hybrid would otherwise win keeps the captured Loop
        // attached to a structurally valid parent.
        const bool isWrapper = isPlacementWrapper (c);
        if (c.isPhrase() && ! c.tapeReference().has_value() && ! isWrapper
            && lmcAtMarkIn >= startLmc && lmcAtMarkIn < endLmc)
            deepestSoFar = HostHit { currentPath, startLmc, endLmc, c.name() };

        const auto childMap = childMapping (c, parentToLmc);
        for (std::size_t i = 0; i < c.children().size(); ++i)
        {
            currentPath.push_back (i);
            findHostRecursive (*c.children()[i], childMap, lmcAtMarkIn,
                               currentPath, deepestSoFar);
            currentPath.pop_back();
        }
    }
```

Update the `promote` signature and body. Replace the function definition (`PromotionResult promote (...)`) with:

```cpp
PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         AttachmentMode       requestedMode,
                         const IdAllocator&   allocateId)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "ida::promotion::promote: region duration must be strictly positive");

    enforceSharedInstancesAreShared (root);

    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    // --- Overlay path: find the deepest enclosing wrapper, attach Loop as
    //     peer of its shared Phrase. Falls through to Shared on downgrade. ---
    if (requestedMode == AttachmentMode::Overlay)
    {
        struct WrapperHit
        {
            std::vector<std::size_t> path;       // path to the wrapper itself
            Rational wrapperStartLmc;
            Rational wrapperEndLmc;
            std::size_t placementIndex { 0 };    // 1-based left-to-right
        };

        // Two-pass: first enumerate all wrappers in tree order with their LMC
        // spans, then pick the deepest one containing lmcAtMarkIn.
        std::vector<WrapperHit> allWrappers;
        std::vector<std::size_t> path;
        std::function<void (const Constituent&, const ParentToLmc&)> collect;
        collect = [&] (const Constituent& c, const ParentToLmc& parentToLmc)
        {
            if (isPlacementWrapper (c))
            {
                const Rational s = parentToLmc (c.conceptualIn().wholeNotes());
                const Rational e = parentToLmc (c.conceptualOut().wholeNotes());
                allWrappers.push_back ({ path, s, e, allWrappers.size() + 1 });
            }
            const auto childMap = childMapping (c, parentToLmc);
            for (std::size_t i = 0; i < c.children().size(); ++i)
            {
                path.push_back (i);
                collect (*c.children()[i], childMap);
                path.pop_back();
            }
        };
        collect (root, rootToLmc);

        std::optional<WrapperHit> hit;
        for (const auto& w : allWrappers)
            if (lmcAtMarkIn >= w.wrapperStartLmc && lmcAtMarkIn < w.wrapperEndLmc)
                hit = w;  // last write wins → deepest (collect order is parents-before-children)

        if (hit.has_value())
        {
            const auto loopId = allocateId();

            // The overlay Loop's conceptual bounds are local to the wrapper:
            // [Mark In − wrapperStart, Mark Out − wrapperStart), clamped to
            // the wrapper's span (M3 1:1 conceptual ↔ LMC).
            const Rational clampedInLmc  = std::max (region.inLmcSeconds,  hit->wrapperStartLmc);
            const Rational clampedOutLmc = std::min (region.outLmcSeconds, hit->wrapperEndLmc);
            const Position loopIn  (clampedInLmc  - hit->wrapperStartLmc);
            const Position loopOut (clampedOutLmc - hit->wrapperStartLmc);

            Constituent loop (loopId, loopIn, loopOut);
            loop = loop.withTapeReference (
                TapeReference (region.tape,
                               region.inLmcSeconds, region.outLmcSeconds));

            // Splice the overlay Loop into the wrapper. The wrapper itself is
            // the node at `hit->path`; we replace it with a copy that has the
            // Loop appended as a new child (children[>=1] is overlay territory).
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

            return PromotionResult {
                .newRoot              = std::move (newRoot),
                .addedLoopId          = loopId,
                .mintedPhraseId       = std::nullopt,
                .hostPhraseName       = std::nullopt,  // shared Phrase exists but is not the host
                .undoLabel            = "capture overlay",
                .resolvedMode         = AttachmentMode::Overlay,
                .overlayPlacementIndex = hit->placementIndex,
            };
        }
        // Fall through: no wrapper covers Mark In → silent downgrade to Shared.
    }

    // --- Shared path: existing host-Phrase walk; descends through wrappers. ---
    std::vector<std::size_t> path;
    std::optional<HostHit> hit;
    findHostRecursive (root, rootToLmc, lmcAtMarkIn, path, hit);

    if (hit.has_value())
    {
        const auto loopId = allocateId();

        const Rational clampedInLmc  = std::max (region.inLmcSeconds,  hit->hostStartLmc);
        const Rational clampedOutLmc = std::min (region.outLmcSeconds, hit->hostEndLmc);
        const Position loopIn  (clampedInLmc  - hit->hostStartLmc);
        const Position loopOut (clampedOutLmc - hit->hostStartLmc);

        Constituent loop (loopId, loopIn, loopOut);
        loop = loop.withTapeReference (
            TapeReference (region.tape,
                           region.inLmcSeconds, region.outLmcSeconds));

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

        return PromotionResult {
            .newRoot              = std::move (newRoot),
            .addedLoopId          = loopId,
            .mintedPhraseId       = std::nullopt,
            .hostPhraseName       = hit->hostName,
            .undoLabel            = std::move (label),
            .resolvedMode         = AttachmentMode::Shared,
            .overlayPlacementIndex = std::nullopt,
        };
    }

    // --- Mint case: no host, fresh Phrase at the song root. ---
    const auto phraseId = allocateId();
    const auto loopId   = allocateId();

    const Position phraseIn  (region.inLmcSeconds);
    const Position phraseOut (region.outLmcSeconds);
    const Position loopIn;
    const Position loopOut (region.outLmcSeconds - region.inLmcSeconds);

    Constituent loop (loopId, loopIn, loopOut);
    loop = loop.withTapeReference (
        TapeReference (region.tape,
                       region.inLmcSeconds, region.outLmcSeconds));

    Constituent newPhrase (phraseId, phraseIn, phraseOut);
    newPhrase = newPhrase.withPhraseMetadata (PhraseMetadata { .role = "capture" })
                         .withChildAdded (std::make_shared<const Constituent> (loop));

    Constituent newRoot = root.withChildAdded (
        std::make_shared<const Constituent> (newPhrase));

    return PromotionResult {
        .newRoot              = std::move (newRoot),
        .addedLoopId          = loopId,
        .mintedPhraseId       = phraseId,
        .hostPhraseName       = std::nullopt,
        .undoLabel            = "capture phrase",
        .resolvedMode         = AttachmentMode::Shared,
        .overlayPlacementIndex = std::nullopt,
    };
}
```

Make sure the `#include` block keeps `Constituent.h` (which now provides `isPlacementWrapper`) and adds `<functional>` if not already present.

- [ ] **Step 3: Update the one call site in `app/MainComponent.cpp`**

The current call (around line 679) reads:

```cpp
        auto result = promotion::promote (
            *undoStack_.current(),
            demo_.sessionToLmc,
            *region,
            region->inLmcSeconds,
            [this] { return ConstituentId (nextConstituentId_++); });
```

Add the new parameter, defaulted to `Shared` for now; Task 7 wires the long-press state in. Replace the call with:

```cpp
        auto result = promotion::promote (
            *undoStack_.current(),
            demo_.sessionToLmc,
            *region,
            region->inLmcSeconds,
            pendingOverlay_ ? promotion::AttachmentMode::Overlay
                            : promotion::AttachmentMode::Shared,
            [this] { return ConstituentId (nextConstituentId_++); });

        // Consume the pending-overlay flag — the next capture starts fresh.
        pendingOverlay_ = false;
```

Add the `pendingOverlay_` member to `app/MainComponent.h` in the private member block alongside `nextConstituentId_`:

```cpp
    bool pendingOverlay_ { false };
```

(Long-press wiring lands in Task 7. Until then, the flag stays false so behavior matches the prior `Shared`-only call.)

- [ ] **Step 4: Extend the test fixture and add the new behavioural tests in `tests/PromotionTests.cpp`**

Update the `using ida::promotion::promote;` alias section at the top of the file to also pull in `AttachmentMode`:

```cpp
using ida::promotion::AttachmentMode;
using ida::promotion::IdAllocator;
using ida::promotion::promote;
```

Find every existing call to `promote (...)` in the file and add the new `AttachmentMode::Shared` argument in fifth position, before the `IdAllocator`. There are calls inside these cases (per the file inventory): host, mint (×2), straddle, hybrid rejection, defensive (×2). Each gets the same fifth argument: `AttachmentMode::Shared`.

Then append the four new behavioural tests at the end of the file:

```cpp
TEST_CASE ("promote with Shared and a wrapper covering Mark In adds the Loop to the shared Phrase",
           "[promotion][shared][wrapper]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    Constituent root = ida::arrangement::sequenceShared (
        emptyRoot(), verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    // Mark In = 5 → wrapper B [4,8). Shared mode → host walk descends through
    // wrapper, lands in the shared verse Phrase. The Loop is added to the
    // shared verse, so it appears under all three wrappers.
    const CaptureRegion region { TapeId (200), Rational (5), Rational (7) };
    Counter counter;
    auto result = promote (root, identityMap(), region, Rational (5),
                           AttachmentMode::Shared,
                           IdAllocator (std::ref (counter)));

    CHECK (result.resolvedMode == AttachmentMode::Shared);
    CHECK_FALSE (result.overlayPlacementIndex.has_value());
    REQUIRE (result.hostPhraseName.has_value());
    CHECK (*result.hostPhraseName == "verse");

    // All three wrappers' shared verse now contains the new Loop. Pointer
    // identity across the three first-children persists post-edit, because
    // copy-on-write replaced exactly one path (the path to the verse) and
    // every wrapper that referenced it now references the replaced version.
    REQUIRE (result.newRoot.children().size() == 3);
    const auto* sharedAfter = result.newRoot.children()[0]->children()[0].get();
    CHECK (sharedAfter == result.newRoot.children()[1]->children()[0].get());
    CHECK (sharedAfter == result.newRoot.children()[2]->children()[0].get());
    REQUIRE (sharedAfter->children().size() == 1);
    CHECK (sharedAfter->children()[0]->id() == result.addedLoopId);
}

TEST_CASE ("promote with Overlay attaches the Loop to the specific wrapper, others unchanged",
           "[promotion][overlay]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    Constituent root = ida::arrangement::sequenceShared (
        emptyRoot(), verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    // Mark In = 5 → wrapper B [4,8). Overlay mode → Loop is a child of
    // wrapper B at index ≥ 1 (peer of the shared verse), not a child of the
    // shared verse. Wrappers A and C are unchanged.
    const CaptureRegion region { TapeId (300), Rational (5), Rational (7) };
    Counter counter;
    auto result = promote (root, identityMap(), region, Rational (5),
                           AttachmentMode::Overlay,
                           IdAllocator (std::ref (counter)));

    CHECK (result.resolvedMode == AttachmentMode::Overlay);
    REQUIRE (result.overlayPlacementIndex.has_value());
    CHECK (*result.overlayPlacementIndex == 2u);  // 1-based, wrapper B is #2

    REQUIRE (result.newRoot.children().size() == 3);
    // Wrapper B now has two children: the shared verse + the overlay Loop.
    const auto& wrapperB = *result.newRoot.children()[1];
    REQUIRE (wrapperB.children().size() == 2);
    CHECK (wrapperB.children()[0]->isPhrase());
    CHECK (wrapperB.children()[1]->isLoop());
    CHECK (wrapperB.children()[1]->id() == result.addedLoopId);

    // Wrappers A and C still have only the shared verse.
    CHECK (result.newRoot.children()[0]->children().size() == 1u);
    CHECK (result.newRoot.children()[2]->children().size() == 1u);

    // The Loop in B's overlay slot has wrapper-local conceptual bounds
    // (Mark In − wrapperStart = 5 − 4 = 1; Mark Out − wrapperStart = 3).
    CHECK (wrapperB.children()[1]->conceptualIn()  == Position (Rational (1)));
    CHECK (wrapperB.children()[1]->conceptualOut() == Position (Rational (3)));
}

TEST_CASE ("promote with Overlay outside any wrapper silently downgrades to Shared",
           "[promotion][overlay][downgrade]")
{
    // Root contains a bare verse [0,4) with no wrapper.
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    Constituent root = emptyRoot().withChildAdded (verse);

    const CaptureRegion region { TapeId (200), Rational (1), Rational (3) };
    Counter counter;
    auto result = promote (root, identityMap(), region, Rational (1),
                           AttachmentMode::Overlay,
                           IdAllocator (std::ref (counter)));

    CHECK (result.resolvedMode == AttachmentMode::Shared);  // downgraded
    CHECK_FALSE (result.overlayPlacementIndex.has_value());
    REQUIRE (result.hostPhraseName.has_value());
    CHECK (*result.hostPhraseName == "verse");
}

TEST_CASE ("promote with Overlay outside any wrapper AND no host mints a Phrase (downgrade then mint)",
           "[promotion][overlay][downgrade][mint]")
{
    Constituent root = emptyRoot();  // no children at all
    const CaptureRegion region { TapeId (200), Rational (10), Rational (12) };
    Counter counter;
    auto result = promote (root, identityMap(), region, Rational (10),
                           AttachmentMode::Overlay,
                           IdAllocator (std::ref (counter)));

    CHECK (result.resolvedMode == AttachmentMode::Shared);  // downgraded
    REQUIRE (result.mintedPhraseId.has_value());
    CHECK_FALSE (result.hostPhraseName.has_value());
    CHECK (result.undoLabel == "capture phrase");
}
```

- [ ] **Step 5: Clean build (CMake source list unchanged but enum + signature changes ripple through Promotion.h consumers)**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 6: Run the new promotion + guard tests**

```bash
./build/tests/IdaTests "[promotion]"
```

Expected: previously-passing tests still pass; 2 new guard cases pass; 4 new behavioural cases pass; total +6 promotion cases beyond the pre-Task-2 baseline (the old "throws on duplicate" case was replaced, not added).

- [ ] **Step 7: Run the full suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 239 (after Task 1) + 6 (Task 3 net new) − 1 (replaced multi-instance case) = 244 tests pass. *Note: existing DemoSession test still passes; Task 6 updates it.*

- [ ] **Step 8: Commit**

```bash
git add core/include/ida/Promotion.h core/src/Promotion.cpp tests/PromotionTests.cpp app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: promotion — AttachmentMode + pointer-aware guard + wrapper-aware host walk"
git push origin master
```

---

## Task 4: TimelineViewState wrapper-aware selector

**Files:**
- Modify: `ui/include/ida/TimelineViewState.h`
- Modify: `ui/src/TimelineViewState.cpp`
- Modify: `tests/TimelineViewStateTests.cpp`

- [ ] **Step 1: Extend `PillState` in `ui/include/ida/TimelineViewState.h`**

Inside `struct PillState`, after the existing `exitName` field and before the closing `}`, add:

```cpp
    /// For shared placements: the ids of the other wrappers that share the
    /// same underlying Phrase ChildPtr. The renderer draws a tie-bar across
    /// the wrappers in this set ∪ {this Pill's id}. Empty for bare Phrases
    /// and for forked placements.
    std::vector<ConstituentId> sharedSiblings;

    /// True iff this placement has instance-only overlay Loops (children at
    /// index ≥ 1 on the wrapper). Renderer draws the overlay-dot marker.
    bool hasOverlays { false };

    /// True iff this Pill represents a placement that was forked from a
    /// previously-shared one. Detected via PhraseMetadata::role ==
    /// "forked-placement". Renderer draws the prime mark.
    bool isForked { false };
```

- [ ] **Step 2: Write the failing selector tests in `tests/TimelineViewStateTests.cpp`**

Append at the end of the file (after the existing test cases):

```cpp
TEST_CASE ("selectTimelineView emits one Pill per wrapper, content delegated to shared Phrase",
           "[timelineView][shared]")
{
    auto verse = std::make_shared<const Constituent> (
        ida::arrangement::layer (
            Constituent (ConstituentId (20), Position(), Position (Rational (4)))
                .withName ("verse")
                .withPhraseMetadata (PhraseMetadata { .role = "verse",
                                                       .entrance = EntranceCharacter::Downbeat,
                                                       .exit     = ExitCharacter::HandOff }),
            { makeLoop (21, "verse: rhythm", Rational (4), 200) }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (12)));
    const Constituent root = ida::arrangement::sequenceShared (
        shell, verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const std::vector<InputDescriptor> inputs {
        { TapeId (200), InputKind::Audio, "Rhythm", 0 } };

    const auto state = ida::selectTimelineView (
        root, identity, inputs, /*armed*/ {}, /*focused*/ TapeId (200));

    // Three Pills, one per wrapper. Shared verse itself is suppressed.
    REQUIRE (state.pills.size() == 3);
    for (std::size_t i = 0; i < state.pills.size(); ++i)
    {
        const auto& pill = state.pills[i];
        // Pill id is the WRAPPER's id, not the shared Phrase's id.
        CHECK (pill.id.value() == static_cast<std::int64_t> (50 + i));
        // Pill content (loop count, primary tape, name, entrance/exit) comes
        // from the shared verse, not from the wrapper itself.
        CHECK (pill.name        == "verse");
        CHECK (pill.loopCount   == 1);
        CHECK (pill.primaryTape == TapeId (200));
        CHECK (pill.entranceName == "downbeat");
        CHECK (pill.exitName     == "hand-off");
    }
}

TEST_CASE ("selectTimelineView populates sharedSiblings via pointer-identity grouping",
           "[timelineView][shared]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (12)));
    const Constituent root = ida::arrangement::sequenceShared (
        shell, verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 3);

    // Pill A (id 50) shares with B (51) and C (52).
    CHECK (state.pills[0].sharedSiblings.size() == 2);
    CHECK (state.pills[1].sharedSiblings.size() == 2);
    CHECK (state.pills[2].sharedSiblings.size() == 2);

    auto containsId = [] (const std::vector<ConstituentId>& v, std::int64_t want)
    {
        for (const auto& id : v) if (id.value() == want) return true;
        return false;
    };
    CHECK (containsId (state.pills[0].sharedSiblings, 51));
    CHECK (containsId (state.pills[0].sharedSiblings, 52));
}

TEST_CASE ("selectTimelineView leaves sharedSiblings empty for bare Phrases",
           "[timelineView][bare]")
{
    const Constituent shell (ConstituentId (1), Position(), Position (Rational (6)));
    auto intro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withName ("intro")
            .withPhraseMetadata (PhraseMetadata { .role = "intro" }));
    const Constituent root = shell.withChildAdded (intro);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].sharedSiblings.empty());
    CHECK_FALSE (state.pills[0].hasOverlays);
    CHECK_FALSE (state.pills[0].isForked);
}

TEST_CASE ("selectTimelineView sets hasOverlays when a wrapper has overlay Loops",
           "[timelineView][overlay]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    auto overlay = std::make_shared<const Constituent> (
        Constituent (ConstituentId (60), Position(), Position (Rational (4)))
            .withTapeReference (TapeReference (TapeId (200), Rational (0), Rational (4))));

    // One wrapper, manually built so it has both the shared verse AND an overlay.
    PhraseMetadata wrapperMeta;
    wrapperMeta.role = "placement";
    const auto wrapper = std::make_shared<const Constituent> (
        Constituent (ConstituentId (50), Position(), Position (Rational (4)))
            .withPhraseMetadata (wrapperMeta)
            .withChildAdded (verse)
            .withChildAdded (overlay));

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (4)));
    const Constituent root = shell.withChildAdded (wrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].id.value() == 50);
    CHECK (state.pills[0].hasOverlays);
}

TEST_CASE ("selectTimelineView sets isForked when wrapper role is 'forked-placement'",
           "[timelineView][forked]")
{
    auto versePhrase = std::make_shared<const Constituent> (
        Constituent (ConstituentId (200), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    PhraseMetadata forkedMeta;
    forkedMeta.role = "forked-placement";
    const auto forked = std::make_shared<const Constituent> (
        Constituent (ConstituentId (50), Position(), Position (Rational (4)))
            .withPhraseMetadata (forkedMeta)
            .withChildAdded (versePhrase));

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (4)));
    const Constituent root = shell.withChildAdded (forked);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].isForked);
    CHECK (state.pills[0].sharedSiblings.empty());
}
```

If `tests/TimelineViewStateTests.cpp` doesn't already pull in `sirius/Arrangement.h`, add the include.

- [ ] **Step 3: Run the tests, verify they fail (wrapper not specially handled yet)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -10
```

Expected: build succeeds (the new fields default-initialise harmlessly); tests fail because the walker emits a Pill for the shared Phrase too (`pills.size() == 6` instead of 3), and `sharedSiblings` / `hasOverlays` / `isForked` stay at their defaults.

- [ ] **Step 4: Update `ui/src/TimelineViewState.cpp` — wrapper-aware walk + post-pass grouping**

Add `#include "sirius/Constituent.h"` is already present (transitively), and add an include for the predicate: it lives in the same header, so nothing new is needed.

Modify the `walk` function. When a wrapper is encountered, emit a Pill *for the wrapper* whose content (`name`, `loopCount`, `primaryTape`, `memberTapes`, `phraseLoopActive`, `entranceName`, `exitName`) is derived from the wrapper's first child (the shared Phrase). Mark `hasOverlays = (wrapper.children().size() >= 2)`. Mark `isForked = (wrapper.phraseMetadata()->role == "forked-placement")` — note that this branch handles **both** `"placement"` (sharing) and `"forked-placement"` (post-fork); the predicate `isPlacementWrapper` only matches the former, so we check the role explicitly when emitting wrapper-style Pills.

Replace the `walk` function's body with:

```cpp
    void walk (const Constituent& c,
               const ParentToLmc& parentToLmc,
               TimelineViewState& out,
               std::unordered_map<std::int64_t, const Constituent*>& wrapperSharedKey)
    {
        const Rational spanStart = parentToLmc (c.conceptualIn().wholeNotes());
        const Rational spanEnd   = parentToLmc (c.conceptualOut().wholeNotes());

        // Forked wrappers carry the same shape as placement wrappers — a
        // role-tagged Phrase whose first child is the (now-private) shared
        // Phrase — but with a different role. Handle both as "wrapper" for
        // Pill-emission purposes.
        const bool isPlacement       = isPlacementWrapper (c);
        const bool isForkedWrapper   = c.isPhrase()
                                    && c.phraseMetadata()->role == "forked-placement"
                                    && ! c.children().empty()
                                    && c.children()[0]->isPhrase();
        const bool isWrapperShape    = isPlacement || isForkedWrapper;

        if (isWrapperShape)
        {
            const auto& sharedChild = *c.children()[0];

            TapeAggregation agg;
            aggregate (sharedChild, agg);

            PillState pill;
            pill.id              = c.id();                       // wrapper's id
            pill.name            = sharedChild.name();           // from shared Phrase
            pill.startLmcSeconds = spanStart;
            pill.endLmcSeconds   = spanEnd;
            pill.loopCount       = agg.loopCount;
            pill.primaryTape     = pickPrimary (agg);
            pill.memberTapes     = agg.tapeOrder;
            const auto& card = sharedChild.repetitionRules().cardinality;
            pill.phraseLoopActive = ! std::holds_alternative<cardinality::Once> (card);

            const auto& meta = *sharedChild.phraseMetadata();
            pill.entranceName = describeEntrance (meta.entrance);
            pill.exitName     = describeExit     (meta.exit);

            pill.hasOverlays = c.children().size() >= 2;
            pill.isForked    = isForkedWrapper;

            // Stash the shared-child pointer keyed by wrapper id. Used by the
            // post-pass to group placement wrappers (NOT forked wrappers) by
            // pointer-identity for tie-bar grouping.
            if (isPlacement)
                wrapperSharedKey.insert ({ c.id().value(), c.children()[0].get() });

            out.pills.push_back (std::move (pill));

            // Walk children: skip the shared Phrase (already represented by
            // this Pill), but descend into overlay Loops. Overlay Loops are
            // leaves so the descent is shallow, but doing it consistently
            // keeps the recursion uniform.
            const auto childMap = childMapping (c, parentToLmc);
            for (std::size_t i = 1; i < c.children().size(); ++i)
                walk (*c.children()[i], childMap, out, wrapperSharedKey);
            return;
        }

        if (c.isPhrase())
        {
            // Bare Phrase (non-wrapper, non-forked) — existing behaviour.
            TapeAggregation agg;
            aggregate (c, agg);

            PillState pill;
            pill.id              = c.id();
            pill.name            = c.name();
            pill.startLmcSeconds = spanStart;
            pill.endLmcSeconds   = spanEnd;
            pill.loopCount       = agg.loopCount;
            pill.primaryTape     = pickPrimary (agg);
            pill.memberTapes     = agg.tapeOrder;
            const auto& card = c.repetitionRules().cardinality;
            pill.phraseLoopActive = ! std::holds_alternative<cardinality::Once> (card);
            const auto& meta = *c.phraseMetadata();
            pill.entranceName = describeEntrance (meta.entrance);
            pill.exitName     = describeExit     (meta.exit);
            out.pills.push_back (std::move (pill));
        }

        if (! c.children().empty())
        {
            const auto childMap = childMapping (c, parentToLmc);
            for (const auto& child : c.children())
                walk (*child, childMap, out, wrapperSharedKey);
        }
    }

    /// childMapping local to this TU (renamed-and-moved from the prior inline
    /// lambda so `walk` can call it from both the wrapper and bare branches).
    ParentToLmc childMapping (const Constituent& parent, const ParentToLmc& parentToLmc)
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
```

Move the `childMapping` helper above `walk` (forward-declare or reorder) so the wrapper branch can call it. If the existing file already defined `childMapping` inline inside `walk`, hoist it to file scope inside the same anonymous namespace.

Add `#include <unordered_map>` to the existing include block.

Update `selectTimelineView` to pass and consume the `wrapperSharedKey` map:

```cpp
TimelineViewState selectTimelineView (const Constituent&                  root,
                                      const TempoMap&                     sessionToLmc,
                                      const std::vector<InputDescriptor>& inputs,
                                      const std::vector<TapeId>&          armedTapes,
                                      TapeId                              focusedTape)
{
    TimelineViewState state;
    state.startLmcSeconds = sessionToLmc.apply (root.conceptualIn().wholeNotes());
    state.endLmcSeconds   = sessionToLmc.apply (root.conceptualOut().wholeNotes());

    state.rows.reserve (inputs.size());
    for (const auto& desc : inputs)
    {
        TrackStripState row;
        row.tapeId      = desc.tapeId;
        row.kind        = desc.inputKind;
        row.displayName = desc.displayName;
        row.isArmed     = std::find (armedTapes.begin(), armedTapes.end(),
                                     desc.tapeId) != armedTapes.end();
        row.isFocused   = (desc.tapeId == focusedTape);
        state.rows.push_back (std::move (row));
    }

    const ParentToLmc rootToLmc =
        [&sessionToLmc] (Rational t) { return sessionToLmc.apply (t); };

    std::unordered_map<std::int64_t, const Constituent*> wrapperSharedKey;
    walk (root, rootToLmc, state, wrapperSharedKey);

    // Second pass: group placement wrappers by pointer-identity of their
    // shared Phrase, then fill each Pill's sharedSiblings with the other
    // members of its group. Forked wrappers do not participate (their
    // wrapperSharedKey entry was never inserted, so they do not appear in
    // any group).
    std::unordered_map<const Constituent*, std::vector<ConstituentId>> groups;
    for (const auto& [wrapperId, sharedPtr] : wrapperSharedKey)
        groups[sharedPtr].push_back (ConstituentId (wrapperId));

    for (auto& pill : state.pills)
    {
        const auto it = wrapperSharedKey.find (pill.id.value());
        if (it == wrapperSharedKey.end()) continue;
        const auto& group = groups[it->second];
        if (group.size() < 2) continue;  // not actually shared
        for (const auto& sibling : group)
            if (sibling.value() != pill.id.value())
                pill.sharedSiblings.push_back (sibling);
    }

    return state;
}
```

- [ ] **Step 5: Build + run the new selector tests**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[timelineView]"
```

Expected: previously-passing TimelineView tests still pass; 5 new cases pass.

- [ ] **Step 6: Run the full suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 244 + 5 = 249 tests pass.

- [ ] **Step 7: Commit**

```bash
git add ui/include/ida/TimelineViewState.h ui/src/TimelineViewState.cpp tests/TimelineViewStateTests.cpp
git commit -m "feat: TimelineViewState — wrapper-aware Pills with sharedSiblings/overlays/forked"
git push origin master
```

---

## Task 5: TimelineView renderer — tie-bar, overlay-dot, prime mark

**Files:**
- Modify: `ui/src/TimelineView.cpp`

This task is JUCE-rendering only — there is no headless test. Operator-side verification at the end.

- [ ] **Step 1: Read the existing pill paint loop to know where to insert**

```bash
sed -n '180,270p' ui/src/TimelineView.cpp
```

Locate the per-pill paint loop (the iteration that draws each `pill` in `state_.pills`, around lines 195-260).

- [ ] **Step 2: Draw the tie-bar across `sharedSiblings`**

Inside the paint method, AFTER all pills are drawn (so the tie sits on top), add a second pass that draws one short horizontal bar above each group of shared wrappers. Insert after the per-pill loop ends:

```cpp
    // Tie-bar: a thin horizontal mark across the top of each group of shared
    // placement wrappers. Surfaces "these are the same phrase" visually so the
    // operator's mental model is shape-and-position, not text. Drawn once per
    // group, spanning the leftmost shared pill's start to the rightmost's end.
    std::unordered_map<std::int64_t, std::vector<const PillState*>> tieGroups;
    for (const auto& pill : state_.pills)
        if (! pill.sharedSiblings.empty())
        {
            // Group key: minimum id in the group (own id and siblings). The
            // smallest id stays stable across paint frames so the same set
            // always coalesces under one entry.
            std::int64_t key = pill.id.value();
            for (const auto& s : pill.sharedSiblings)
                if (s.value() < key) key = s.value();
            tieGroups[key].push_back (&pill);
        }

    g.setColour (juce::Colours::lightblue.withAlpha (0.75f));
    for (const auto& [key, members] : tieGroups)
    {
        if (members.size() < 2) continue;
        int leftX  = std::numeric_limits<int>::max();
        int rightX = std::numeric_limits<int>::min();
        for (const auto* p : members)
        {
            leftX  = std::min (leftX,  timeToX (p->startLmcSeconds));
            rightX = std::max (rightX, timeToX (p->endLmcSeconds));
        }
        // Sit 6 px above the top edge of the pill row.
        const int topY = pillRowTopY() - 6;
        g.fillRect (juce::Rectangle<int> (leftX, topY, rightX - leftX, 2));
    }
```

If `pillRowTopY()` does not exist in the file, replace with whatever expression yields the top of the pill rectangle (read the existing `int primaryIdx = findRowIndexForTape (...)` / `y = ...` line and reuse). Add `#include <unordered_map>` and `#include <limits>` to the include block.

- [ ] **Step 3: Draw the overlay-dot on pills with `hasOverlays`**

Inside the per-pill loop, after the existing text draws, add:

```cpp
        if (pill.hasOverlays)
        {
            // Small filled circle at the pill's top-right corner — "something
            // extra lives here on this one only." No text; the dot is the
            // signal, matched to the white paper's glanceable principle.
            const int dotR = 3;
            g.setColour (juce::Colours::orange.withAlpha (0.9f));
            g.fillEllipse (juce::Rectangle<float> (
                static_cast<float> (x2 - dotR * 2 - 4),
                static_cast<float> (pillTopY + 4),
                static_cast<float> (dotR * 2),
                static_cast<float> (dotR * 2)));
        }
```

If `pillTopY` is not the local variable used in the existing draw loop, substitute the actual y-coordinate of the pill's top edge from the existing rendering code.

- [ ] **Step 4: Draw the prime mark on forked pills**

Inside the per-pill loop, after the dot draw, add:

```cpp
        if (pill.isForked)
        {
            // Prime mark — a small upright tick above the pill, signaling
            // "this one is its own thing now." Distinct from the tie-bar
            // (horizontal) and the overlay dot (round) so the three marks
            // read independently at a glance.
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (14.0f, juce::Font::bold));
            g.drawText ("'", juce::Rectangle<int> (x2 - 14, pillTopY - 14, 12, 14),
                        juce::Justification::centred, false);
        }
```

- [ ] **Step 5: Clean build and operator-side verification**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 249 tests pass (no regressions; rendering is GUI-only).

**Operator verification** — ask the operator (do NOT run `open` yourself):
1. Launch `./build/app/IDA_artefacts/Release/IDA.app` on macOS.
2. The Preparation tab shows the demo timeline. (The demo's verse-×3 shape lands in Task 6 — for this task, expect the *current* single-verse layout; the tie-bar will appear only after Task 6.) Verify nothing broke: intro/verse/outro pills still draw with their loop count, name, entrance/exit text.
3. Confirm no orange dot or prime mark appears on any current pill (overlays and forks land in later tasks).

If anything looks wrong, debug before committing. The tie-bar / dot / prime visuals are verified end-to-end in Task 10 once the demo and fork gestures are in.

- [ ] **Step 6: Commit**

```bash
git add ui/src/TimelineView.cpp
git commit -m "feat: TimelineView — tie-bar across shared placements + overlay dot + fork prime"
git push origin master
```

---

## Task 6: DemoSession ×3 migration

**Files:**
- Modify: `app/DemoSession.cpp`
- Modify: `tests/DemoSessionTests.cpp`

- [ ] **Step 1: Update the existing demo-shape test in `tests/DemoSessionTests.cpp`**

Replace the existing `TEST_CASE ("DemoSession top-level children are all Phrase shells, never hybrids", "[demoSession][shape]")` body with the new expected shape, and add a second test case asserting pointer-identity sharing:

```cpp
TEST_CASE ("DemoSession top-level children are all Phrase shells, never hybrids",
           "[demoSession][shape]")
{
    const auto demo = buildDemoSession();
    REQUIRE (demo.root != nullptr);
    REQUIRE_FALSE (demo.root->children().empty());

    // Five top-level children: intro, three verse wrappers, outro.
    REQUIRE (demo.root->children().size() == 5);

    // Intro [0,3) — bare Phrase, has a Loop descendant.
    {
        const auto& intro = *demo.root->children()[0];
        REQUIRE (intro.isPhrase());
        REQUIRE_FALSE (isHybrid (intro));
        REQUIRE (hasLoopChild (intro));
        CHECK (intro.conceptualIn()  == ida::Position (ida::Rational (0)));
        CHECK (intro.conceptualOut() == ida::Position (ida::Rational (3)));
    }

    // Three verse wrappers at [3,9), [9,15), [15,21).
    for (std::size_t i = 1; i <= 3; ++i)
    {
        const auto& wrapper = *demo.root->children()[i];
        REQUIRE (ida::isPlacementWrapper (wrapper));
        REQUIRE_FALSE (isHybrid (wrapper));
        // Wrapper itself has no direct Loop child (the shared verse does).
        CHECK (wrapper.conceptualIn()  ==
               ida::Position (ida::Rational (3 + static_cast<int> (i - 1) * 6)));
        CHECK (wrapper.conceptualOut() ==
               ida::Position (ida::Rational (3 + static_cast<int> (i) * 6)));
    }

    // Outro [21,24) — bare Phrase, has a Loop descendant.
    {
        const auto& outro = *demo.root->children()[4];
        REQUIRE (outro.isPhrase());
        REQUIRE_FALSE (isHybrid (outro));
        REQUIRE (hasLoopChild (outro));
        CHECK (outro.conceptualIn()  == ida::Position (ida::Rational (21)));
        CHECK (outro.conceptualOut() == ida::Position (ida::Rational (24)));
    }

    // Total span: 24 whole notes.
    CHECK (demo.root->conceptualOut() == ida::Position (ida::Rational (24)));
}

TEST_CASE ("DemoSession's three verse wrappers share one Phrase ChildPtr",
           "[demoSession][shared]")
{
    const auto demo = buildDemoSession();
    REQUIRE (demo.root->children().size() == 5);

    const auto& wrapperA = *demo.root->children()[1];
    const auto& wrapperB = *demo.root->children()[2];
    const auto& wrapperC = *demo.root->children()[3];
    REQUIRE (wrapperA.children().size() >= 1);
    REQUIRE (wrapperB.children().size() >= 1);
    REQUIRE (wrapperC.children().size() >= 1);

    // The canary: real sharing, not duplicate-id-by-mistake.
    const auto* a = wrapperA.children()[0].get();
    const auto* b = wrapperB.children()[0].get();
    const auto* c = wrapperC.children()[0].get();
    CHECK (a == b);
    CHECK (b == c);
    CHECK (a->id().value() == 20);  // canonical shared verse id
    CHECK (a->name() == "verse");
}
```

Add `#include "sirius/Position.h"`, `#include "sirius/Rational.h"`, and `#include "sirius/Constituent.h"` (the last one provides `isPlacementWrapper`) to the includes if any are missing.

- [ ] **Step 2: Run the tests, verify they fail (demo still has the single-verse shape)**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[demoSession]"
```

Expected: FAIL — `demo.root->children().size()` is 3, not 5.

- [ ] **Step 3: Rewrite the demo in `app/DemoSession.cpp`**

Replace the body of `buildDemoSession()` with:

```cpp
DemoSession buildDemoSession()
{
    // Intro [0,3) — bare Phrase containing one Loop. Convention unchanged:
    // every top-level child is a Phrase container, never a hybrid.
    const Constituent introPhraseShell =
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withName ("intro")
            .withPhraseMetadata (phraseMeta ("intro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto intro = std::make_shared<const Constituent> (
        arrangement::layer (introPhraseShell,
            { makeLoop (11, "intro", Rational (3), 100) }));

    // Verse — ONE shared Phrase that the song places three times. The shared
    // Phrase is a layer of two simultaneous loops (rhythm + lead) and lives
    // at id 20. sequenceShared then mints three wrapper Constituents (ids
    // 51, 52, 53) at offsets 3, 9, 15, all pointing at this same ChildPtr.
    const Constituent versePhraseShell =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (phraseMeta ("verse",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto verse = std::make_shared<const Constituent> (
        arrangement::layer (versePhraseShell,
            { makeLoop (21, "verse: rhythm", Rational (6), 200),
              makeLoop (22, "verse: lead",   Rational (3), 300) }));

    // Outro [21,24).
    const Constituent outroPhraseShell =
        Constituent (ConstituentId (30), Position(), Position (Rational (3)))
            .withName ("outro")
            .withPhraseMetadata (phraseMeta ("outro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::Resolution));
    const auto outro = std::make_shared<const Constituent> (
        arrangement::layer (outroPhraseShell,
            { makeLoop (31, "outro", Rational (3), 400) }));

    // Build the song: intro at [0,3), three verse wrappers at [3,9), [9,15),
    // [15,21), outro at [21,24). Twenty-four whole notes total.
    std::int64_t nextWrapperId = 51;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent sessionShell =
        Constituent (ConstituentId (1), Position(), Position (Rational (24)))
            .withName ("demo session");

    const Constituent withIntro =
        arrangement::sequence (sessionShell, { intro });
    const Constituent withVerses =
        arrangement::sequenceShared (withIntro, verse,
            { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
            allocateWrapper);
    const auto session = std::make_shared<const Constituent> (
        arrangement::sequence (withVerses, { outro }));

    // 120 quarter-note BPM => one whole note is 2 LMC seconds, so the
    // twenty-four-whole-note session spans 48 LMC seconds.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));
    const Rational lengthSeconds = sessionToLmc.apply (Rational (24));

    std::vector<InputDescriptor> inputs;
    inputs.push_back ({ TapeId (100), InputKind::Audio, "Intro pad",   0 });
    inputs.push_back ({ TapeId (200), InputKind::Audio, "Verse rhythm",1 });
    inputs.push_back ({ TapeId (300), InputKind::Audio, "Verse lead",  2 });
    inputs.push_back ({ TapeId (400), InputKind::Audio, "Outro pad",   3 });

    return DemoSession { session, std::move (sessionToLmc), lengthSeconds,
                         std::move (inputs) };
}
```

- [ ] **Step 4: Build + run demo tests**

```bash
cmake --build build --target IdaTests 2>&1 | tail -5
./build/tests/IdaTests "[demoSession]"
```

Expected: 2 test cases pass (shape + shared).

- [ ] **Step 5: Run full suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 249 + 1 (the `[demoSession][shared]` new case) = 250 tests pass. (The shape case was replaced, not added.)

Some PerformanceViewState / TimelineViewState tests may have hard-coded the old 12-whole-note length — if any fail, they need expectations bumped to 24. Inspect failures and update only the affected assertions (do not change semantics).

- [ ] **Step 6: Commit**

```bash
git add app/DemoSession.cpp tests/DemoSessionTests.cpp
# Plus any test files updated for the 24-whole-note demo length, if applicable.
git commit -m "feat: DemoSession — verse plays three times via shared placement"
git push origin master
```

---

## Task 7: MainComponent — long-press capture + §11 banner copy

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Add the long-press infrastructure to `app/MainComponent.h`**

In the private members area, after the existing `pendingOverlay_` flag (added in Task 3), add:

```cpp
    /// 500 ms long-press detector on Mark In. The press starts a timer; if
    /// the user releases before the timer fires, the capture stays Shared
    /// (the default). If the timer fires, pendingOverlay_ is set true and
    /// the next Mark Out resolves to Overlay (see promote()).
    static constexpr int kOverlayLongPressMs = 500;
    std::unique_ptr<juce::Timer> longPressTimer_;
```

If `<memory>` and `juce_events/juce_events.h` (transitive via JUCE main include) are not already present, JUCE's umbrella include already brings them in.

- [ ] **Step 2: Wire the long-press timer in `app/MainComponent.cpp`**

In the constructor, AFTER `markInButton_.onClick = [this] { onMarkIn(); };` (around line 410), add:

```cpp
    // Long-press on Mark In: hold ≥ 500 ms to request Overlay. Mark In fires
    // at click (onClick); the long-press timer upgrades the pending mode if
    // the user keeps holding past the threshold, before they Mark Out.
    markInButton_.addMouseListener (this, false);
```

Override `mouseDown` and `mouseUp` (declare them in `MainComponent.h` in the `public:` section near `paint` / `resized`):

```cpp
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
```

Implement them in `MainComponent.cpp` near the other JUCE overrides (after `resized()`):

```cpp
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != &markInButton_) return;

    pendingOverlay_ = false;  // every press starts Shared until proven otherwise

    longPressTimer_ = std::make_unique<juce::FunctionTimer> ();
    auto* raw = longPressTimer_.get();
    raw->onTimer = [this, raw]
    {
        raw->stopTimer();
        pendingOverlay_ = true;
        // Visual feedback: tint the Mark In button to confirm the upgrade.
        // No banner here — the banner fires at Mark Out, with the resolved
        // mode reflected in the §11 template.
        markInButton_.setColour (juce::TextButton::buttonColourId,
                                 juce::Colours::orange.darker());
    };
    raw->startTimer (kOverlayLongPressMs);
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent != &markInButton_) return;
    if (longPressTimer_)
    {
        longPressTimer_->stopTimer();
        longPressTimer_.reset();
    }
    // Restore the button colour if it was tinted.
    markInButton_.removeColour (juce::TextButton::buttonColourId);
}
```

If `juce::FunctionTimer` is not in the JUCE version used by this repo, substitute a small local subclass of `juce::Timer` whose `timerCallback` invokes a captured `std::function<void()>`. Inline that subclass in an anonymous namespace at the top of `MainComponent.cpp`.

- [ ] **Step 3: Replace the body of `announceCapture` with the four §11 banner templates**

The current implementation (around line 700) emits `"Loop added to <hostName>  ·  3.42 s  ·  tape #200"` — tape numbers and durations are §15 violations. Replace the entire function body with:

```cpp
void MainComponent::announceCapture (const CaptureRegion& region,
                                     const promotion::PromotionResult& result)
{
    // Spec §11 — four templates only. No tape numbers. No durations. No mode
    // indicators. The musician sees what landed, in their own vocabulary.
    juce::ignoreUnused (region);  // intentional: region details are plumbing

    juce::String msg;

    const bool wasOverlay = result.resolvedMode == promotion::AttachmentMode::Overlay;
    const bool wasDowngrade = (! wasOverlay)
                              && (! result.hostPhraseName.has_value())
                              && (! result.mintedPhraseId.has_value());
    // Note: a "downgrade with no host AND no minted phrase" should not happen
    // in practice — the Shared path always mints when no host exists. The
    // downgrade case below covers the Overlay→Shared path landing in the mint
    // branch, where `mintedPhraseId` IS set; the banner still wants the
    // "no section here yet" phrasing per §11 row 4.

    if (wasOverlay)
    {
        // "Added to verse 2 only"  (placement ordinal from the data field)
        const juce::String hostName =
            result.hostPhraseName.value_or (std::string ("the phrase here"));
        const auto idx = result.overlayPlacementIndex.value_or (0u);
        msg << "Added to " << hostName << " " << static_cast<int> (idx) << " only";
    }
    else if (result.mintedPhraseId.has_value() && ! result.hostPhraseName.has_value())
    {
        // Two §11 rows produce this branch:
        //   - Shared + mint (no host found anywhere): "New phrase captured"
        //   - Overlay requested but downgraded AND fell through to mint:
        //     "Added — no section here yet"
        // We disambiguate by whether the operator's pending request was Overlay
        // (pendingOverlay_ was true at promote() time and got consumed there;
        // we reach the consumed-state here, so check the prior value via a
        // cached copy that mouseUp sets before clearing).
        msg = lastRequestWasOverlay_
              ? juce::String ("Added — no section here yet")
              : juce::String ("New phrase captured");
    }
    else if (result.hostPhraseName.has_value())
    {
        // Shared, host found — the bread-and-butter case.
        msg << "Added to " << juce::String (*result.hostPhraseName);
        if (lastRequestWasOverlay_)
            msg << " — no section here yet";
    }
    else
    {
        // Defensive: no host, no mint — shouldn't reach here, but stay safe.
        msg = "Added";
    }

    lastRequestWasOverlay_ = false;
    captureBanner_->show (msg);
}
```

Add the cache member to `app/MainComponent.h` alongside `pendingOverlay_`:

```cpp
    bool lastRequestWasOverlay_ { false };
```

In `onMarkOut` (line 671), before calling `promote`, snapshot the flag for the banner to consume:

```cpp
        lastRequestWasOverlay_ = pendingOverlay_;
```

Place that line directly above the existing `auto result = promotion::promote (...);` call.

- [ ] **Step 4: Verify the audit greps — no tape numbers in any musician-facing string in MainComponent.cpp**

```bash
grep -nE '"[^"]*(tape #|tape number|Phrase captured  )' app/MainComponent.cpp
```

Expected: zero hits. (The old `Phrase captured  ·  ... s  ·  tape #...` string is gone.)

- [ ] **Step 5: Clean build + full test suite**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 250 tests pass (no headless tests touch the banner; this change is exercised manually).

- [ ] **Step 6: Operator-side verification**

Ask the operator (do not run `open` yourself):

1. Launch the .app.
2. Arm input. Tap Mark In, then Mark Out (no hold). Banner shows `Added to verse` (the demo's verse is the host).
3. Arm input. Move playhead past the third verse wrapper (so Mark In is in the gap or past 21 seconds). Tap Mark In, Mark Out. Banner shows `New phrase captured`.
4. Arm input. Move playhead inside verse 2 (LMC ~ 18-30 s region). Hold Mark In for ≥ 500 ms (the button tints orange). Mark Out. Banner shows `Added to verse 2 only`.
5. Arm input. Move playhead past 42 seconds (in the outro or past it). Hold Mark In for ≥ 500 ms. Mark Out. Banner shows `Added — no section here yet` (Overlay requested, no wrapper at Mark In, downgraded then minted).

If any banner contains the literal string `tape #`, a number followed by `s`, or `Loop added to`, the §15 audit fails — debug before committing.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: MainComponent — long-press Mark In requests Overlay; banner uses §11 musician copy"
git push origin master
```

---

## Task 8: Fork gesture — data path + minimal UI surface

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

The fork operation:

1. Deep-copy the wrapper's shared Phrase subtree, minting fresh `ConstituentId`s for every node in the copy.
2. Replace the wrapper's first child (`withChildReplaced(0, copy)`) with the deep copy.
3. Change the wrapper's `PhraseMetadata::role` from `"placement"` to `"forked-placement"`.
4. Push one UndoStack entry; the selector's `isForked` flips to true; the renderer's prime mark appears.

UI surface (decided here against the spec's open item): a **right-click / long-press on a Pill in the Preparation tab** opens a one-item popup menu labeled `"Vary this one"`. The Pill itself is a rendered rectangle, not a JUCE component — so the right-click handler lives in the timeline view component's `mouseDown` and resolves the click position to a Pill id via hit-test.

- [ ] **Step 1: Add the fork-edit helper in `app/MainComponent.cpp` (anonymous namespace at top)**

```cpp
namespace
{
    /// Deep-copy a Constituent subtree, minting fresh ConstituentIds for
    /// every node. The structure (boundaries, names, metadata, tape refs) is
    /// preserved; only ids change. Used by the fork gesture to break sharing.
    ida::Constituent deepCopyWithFreshIds (
        const ida::Constituent& src,
        const ida::promotion::IdAllocator& allocate)
    {
        ida::Constituent copy (allocate(), src.conceptualIn(), src.conceptualOut());
        if (! src.name().empty())  copy = copy.withName (src.name());
        if (src.phraseMetadata())  copy = copy.withPhraseMetadata (*src.phraseMetadata());
        if (src.tapeReference())   copy = copy.withTapeReference  (*src.tapeReference());
        if (src.localMeter())      copy = copy.withLocalMeter     (*src.localMeter());
        if (src.localTempoMap())   copy = copy.withLocalTempoMap  (*src.localTempoMap());
        if (src.hasEffectChain())  copy = copy.withEffectChain    (*src.effectChain());
        copy = copy.withAnchor (src.anchor());
        copy = copy.withRepetitionRules (src.repetitionRules());
        for (const auto& child : src.children())
            copy = copy.withChildAdded (
                std::make_shared<const ida::Constituent> (
                    deepCopyWithFreshIds (*child, allocate)));
        return copy;
    }

    /// Locate the wrapper by id in `root` and return its index path from the
    /// root's children. Returns empty optional if not found (caller can no-op).
    std::optional<std::vector<std::size_t>> findWrapperPath (
        const ida::Constituent& root, ida::ConstituentId wrapperId)
    {
        std::optional<std::vector<std::size_t>> found;
        std::vector<std::size_t> path;
        std::function<void (const ida::Constituent&)> walk;
        walk = [&] (const ida::Constituent& c)
        {
            if (c.id() == wrapperId) { found = path; return; }
            for (std::size_t i = 0; i < c.children().size(); ++i)
            {
                path.push_back (i);
                walk (*c.children()[i]);
                path.pop_back();
                if (found) return;
            }
        };
        // Skip the root itself (a wrapper would never live there).
        for (std::size_t i = 0; i < root.children().size(); ++i)
        {
            path.push_back (i);
            walk (*root.children()[i]);
            path.pop_back();
            if (found) return found;
        }
        return found;
    }
}
```

- [ ] **Step 2: Add `MainComponent::forkPlacement` method**

In `app/MainComponent.h`, declare in the private section:

```cpp
    void forkPlacement (ConstituentId wrapperId);
```

Implement in `MainComponent.cpp`:

```cpp
void MainComponent::forkPlacement (ConstituentId wrapperId)
{
    const auto& root = *undoStack_.current();
    const auto wrapperPath = findWrapperPath (root, wrapperId);
    if (! wrapperPath) return;

    // Walk to the wrapper, confirm shape, deep-copy its shared first child,
    // then splice the copy back in via copy-on-write down the same path.
    std::function<ida::Constituent (const ida::Constituent&, std::size_t)>
        forkedSplice;
    forkedSplice = [&] (const ida::Constituent& c, std::size_t depth)
                       -> ida::Constituent
    {
        if (depth == wrapperPath->size())
        {
            if (! ida::isPlacementWrapper (c)) return c;  // not a wrapper, no-op
            const auto& sharedPhrase = *c.children()[0];
            auto allocate = [this] { return ConstituentId (nextConstituentId_++); };
            const auto deepCopy = std::make_shared<const ida::Constituent> (
                deepCopyWithFreshIds (sharedPhrase, allocate));

            // Change the wrapper's role to "forked-placement" (so the selector
            // reports isForked = true and pulls it out of tie-bar grouping)
            // and replace its first child with the deep copy.
            ida::PhraseMetadata forkedMeta = *c.phraseMetadata();
            forkedMeta.role = "forked-placement";
            return c.withPhraseMetadata (std::move (forkedMeta))
                    .withChildReplaced (0, deepCopy);
        }
        const std::size_t i = (*wrapperPath)[depth];
        auto childCopy = std::make_shared<const ida::Constituent> (
            forkedSplice (*c.children()[i], depth + 1));
        return c.withChildReplaced (i, childCopy);
    };

    ida::Constituent newRoot = forkedSplice (root, 0);

    // One undo entry, plain label. Banner is not used for fork — the visual
    // change on the Pill (prime mark) IS the feedback per spec §13.
    undoStack_.push (
        std::make_shared<const ida::Constituent> (std::move (newRoot)),
        "vary this placement");

    refreshPerformance();
    refreshPreparation();
    refreshCaptureControls();
    refreshDiagnostics();
}
```

- [ ] **Step 3: Wire the right-click / long-press on Pills in the TimelineView component**

This step's exact code depends on the structure of `ui/src/TimelineView.cpp` and how the host (`MainComponent`) gets notified of pill clicks. The most likely existing pattern: the TimelineView class is a JUCE Component owned by the Preparation tab; it overrides `mouseDown` and computes which Pill (if any) was hit.

Inspect first:

```bash
grep -n "class TimelineView\|mouseDown\|onPillClicked\|onPill" ui/src/TimelineView.cpp ui/include/ida/TimelineView.h
```

Add a `std::function<void (ConstituentId)> onPillContextMenuRequested;` public member to the `TimelineView` class declaration. In its `mouseDown`, when the click is a right-click (or a long-press on touch), hit-test against `state_.pills` and fire the callback with the matching Pill id:

```cpp
void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    const bool isContextGesture = e.mods.isRightButtonDown()
                                || e.mods.isCtrlDown()   // macOS Ctrl-click
                                || e.source.isLongPress(); // touch long-press
    if (! isContextGesture) return;

    for (const auto& pill : state_.pills)
    {
        const int x1 = timeToX (pill.startLmcSeconds);
        const int x2 = timeToX (pill.endLmcSeconds);
        const int y  = pillRowTopY();  // existing helper, or use the same expression as the paint loop
        const int h  = pillRowHeight(); // same
        if (e.x >= x1 && e.x < x2 && e.y >= y && e.y < y + h)
        {
            if (onPillContextMenuRequested)
                onPillContextMenuRequested (pill.id);
            return;
        }
    }
}
```

If `e.source.isLongPress()` does not exist in this JUCE version, drop that condition (right-click suffices on desktop; touch users get the long-press in a follow-up).

In MainComponent's constructor, after the TimelineView is constructed (find the relevant `addAndMakeVisible (timelineView_)` call), wire the callback:

```cpp
    timelineView_.onPillContextMenuRequested =
        [this] (ConstituentId wrapperId)
    {
        // Only offer "Vary this one" on actual placement wrappers (not bare
        // intro/outro Pills, not already-forked ones). Look up the Pill in
        // the current view state to check.
        const auto& root = *undoStack_.current();
        const auto path = findWrapperPath (root, wrapperId);
        if (! path) return;
        auto target = root.children()[(*path)[0]];
        for (std::size_t depth = 1; depth < path->size(); ++depth)
            target = target->children()[(*path)[depth]];

        if (! ida::isPlacementWrapper (*target)) return;

        juce::PopupMenu menu;
        menu.addItem ("Vary this one", [this, wrapperId] { forkPlacement (wrapperId); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withMousePosition());
    };
```

- [ ] **Step 4: Vocabulary scrub**

```bash
grep -nE '"[^"]*(Fork|fork|Wrapper|wrapper|Placement|placement|AttachmentMode)[^"]*"' app/MainComponent.cpp
```

Inspect each hit. The only acceptable musician-facing strings are `"Vary this one"` (the menu item) and `"vary this placement"` (the undo label — visible only in advanced surfaces; acceptable). Any banner/menu string containing "fork", "wrapper", or "placement" in operator-visible position is a §15 violation — fix immediately.

- [ ] **Step 5: Clean build + run full suite**

```bash
cd /Users/larryseyer/IDA
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -5
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: 250 tests pass (no headless test exercises the fork UI gesture; the data path is covered transitively by the `[timelineView][forked]` case in Task 4).

- [ ] **Step 6: Operator-side verification**

Ask the operator:

1. Launch the .app.
2. On the Preparation tab, the three verse Pills show with a tie-bar above them (from Task 5 + Task 6).
3. Right-click (or Ctrl-click) on the middle verse Pill. A popup menu appears with one item: `"Vary this one"`.
4. Select it. The popup dismisses. The middle Pill loses its share of the tie-bar (the bar now spans only verses 1 and 3) and gains a small prime mark (`'`) at its top-right.
5. Hit Undo (bottom bar). The middle Pill rejoins the tie, the prime mark disappears.
6. Right-click on the intro Pill. **No menu appears** (intro is a bare Phrase, not a wrapper — the early-return at the top of the callback suppresses the menu).

If any string in the popup or banner reads "Fork" or "Diverge" or any other non-musician term, fix it before committing.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp ui/include/ida/TimelineView.h ui/src/TimelineView.cpp
git commit -m "feat: fork — 'Vary this one' context menu on placement Pills"
git push origin master
```

---

## Task 9: User guide Roadmap update

**Files:**
- Modify: `docs/IDA User Guide.md`

- [ ] **Step 1: Locate the Roadmap section**

```bash
grep -n "^## Roadmap\|^# Roadmap" "docs/IDA User Guide.md"
```

If the Roadmap section does not exist, find the most natural place to add it (typically near the end, after the per-chapter how-to content).

- [ ] **Step 2: Add the line**

In the Roadmap section, insert a new bullet (or new paragraph) in musician language. No internal terms. Suggested copy:

```markdown
### Repeating song sections

The verse plays three times. Recording into a verse adds to every verse — that's the default. Hold the Mark In button for a moment to record into just one. If a verse needs to drift on its own ("Vary this one" from its menu), it stops being tied to the others from that point on. The timeline shows you which is which: a tie above the pills means "the same verse," a dot means "something just for this one," a small mark means "this one is its own thing now."

A full chapter on this lands once the gesture feels right in real use.
```

Words/phrases that must NOT appear anywhere in this section: `wrapper`, `placement`, `overlay`, `fork`, `attachment`, `shared_ptr`, `Constituent`, `ChildPtr`.

- [ ] **Step 3: Grep audit**

```bash
grep -nE 'wrapper|placement|overlay|attachment|shared_ptr|Constituent|ChildPtr' "docs/IDA User Guide.md"
```

Expected: zero hits in any musician-facing prose. (Existing mentions, if any, are bugs to fix as part of this task.)

- [ ] **Step 4: Commit**

```bash
git add "docs/IDA User Guide.md"
git commit -m "docs: user guide — Roadmap line for repeating song sections"
git push origin master
```

---

## Task 10: End-to-end verification

**Files:** none (verification only)

- [ ] **Step 1: Clean build from scratch**

```bash
cd /Users/larryseyer/IDA
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build 2>&1 | tail -5
```

Expected: zero warnings from any source under our control; build succeeds.

- [ ] **Step 2: Full test suite**

```bash
./build/tests/IdaTests 2>&1 | tail -3
```

Expected: ≥ 250 tests pass, no failures. (Per-task expectations: Task 1 +4, Task 3 +6 −1, Task 4 +5, Task 6 ±0, Task 8 ±0 → 235 + 14 = 249, plus the new `[demoSession][shared]` case in Task 6 = 250.)

- [ ] **Step 3: §15 vocabulary final audit**

```bash
grep -nE '"[^"]*(tape #|wrapper|Wrapper|AttachmentMode|placement(?! demo)|Fork[^"]*"|Overlay mode|Shared mode)[^"]*"' \
    app/MainComponent.cpp ui/src/TimelineView.cpp ui/src/TimelineViewState.cpp
```

Expected: zero hits in any user-facing string. (C++ type names like `AttachmentMode` may appear in code identifiers — those are not user-facing strings. Hits are only failures when they live inside quoted strings shown to the operator.)

- [ ] **Step 4: Operator-side end-to-end script**

Ask the operator:

1. **Default capture (Shared, lands in shared verse).** Arm input. Set playhead inside verse 1 (LMC ~ 6-18 s). Tap Mark In, Mark Out. Banner: `Added to verse`. The new Loop appears as an icon on all three verse Pills (they share the verse Phrase).
2. **Overlay capture (long-press, lands on one wrapper).** Arm. Set playhead inside verse 2 (LMC ~ 18-30 s). Hold Mark In ≥ 500 ms (button tints), Mark Out. Banner: `Added to verse 2 only`. The middle verse Pill gains an orange dot; the outer two are unchanged.
3. **Fork ("Vary this one").** Right-click on verse 3. Menu: `Vary this one`. Click it. Verse 3 drops out of the tie-bar and shows a prime mark.
4. **Undo each gesture in reverse.** Bottom-bar Undo three times. Fork undoes first (tie restored, prime gone), then overlay undoes (dot gone), then shared add undoes. Each undo is one entry.
5. **Mint case (no host, Overlay outside any wrapper).** Move playhead past 42 s (in outro). Hold Mark In ≥ 500 ms, Mark Out. Banner: `Added — no section here yet`. A fresh Phrase Pill appears at the playhead.

If any step fails or any banner string contains internal vocabulary, debug before declaring the milestone done.

- [ ] **Step 5: Update `continue.md` for the next session**

The implementation is complete. Write a fresh handoff doc capturing:

- This session shipped shared-placement architecture end-to-end.
- Tests went from 235 → ≥ 250.
- The capture-promotion runtime guard is gone; replaced by pointer-aware `enforceSharedInstancesAreShared`.
- Next big topic candidates: session-format encoding of sharing (already in `todo.md`), M8 ensemble, the user guide's full "When a section plays more than once" chapter, or the spec's open desktop-accelerator (Option-click for overlay).

Use the `continue` skill if available; otherwise hand-write following the existing `continue.md` shape.

- [ ] **Step 6: Final commits**

```bash
git add continue.md
git commit -m "docs: continue.md — shared-placement shipped end-to-end"
git push origin master
```

Also update `todo.md` to mark the original shared-placement entry as actually SUPERSEDED-AND-IMPLEMENTED (it was marked SUPERSEDED when the spec landed; the implementation is now done):

```bash
# Edit todo.md, append " — IMPLEMENTED 2026-MM-DD" to the existing SUPERSEDED line.
git add todo.md
git commit -m "docs: todo.md — shared-placement implementation complete"
git push origin master
```

---

## Self-Review

**Spec coverage:** every section of the spec has at least one task implementing it.

- §1 Data shape → Task 1 (predicate) + Task 3 (promote enforces invariants on writes).
- §2 `sequenceShared` → Task 1.
- §3 `isPlacementWrapper` predicate → Task 1.
- §4 Pointer-aware runtime guard → Task 2.
- §5 Promotion changes (host descends through wrappers, AttachmentMode, downgrade) → Task 3.
- §6 Long-press Mark In = Overlay → Task 7.
- §7 TimelineViewState wrapper-aware selector → Task 4.
- §8 Fork gesture and bookkeeping → Task 8.
- §9 Undo semantics → preserved automatically (no UndoStack change; both fork and overlay are single-snapshot edits).
- §10 Demo migration → Task 6.
- §11 MainComponent wiring (long-press + §11 banner copy) → Task 7.
- §12 Tests → covered inline in Tasks 1, 3, 4, 6.
- §13 User guide Roadmap → Task 9; full chapter explicitly deferred (per spec, lands after operator verification).
- §14 `todo.md` SUPERSEDED → already done last session; Task 10 marks IMPLEMENTED.
- §15 Operator vocabulary → enforced inline in every task that touches a UI string (predicate + greps + manual operator verification).
- Out-of-scope (persistence, per-instance metadata, Phrase-shaped overlays, Option-click) → no task introduces any of these.
- Open items (long-press 500 ms, fork UI surface, banner strings) → decided in Tasks 7 & 8 and frozen in code.

**Placeholders:** none. Every step shows the exact code or command. The M3 simplification (1:1 conceptual ↔ LMC) is preserved unchanged and called out in the plan header.

**Type consistency:** `AttachmentMode`, `PromotionResult` (with `resolvedMode` + `overlayPlacementIndex`), `IdAllocator` (arrangement-local typedef matching promotion's), `isPlacementWrapper`, `sequenceShared` signatures are identical across all tasks that reference them. `pendingOverlay_` / `lastRequestWasOverlay_` / `longPressTimer_` / `kOverlayLongPressMs` names are stable. `forkPlacement(ConstituentId)` matches between header and impl.

**§15 vocabulary check:** every user-visible string introduced is one of:
- `"Added to verse"` / `"Added to <hostName>"` (Shared, host)
- `"New phrase captured"` (Shared, mint)
- `"Added to verse 2 only"` / `"Added to <hostName> <i> only"` (Overlay)
- `"Added to <hostName> — no section here yet"` / `"Added — no section here yet"` (downgrade)
- `"Vary this one"` (menu label)
- `"vary this placement"` (undo label — appears only in advanced surfaces; acceptable per §15 advanced-surface exception)
- Roadmap prose in user guide (greps clean)

No `Wrapper`, `Placement`, `Fork`, `Overlay`, `AttachmentMode`, `tape #`, or raw `ConstituentId` appears in any operator-facing string.

**Scope check:** plan stays focused on the 10 dependency-block items. No persistence work, no M8 ensemble work, no Phrase-shaped-overlay work, no Option-click accelerator. The user guide's full chapter is explicitly deferred until operator verification, per the spec.
