#include "sirius/MixerGraph.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>
#include <vector>

using sirius::MixerGraph;
using sirius::MixerNodeId;
using sirius::MixerNodeKind;
using sirius::MixerTerminal;

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
    MixerGraph g (MixerTerminal::Output);
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
    MixerGraph g (MixerTerminal::Output);
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
    MixerGraph g (MixerTerminal::Output);
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
    MixerGraph g (MixerTerminal::Output);
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
