#include "sirius/Promotion.h"

#include "sirius/Phrase.h"
#include "sirius/TapeReference.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace sirius::promotion
{

namespace
{
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
                    "sirius::promotion: ConstituentId aliased across distinct allocations "
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

    /// Compose a child's parent-to-LMC mapping from its placement and any
    /// optional local TempoMap. Same pattern as TimelineViewState::walk.
    using ParentToLmc = std::function<Rational (Rational)>;

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

    /// Result of finding the deepest Phrase whose LMC span contains `lmcAtMarkIn`.
    struct HostHit
    {
        std::vector<std::size_t> path;  // index path through children from root
        Rational hostStartLmc;
        Rational hostEndLmc;
        std::string hostName;
    };

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
}

// Exceeds the 100-line default because Overlay / Shared / Mint are three
// exhaustive, mutually structured paths the plan inlines together; splitting
// them would scatter the decision shape that this function exists to make.
PromotionResult promote (const Constituent&   root,
                         const TempoMap&      sessionToLmc,
                         const CaptureRegion& region,
                         Rational             lmcAtMarkIn,
                         AttachmentMode       requestedMode,
                         const IdAllocator&   allocateId)
{
    if (! (region.outLmcSeconds > region.inLmcSeconds))
        throw std::invalid_argument (
            "sirius::promotion::promote: region duration must be strictly positive");

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

        // Pointer-identity-preserving splice: locate the original host
        // ChildPtr by walking `hit->path`, build the new host (host + loop
        // appended), then walk the entire tree and replace every occurrence
        // of the original ChildPtr with the new one. This keeps shared
        // placements actually shared after the edit — wrappers that pointed
        // to the same host before the edit all point to the same new host
        // after the edit. Falls back to a per-index splice when the host
        // sits directly at the root (path of length zero), which has no
        // enclosing ChildPtr to share.
        Constituent newRoot = root;
        if (hit->path.empty())
        {
            newRoot = root.withChildAdded (
                std::make_shared<const Constituent> (loop));
        }
        else
        {
            // Descend to the parent of the host, locate the original host
            // ChildPtr, and build the new host pointer.
            const Constituent* parent = &root;
            for (std::size_t depth = 0; depth + 1 < hit->path.size(); ++depth)
                parent = parent->children()[hit->path[depth]].get();
            const auto& originalHostPtr = parent->children()[hit->path.back()];
            const auto newHostPtr = std::make_shared<const Constituent> (
                originalHostPtr->withChildAdded (
                    std::make_shared<const Constituent> (loop)));

            // Walk the tree and replace every ChildPtr equal (by pointer) to
            // originalHostPtr with newHostPtr. Untouched subtrees keep their
            // ChildPtrs intact so unrelated structural sharing is preserved.
            // Returns nullopt when the subtree had no occurrences (caller
            // keeps the original pointer); returns a rebuilt Constituent
            // when at least one replacement happened in that subtree.
            std::function<std::optional<Constituent> (const Constituent&)> replaceShared;
            replaceShared = [&] (const Constituent& c) -> std::optional<Constituent>
            {
                std::optional<Constituent> rebuilt;
                for (std::size_t i = 0; i < c.children().size(); ++i)
                {
                    const auto& childPtr = c.children()[i];
                    if (childPtr.get() == originalHostPtr.get())
                    {
                        if (! rebuilt.has_value()) rebuilt = c;
                        rebuilt = rebuilt->withChildReplaced (i, newHostPtr);
                        continue;
                    }
                    if (auto rewrittenChild = replaceShared (*childPtr))
                    {
                        if (! rebuilt.has_value()) rebuilt = c;
                        rebuilt = rebuilt->withChildReplaced (i,
                            std::make_shared<const Constituent> (
                                std::move (*rewrittenChild)));
                    }
                }
                return rebuilt;
            };
            if (auto rewrittenRoot = replaceShared (root))
                newRoot = std::move (*rewrittenRoot);
        }

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

} // namespace sirius::promotion
