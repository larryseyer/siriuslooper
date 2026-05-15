#pragma once

#include "sirius/CaptureSession.h"
#include "sirius/Constituent.h"
#include "sirius/ConstituentId.h"
#include "sirius/Rational.h"
#include "sirius/TempoMap.h"

#include <functional>
#include <optional>
#include <string>

namespace sirius::promotion
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

} // namespace sirius::promotion
