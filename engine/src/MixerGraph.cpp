#include "sirius/MixerGraph.h"

#include <algorithm>

namespace sirius
{

MixerGraph::MixerGraph (MixerTerminal terminal)
    : terminal_ (terminal)
{
    nodes_.reserve (kMaxNodes);
    sends_.reserve (kMaxNodes);
    order_.reserve (kMaxNodes + 1);
    terminalId_ = MixerNodeId { nextId_++ }; // the implicit terminal owns id 1
    recomputeOrder();
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
    return node == terminalId_ || find (node) != nullptr;
}

MixerNodeKind MixerGraph::kindOf (MixerNodeId node) const noexcept
{
    if (node == terminalId_) return MixerNodeKind::Terminal;
    if (const Node* n = find (node)) return n->kind;
    return MixerNodeKind::Terminal; // unknown ids are treated as the sink
}

MixerNodeId MixerGraph::addNode (MixerNodeKind kind)
{
    if (kind == MixerNodeKind::Terminal) return MixerNodeId {}; // implicit only
    if (static_cast<int> (nodes_.size()) >= kMaxNodes) return MixerNodeId {};

    const MixerNodeId id { nextId_++ };
    nodes_.push_back (Node { id, kind, terminalId_ }); // main-out defaults to terminal
    recomputeOrder();
    return id;
}

void MixerGraph::removeNode (MixerNodeId node)
{
    if (node == terminalId_) return;

    nodes_.erase (std::remove_if (nodes_.begin(), nodes_.end(),
                                  [node] (const Node& n) { return n.id == node; }),
                  nodes_.end());

    // Drop any send edges touching the removed node.
    sends_.erase (std::remove_if (sends_.begin(), sends_.end(),
                                  [node] (const SendEdge& e)
                                  { return e.source == node || e.fxReturn == node; }),
                  sends_.end());

    // Any node whose main-out pointed at the removed node falls back to terminal.
    for (auto& n : nodes_)
        if (n.mainOut == node) n.mainOut = terminalId_;

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
    const bool destValid = (dest == terminalId_) || (destKind == MixerNodeKind::Bus);
    if (! destValid) return false; // only a Bus or the Terminal is a valid destination

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
    // Kahn's algorithm. Nodes: all registered nodes + the implicit terminal.
    // Edges: each node's main-out (node -> mainOut) and each send (source -> fxReturn).
    order_.clear();

    std::vector<MixerNodeId> ids;
    ids.reserve (nodes_.size() + 1);
    for (const auto& n : nodes_) ids.push_back (n.id);
    ids.push_back (terminalId_);

    // in-degree per node id (parallel to ids)
    std::vector<int> indeg (ids.size(), 0);
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
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (indeg[i] == 0) queue.push_back (ids[i]);

    while (! queue.empty())
    {
        const MixerNodeId cur = queue.front();
        queue.erase (queue.begin());
        order_.push_back (cur);

        // Decrement in-degree of every node cur points at.
        const auto relax = [&] (MixerNodeId to)
        {
            const int ti = indexOf (to);
            if (ti >= 0 && --indeg[static_cast<std::size_t> (ti)] == 0)
                queue.push_back (to);
        };

        if (const Node* n = find (cur))
        {
            if (n->mainOut.isValid()) relax (n->mainOut);
            for (const auto& e : sends_)
                if (e.source == cur) relax (e.fxReturn);
        }
    }
    // The graph is acyclic by construction (setMainOut/setSend reject cycles), so
    // order_ contains every id. The terminal, having no outgoing edge, sorts last.
}

} // namespace sirius
