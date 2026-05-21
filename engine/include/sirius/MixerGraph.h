#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace sirius
{

/// Graph-layer node kind (distinct from BusConfig::BusKind, which is the DSP
/// registry's view). A Channel is a source (input strip or phrase strip); Bus
/// and FxReturn are summing nodes; Terminal is an implicit sink node (tape on
/// the input side, hardware output on the output side; multi-terminal graphs
/// have one Terminal node per kind).
enum class MixerNodeKind { Channel, Bus, FxReturn, Terminal };

/// A terminal kind. The primary terminal is fixed at construction; additional
/// terminals (e.g. one Tape terminal per pooled tape on the input side) may be
/// added and removed dynamically via addTerminal/removeTerminal.
enum class MixerTerminal { Tape, HardwareOutput };

/// Strong id for routing-graph nodes. 0 == invalid. Distinct from BusId /
/// OutputChannelId: the graph is a topology layer above the DSP registries.
class MixerNodeId
{
public:
    constexpr MixerNodeId() noexcept = default;
    explicit constexpr MixerNodeId (std::int64_t v) noexcept : value_ (v) {}
    std::int64_t value() const noexcept { return value_; }
    bool isValid() const noexcept { return value_ != 0; }
    bool operator== (const MixerNodeId& o) const noexcept { return value_ == o.value_; }
    bool operator!= (const MixerNodeId& o) const noexcept { return value_ != o.value_; }
private:
    std::int64_t value_ { 0 };
};

/// Pure routing-topology model shared by both mixers. Owns the node registry,
/// each node's single main-out destination, the send edges, acyclic enforcement,
/// and a topologically-sorted evaluation order recomputed on every successful
/// mutation.
///
/// Threading: ALL mutators are message-thread only. The owning mixer brackets
/// mutations with removeAudioCallback/addAudioCallback (the rebuildInputStrips
/// pattern), so the audio thread never reads evaluationOrder() mid-mutation —
/// no atomic snapshot needed here. evaluationOrder() is the only audio-thread
/// read surface and is const noexcept.
class MixerGraph
{
public:
    /// Generous ceiling: 32 channels + 64 buses + headroom. Pre-reserved so the
    /// node/edge/order vectors never reallocate during normal use.
    static constexpr int kMaxNodes = 256;

    /// Single-terminal graph (Output Mixer): one implicit terminal of the given
    /// kind. Delegates to the list constructor.
    explicit MixerGraph (MixerTerminal terminal);

    /// Multi-terminal graph (Input Mixer): one implicit Terminal node per kind,
    /// in order. terminals[0] is the PRIMARY — the zero-config default main-out
    /// destination and the removeNode fallback. Precondition: non-empty.
    explicit MixerGraph (std::initializer_list<MixerTerminal> terminals);

    MixerTerminal terminal()     const noexcept { return terminals_.front().kind; }
    MixerNodeId   terminalNode() const noexcept { return terminals_.front().id; }

    /// The Terminal node id for the FIRST terminal of the given kind (the
    /// primary, if multiple terminals of that kind exist), or an invalid id if
    /// this graph has no terminal of that kind. Callers adding secondary
    /// terminals via addTerminal() must retain the returned MixerNodeId — this
    /// method does not distinguish same-kind terminals.
    MixerNodeId   terminalNode (MixerTerminal kind) const noexcept;

    /// Registers a Channel / Bus / FxReturn node. Its main-out defaults to the
    /// terminal (zero-config). Passing Terminal returns an invalid id (the
    /// terminal is implicit and created at construction).
    MixerNodeId addNode (MixerNodeKind kind);
    void        removeNode (MixerNodeId node);
    bool        contains (MixerNodeId node) const noexcept;
    MixerNodeKind kindOf (MixerNodeId node) const noexcept; // Terminal for the terminal/unknown
    int         nodeCount() const noexcept; // excludes the implicit terminal

    /// Mints an additional terminal node of the given kind and returns its id.
    /// Terminals are sinks: no main-out, always last in evaluationOrder(). Used
    /// by the input mixer to add a Tape terminal per pooled tape. Recomputes the
    /// evaluation order. Message-thread only.
    MixerNodeId addTerminal (MixerTerminal kind);

    /// Removes a terminal node. Returns false (no change) if the id is unknown,
    /// is not a terminal, or is the PRIMARY terminal (terminals_.front() — the
    /// removeNode fallback must always exist). Any node whose main-out pointed at
    /// the removed terminal falls back to the primary terminal. Recomputes the
    /// evaluation order. Message-thread only.
    bool removeTerminal (MixerNodeId node);

    /// Assigns node's single main-out. dest must be a Bus or the Terminal.
    /// Returns false (no change) if node is unknown/Terminal, dest is an invalid
    /// kind, or the edge would create a cycle.
    bool        setMainOut (MixerNodeId node, MixerNodeId dest);
    MixerNodeId mainOutOf (MixerNodeId node) const noexcept;

    /// Leveled send from a Channel/Bus into an FxReturn. level clamped [0,1];
    /// level<=0 removes the edge. Returns false if fxReturn is not an FxReturn,
    /// source is an FxReturn (v1: no FX-return sends), source==fxReturn, either
    /// is unknown, or the edge would create a cycle.
    bool        setSend (MixerNodeId source, MixerNodeId fxReturn, float level);
    float       sendLevel (MixerNodeId source, MixerNodeId fxReturn) const noexcept;

    /// Message-thread predicate for the UI destination picker: would assigning
    /// node's main-out to dest close a cycle? (true iff dest already reaches node.)
    bool        wouldMainOutCycle (MixerNodeId node, MixerNodeId dest) const noexcept;

    /// Audio-thread read: topologically sorted (sources before destinations,
    /// terminal last). Recomputed on every successful mutation.
    const std::vector<MixerNodeId>& evaluationOrder() const noexcept { return order_; }

    /// One leveled send edge (source -> FX return). Public so the owning mixer's
    /// audio-thread traversal can sum sources into FX returns. Read-only view.
    struct SendEdge { MixerNodeId source; MixerNodeId fxReturn; float level; };

    /// Audio-thread read: all send edges. const& to a pre-built vector — no alloc.
    const std::vector<SendEdge>& sendEdges() const noexcept { return sends_; }

private:
    struct Node { MixerNodeId id; MixerNodeKind kind; MixerNodeId mainOut; };
    struct TerminalNode { MixerNodeId id; MixerTerminal kind; };

    bool        isTerminal (MixerNodeId id) const noexcept;

    const Node* find (MixerNodeId id) const noexcept;
    Node*       find (MixerNodeId id) noexcept;
    bool        reaches (MixerNodeId from, MixerNodeId target) const noexcept;
    void        recomputeOrder();

    std::vector<TerminalNode> terminals_; // [0] = primary; >= 1 by construction
    std::vector<Node>         nodes_;     // excludes terminal nodes
    std::vector<SendEdge>     sends_;
    std::vector<MixerNodeId>  order_;
    std::int64_t              nextId_ { 1 };
};

} // namespace sirius
