#include "sirius/MixerGraph.h"

#include <algorithm>

namespace sirius
{

MixerGraph::MixerGraph (std::initializer_list<MixerTerminal> terminals)
{
    nodes_.reserve (kMaxNodes);
    sends_.reserve (kMaxNodes);
    order_.reserve (static_cast<std::size_t> (kMaxNodes) + terminals.size());
    terminals_.reserve (terminals.size());
    for (const MixerTerminal kind : terminals)
        terminals_.push_back (TerminalNode { MixerNodeId { nextId_++ }, kind });
    recomputeOrder();
}

MixerGraph::MixerGraph (MixerTerminal terminal)
    : MixerGraph (std::initializer_list<MixerTerminal> { terminal })
{
}

bool MixerGraph::isTerminal (MixerNodeId id) const noexcept
{
    for (const auto& t : terminals_)
        if (t.id == id) return true;
    return false;
}

MixerNodeId MixerGraph::terminalNode (MixerTerminal kind) const noexcept
{
    for (const auto& t : terminals_)
        if (t.kind == kind) return t.id;
    return MixerNodeId {};
}

const MixerGraph::Node* MixerGraph::find (MixerNodeId id) const noexcept
{
    for (const auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

MixerGraph::Node* MixerGraph::find (MixerNodeId id) noexcept
{
    for (auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

int MixerGraph::nodeCount() const noexcept { return static_cast<int> (nodes_.size()); }

bool MixerGraph::contains (MixerNodeId node) const noexcept
{
    return isTerminal (node) || find (node) != nullptr;
}

MixerNodeKind MixerGraph::kindOf (MixerNodeId node) const noexcept
{
    if (isTerminal (node)) return MixerNodeKind::Terminal;
    if (const Node* n = find (node)) return n->kind;
    return MixerNodeKind::Terminal; // unknown ids are treated as the sink
}

MixerNodeId MixerGraph::addNode (MixerNodeKind kind)
{
    if (kind == MixerNodeKind::Terminal) return MixerNodeId {}; // implicit only
    if (static_cast<int> (nodes_.size()) >= kMaxNodes) return MixerNodeId {};

    const MixerNodeId id { nextId_++ };
    nodes_.push_back (Node { id, kind, terminals_.front().id }); // default: primary terminal
    recomputeOrder();
    return id;
}

void MixerGraph::removeNode (MixerNodeId node)
{
    if (isTerminal (node)) return;

    nodes_.erase (std::remove_if (nodes_.begin(), nodes_.end(),
                                  [node] (const Node& n) { return n.id == node; }),
                  nodes_.end());

    // Drop any send edges touching the removed node.
    sends_.erase (std::remove_if (sends_.begin(), sends_.end(),
                                  [node] (const SendEdge& e)
                                  { return e.source == node || e.fxReturn == node; }),
                  sends_.end());

    // Any node whose main-out pointed at the removed node falls back to the primary terminal.
    for (auto& n : nodes_)
        if (n.mainOut == node) n.mainOut = terminals_.front().id;

    recomputeOrder();
}

MixerNodeId MixerGraph::mainOutOf (MixerNodeId node) const noexcept
{
    if (const Node* n = find (node)) return n->mainOut;
    return MixerNodeId {};
}

bool MixerGraph::reaches (MixerNodeId from, MixerNodeId target) const noexcept
{
    // DFS over outgoing edges (main-out + sends). Bounded by node count; the
    // graph is small (<= kMaxNodes) and acyclic by construction, so the explicit
    // visited set guards only against the transient pre-validation state.
    std::vector<MixerNodeId> stack;
    std::vector<MixerNodeId> visited;
    stack.push_back (from);
    while (! stack.empty())
    {
        const MixerNodeId cur = stack.back();
        stack.pop_back();
        if (cur == target) return true;
        if (std::find (visited.begin(), visited.end(), cur) != visited.end()) continue;
        visited.push_back (cur);

        if (const Node* n = find (cur))
        {
            if (n->mainOut.isValid()) stack.push_back (n->mainOut);
            for (const auto& e : sends_)
                if (e.source == cur) stack.push_back (e.fxReturn);
        }
    }
    return false;
}

bool MixerGraph::wouldMainOutCycle (MixerNodeId node, MixerNodeId dest) const noexcept
{
    // Adding node -> dest closes a cycle iff dest can already reach node.
    return reaches (dest, node);
}

bool MixerGraph::setMainOut (MixerNodeId node, MixerNodeId dest)
{
    Node* n = find (node);
    if (n == nullptr) return false; // unknown / terminal cannot have a main-out

    const MixerNodeKind destKind = kindOf (dest);
    const bool destValid = isTerminal (dest) || (destKind == MixerNodeKind::Bus);
    if (! destValid) return false; // only a Bus or a Terminal is a valid destination

    if (wouldMainOutCycle (node, dest)) return false;

    n->mainOut = dest;
    recomputeOrder();
    return true;
}

float MixerGraph::sendLevel (MixerNodeId source, MixerNodeId fxReturn) const noexcept
{
    for (const auto& e : sends_)
        if (e.source == source && e.fxReturn == fxReturn) return e.level;
    return 0.0f;
}

bool MixerGraph::setSend (MixerNodeId source, MixerNodeId fxReturn, float level)
{
    if (source == fxReturn) return false;
    const Node* src = find (source);
    if (src == nullptr || src->kind == MixerNodeKind::FxReturn) return false; // v1: no FX-return sends
    if (kindOf (fxReturn) != MixerNodeKind::FxReturn) return false;

    // A send is an edge source -> fxReturn; reject if fxReturn already reaches source.
    if (reaches (fxReturn, source)) return false;

    const float clamped = std::clamp (level, 0.0f, 1.0f);

    auto it = std::find_if (sends_.begin(), sends_.end(),
                            [source, fxReturn] (const SendEdge& e)
                            { return e.source == source && e.fxReturn == fxReturn; });

    if (clamped <= 0.0f)
    {
        if (it != sends_.end()) { sends_.erase (it); recomputeOrder(); }
        return true;
    }

    if (it != sends_.end()) it->level = clamped;
    else                    sends_.push_back (SendEdge { source, fxReturn, clamped });
    recomputeOrder();
    return true;
}

void MixerGraph::recomputeOrder()
{
    // Kahn's algorithm. Nodes: all registered nodes + all terminal nodes.
    // Edges: each node's main-out (node -> mainOut) and each send (source -> fxReturn).
    //
    // Terminals are always last in the output regardless of their real in-degree
    // (an unreachable terminal has in-degree 0 but must still follow all non-terminal
    // nodes). We achieve this by seeding each terminal's in-degree to 1 and appending
    // them explicitly after Kahn has drained the non-terminal nodes.
    order_.clear();

    std::vector<MixerNodeId> ids;
    ids.reserve (nodes_.size() + terminals_.size());
    for (const auto& n : nodes_) ids.push_back (n.id);
    for (const auto& t : terminals_) ids.push_back (t.id);

    // in-degree per node id (parallel to ids); terminals start at 1 so they
    // never enter the queue during the normal Kahn pass.
    std::vector<int> indeg (ids.size(), 0);
    const std::size_t nonTerminalCount = nodes_.size();
    for (std::size_t i = nonTerminalCount; i < ids.size(); ++i)
        indeg[i] = 1;

    const auto indexOf = [&ids] (MixerNodeId id) -> int
    {
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id) return static_cast<int> (i);
        return -1;
    };

    const auto addEdge = [&] (MixerNodeId from, MixerNodeId to)
    {
        (void) from;
        const int ti = indexOf (to);
        if (ti >= 0) ++indeg[static_cast<std::size_t> (ti)];
    };

    for (const auto& n : nodes_)
        if (n.mainOut.isValid()) addEdge (n.id, n.mainOut);
    for (const auto& e : sends_)
        addEdge (e.source, e.fxReturn);

    std::vector<MixerNodeId> queue;
    for (std::size_t i = 0; i < nonTerminalCount; ++i)
        if (indeg[i] == 0) queue.push_back (ids[i]);

    while (! queue.empty())
    {
        const MixerNodeId cur = queue.front();
        queue.erase (queue.begin());
        order_.push_back (cur);

        // Decrement in-degree of every non-terminal node cur points at.
        const auto relax = [&] (MixerNodeId to)
        {
            const int ti = indexOf (to);
            // Only enqueue non-terminal nodes during the Kahn pass; terminals
            // are appended explicitly after.
            if (ti >= 0 && static_cast<std::size_t> (ti) < nonTerminalCount
                && --indeg[static_cast<std::size_t> (ti)] == 0)
                queue.push_back (to);
        };

        if (const Node* n = find (cur))
        {
            if (n->mainOut.isValid()) relax (n->mainOut);
            for (const auto& e : sends_)
                if (e.source == cur) relax (e.fxReturn);
        }
    }

    // Append all terminals in declaration order — they are always last.
    for (const auto& t : terminals_)
        order_.push_back (t.id);
}

} // namespace sirius
