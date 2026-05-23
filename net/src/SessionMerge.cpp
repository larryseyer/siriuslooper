#include "ida/SessionMerge.h"

#include <algorithm>

namespace ida
{

namespace
{
    /// Two ConstituentVersions coalesce as the same entry when they share an
    /// identity and a timestamp — the assumption is that the same edit on the
    /// same identity at the same wall-clock moment is the same edit, no
    /// matter which node observed it. (Practically, the writer tags every
    /// version it produces with a unique monotonic timestamp.)
    bool sameVersion (const ConstituentVersion& a, const ConstituentVersion& b)
    {
        return a.id.value() == b.id.value() && a.editTimestamp == b.editTimestamp;
    }
}

MergeableSession merge (const MergeableSession& a, const MergeableSession& b)
{
    MergeableSession out;
    out.tapeHashes = a.tapeHashes;
    out.tapeHashes.insert (b.tapeHashes.begin(), b.tapeHashes.end());

    out.versions.reserve (a.versions.size() + b.versions.size());
    out.versions = a.versions;
    for (const auto& v : b.versions)
    {
        const bool already = std::any_of (out.versions.begin(), out.versions.end(),
            [&v] (const ConstituentVersion& existing) { return sameVersion (existing, v); });
        if (! already) out.versions.push_back (v);
    }

    // Canonical order keeps merge deterministic — important for the
    // commutativity property and for any equality check downstream.
    std::sort (out.versions.begin(), out.versions.end());
    return out;
}

std::map<std::int64_t, ConstituentVersion> activeVersions (const MergeableSession& session)
{
    std::map<std::int64_t, ConstituentVersion> active;
    for (const auto& v : session.versions)
    {
        const auto key = v.id.value();
        auto it = active.find (key);
        if (it == active.end())
        {
            active.insert ({ key, v });
        }
        else if (v.editTimestamp > it->second.editTimestamp)
        {
            // Last-writer-wins by timestamp. The version graph keeps the
            // loser; the active map just forgets it.
            it->second = v;
        }
    }
    return active;
}

} // namespace ida
