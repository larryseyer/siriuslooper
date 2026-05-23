// Tests for the distributed LMC election (white paper Part 12.3–12.4). These
// pin down the three claims that make the protocol trustworthy: anchor-node
// override beats tier dominance, tier dominance beats numerical majority, and
// inside the dominant tier the narrowest interval wins as master after
// Marzullo discards the falsetickers.
#include "sirius/LmcElection.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <stdexcept>

using ida::DisciplineTier;
using ida::ElectionResult;
using ida::NodeClockEstimate;

namespace
{
    NodeClockEstimate node (int id, DisciplineTier tier,
                            ida::Rational lo, ida::Rational hi,
                            bool anchor = false)
    {
        return { id, tier, lo, hi, anchor };
    }

    bool contains (const std::vector<int>& v, int id)
    {
        return std::find (v.begin(), v.end(), id) != v.end();
    }
}

TEST_CASE ("electLmc rejects bad input", "[lmc-election]")
{
    CHECK_THROWS_AS (ida::electLmc ({}), std::invalid_argument);

    // Inverted interval — a "max before min" report is a bug at the source.
    CHECK_THROWS_AS (
        ida::electLmc ({ node (1, DisciplineTier::Ntp,
                                  ida::Rational (5), ida::Rational (3)) }),
        std::invalid_argument);

    // More than one anchor — musical authority is singular by design.
    CHECK_THROWS_AS (ida::electLmc ({
        node (1, DisciplineTier::Ntp, ida::Rational (0), ida::Rational (1), true),
        node (2, DisciplineTier::Ntp, ida::Rational (0), ida::Rational (1), true)
    }), std::invalid_argument);
}

TEST_CASE ("the anchor node wins regardless of tier", "[lmc-election]")
{
    // White paper 12.4: "Musical authority outranks technical authority." A
    // user-designated anchor beats every GPS source in the room.
    const auto result = ida::electLmc ({
        node (1, DisciplineTier::Gps,       ida::Rational (10), ida::Rational (11)),
        node (2, DisciplineTier::Gps,       ida::Rational (10), ida::Rational (11)),
        node (3, DisciplineTier::Estimated, ida::Rational (8),  ida::Rational (12), true)
    });
    CHECK (result.anchorOverrideUsed);
    CHECK (result.masterNodeId == 3);
    CHECK (result.consensusMembers.size() == 1);
}

TEST_CASE ("tier dominance discards every lower-tier node", "[lmc-election]")
{
    // White paper 12.3: "one GPS-disciplined node beats any number of NTP-
    // disciplined nodes." Numerical majority is not the criterion.
    const auto result = ida::electLmc ({
        node (10, DisciplineTier::Gps,    ida::Rational (100), ida::Rational (101)),
        node (20, DisciplineTier::Ntp,    ida::Rational (200), ida::Rational (210)),
        node (21, DisciplineTier::Ntp,    ida::Rational (200), ida::Rational (210)),
        node (22, DisciplineTier::Ntp,    ida::Rational (200), ida::Rational (210)),
        node (30, DisciplineTier::Quartz, ida::Rational (300), ida::Rational (350))
    });
    CHECK_FALSE (result.anchorOverrideUsed);
    CHECK (result.masterNodeId == 10);
    CHECK (result.consensusMembers.size() == 1);
    CHECK (contains (result.falsetickers, 20));
    CHECK (contains (result.falsetickers, 30));
}

TEST_CASE ("Marzullo discards falsetickers and picks the narrowest interval",
           "[lmc-election]")
{
    // Four NTP nodes; three agree closely, one is way off. Marzullo finds the
    // tight consensus among the three and discards the outlier as a
    // falseticker. Within the consensus, the narrowest interval wins as
    // master.
    const auto result = ida::electLmc ({
        node (1, DisciplineTier::Ntp, ida::Rational (100), ida::Rational (110)),
        node (2, DisciplineTier::Ntp, ida::Rational (102), ida::Rational (108)),
        node (3, DisciplineTier::Ntp, ida::Rational (104), ida::Rational (106)),
        node (4, DisciplineTier::Ntp, ida::Rational (200), ida::Rational (210))
    });
    CHECK (result.consensusMembers.size() == 3);
    CHECK (contains (result.falsetickers, 4));
    CHECK (result.masterNodeId == 3); // narrowest interval [104,106]
    CHECK (result.consensusMin == ida::Rational (104));
    CHECK (result.consensusMax == ida::Rational (106));
}

TEST_CASE ("a single-node ensemble is its own consensus", "[lmc-election]")
{
    const auto result = ida::electLmc ({
        node (7, DisciplineTier::Quartz, ida::Rational (0), ida::Rational (1))
    });
    CHECK (result.masterNodeId == 7);
    CHECK (result.consensusMembers == std::vector<int>{ 7 });
    CHECK (result.falsetickers.empty());
}
