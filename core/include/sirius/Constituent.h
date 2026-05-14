#pragma once

#include "sirius/Meter.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TempoMap.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sirius
{

/// A Constituent's persistent identity. It survives every content revision: a
/// phrase named "the verse" remains the same phrase, with the same id, through
/// fifteen revisions of its content (white paper Part 7.6). Versioning happens
/// at the content level; identity persists at the structural level.
class ConstituentId
{
public:
    explicit constexpr ConstituentId (std::int64_t value) noexcept : value_ (value) {}

    std::int64_t value() const noexcept { return value_; }

    bool operator== (const ConstituentId& other) const noexcept { return value_ == other.value_; }
    bool operator!= (const ConstituentId& other) const noexcept { return value_ != other.value_; }

private:
    std::int64_t value_;
};

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
    std::vector<ChildPtr> children_;
};

} // namespace sirius
