# Capture Promotion — Design

**Date:** 2026-05-15
**Scope:** Single-instance capture promotion (Mark Out → Constituent tree).
**Status:** Brainstorm complete; awaiting implementation plan.

## Problem

Captured regions today live only in `MainComponent::capturedRegions_` —
ephemeral RAM that evaporates on app exit. The capture flow's actual
completion — turning a captured region into a Loop Constituent attached
to the session graph and surfaced as a Pill — is not yet implemented.
This design specifies what Mark Out produces in the Constituent tree.

## Established model (from brainstorm)

The Constituent type system today is permissive: a `Constituent` can
carry `PhraseMetadata`, a `TapeReference`, both, or neither. This
design adopts the following **convention** (the data model stays
permissive; the convention is enforced by the promotion code path):

| Type   | What's attached                    | Children allowed?              |
|--------|------------------------------------|--------------------------------|
| Loop   | `TapeReference` (audio slice)      | No — leaf only                 |
| Phrase | `PhraseMetadata` (musical thought) | Yes — Loops and/or sub-Phrases |

Consequences:

- **A Loop is a leaf.** It points at audio (a tape slice).
- **A Phrase is a container.** It gives a group of Loops (or sub-Phrases) musical identity.
- **A Loop is never standalone in the tree.** Every Loop has a Phrase parent.
  The "standalone Loops on the timeline" open question from prior
  sessions is answered by saying *that case cannot occur*.
- The timeline selector (`TimelineViewState`) keeps its current
  Phrase-only Pill rendering; no extension needed.

## Design

### 1. Promotion semantics

Every Mark Out auto-promotes. There is no separate Promote gesture and
no `capturedRegions_` staging area. The captured region lands in the
Constituent tree on the same gesture that produced it.

**Where it lands** (per the brainstorm rule):

- **Mark In playhead inside an existing Phrase's LMC span** → emit a
  Loop child of that Phrase. No Phrase wrapper is minted.
- **Mark In playhead outside any existing Phrase** → mint a new Phrase
  (PhraseMetadata `role="capture"`, `intent=""`, defaults elsewhere)
  at the song root, containing one Loop child sized to the captured
  region.
- **Straddle case** (captured region extends past the host Phrase's end,
  or starts before any Phrase but Mark Out lands in one): **Mark In
  position wins.** The host Phrase is the one whose span contains the
  Mark In playhead. Loop boundaries that extend past the host are
  clamped to the host's parent span.

### 2. New module: `core/sirius::promotion`

Pure functions, mirroring the existing `core/sirius::arrangement`
pattern (`core/include/sirius/Arrangement.h`).

**Files:**

- `core/include/sirius/Promotion.h` — public header.
- `core/src/Promotion.cpp` — implementation.

**Public surface:**

```cpp
namespace sirius::promotion
{
struct PromotionResult
{
    Constituent newRoot;                          // root with the Loop (and possibly Phrase) added
    ConstituentId addedLoopId;                    // id of the new Loop Constituent
    std::optional<ConstituentId> mintedPhraseId;  // present iff a Phrase wrapper was minted
    std::string undoLabel;                        // "capture loop into <phrase name>" or "capture phrase"
};

using IdAllocator = std::function<ConstituentId()>;

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         const IdAllocator&   allocateId);
}
```

`promote` is referentially transparent given a deterministic
`allocateId`. It calls `allocateId` exactly once when the host-Phrase
case fires (one id for the Loop) and exactly twice when the mint case
fires (one id for the Phrase wrapper, one for the Loop child). The
caller owns id allocation — typically a monotonic counter in
MainComponent today; a future IdMint when persistence demands it.
Using a single callback (not two id parameters) avoids wasting an id
in the host-Phrase case, since `ConstituentId` is a project-wide
counter, not per-type.

**Behavior:**

1. Run the multi-instance guard (see §3). If it fires, throw before
   any other work.
2. Walk `root` to find the deepest Phrase whose LMC span contains
   `lmcAtMarkIn`. LMC spans are computed via `sessionToLmc` the same
   way `selectTimelineView` walks the tree.
3. If a host Phrase is found:
   - Allocate one id via `allocateId`. Construct a Loop Constituent
     with that id. Its conceptual boundaries are derived from the
     captured region's LMC times via `sessionToLmc`, expressed in the
     host Phrase's local conceptual time. Boundaries are clamped to
     the host Phrase's duration if the region extends past.
   - Attach `TapeReference{region.tape, region.inLmcSeconds, region.outLmcSeconds}`.
   - Add the Loop as a child of the host Phrase via copy-on-write.
   - `mintedPhraseId == nullopt`. `undoLabel = "capture loop into " + hostPhrase.name()` (or `"capture loop"` if name is empty).
4. If no host Phrase is found:
   - Allocate one id for the Phrase. Mint a Phrase Constituent with
     that id, conceptual boundaries derived from the region,
     `PhraseMetadata` defaults (`role = "capture"`, everything else
     default).
   - Allocate a second id for the Loop. Construct the Loop as in step
     3 (attached as the Phrase's only child).
   - Add the Phrase as a child of the song root via copy-on-write.
   - `mintedPhraseId == <minted id>`. `undoLabel = "capture phrase"`.

### 3. Write-protect for the multi-instance case

`promote` runs a precondition check first: walk the entire `root` and
collect every `ConstituentId` encountered. If any id appears more than
once anywhere in the tree, throw:

```cpp
throw std::logic_error (
    "sirius::promotion: shared-placement architecture not yet implemented; "
    "see todo.md \"Shared-placement-with-per-instance-overlays architecture\"");
```

This is the explicit gate flagged during the brainstorm. Today
`arrangement::sequence` (`core/src/Arrangement.cpp:60`) creates
per-placement Constituent copies that share an id but are distinct
objects — exactly the shape this guard catches. When shared placements
eventually land, this check is the canary that flags every call site
needing extension. A test verifies the guard fires.

### 4. MainComponent wiring (`onMarkOut` shrinks)

Current `onMarkOut` (app/MainComponent.cpp:616) becomes:

```cpp
void MainComponent::onMarkOut()
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    if (auto region = captureSession_.markOut (t))
    {
        const Rational markInLmc = region->inLmcSeconds;
        const CaptureRestorePoint restorePoint { markInLmc, region->tape };

        auto result = promotion::promote (
            *undoStack_.current(),
            demo_.sessionToLmc,
            *region,
            markInLmc,
            [this] { return ConstituentId{nextConstituentId_++}; });

        undoStack_.push (
            std::make_shared<const Constituent>(std::move(result.newRoot)),
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

**Removed:** `capturedRegions_` member, the `push_back` call, the
prior `announceCapture(region, loopNumber)` signature.

**Added:** one id-mint counter (`nextConstituentId_`) as a private
MainComponent member, since `ConstituentId` is a project-wide counter,
not per-type. Initialized from the demo session's maximum existing id
+ 1 by walking the demo's root once at construction. Persistence will
need to round-trip this counter; `todo.md` "Session-as-directory
format" already covers serialization scope.

### 5. Undo restoration of capture state (the α decision)

Undoing a promotion must:

1. Revert the Constituent tree (handled by `UndoStack::undo()` today).
2. **Restore `CaptureSession` to `AwaitingOut` with the original
   in-point and tape intact.** The tape between Mark In and the
   (now-undone) Mark Out is still on tape (tapes are always running),
   so the operator can immediately Mark Out again at a different time,
   or `Cancel` to abandon.

This requires extending `UndoStack::Entry` with an optional
`CaptureRestorePoint`:

```cpp
// in ui/include/sirius/UndoStack.h
struct CaptureRestorePoint
{
    Rational pendingIn;
    TapeId   pendingTape;
};

class UndoStack
{
public:
    // existing push() unchanged for non-promotion edits
    void push (RootPtr nextRoot, std::string label = {});

    // new overload for promotion entries
    void push (RootPtr nextRoot, std::string label, CaptureRestorePoint restore);

    // new accessor — populated only on promotion entries
    const std::optional<CaptureRestorePoint>& currentEntryRestorePoint() const noexcept;
};
```

`MainComponent::onUndo` checks the restore point after `undoStack_.undo()`
and, if present, calls `captureSession_.markIn(pendingIn, pendingTape)`
to restore `AwaitingOut` state. Default behavior is unchanged for
non-promotion edits.

### 6. CaptureBanner gains a tap-to-undo affordance

The existing `CaptureBanner` (app/MainComponent.cpp, recent session)
becomes clickable. A click invokes the same code path as the bottom-bar
Undo button. Visual: small `↶ Undo` hint inside the banner, right side,
muted accent. Banner stays for 1.5s as today; if clicked within the
window, undo fires and the banner dismisses immediately.

The banner message reflects the promotion outcome:
- Phrase minted: `"Phrase N captured · X.XX s · tape #YYY"`
- Loop joined an existing Phrase: `"Loop added to <phrase name> · X.XX s · tape #YYY"`

### 7. Tests

`tests/PromotionTests.cpp` — pure-function tests, no JUCE:

| Case | Expected |
|------|----------|
| Empty root + one capture | Mints Phrase containing one Loop; `mintedPhraseId.has_value()` |
| Existing Phrase, capture inside its span | Loop becomes child of that Phrase; `mintedPhraseId == nullopt` |
| Existing Phrase, Mark In inside, Mark Out beyond Phrase end | Loop clamped to host Phrase boundary |
| Mark In before any Phrase, Mark Out inside one | Mints new Phrase (Mark In wins) |
| Multi-instance Phrase id present anywhere in tree | Throws `std::logic_error` |
| Captured region duration ≤ 0 | Throws (defensive; CaptureSession should prevent) |
| Result's `addedLoopId` matches Loop's id in `newRoot` | Pass |
| Result's `mintedPhraseId` matches mint-or-not | Pass |
| Tape information round-trips into the Loop's TapeReference | Pass |

`tests/UndoStackTests.cpp` additions:

| Case | Expected |
|------|----------|
| Promotion-flavored push round-trips the CaptureRestorePoint | Pass |
| `currentEntryRestorePoint()` after `undo()` of a promotion entry | Returns the restore point |
| `currentEntryRestorePoint()` for non-promotion entries | Returns nullopt |

Manual verification (operator-side, not in CI):

- Capture a phrase, see banner, see Pill on timeline.
- Tap banner Undo within 1.5s — Pill disappears, CaptureSession state is `AwaitingOut`, can immediately Mark Out again.
- Mark Out outside any Phrase, then again outside any Phrase — two new Phrases at root.
- Mark Out inside an existing Phrase (overdub case) — new Loop added to it; no second Pill.

### 8. User guide — first chapter

`docs/Sirius Looper User Guide.md` (alongside the white paper in `docs/`).
First chapter:
"Capturing Phrases and Loops." Outline:

- **Glossary** — Tape, Input, Phrase, Loop, Pill, CaptureRegion,
  Mark In, Mark Out, Arm, Playhead, LMC. (Placement and Overlay are
  intentionally **not** in this chapter's glossary — they belong to
  the deferred shared-placement work and would confuse readers about
  what the system does today.)
- **The capture gesture** — Arm → Mark In → Mark Out
- **What Mark Out does** — auto-promotion, the "where does it land?"
  rule with two ASCII diagrams (outside-any-Phrase case,
  inside-existing-Phrase case)
- **When the captured region straddles a Phrase boundary** — Mark In
  wins; clamping behavior; one-line guidance to Undo + retry
- **Recovering from an early Mark Out** — the α undo path, the
  tap-to-undo banner, what state the system returns to
- **Building a song from one instrument, then layering** — the
  workflow:
  1. Pass 1: capture intro / verse / chorus / bridge sequentially on
     guitar (tape 1). Each Mark Out mints a fresh Phrase.
  2. Pass 2: switch to bass (tape 2), scrub to song start, play
     through, capture as each Phrase passes. Each Mark Out joins the
     corresponding Phrase as a Loop child.
- **What's deferred** (Roadmap) — repeating song sections (verse × 3
  sharing layers), normally-hidden tapes view, Phrase rename / role
  edit, capture-history widget (superseded by auto-promotion).

### 9. `todo.md` entry for the deferred prerequisite

A comprehensive entry per `CLAUDE.md` "No Silent Deferral" rule:

```
### 2026-05-15 — Shared-placement-with-per-instance-overlays architecture

- Files: core/include/sirius/Arrangement.h, core/src/Arrangement.cpp,
  core/include/sirius/Constituent.h (possibly), ui/src/TimelineViewState.cpp,
  ui/include/sirius/UndoStack.h, core/src/Promotion.cpp (the runtime
  guard goes away), docs/Sirius Looper User Guide.md (Roadmap section).
- What was deferred: shared-placement semantics for repeated Phrases.
  Today arrangement::sequence creates per-placement Constituent copies;
  the user model requires shared-by-reference with per-instance
  overlay buckets so common layers (drums, bass, rhythm) propagate
  across all verse instances while differentiating layers (vocals,
  fills) attach to one placement only.
- Why deferred: load-bearing for repeated-section workflows but bigger
  than promotion — touches Arrangement, possibly Constituent, the
  TimelineViewState selector, undo semantics across instances, and
  the renderer. Promotion can ship single-instance-correct in the
  meantime; the runtime guard in promote() ensures multi-instance
  cases throw loudly until this is done.
- What's needed:
    1. Settle Path B: arrangement layer becomes a sequence of
       (Phrase ChildPtr, Position, optional overlay-children) tuples.
       Phrase ChildPtr is shared across placements.
    2. Design the per-instance overlay UX (where overlays attach in
       the data model; how the timeline distinguishes shared vs
       overlay; whether overlays are themselves Phrase-shaped or a
       new struct).
    3. Design the "fork this placement into its own Phrase" gesture.
    4. Decide undo semantics across instances (one undo entry =
       all-instances revert, or per-instance?).
    5. Extend promotion::promote to handle the multi-instance case
       (remove the runtime guard, propagate Loop adds across all
       Constituents with matching host id, handle overlay vs shared).
    6. Update TimelineViewState selector to render shared vs forked
       placements distinguishably.
- Prerequisite for: full multi-instance capture/promotion, Loop/Pill
  rendering for repeated phrases, the user-guide chapter on
  "Repeating song sections."
```

## Dependencies between work items

```
1. promotion::promote + tests          ← can land independently
2. UndoStack CaptureRestorePoint       ← can land independently
3. MainComponent wiring                ← needs 1 + 2
4. CaptureBanner tap-to-undo           ← needs 3
5. User guide chapter 1                ← needs 1-4 done so the workflow
                                          described in it is real
6. todo.md entry                       ← lands with 1
```

## Out of scope (explicit)

- Shared-placement semantics. See todo.md entry.
- Per-instance overlay UX. See todo.md entry.
- "Fork this placement" gesture. See todo.md entry.
- Standalone Loop rendering on the timeline. The convention forbids
  standalone Loops; this case cannot occur.
- The capture-history widget (mentioned in the Mark Out todo entry).
  Promotion makes captures persistent and Pill-visible, which serves
  the same need; the explicit history widget is no longer required.
  todo.md should mark it superseded.
- Phrase rename / role-edit UX. Mint defaults are good enough for now;
  editing happens in a later session.
