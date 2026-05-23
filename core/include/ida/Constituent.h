#pragma once

#include "ida/ConstituentId.h"
#include "ida/EffectChain.h"
#include "ida/Meter.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/RepetitionRules.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// How a Constituent aligns to its parent (white paper Appendix A, "Anchor to
/// parent").
enum class AnchorToParent
{
    Start,  ///< aligns to the parent's start
    End,    ///< aligns to the parent's end
    Both,   ///< aligns to both — locked to the parent's full span
    Locked, ///< locked throughout to the parent
    Free    ///< not anchored
};

/// The unifying abstraction for every musical object: loops, phrases, sections,
/// songs, sets all *are* Constituents (white paper Part VII). A Constituent is
/// structure, not data — it holds conceptual boundaries and references to child
/// Constituents, never audio.
///
/// Constituents are immutable. Every edit is copy-on-write: it returns a *new*
/// Constituent that keeps the same identity and *shares* its untouched children
/// (white paper Part 7.3) — child Constituents are held by shared_ptr, so an
/// edit copies a path, never a subtree.
class Constituent
{
public:
    using ChildPtr = std::shared_ptr<const Constituent>;

    /// Constructs a leaf Constituent. `conceptualOut` must not precede
    /// `conceptualIn`; throws std::invalid_argument otherwise.
    Constituent (ConstituentId id, Position conceptualIn, Position conceptualOut);

    ConstituentId id()            const noexcept { return id_; }
    Position      conceptualIn()  const noexcept { return conceptualIn_; }
    Position      conceptualOut() const noexcept { return conceptualOut_; }
    AnchorToParent anchor()       const noexcept { return anchor_; }
    const std::string& name()     const noexcept { return name_; }

    const std::optional<Meter>&    localMeter()    const noexcept { return localMeter_; }
    const std::optional<TempoMap>& localTempoMap() const noexcept { return localTempoMap_; }

    /// How this Constituent plays back — the five dimensions of white paper
    /// Part X. A freshly constructed Constituent carries the default loop rules.
    const RepetitionRules& repetitionRules() const noexcept { return repetitionRules_; }

    /// The phrase metadata, present only when this Constituent is functioning
    /// as a phrase (white paper Part VIII) rather than a bare loop or slice.
    const std::optional<PhraseMetadata>& phraseMetadata() const noexcept { return phraseMetadata_; }
    bool isPhrase() const noexcept { return phraseMetadata_.has_value(); }

    /// The reference into a source tape, present only when this Constituent is
    /// a loop — the Constituent type that directly references tape data (white
    /// paper Part 7.4).
    const std::optional<TapeReference>& tapeReference() const noexcept { return tapeReference_; }
    bool isLoop() const noexcept { return tapeReference_.has_value(); }

    /// The signal-processing chain attached to this Constituent (white paper
    /// Part 7.7: "Effects are applied per-Constituent and are replaceable").
    /// Present only when at least one effect has been added; absent means the
    /// dry signal is passed through.
    const std::optional<EffectChain>& effectChain() const noexcept { return effectChain_; }
    bool hasEffectChain() const noexcept { return effectChain_.has_value(); }

    const std::vector<ChildPtr>& children() const noexcept { return children_; }
    bool isLeaf() const noexcept { return children_.empty(); }

    /// The Constituent's span, in its parent's conceptual time (whole notes).
    Rational duration() const;

    // --- Copy-on-write edits. Each returns a new Constituent with the same
    //     identity; untouched children are shared, not copied. ---

    Constituent withBoundaries (Position conceptualIn, Position conceptualOut) const;
    Constituent withName (std::string name) const;
    Constituent withAnchor (AnchorToParent anchor) const;
    Constituent withLocalMeter (Meter meter) const;
    Constituent withoutLocalMeter() const;
    Constituent withLocalTempoMap (TempoMap tempoMap) const;
    Constituent withoutLocalTempoMap() const;
    Constituent withRepetitionRules (RepetitionRules rules) const;
    Constituent withPhraseMetadata (PhraseMetadata metadata) const;
    Constituent withoutPhraseMetadata() const;
    Constituent withTapeReference (TapeReference reference) const;
    Constituent withoutTapeReference() const;
    Constituent withEffectChain (EffectChain chain) const;
    Constituent withoutEffectChain() const;
    Constituent withChildAdded (ChildPtr child) const;
    Constituent withChildReplaced (std::size_t index, ChildPtr child) const;
    Constituent withChildRemoved (std::size_t index) const;

private:
    ConstituentId id_;
    Position conceptualIn_;
    Position conceptualOut_;
    std::optional<Meter> localMeter_;
    std::optional<TempoMap> localTempoMap_;
    AnchorToParent anchor_ { AnchorToParent::Free };
    std::string name_;
    RepetitionRules repetitionRules_;
    std::optional<PhraseMetadata> phraseMetadata_;
    std::optional<TapeReference> tapeReference_;
    std::optional<EffectChain>   effectChain_;
    std::vector<ChildPtr> children_;
};

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

} // namespace ida
