#include "sirius/LmcElection.h"

#include <algorithm>
#include <stdexcept>

namespace sirius
{

namespace
{
    void validate (const std::vector<NodeClockEstimate>& nodes)
    {
        if (nodes.empty())
            throw std::invalid_argument ("ida::electLmc: at least one node is required");

        int anchorCount = 0;
        for (const auto& n : nodes)
        {
            if (n.intervalMax < n.intervalMin)
                throw std::invalid_argument ("ida::electLmc: inverted interval");
            if (n.isAnchor) ++anchorCount;
        }
        if (anchorCount > 1)
            throw std::invalid_argument ("ida::electLmc: at most one anchor node is allowed");
    }

    ElectionResult anchorElection (const std::vector<NodeClockEstimate>& nodes)
    {
        for (const auto& n : nodes)
            if (n.isAnchor)
                return { n.nodeId, n.intervalMin, n.intervalMax, { n.nodeId }, {}, true };
        // Unreachable: validate() guarantees an anchor exists when called.
        return {};
    }

    DisciplineTier bestTier (const std::vector<NodeClockEstimate>& nodes)
    {
        DisciplineTier best = nodes.front().tier;
        for (const auto& n : nodes)
            if (n.tier < best) best = n.tier;
        return best;
    }

    /// Marzullo's interval-intersection sweep: find the maximum-overlap window
    /// across the given intervals. The result is the [start, end] of that
    /// window — the consensus interval. Among consensus members (those whose
    /// own interval overlaps the window), the result is exactly the
    /// intersection.
    struct Endpoint { Rational time; int delta; }; // +1 at start, -1 just after end

    void marzulloSweep (const std::vector<NodeClockEstimate>& nodes,
                        Rational& outMin, Rational& outMax)
    {
        std::vector<Endpoint> endpoints;
        endpoints.reserve (nodes.size() * 2);
        for (const auto& n : nodes)
        {
            endpoints.push_back ({ n.intervalMin, +1 });
            endpoints.push_back ({ n.intervalMax, -1 });
        }
        // Sort by time; on ties, opens (+1) come before closes (-1) so
        // intervals that merely touch are counted as overlapping.
        std::sort (endpoints.begin(), endpoints.end(),
                   [] (const Endpoint& a, const Endpoint& b)
                   {
                       if (a.time < b.time) return true;
                       if (b.time < a.time) return false;
                       return a.delta > b.delta;
                   });

        int running = 0;
        int bestDepth = 0;
        Rational bestStart = endpoints.front().time;
        Rational bestEnd   = endpoints.front().time;

        // Track the start of the current run at the running maximum, and the
        // end the moment the run breaks (a -1 event after the peak).
        Rational runStart = endpoints.front().time;
        for (std::size_t i = 0; i < endpoints.size(); ++i)
        {
            const auto& e = endpoints[i];
            if (e.delta == +1)
            {
                ++running;
                if (running > bestDepth)
                {
                    bestDepth = running;
                    runStart  = e.time;
                }
            }
            else
            {
                if (running == bestDepth)
                {
                    bestStart = runStart;
                    bestEnd   = e.time;
                }
                --running;
            }
        }

        outMin = bestStart;
        outMax = bestEnd;
    }

    bool overlapsConsensus (const NodeClockEstimate& n, Rational cMin, Rational cMax)
    {
        return n.intervalMin <= cMax && n.intervalMax >= cMin;
    }
}

ElectionResult electLmc (const std::vector<NodeClockEstimate>& nodes)
{
    validate (nodes);

    // (1) Anchor override — musical authority outranks tier.
    for (const auto& n : nodes)
        if (n.isAnchor)
            return anchorElection (nodes);

    // (2) Tier dominance: keep only the best tier present.
    const DisciplineTier best = bestTier (nodes);
    std::vector<NodeClockEstimate> dominant;
    for (const auto& n : nodes)
        if (n.tier == best) dominant.push_back (n);

    // (3) Marzullo: find the maximum-overlap window across the dominant tier.
    Rational consensusMin {}, consensusMax {};
    marzulloSweep (dominant, consensusMin, consensusMax);

    // (4) Partition into consensus members vs falsetickers; the master is the
    // consensus member with the narrowest interval.
    ElectionResult out;
    out.consensusMin = consensusMin;
    out.consensusMax = consensusMax;

    const NodeClockEstimate* master = nullptr;
    Rational narrowest;
    bool haveNarrowest = false;

    for (const auto& n : nodes)
    {
        if (n.tier != best)
        {
            out.falsetickers.push_back (n.nodeId);
            continue;
        }
        if (! overlapsConsensus (n, consensusMin, consensusMax))
        {
            out.falsetickers.push_back (n.nodeId);
            continue;
        }
        out.consensusMembers.push_back (n.nodeId);
        const Rational width = n.intervalMax - n.intervalMin;
        if (! haveNarrowest || width < narrowest)
        {
            narrowest = width;
            haveNarrowest = true;
            master = &n;
        }
    }

    out.masterNodeId = master != nullptr ? master->nodeId : nodes.front().nodeId;
    return out;
}

} // namespace sirius
