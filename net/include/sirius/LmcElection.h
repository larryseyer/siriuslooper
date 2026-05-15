#pragma once

#include "sirius/Rational.h"

#include <vector>

namespace sirius
{

/// The quality tiers of a clock-discipline source (white paper Part 12.3 —
/// "tier dominance: one GPS-disciplined node beats any number of NTP-
/// disciplined nodes"). Enumerated best-first so the numerically smallest
/// value is the highest tier.
enum class DisciplineTier
{
    Gps = 0,       ///< GPS-disciplined
    Ntp = 1,       ///< NTP-disciplined
    Quartz = 2,    ///< local quartz oscillator
    Audio = 3,     ///< Word/MTC slaved
    Estimated = 4  ///< software estimate of last resort
};

/// One participant's bid in an LMC election. The node advertises its current
/// LMC time as a *confidence interval* [intervalMin, intervalMax] (not a single
/// point) — exactly the input Marzullo's algorithm takes.
struct NodeClockEstimate
{
    int            nodeId;
    DisciplineTier tier;
    Rational       intervalMin;
    Rational       intervalMax;
    bool           isAnchor { false }; ///< user override: this node is master regardless of tier
};

/// The outcome of an election (white paper Part 12.3–12.4): which node is
/// LMC Master, the consensus time interval the ensemble agrees on, which
/// nodes belonged to the consensus, and which were excluded as falsetickers.
struct ElectionResult
{
    int               masterNodeId;
    Rational          consensusMin;        ///< intersection of consensus members' intervals
    Rational          consensusMax;
    std::vector<int>  consensusMembers;    ///< node ids in the agreed set
    std::vector<int>  falsetickers;        ///< node ids excluded — disagreed with consensus
    bool              anchorOverrideUsed { false };
};

/// Elects an LMC master from `nodes` using the white paper's protocol:
///
///   1. If exactly one node is marked `isAnchor`, it wins — the user's
///      designation of musical authority outranks every technical tier
///      (Part 12.4). The "consensus" reported is just that node's interval.
///   2. Otherwise, *tier dominance* discards every node below the best tier
///      present (Part 12.3) — one GPS-disciplined node beats any number of
///      NTP-disciplined nodes.
///   3. Marzullo's interval-intersection algorithm finds the largest set of
///      mutually-intersecting intervals within the dominant tier. Intervals
///      outside the consensus are falsetickers and are excluded.
///   4. Within the consensus, the node with the *narrowest* interval is the
///      master — that source has reported its time most confidently.
///
/// Throws std::invalid_argument if `nodes` is empty, if more than one node is
/// marked `isAnchor`, or if any node's interval is inverted (max < min).
ElectionResult electLmc (const std::vector<NodeClockEstimate>& nodes);

} // namespace sirius
