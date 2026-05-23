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
    std::optional<std::string>   hostPhraseName;  // present iff a host Phrase was found

    /// What promote() actually did. Equal to the requested mode except in the
    /// Overlay-outside-any-wrapper case, where it downgrades to Shared.
    AttachmentMode resolvedMode { AttachmentMode::Shared };

    /// 1-based left-to-right placement index of the wrapper the Overlay was
    /// attached to. Present only when `resolvedMode == Overlay`.
    std::optional<std::size_t> overlayPlacementIndex;

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
///   * Throws std::logic_error if the pointer-aware guard catches aliased ids
///     — distinct Constituents sharing one id but not one ChildPtr.
///   * Throws std::invalid_argument if the region's duration is non-positive.
///   * `requestedMode == Shared` walks `root` to find the deepest Phrase
///     whose LMC span contains `lmcAtMarkIn`. Wrappers are descended through
///     but never themselves a host; the host is the shared Phrase beneath.
///     If found, adds a Loop as a child of that Phrase (clamped to the
///     host's bounds in the straddle case). If not found, mints a new Phrase
///     at the song root, containing the Loop.
///   * `requestedMode == Overlay` finds the deepest wrapper containing
///     `lmcAtMarkIn` and attaches the Loop as a peer of the shared Phrase
///     within that wrapper. If no wrapper covers Mark In, silently
///     downgrades to Shared (see `PromotionResult::resolvedMode`).
///
/// M3 simplification: conceptual time is treated as 1:1 with LMC seconds for
/// the purposes of computing new Loop / Phrase boundaries. The demo session
/// uses identity-rate tempo maps. Non-trivial tempo maps will require an
/// inverse mapping on TempoMap before they can compose with promotion.
PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         AttachmentMode       requestedMode,
                         const IdAllocator&   allocateId);

/// Walk `root` and throw std::logic_error on any ConstituentId reached via
/// two distinct shared_ptr allocations. Genuine sharing (one ChildPtr held
/// at multiple positions in the tree, the verse-×-3 shape) is permitted and
/// each unique allocation is walked exactly once. This is the shared-placement
/// invariant from the 2026-05-16 spec; `promote()` runs it before any edit,
/// and `persistence::deserializeSession` runs it after rebuilding the graph
/// from JSON so loads fail loud when the on-disk shape is inconsistent.
void enforceSharedInstancesAreShared (const Constituent& root);

} // namespace ida::promotion
