#pragma once

#include "sirius/Arrangement.h"
#include "sirius/Constituent.h"
#include "sirius/ConstituentId.h"

#include <vector>

namespace sirius
{

/// Find every Constituent in `pool` that can fill `slot`. A candidate matches
/// when it carries PhraseMetadata, is marked role-fillable, and its role
/// string equals the slot's role.
///
/// Returns ids in `pool`'s enumeration order. This is deliberately a pure
/// enumeration: the white paper (Part 8.4) is explicit that *which* phrase
/// fills a role is the performer's moment-to-moment choice, not the engine's.
/// The engine surfaces the eligible set; the performer (or a higher-level
/// policy) picks.
///
/// Null entries in `pool` are skipped. An empty result means no candidate
/// matches — the caller decides whether to silence the slot, leave it
/// unfilled, or surface an error.
std::vector<ConstituentId>
findCandidatesFor (const RoleSlot& slot,
                   const std::vector<Constituent::ChildPtr>& pool);

/// Convenience: return `slot` filled by the first eligible candidate from
/// `pool`, or `slot` unchanged if none matches. Useful as the trivial
/// "pick the first one" policy in tests and as a sensible default before
/// performer-driven selection is wired up.
RoleSlot resolveFirst (const RoleSlot& slot,
                       const std::vector<Constituent::ChildPtr>& pool);

} // namespace sirius
