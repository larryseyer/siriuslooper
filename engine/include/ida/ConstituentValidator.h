#pragma once

#include "ida/Constituent.h"
#include "ida/ConstituentId.h"
#include "ida/ConstituentState.h"
#include "ida/INotificationSink.h"
#include "ida/TapeReference.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace std
{
/// ConstituentId is a thin wrapper over int64; hash by its value so it can key
/// the validation map. Lives here (not in core's ConstituentId.h) to keep this
/// the only file that depends on ConstituentId being hashable.
template <>
struct hash<ida::ConstituentId>
{
    std::size_t operator() (const ida::ConstituentId& id) const noexcept
    {
        return std::hash<std::int64_t> {} (id.value());
    }
};
} // namespace std

namespace sirius
{

/// "Does this tape reference resolve to real content?" Injected so the validator
/// has no data-layer dependency. M8 S7–S8 supplies a TapeStore-backed resolver;
/// until then production uses alwaysResolves and only structural Invalid is
/// detectable live.
using TapeResolver = std::function<bool (const TapeReference&)>;

/// The honest default resolver: with no TapeId->content manifest yet (M8 S7–S8),
/// the engine cannot know a segment is missing, so it assumes presence.
bool alwaysResolves (const TapeReference&) noexcept;

/// Immutable result of one validation pass. Stores only the non-Valid nodes;
/// unknown ids report Valid by default.
class ConstituentValidation
{
public:
    ConstituentValidation() = default;
    explicit ConstituentValidation (std::unordered_map<ConstituentId, ConstituentState> states);

    /// The state of `id`. Returns Valid for any id not recorded as non-Valid.
    ConstituentState state (ConstituentId id) const noexcept;

    /// Convenience: true iff `id` is Valid (the only renderable state).
    bool renderable (ConstituentId id) const noexcept;

private:
    std::unordered_map<ConstituentId, ConstituentState> states_;
};

/// Walk `root`, deriving each Constituent's state. Pure — touches no bus.
///   Invalid: span not contained in the parent's local frame (dominates Broken).
///   Broken : leaf loop whose tapeReference fails `resolver`.
ConstituentValidation validate (const Constituent& root, const TapeResolver& resolver);

/// Post a Warning (Category::StateRepair) on `sink` for every Broken/Invalid
/// node, depth-first so messages arrive in tree order. The load path calls this
/// after validate(); keeping it out of validate() leaves validate() unit-testable
/// without a sink.
void postConstituentStateNotifications (const Constituent&           root,
                                        const ConstituentValidation& validation,
                                        INotificationSink&           sink);

} // namespace sirius
