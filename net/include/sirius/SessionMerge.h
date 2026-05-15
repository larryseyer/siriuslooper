#pragma once

#include "sirius/Constituent.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sirius
{

/// One historical version of a Constituent, tagged with the wall-clock
/// timestamp of the edit. The version's `id` is the identity that persists
/// across edits (white paper Part 7.6); `state` is the immutable Constituent
/// it resolved to at that moment; `editTimestamp` is what last-writer-wins
/// uses to pick the active version when two divergent sessions touched the
/// same identity (Part 12.6).
struct ConstituentVersion
{
    ConstituentId                       id;
    std::shared_ptr<const Constituent>  state;
    std::int64_t                        editTimestamp;

    bool operator< (const ConstituentVersion& other) const noexcept
    {
        if (id.value() != other.id.value()) return id.value() < other.id.value();
        return editTimestamp < other.editTimestamp;
    }
};

/// A view of a session that supports CRDT-style merging (white paper Part
/// 12.6). The session carries the *full* history of every Constituent
/// identity, not just the active version, because two divergent copies might
/// each contain edits the other lacks — and an immutable-version graph
/// preserves them both. Tape data is stored as content hashes (the actual
/// bytes live in a TapeStore), so two sessions that captured the same take
/// share a hash and therefore merge as a single entry.
struct MergeableSession
{
    /// Content hashes of every tape this session references — the same
    /// strings TapeStore returns from `store()`. Union semantics.
    std::set<std::string> tapeHashes;

    /// Every Constituent version this session has ever produced. Deduplicated
    /// across (id, timestamp) — two copies of the same version coalesce.
    std::vector<ConstituentVersion> versions;
};

/// Merges two divergent sessions. The result is the union of their tape
/// hashes (content-addressing makes collision impossible) and the union of
/// their Constituent versions (immutability eliminates conflict). No human
/// arbitration; no rewritten tape bytes; the operation is deterministic,
/// commutative, associative, and idempotent.
MergeableSession merge (const MergeableSession& a, const MergeableSession& b);

/// The active Constituent for each identity after a merge: the version with
/// the latest `editTimestamp`. Ties broken by the higher Constituent address
/// — irrelevant for content, present only so the function is total.
std::map<std::int64_t, ConstituentVersion> activeVersions (const MergeableSession& session);

} // namespace sirius
