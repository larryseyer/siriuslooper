# Shared-placement architecture — Design

**Date:** 2026-05-16
**Scope:** Shared-by-reference Phrase placements with per-instance overlay Loops, the fork-this-placement escape hatch, the multi-instance promotion path, and the timeline rendering that surfaces all three. Removes the runtime guard installed by the capture-promotion design.
**Status:** Brainstorm complete; awaiting implementation plan.

## Problem

`arrangement::sequence` (`core/src/Arrangement.cpp:60`) creates
**per-placement Constituent copies**: `placedAt` calls `withBoundaries`
to rewrite each child's `conceptualIn/Out`, so three placements of the
verse become three distinct `Constituent` objects that happen to share
the same `ConstituentId`. The capture-promotion runtime guard in
`core/src/Promotion.cpp:17-27` catches this shape and throws — it is
the IOU the prior design wrote, and this design pays it.

The operator's musical model: **"the verse plays 3 times, sharing
common layers (drums, bass, rhythm) with per-instance vocal
variations."** Repeated phrases should be shared by reference, with
per-instance overlay buckets for the differentiating layers.

This design unblocks the M3 arrangement story and is the data-model
prerequisite for M8 ensemble (multi-tape capture across networked
nodes).

## Design principle: protect the creative musician

Every decision here is filtered through one constraint: the operator is
a musician in flow, not a configuration editor. The data model must
support sharing-and-overlay semantics, but **the operator-facing
surface must stay minimal**. New affordances earn their place by
solving a real musical problem; they are not added because the data
model now permits them. The simplest possible UX wins — if the
operator can't form a mental model of what a gesture does in under a
second, the gesture is wrong.

**Hide the internals.** Tapes, the wrapper convention, placement
indices, role strings, sibling sets, attachment modes — none of these
appear in the default operator view. The musician sees **phrases and
loops**: the verse, the chorus, the bass line, the vocal ad-lib. They
do not see "tape #200" or "placement 2" or "shared host." Advanced
inspection surfaces (the Preparation pane's diagnostics row, a future
session-inspector) may expose internals for debugging, but they are
opt-in and never appear in the primary capture/playback flow. Every
UI string introduced by the implementation plan must pass this check:
*would a musician understand it without learning IDA's data
model?* If not, the string is wrong.

## Established model (from brainstorm)

The Constituent type system today is permissive — a `Constituent` can
carry `PhraseMetadata`, a `TapeReference`, both, or neither. The
capture-promotion design tightened the convention:

| Type   | What's attached                    | Children allowed?              |
|--------|------------------------------------|--------------------------------|
| Loop   | `TapeReference` (audio slice)      | No — leaf only                 |
| Phrase | `PhraseMetadata` (musical thought) | Yes — Loops and/or sub-Phrases |

This design preserves that convention. **Phrases-inside-Phrases stays
fully supported** — e.g. a song's verse Phrase can still contain
sub-Phrases for chorus/bridge nesting. The new restriction is narrower
and applies only to the wrapper's overlay slots; see §1.

### Decisions locked in the brainstorm

| # | Decision |
|---|---|
| 1 | **Wrapper Constituent** as the placement carrier (chosen over a tuple layer or a positioned-children rewrite of `Constituent`) |
| 2 | **Shared-by-default** capture into a shared placement; long-press Mark In = overlay (instance-only) |
| 3 | **Fork is irreversible** (UndoStack covers immediate regret; no explicit re-share/merge-back command) |
| 4 | **Tie-bar render** across shared Pills; small dot for overlays; prime mark for forked |
| 5 | **Promote demo verse to ×3** + **lazy wrapping** (only wrap when count > 1) |
| 6 | **Wrapper recognized by convention:** `PhraseMetadata::role == "placement"`. No new field on `Constituent` |
| 7 | **Wrapper overlay slots accept Loops only.** Phrases-inside-Phrases remains supported everywhere else |
| 8 | **Runtime guard becomes pointer-aware:** duplicate ids allowed iff every occurrence is the same `shared_ptr` |
| 9 | **`arrangement::sequenceShared(parent, phrase, offsets, IdAllocator)`** — uses the same allocator pattern as `promotion::promote` |

## Design

### 1. Data shape

No new types in `core/`. The wrapper convention is layered on top of
the existing `Constituent` API.

```
song (no PhraseMetadata; bare container)
├── intro Phrase (role="intro")                    ← bare, no wrapper
│   └── intro Loop (TapeReference)
├── verse wrapper-A (role="placement", [3,9))      ← wrapper
│   └── verse Phrase (role="verse", id=20)         ← SHARED ChildPtr
├── verse wrapper-B (role="placement", [9,15))     ← wrapper
│   ├── verse Phrase (id=20)                       ← SAME ChildPtr
│   └── vocals-B Loop (TapeReference, overlay)
├── verse wrapper-C (role="placement", [15,21))    ← wrapper
│   └── verse Phrase (id=20)                       ← SAME ChildPtr
└── outro Phrase (role="outro")                    ← bare, no wrapper
    └── outro Loop (TapeReference)
```

**Wrapper invariants** (enforced by `promotion` and `arrangement` code
paths; tests pin them):

- `phraseMetadata().has_value() && phraseMetadata()->role == "placement"`
- `! tapeReference().has_value()` (the wrapper is a Phrase, never a hybrid — same convention Pick A locked)
- `children().size() >= 1`
- `children()[0]->isPhrase()` (the shared canonical Phrase, never a Loop)
- For `i >= 1`: `children()[i]->isLoop()` (overlays are leaf Loops only)

**What this means for "phrases inside phrases":** every Phrase in the
tree can still contain sub-Phrase children, *including* the shared
Phrase inside a wrapper. The restriction in the fifth bullet applies
only to the wrapper's *overlay positions* (children at index ≥ 1) — it
does not propagate down into the shared Phrase. If the operator wants a
per-instance sub-Phrase (rare; usually means "this verse should be its
own thing"), the path is **fork the placement**, not "Phrase as
overlay." The fork gesture is what makes per-instance Phrase-shaped
content possible.

### 2. New primitive: `arrangement::sequenceShared`

`core/include/ida/Arrangement.h` gains one declaration:

```cpp
namespace ida::arrangement
{
/// Places `phrase` at each offset in `offsets`, producing one wrapper
/// Constituent per offset. All wrappers share `phrase` by reference
/// (the same shared_ptr). Each wrapper's conceptualIn/Out spans
/// [offset, offset + phrase->duration()) in `parent`'s local time.
/// Each wrapper is minted with PhraseMetadata{ role = "placement" }
/// and the shared `phrase` as its only child.
///
/// `allocateId` is called once per offset to mint the wrapper id, in
/// offset order, matching the IdAllocator pattern used by
/// `promotion::promote`.
///
/// Throws std::invalid_argument if `phrase` is null or `offsets` is
/// empty.
Constituent sequenceShared (const Constituent&             parent,
                            const Constituent::ChildPtr&   phrase,
                            const std::vector<Position>&   offsets,
                            const IdAllocator&             allocateId);
}
```

`IdAllocator` is the same `std::function<ConstituentId()>` typedef the
promotion module already defines. `DemoSession` passes a deterministic
counter that yields the demo's hard-coded wrapper ids in order;
`MainComponent` passes its live id-mint closure. One signature, two
call sites, consistent with `promote`.

The existing `arrangement::sequence` is unchanged and stays in use for
single-placement layouts (intro and outro in the demo, and any future
Phrase that appears exactly once).

### 3. Wrapper recognition predicate

A single shared free function in `core/include/ida/Constituent.h`,
used by every tree walker (selector, promotion, renderer) that needs
the wrapper-vs-bare-Phrase distinction:

```cpp
inline bool isPlacementWrapper (const Constituent& c) noexcept
{
    return c.isPhrase()
        && c.phraseMetadata()->role == "placement"
        && ! c.children().empty()
        && c.children()[0]->isPhrase();
}
```

Convention, not a new type. The predicate is the single source of
truth, so a future migration to a dedicated `bool is_placement` field
on `Constituent` is a one-file refactor: change the predicate body, no
call site moves. We do not take that escape hatch in v1.

### 4. Runtime guard becomes pointer-aware

`core/src/Promotion.cpp`'s `enforceSingleInstance` is replaced with
`enforceSharedInstancesAreShared`. It walks the tree once, mapping each
encountered `ConstituentId::value()` to the `shared_ptr` it was first
reached through; on subsequent encounters of the same id, it checks
`shared_ptr` identity:

- **Pointer-equal** (true sharing) → allowed, continue walking.
- **Pointer-distinct** (two distinct allocations with the same id) →
  throw `std::logic_error`. This catches aliasing-by-mistake — the
  exact bug shape the old guard caught — without forbidding the
  legitimate shared-placement case.

The guard runs at the top of `promotion::promote`, same as today.

### 5. Promotion changes (`promotion::promote`)

Two behavioral changes to `core/src/Promotion.cpp`:

**(a) Host-finding descends through wrappers.** When
`findHostRecursive` encounters a wrapper Constituent (`isPlacementWrapper`),
it does NOT consider the wrapper a candidate host. It descends into the
wrapper's first child (the shared Phrase) and continues. This
implements decision #2 — the host the operator "means" is the shared
musical Phrase, never the placement structure.

**(b) Attachment-mode parameter.** `promote` gains an `AttachmentMode`
argument and `PromotionResult` gains a matching field:

```cpp
namespace ida::promotion
{
enum class AttachmentMode
{
    Shared,  ///< default: add Loop to the shared Phrase host
    Overlay  ///< add Loop to the enclosing wrapper as an overlay; only
             ///< meaningful when Mark In falls inside a wrapper
};

struct PromotionResult
{
    Constituent newRoot;
    ConstituentId addedLoopId;
    std::optional<ConstituentId> mintedPhraseId;
    std::optional<std::string> hostPhraseName;
    std::string undoLabel;
    AttachmentMode resolvedMode;  ///< what actually happened (may differ
                                  ///< from request — see downgrade rule)
    std::optional<std::size_t> overlayPlacementIndex;  ///< 1-based, when
                                                       ///< resolvedMode == Overlay
};

PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         AttachmentMode       requestedMode,
                         const IdAllocator&   allocateId);
}
```

**Behavior:**

- `requestedMode == Shared` (the default): same as the prior
  single-instance promotion, except the host walk now descends through
  wrappers per (a).
- `requestedMode == Overlay` AND the deepest containing wrapper covers
  Mark In: the new Loop is added as a child of that wrapper (peer of
  the wrapper's shared Phrase), not as a child of the shared Phrase.
  `resolvedMode == Overlay`, `overlayPlacementIndex` reports which
  wrapper among the shared siblings (1-based, by left-to-right order
  on the timeline).
- `requestedMode == Overlay` but no wrapper covers Mark In: silently
  downgrade to `Shared`. The request has no coherent meaning outside a
  shared-placement context (there's no per-instance slot to attach
  to). `resolvedMode == Shared`. The banner copy reflects the
  downgrade so the operator gets feedback ("captured as shared — no
  placement at Mark In").

### 6. Capture gesture: long-press Mark In = overlay

**The cross-platform analog of an Option-modifier on iOS is
long-press.** This design adopts long-press as the single
overlay-capture gesture across all platforms; no separate desktop
modifier. The operator's mental model is the same everywhere:

- **Tap (or click) Mark In** → `requestedMode = Shared`. The default.
- **Long-press Mark In** (≥ 500 ms held) → `requestedMode = Overlay`.
  Mark In fires at press onset; the long-press detection upgrades the
  pending mode before Mark Out.

On desktop, the same long-press works (mouse-down for 500 ms). For
desktop power users who dislike holding, an Option-click accelerator
is acceptable as a future addition — but v1 ships long-press only, to
keep one mental model on day one. The capture banner reflects the
resolved mode so the operator confirms intent without checking a
modifier-state indicator.

This decision is calibrated to the design principle (§ "Protect the
creative musician"): one gesture, one meaning, works the same on every
device. No keyboard chord to learn, no toggle to forget about.

### 7. Selector changes (`TimelineViewState`)

`ui/include/ida/TimelineViewState.h` extends `PillState`:

```cpp
struct PillState
{
    // ... existing fields ...

    /// For shared placements: the ids of the other wrappers that share
    /// the same underlying Phrase ChildPtr. The renderer draws a
    /// tie-bar across the wrappers in this set ∪ {this Pill's id}.
    /// Empty for bare Phrases and for forked placements.
    std::vector<ConstituentId> sharedSiblings;

    /// True iff this placement has instance-only overlay Loops
    /// (children at index ≥ 1 on the wrapper). Renderer draws the
    /// overlay-dot marker.
    bool hasOverlays { false };

    /// True iff this Pill represents a placement that was forked from
    /// a previously-shared one. Detected via PhraseMetadata::role ==
    /// "forked-placement". Renderer draws the prime mark (').
    bool isForked { false };
};
```

`ui/src/TimelineViewState.cpp` (`selectTimelineView`) changes:

- **Bare Phrase under root** → one Pill, content unchanged.
- **Wrapper (`isPlacementWrapper`)** → one Pill emitted *for the
  wrapper*. The Pill's `id` is the wrapper's id. Pill content
  (`loopCount`, `entranceName`, `exitName`, `primaryTape`,
  `memberTapes`, `name`) is derived from the **shared Phrase child**,
  not the wrapper itself. Wrapper overlays are tallied into
  `hasOverlays` but are NOT independently emitted as Pills.
- **Shared Phrase reached inside a wrapper** → suppressed (already
  represented by the wrapping Pill).
- **Forked placement** → `phraseMetadata().role == "forked-placement"`
  on the wrapper triggers `isForked = true` and excludes the wrapper
  from `sharedSiblings` grouping.

`sharedSiblings` is computed in a second pass after the first walk
collects all wrappers: group wrappers by the pointer-identity (`get()`
on the `shared_ptr`) of their first child; every wrapper in a group of
size ≥ 2 records the other group members' ids.

### 8. Fork gesture and bookkeeping

The fork action targets one wrapper (e.g. `wrapper-B`):

1. Deep-copy the shared Phrase subtree: every Constituent in the
   subtree receives a fresh `ConstituentId` from the allocator. The
   structure is preserved; only ids change.
2. Replace the wrapper's first child (`withChildReplaced(0, copy)`)
   with the deep copy. The wrapper keeps its own id.
3. Change the wrapper's `PhraseMetadata::role` from `"placement"` to
   `"forked-placement"`. The selector keys on this for `isForked`.
4. Post-fork, the wrapper is no longer a sharing participant — it
   drops out of the tie-bar grouping. The forked Phrase has no
   sharing relationship with the original.

The action is a single edit producing one new root and one UndoStack
entry. Undo restores the pre-fork shape. There is no separate "unfork"
or "re-share" command (decision #3) — once divergent edits accumulate,
the only "back" is the regular undo chain.

The **UI surface for the fork gesture** is deliberately left to the
implementation plan, not pinned here. The operator-facing label is
**"Vary this one"** — never "fork" (engineering vocabulary), never
"diverge" (jargon), never "unshare" (negative framing of a positive
musical intent). The musician's frame is "I want this verse to be a
little different" — the label matches that frame. Candidates for the
gesture surface: right-click on a Pill in Preparation → "Vary this
one," long-press on a Pill on touch. Spec invariant: the gesture is
reachable only from Preparation mode (not Performance) — varying a
section is a compositional act, not a performance act.

### 9. Undo semantics

UndoStack architecture is unchanged. Every gesture (shared layer add,
overlay add, fork) produces one new root snapshot and one UndoStack
entry; undo reverts the entire root. The "per-instance vs all-instances
undo" question the brainstorm posed collapses into "which capture
gesture did the operator use" — the gesture's *scope* (shared via
default; overlay via long-press) already encodes whether one or all
placements are affected. No special multi-instance undo machinery is
required.

The existing `CaptureRestorePoint` mechanism (capture-promotion §5) is
unchanged — Mark Out → undo still restores `CaptureSession::AwaitingOut`
regardless of `resolvedMode`.

### 10. Demo session migration

`app/DemoSession.cpp` is rewritten so the demo natively exercises the
new shape:

- `sessionShell` length grows from 12 to **24 whole notes**.
- `intro` stays a bare Phrase at [0, 3).
- `verse` becomes **one shared Phrase** (id=20, the existing id),
  placed three times via `arrangement::sequenceShared` at offsets 3, 9,
  15. Three wrappers are minted with ids 51, 52, 53. All three share
  the same `ChildPtr` for the verse Phrase.
- `outro` stays a bare Phrase at [21, 24).

LMC seconds: at 120 BPM, 24 whole notes = 48 LMC seconds.

The demo's existing per-tape `InputDescriptor`s are unchanged — the
shared verse Phrase still references the same rhythm/lead tapes. The
demo's headless tests assert pointer-identity equality across the
three wrappers' first children, which is the cheapest way to prove
"this is real sharing, not duplicate-id-by-mistake."

### 11. MainComponent wiring

Two additions:

- **Long-press detection on Mark In.** A 500 ms hold upgrades the
  pending capture's `AttachmentMode` from `Shared` to `Overlay` before
  Mark Out fires. JUCE's `juce::Button::onClick` plus
  `MouseListener::mouseDown`/`mouseUp` with a timer covers this on
  every platform.
- **Banner copy reflects `resolvedMode` — musician language only.** Four
  message templates. Tape numbers, durations, attachment-mode names,
  and placement indices are *plumbing* and do not appear in the banner;
  they live in the Preparation pane's diagnostics row for the operator
  who actually wants them.

  | resolvedMode | mintedPhraseId | Banner |
  |---|---|---|
  | Shared | none | `"Added to <phrase name>"` (e.g. `"Added to verse"`) |
  | Shared | some | `"New phrase captured"` |
  | Overlay | none | `"Added to <phrase name> <i> only"` (e.g. `"Added to verse 2 only"`) |
  | Shared (downgraded from Overlay) | none | `"Added to <phrase name> — no section here yet"` or `"Added — no section here yet"` if no host |

  The ordinal in the Overlay case ("verse 2") is the placement's
  left-to-right position on the timeline (1-based), not its
  `overlayPlacementIndex` data field — same number, different
  vocabulary. If the shared phrase is unnamed, the banner falls back
  to `"Added to the phrase here"` (and equivalents). No internal
  identifiers ever surface.

### 12. Tests

`tests/ArrangementTests.cpp` — new cases:

| Case | Expected |
|------|----------|
| `sequenceShared` with one offset | Wrapper of correct shape; pointer-identical shared child |
| `sequenceShared` with three offsets | Three wrappers, all pointing at the same shared_ptr (verified by `get()` equality) |
| `sequenceShared` null phrase | Throws `std::invalid_argument` |
| `sequenceShared` empty offsets | Throws `std::invalid_argument` |
| Mixed bare + wrapped sequence | Root holds intro (bare), three verse wrappers, outro (bare); all placements end-to-end |

`tests/PromotionTests.cpp` — new cases:

| Case | Expected |
|------|----------|
| Shared-default capture lands in shared Phrase | New Loop is a child of `shared`; visible from all wrappers; `resolvedMode == Shared` |
| Overlay-modifier capture lands on wrapper | New Loop is a child of the specific wrapper; other wrappers unchanged; `resolvedMode == Overlay`; `overlayPlacementIndex` correct |
| Overlay-modifier capture outside any wrapper | Downgraded to `Shared`; `resolvedMode == Shared`; landing matches the existing host-Phrase/mint logic |
| Pointer-aware guard accepts true sharing | Tree built via `sequenceShared` does not throw |
| Pointer-aware guard rejects aliased-id-by-mistake | Tree with two distinct Constituents sharing an id throws `std::logic_error` |
| Existing 8 promotion cases | All still pass unchanged |

`tests/TimelineViewStateTests.cpp` — new cases:

| Case | Expected |
|------|----------|
| Wrapper produces one Pill (not two) | Wrapper's id is the Pill's id; shared Phrase suppressed |
| Pill content delegates to shared Phrase | `loopCount`, `entranceName`, `name` derived from shared child |
| `sharedSiblings` populated for shared placements | Three wrappers → each Pill's `sharedSiblings` contains the other two ids |
| `sharedSiblings` empty for bare Phrases | Intro/outro Pills have `sharedSiblings.empty()` |
| `hasOverlays` flips when overlay Loop present | Wrapper-with-overlay's Pill has `hasOverlays == true` |
| `isForked` flips after fork edit | Forked wrapper's Pill has `isForked == true` and `sharedSiblings.empty()` |

`tests/DemoSessionTests.cpp` — updated cases:

| Case | Expected |
|------|----------|
| `[demoSession][shape]` | 24-whole-note span; intro [0,3); three verse wrappers at [3,9), [9,15), [15,21); outro [21,24) |
| `[demoSession][shared]` (new) | The three verse wrappers' first children are pointer-equal (`get()` equality) |

Full suite target: ≥ 235 (current) + new cases passing, zero
regressions. Some existing `[demoSession]`-tagged tests will update
expectations to match the new shape; that is expected scope of this
change, not regression.

### 13. User guide — Roadmap update + future chapter

`docs/IDA User Guide.md` Roadmap section gains a paragraph
naming "repeating song sections" as the next musician-visible feature
landing in this design. A dedicated chapter is **not** written yet; it
lands once the implementation ships and operator verification confirms
the gestures feel right. Working chapter title: **"When a section
plays more than once."** Outline, in musician framing — note that
"shared," "wrapper," "placement," "overlay," and "fork" do not appear
in the chapter at all:

- **The verse plays three times.** What it looks like on the timeline
  (three verse pills with a tie above them — the tie means "these are
  the same verse").
- **Recording into a verse adds to every verse.** That's the default.
  Lay down the bass once; it's there in all three.
- **Recording something just for one verse.** Hold the Mark In button
  for a beat (long-press). The capture lands only in that verse — the
  tie tells you which one. The pill gets a little dot showing it has
  something extra.
- **Making one verse different on purpose.** "Vary this one" on the
  pill — that verse stops being tied to the others. From then on you
  edit it on its own. The pill loses its tie and gains a small mark
  showing it's its own thing now.
- **Reading the timeline at a glance.** The tie, the dot, the
  variation mark — what each one means in one line each.

### 14. `todo.md` resolution

The existing `todo.md:42` entry ("Shared-placement-with-per-instance-
overlays architecture") is the brief that produced this design. On
spec approval it is marked **SUPERSEDED** with a pointer to this spec
and to the implementation plan that follows; the original entry stays
in `todo.md` (history) but no longer represents open work.

## Dependencies between work items

```
1. arrangement::sequenceShared + tests        ← can land independently
2. isPlacementWrapper predicate               ← lands with (1)
3. Pointer-aware runtime guard + tests        ← can land independently
4. promotion AttachmentMode + tests           ← needs (2), (3)
5. TimelineViewState wrapper-aware selector   ← needs (2)
6. TimelineView renderer (tie-bar, dot, prime) ← needs (5)
7. DemoSession ×3 migration + tests           ← needs (1), (2), (3)
8. MainComponent long-press + banner copy     ← needs (4)
9. Fork gesture (data + minimal UI surface)   ← needs (5), (6)
10. User guide Roadmap update                 ← lands with (9)
11. todo.md SUPERSEDED                        ← lands with (1)
```

## Out of scope (explicit)

- **Persistence of shared placements on disk.** `SessionFormat` will
  need to encode sharing (content-address the shared Phrase by id;
  rehydrate with a pointer-pool that reattaches the same shared_ptr to
  every wrapper). Tracked in the existing session-directory-format
  todo entry; this spec assumes round-trip is a follow-up and the
  in-memory demo is the verification surface.
- **Per-instance metadata richer than "is this a fork"** (e.g. "this
  verse plays softer than the others," per-instance effect chains).
  Hooks exist via `PhraseMetadata` if needed, but no specific
  extension is designed here.
- **Phrase-shaped overlays.** Constraint #7 explicitly forbids them in
  v1. The escape hatch is fork-the-placement. If a real operator
  workflow demonstrates the need, the constraint is straightforward to
  lift.
- **Fork-gesture UI surface details** (menu wording, keyboard shortcut,
  touch affordance). Pinned to "Preparation-mode only" here; concrete
  affordance is an implementation-plan decision so it can be evaluated
  against the actual UI alongside the rest of the work.
- **M8 ensemble (multi-tape capture across networked nodes).** This
  design is the data-model prerequisite; the ensemble layer is a
  future spec.

## 15. Operator vocabulary (QA checklist for UI strings)

Every UI string introduced by the implementation plan is checked
against this table. Internal terms on the left **must not appear** in
banners, menus, tooltips, error toasts, or any other operator-facing
surface in the default flow. Where an operator-facing equivalent
exists, use it; where it doesn't, surface nothing.

| Internal term (data model) | Operator-facing term (default UI) | Notes |
|---|---|---|
| Tape, TapeId, "tape #200" | *(not surfaced)* | Tapes are routing plumbing. The musician sees inputs by their name ("guitar," "vocals"), never tape numbers. Diagnostics row in Preparation may show tape ids for advanced inspection. |
| Constituent | *(not surfaced)* | The musician sees phrases and loops, never the underlying abstraction. |
| Wrapper, placement wrapper | *(not surfaced)* | The musician sees a phrase pill on the timeline. "Wrapper" is implementation. |
| Placement, placement index | "verse 2" / "the second verse" | Ordinal counting that matches what the musician sees on the timeline (1-based, left-to-right). |
| Shared sibling, sharedSiblings | The tie above the pills | The visual artifact is the only surfacing of sharing; no text. |
| Overlay, overlay slot | "something just for this verse" / the dot on the pill | The dot is the visual; if text is needed, frame it as "for this one only." |
| Fork, divergence | "Vary this one" / the small mark on the pill | The menu label is "Vary this one." The visual is a small mark distinguishing the variation from its siblings. |
| AttachmentMode (Shared / Overlay) | *(not surfaced as a mode)* | The musician does not pick a mode; they make a gesture (tap or long-press) and the system responds. No mode indicator in the default UI. |
| Role string ("placement", "forked-placement", "capture") | *(not surfaced)* | These are dispatch keys for the selector. They never appear in operator-visible text. |
| ConstituentId, addedLoopId, mintedPhraseId | *(not surfaced)* | Numeric ids exist for the data model; the musician sees names. |
| LMC seconds, conceptualIn/Out, whole notes | "seconds" / "bars" | Time in operator-facing copy uses the musician's units (seconds for raw time; bars when a meter is in play). LMC is the engine's internal coordinate. |
| `resolvedMode`, `overlayPlacementIndex`, `hostPhraseName` | *(not surfaced)* | Result-struct fields drive banner *content*; their names never appear. |
| Mark In, Mark Out, Arm | Mark In, Mark Out, Arm | These ARE operator vocabulary — established in the user guide chapter 1. Keep them as-is. |
| Phrase, Loop, Section | Phrase, Loop, Section | These ARE operator vocabulary — established by the white paper and user guide. Keep them as-is. |

**Advanced-surface exception:** the Preparation pane's diagnostics
row, a future session-inspector window, and any debug/developer view
may surface internal terms freely. These surfaces are opt-in (the
musician must navigate to them deliberately) and never appear during
capture, playback, or arrangement editing in the default flow.

The implementation plan's review checklist for any new UI string is
a single question: *if a musician who has never read the white paper
sees this, do they understand it?* If no, the string fails review.

## Open items the implementation plan will revisit

These are flagged here, not decided here, because they are better
judged when the implementation reveals their cost:

- **Long-press duration tuning.** 500 ms is a starting point; the
  implementation plan and operator verification should re-evaluate
  against the actual capture flow. Too short and a normal tap fires
  Overlay; too long and the gesture feels sticky.
- **Desktop accelerator for overlay.** Option-click as a future addition
  for desktop power users. Deferred until v1 long-press lands and the
  ergonomics are real.
- **Fork UI surface.** Right-click menu, long-press on Pill, dedicated
  button, or Preparation-mode-only context affordance. Decided in the
  implementation plan against the live UI.
