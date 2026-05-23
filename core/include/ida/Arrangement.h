#pragma once

#include "ida/Constituent.h"
#include "ida/ConstituentId.h"
#include "ida/Position.h"
#include "ida/Rational.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// A role-fillable position within an arrangement (white paper Parts 8.4, 11.3):
/// an unfilled position that names a *role* and a span, to be resolved to a
/// concrete Constituent at play time. It is structured improvisation made
/// explicit — the arrangement fixes "a solo happens here, for these eight bars"
/// while leaving *which* solo open.
///
/// M3 scope: this is the data model only. The resolution logic — matching a slot
/// against a pool of role-fillable Constituents and choosing one — is a later
/// subsystem (the white paper itself flags role-fillable phrases as a novel,
/// untested open question). A slot carries at most the id it was resolved to.
class RoleSlot
{
public:
    /// `conceptualOut` must not precede `conceptualIn`; throws
    /// std::invalid_argument otherwise. The span is in the parent's local time,
    /// the same convention as a Constituent's placement.
    RoleSlot (std::string role, Position conceptualIn, Position conceptualOut);

    const std::string& role()          const noexcept { return role_; }
    Position           conceptualIn()  const noexcept { return conceptualIn_; }
    Position           conceptualOut() const noexcept { return conceptualOut_; }

    /// The slot's span, in its parent's conceptual time (whole notes).
    Rational duration() const;

    /// The Constituent this slot was resolved to, if it has been filled.
    const std::optional<ConstituentId>& filledBy() const noexcept { return filledBy_; }
    bool isFilled() const noexcept { return filledBy_.has_value(); }

    // --- Copy-on-write edits, mirroring Constituent's editing model. ---

    RoleSlot withFilledBy (ConstituentId id) const;
    RoleSlot withoutFill() const;

private:
    std::string role_;
    Position conceptualIn_;
    Position conceptualOut_;
    std::optional<ConstituentId> filledBy_;
};

/// Arrangement primitives (white paper Part 11.3). These are not new engine
/// features — they are Constituent-tree placement operations applied at the
/// phrase level and above. Each is pure: it takes a parent and a set of
/// children and returns a new parent, leaving its arguments untouched.
namespace arrangement
{

/// Places `children` end-to-end inside `parent`: explicit order, each child's
/// `conceptualIn` set to the previous child's `conceptualOut`, every child's own
/// duration preserved. The run begins where `parent`'s existing children end
/// (or at the start of `parent`'s local time if it has none), so `sequence` can
/// be applied repeatedly to extend an arrangement. Returns a new Constituent
/// with `children` appended in order. Throws std::invalid_argument if any child
/// is null.
Constituent sequence (const Constituent& parent,
                      const std::vector<Constituent::ChildPtr>& children);

/// Places `children` simultaneously inside `parent`: every child's `conceptualIn`
/// set to the start of `parent`'s local time, each child's own duration
/// preserved, so the children overlap. Returns a new Constituent with `children`
/// appended. Throws std::invalid_argument if any child is null.
Constituent layer (const Constituent& parent,
                   const std::vector<Constituent::ChildPtr>& children);

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

} // namespace arrangement

} // namespace ida
