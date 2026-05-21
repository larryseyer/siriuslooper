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
