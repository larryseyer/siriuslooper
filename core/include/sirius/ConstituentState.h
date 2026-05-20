#pragma once

namespace sirius
{

/// The validity of a Constituent, derived from the graph at load and on edits
/// (white paper §17.7). It is NEVER persisted and is NOT a field on Constituent
/// — Constituents are immutable/structural; state is recomputed by a validation
/// pass (sirius::validate). Broken and Invalid both render as silence; identity,
/// metadata, and content survive so the performer can repair, not recreate.
enum class ConstituentState
{
    Valid,   ///< references resolve and placement is contained in the parent
    Broken,  ///< a leaf loop whose tape reference does not resolve to content
    Invalid  ///< anchor/bounds place the Constituent outside its parent's span
};

} // namespace sirius
