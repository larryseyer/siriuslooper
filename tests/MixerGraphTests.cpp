// The float == comparisons below assert EXACT stored send levels (clamp
// boundaries 0/0.5/1.0 and edge removal), not arithmetic results, so exact
// equality is the correct check; silence the project's -Wfloat-equal here.
#if defined(__clang__)
 #pragma clang diagnostic ignored "-Wfloat-equal"
#elif defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include "sirius/MixerGraph.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using sirius::MixerGraph;
using sirius::MixerNodeId;
using sirius::MixerNodeKind;
using sirius::MixerTerminal;

// Compile-time invariant — evaluationOrder() is the only audio-thread read
// surface and MUST be noexcept (RT-safety contract §6).
static_assert (noexcept (std::declval<const MixerGraph&>().evaluationOrder()),
               "MixerGraph::evaluationOrder must be noexcept (RT-safety contract §6)");

TEST_CASE ("MixerNodeId validity + equality", "[mixer-graph][node-registry]")
{
    CHECK_FALSE (MixerNodeId {}.isValid());
    CHECK (MixerNodeId { 1 }.isValid());
    CHECK (MixerNodeId { 3 } == MixerNodeId { 3 });
    CHECK (MixerNodeId { 3 } != MixerNodeId { 4 });
}

TEST_CASE ("MixerGraph constructs with an implicit terminal and no nodes",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::Tape);
    CHECK (g.terminal() == MixerTerminal::Tape);
    CHECK (g.terminalNode().isValid());
    CHECK (g.kindOf (g.terminalNode()) == MixerNodeKind::Terminal);
    CHECK (g.nodeCount() == 0);
    CHECK (g.contains (g.terminalNode()));
}

TEST_CASE ("MixerGraph::addNode registers distinct nodes; Terminal is rejected",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto bus = g.addNode (MixerNodeKind::Bus);
    const auto fx  = g.addNode (MixerNodeKind::FxReturn);

    CHECK (ch.isValid());
    CHECK (bus.isValid());
    CHECK (fx.isValid());
    CHECK (ch != bus);
    CHECK (bus != fx);
    CHECK (g.kindOf (ch)  == MixerNodeKind::Channel);
    CHECK (g.kindOf (bus) == MixerNodeKind::Bus);
    CHECK (g.kindOf (fx)  == MixerNodeKind::FxReturn);
    CHECK (g.nodeCount() == 3);

    const auto rejected = g.addNode (MixerNodeKind::Terminal);
    CHECK_FALSE (rejected.isValid());
    CHECK (g.nodeCount() == 3);
}

TEST_CASE ("MixerGraph::removeNode drops the node, its main-out, and its sends",
           "[mixer-graph][node-registry]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto fx  = g.addNode (MixerNodeKind::FxReturn);
    CHECK (g.setSend (ch, fx, 0.5f));

    g.removeNode (fx);
    CHECK_FALSE (g.contains (fx));
    CHECK (g.nodeCount() == 1);
    CHECK (g.sendLevel (ch, fx) == 0.0f); // edge gone with the node
    CHECK (g.mainOutOf (ch) == g.terminalNode()); // unaffected
}

TEST_CASE ("MixerGraph main-out defaults to terminal and validates destination kind",
           "[mixer-graph][main-out]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto fx   = g.addNode (MixerNodeKind::FxReturn);

    SECTION ("new node main-out defaults to terminal")
    {
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("main-out to a Bus succeeds")
    {
        CHECK (g.setMainOut (ch, busA));
        CHECK (g.mainOutOf (ch) == busA);
    }
    SECTION ("main-out to a Channel is rejected and leaves main-out unchanged")
    {
        const auto ch2 = g.addNode (MixerNodeKind::Channel);
        CHECK_FALSE (g.setMainOut (ch, ch2));
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("main-out to an FxReturn is rejected (FX returns are send-fed)")
    {
        CHECK_FALSE (g.setMainOut (ch, fx));
        CHECK (g.mainOutOf (ch) == g.terminalNode());
    }
    SECTION ("the terminal cannot be given a main-out")
    {
        CHECK_FALSE (g.setMainOut (g.terminalNode(), busA));
    }
}

TEST_CASE ("MixerGraph rejects main-out assignments that would create a cycle",
           "[mixer-graph][cycle]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);

    SECTION ("a valid subgroup A->B succeeds")
    {
        CHECK (g.setMainOut (busA, busB));
        CHECK (g.mainOutOf (busA) == busB);
    }
    SECTION ("closing the loop B->A is rejected and leaves B unchanged")
    {
        REQUIRE (g.setMainOut (busA, busB));
        CHECK (g.wouldMainOutCycle (busB, busA));
        CHECK_FALSE (g.setMainOut (busB, busA));
        CHECK (g.mainOutOf (busB) == g.terminalNode());
    }
}

TEST_CASE ("MixerGraph sends target FX returns only, clamp, and reject cycles",
           "[mixer-graph][sends]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto fxA  = g.addNode (MixerNodeKind::FxReturn);
    const auto fxB  = g.addNode (MixerNodeKind::FxReturn);

    SECTION ("channel -> FX return succeeds and stores the level")
    {
        CHECK (g.setSend (ch, fxA, 0.5f));
        CHECK (g.sendLevel (ch, fxA) == 0.5f);
    }
    SECTION ("levels clamp to [0,1]")
    {
        CHECK (g.setSend (ch, fxA, 2.0f));
        CHECK (g.sendLevel (ch, fxA) == 1.0f);
        CHECK (g.setSend (ch, fxA, -1.0f)); // <=0 removes the edge
        CHECK (g.sendLevel (ch, fxA) == 0.0f);
    }
    SECTION ("send to a Bus is rejected (not an FX return)")
    {
        CHECK_FALSE (g.setSend (ch, busA, 0.5f));
    }
    SECTION ("FX-return-sourced sends are rejected (v1: no FX-return sends)")
    {
        CHECK_FALSE (g.setSend (fxA, fxB, 0.5f));
    }
    SECTION ("a send that would close a cycle is rejected")
    {
        // fxA -> busA via main-out, then busA -> fxA via send would loop.
        REQUIRE (g.setMainOut (fxA, busA));
        CHECK_FALSE (g.setSend (busA, fxA, 0.5f));
    }
}

namespace
{
    int posOf (const std::vector<MixerNodeId>& order, MixerNodeId id)
    {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == id) return static_cast<int> (i);
        return -1;
    }
}

TEST_CASE ("MixerGraph evaluation order: sources before destinations, terminal last",
           "[mixer-graph][evaluation-order]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);

    SECTION ("default graph — all channels before the terminal")
    {
        const auto c1 = g.addNode (MixerNodeKind::Channel);
        const auto c2 = g.addNode (MixerNodeKind::Channel);
        const auto c3 = g.addNode (MixerNodeKind::Channel);
        const auto& order = g.evaluationOrder();
        const int t = posOf (order, g.terminalNode());
        REQUIRE (t >= 0);
        CHECK (posOf (order, c1) < t);
        CHECK (posOf (order, c2) < t);
        CHECK (posOf (order, c3) < t);
        CHECK (t == static_cast<int> (order.size()) - 1); // terminal last
    }

    SECTION ("subgroup chain — chan before busA before busB before terminal")
    {
        const auto ch   = g.addNode (MixerNodeKind::Channel);
        const auto busA = g.addNode (MixerNodeKind::Bus);
        const auto busB = g.addNode (MixerNodeKind::Bus);
        REQUIRE (g.setMainOut (ch,   busA));
        REQUIRE (g.setMainOut (busA, busB));
        REQUIRE (g.setMainOut (busB, g.terminalNode()));
        const auto& order = g.evaluationOrder();
        CHECK (posOf (order, ch)   < posOf (order, busA));
        CHECK (posOf (order, busA) < posOf (order, busB));
        CHECK (posOf (order, busB) < posOf (order, g.terminalNode()));
    }

    SECTION ("send fan-in — both sources before the FX return before the terminal")
    {
        const auto ch   = g.addNode (MixerNodeKind::Channel);
        const auto busA = g.addNode (MixerNodeKind::Bus);
        const auto fx   = g.addNode (MixerNodeKind::FxReturn);
        REQUIRE (g.setSend (ch,   fx, 0.4f));
        REQUIRE (g.setSend (busA, fx, 0.6f));
        const auto& order = g.evaluationOrder();
        CHECK (posOf (order, ch)   < posOf (order, fx));
        CHECK (posOf (order, busA) < posOf (order, fx));
        CHECK (posOf (order, fx)   < posOf (order, g.terminalNode()));
    }
}

TEST_CASE ("MixerGraph supports multiple typed terminals",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });

    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);

    CHECK (tape.isValid());
    CHECK (hw.isValid());
    CHECK (tape != hw);
    CHECK (g.kindOf (tape) == MixerNodeKind::Terminal);
    CHECK (g.kindOf (hw)   == MixerNodeKind::Terminal);
    CHECK (g.contains (tape));
    CHECK (g.contains (hw));

    // Primary (no-arg) accessors return the first-listed terminal.
    CHECK (g.terminalNode() == tape);
    CHECK (g.terminal()     == MixerTerminal::Tape);

    // A node defaults its main-out to the primary terminal (tape = capture).
    const auto ch = g.addNode (MixerNodeKind::Channel);
    CHECK (g.mainOutOf (ch) == tape);

    // An absent terminal kind resolves to invalid on a single-terminal graph.
    MixerGraph single (MixerTerminal::HardwareOutput);
    CHECK_FALSE (single.terminalNode (MixerTerminal::Tape).isValid());
    CHECK (single.terminalNode (MixerTerminal::HardwareOutput) == single.terminalNode());
}

TEST_CASE ("MixerGraph routes distinct nodes to distinct terminals",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);

    REQUIRE (g.setMainOut (busA, tape));
    REQUIRE (g.setMainOut (busB, hw));
    CHECK (g.mainOutOf (busA) == tape);
    CHECK (g.mainOutOf (busB) == hw);
}

TEST_CASE ("MixerGraph removeNode falls a dangling main-out back to the primary terminal",
           "[mixer-graph][multi-terminal]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    const auto busB = g.addNode (MixerNodeKind::Bus);
    REQUIRE (g.setMainOut (busA, busB)); // busA -> busB

    g.removeNode (busB);
    CHECK (g.mainOutOf (busA) == tape); // fell back to the primary terminal
}

TEST_CASE ("MixerGraph evaluation order: every node precedes all terminals",
           "[mixer-graph][multi-terminal][evaluation-order]")
{
    MixerGraph g ({ MixerTerminal::Tape, MixerTerminal::HardwareOutput });
    const auto tape = g.terminalNode (MixerTerminal::Tape);
    const auto hw   = g.terminalNode (MixerTerminal::HardwareOutput);
    const auto ch   = g.addNode (MixerNodeKind::Channel);
    const auto busA = g.addNode (MixerNodeKind::Bus);
    REQUIRE (g.setMainOut (ch,   busA));
    REQUIRE (g.setMainOut (busA, hw)); // busA -> hardware output

    const auto& order = g.evaluationOrder();
    REQUIRE (order.size() == 4); // ch, busA, tape, hw

    const int chPos = posOf (order, ch);
    const int buPos = posOf (order, busA);
    CHECK (chPos < buPos);
    CHECK (buPos < posOf (order, tape));
    CHECK (buPos < posOf (order, hw));
    const int lastNonTerminal = (chPos > buPos) ? chPos : buPos;
    CHECK (posOf (order, tape) > lastNonTerminal);
    CHECK (posOf (order, hw)   > lastNonTerminal);
}

TEST_CASE ("MixerGraph exposes its send edges for audio-thread traversal",
           "[mixer-graph][sends]")
{
    MixerGraph g (MixerTerminal::HardwareOutput);
    const auto ch  = g.addNode (MixerNodeKind::Channel);
    const auto fxA = g.addNode (MixerNodeKind::FxReturn);
    const auto fxB = g.addNode (MixerNodeKind::FxReturn);
    REQUIRE (g.setSend (ch, fxA, 0.5f));
    REQUIRE (g.setSend (ch, fxB, 0.25f));

    const auto& edges = g.sendEdges();
    REQUIRE (edges.size() == 2);

    float toA = 0.0f, toB = 0.0f;
    for (const auto& e : edges)
    {
        CHECK (e.source == ch);
        if (e.fxReturn == fxA) toA = e.level;
        if (e.fxReturn == fxB) toB = e.level;
    }
    CHECK (toA == 0.5f);
    CHECK (toB == 0.25f);
}

static_assert (noexcept (std::declval<const MixerGraph&>().sendEdges()),
               "MixerGraph::sendEdges must be noexcept (audio-thread read)");

TEST_CASE ("MixerGraph::addTerminal mints a new terminal node usable as a main-out dest",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);          // primary tape terminal
    const auto primaryTape = g.terminalNode();
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);

    CHECK (tape2.isValid());
    CHECK (tape2 != primaryTape);
    CHECK (g.kindOf (tape2) == MixerNodeKind::Terminal);
    CHECK (g.contains (tape2));

    const auto ch = g.addNode (MixerNodeKind::Channel);
    CHECK (g.mainOutOf (ch) == primaryTape);     // default still the primary
    REQUIRE (g.setMainOut (ch, tape2));          // a second tape is a valid dest
    CHECK (g.mainOutOf (ch) == tape2);
}

TEST_CASE ("MixerGraph evaluationOrder keeps ALL terminals last with >1 tape terminal",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);
    const auto chA = g.addNode (MixerNodeKind::Channel);
    const auto chB = g.addNode (MixerNodeKind::Channel);
    REQUIRE (g.setMainOut (chB, tape2));

    const auto& order = g.evaluationOrder();
    const auto idx = [&order] (MixerNodeId id) {
        for (std::size_t i = 0; i < order.size(); ++i) if (order[i] == id) return (int) i;
        return -1;
    };
    REQUIRE (idx (g.terminalNode()) >= 0);
    REQUIRE (idx (tape2) >= 0);
    CHECK (idx (chA) < idx (g.terminalNode()));
    CHECK (idx (chA) < idx (tape2));
    CHECK (idx (chB) < idx (tape2));
}

TEST_CASE ("MixerGraph::removeTerminal reassigns orphaned main-outs to the primary and refuses the primary",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);
    const auto primaryTape = g.terminalNode();
    const auto tape2 = g.addTerminal (MixerTerminal::Tape);
    const auto ch = g.addNode (MixerNodeKind::Channel);
    REQUIRE (g.setMainOut (ch, tape2));

    SECTION ("removing a non-primary terminal succeeds and orphans fall back to primary")
    {
        CHECK (g.removeTerminal (tape2));
        CHECK_FALSE (g.contains (tape2));
        CHECK (g.mainOutOf (ch) == primaryTape);
    }
    SECTION ("removing the primary terminal is refused")
    {
        CHECK_FALSE (g.removeTerminal (primaryTape));
        CHECK (g.contains (primaryTape));
    }
    SECTION ("removing an unknown / non-terminal id is refused")
    {
        CHECK_FALSE (g.removeTerminal (ch));            // a registered node, not a terminal
        CHECK_FALSE (g.removeTerminal (MixerNodeId {})); // invalid
    }
}

TEST_CASE ("MixerGraph::addTerminal returns an invalid id at the node ceiling",
           "[mixer-graph][terminal]")
{
    MixerGraph g (MixerTerminal::Tape);
    // Fill to the ceiling: the ctor seeded 1 terminal, so kMaxNodes-1 more fit.
    for (int i = 0; i < MixerGraph::kMaxNodes - 1; ++i)
        REQUIRE (g.addTerminal (MixerTerminal::Tape).isValid());
    CHECK_FALSE (g.addTerminal (MixerTerminal::Tape).isValid()); // ceiling hit
}
