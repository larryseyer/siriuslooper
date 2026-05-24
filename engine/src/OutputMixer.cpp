#include "ida/OutputMixer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace ida
{

namespace
{
    /// Per-channel scratch ceiling — must match (or exceed) any realistic
    /// audio device buffer size. Tied to `Bus::kMaxBusMixSamples` so the
    /// channel scratch and the bus mix scratch share one envelope; drift
    /// would let the renderBuffer traversal write past the bus mix.
    constexpr std::size_t kMaxBlockSamples = Bus::kMaxBusMixSamples;
    static_assert (kMaxBlockSamples == Bus::kMaxBusMixSamples,
                   "OutputMixer scratch ceiling must match Bus mix-buffer ceiling");

    /// Stereo channel count for the per-channel `ChannelStrip<Audio>::process`
    /// call. `ChannelStrip` does mono (gain only) or stereo (equal-power
    /// pan); M5 always feeds it stereo so pan works correctly. The per-
    /// channel scratch stride assumes this is 2 in several places below
    /// (`leftScratch + kMaxBlockSamples`, the `stripChannels[2]` array, the
    /// numOutputChannels clamps); bumping it would silently miscompile.
    constexpr int kStripChannelCount = 2;
    static_assert (kStripChannelCount == 2,
                   "OutputMixer per-channel scratch stride assumes stereo");

    /// Classifies a bus node's graph main-out for the persistence snapshot.
    /// Output buses route bus->bus (incl. ->master) only; the sole terminal
    /// main-out belongs to the master. Returns Kind::Bus with the resolved
    /// busId for everything else.
    // Mirrors InputMixer::mainOutSnapshot intentionally — the two consoles are
    // separate by design (one terminal here vs two there); shared *types* only.
    ida::MixerMainOut busMainOutSnapshot (const ida::MixerGraph& graph,
                                             ida::MixerNodeId node,
                                             const std::vector<ida::MixerNodeId>& busNodeIds,
                                             const std::vector<ida::Bus>& buses,
                                             int pairIndex)
    {
        using namespace ida;
        const auto dest = graph.mainOutOf (node);
        MixerMainOut out;
        if (dest == graph.terminalNode (MixerTerminal::HardwareOutput))
        {
            out.kind = MixerMainOut::Kind::Terminal;
            out.terminal = MixerTerminalKind::HardwareOutput;
            out.hardwareOutPair = pairIndex;
            return out;
        }
        out.kind = MixerMainOut::Kind::Bus;
        for (std::size_t i = 0; i < busNodeIds.size(); ++i)
            if (busNodeIds[i] == dest) { out.busId = buses[i].id().value(); break; }
        jassertfalse; // graph invariant: dest is a bus node absent from busNodeIds
        return out;
    }
}

OutputMixer::OutputMixer()
    : sendMatrix_ (static_cast<std::size_t> (kMaxOutputChannels)
                       * static_cast<std::size_t> (kMaxBuses),
                   0.0f),
      channelScratch_ (static_cast<std::size_t> (kMaxOutputChannels)
                           * kMaxBlockSamples
                           * static_cast<std::size_t> (kStripChannelCount),
                       0.0f)
{
    // Reserve to the hard caps so push_backs inside addChannel/addBus never
    // reallocate (would otherwise invalidate any const references S3 might
    // take into the registries). reserve() doesn't construct elements —
    // size stays 0 until addChannel/addBus is called.
    channels_.reserve (static_cast<std::size_t> (kMaxOutputChannels));
    buses_.reserve   (static_cast<std::size_t> (kMaxBuses));
    busHardwareOutPair_.reserve     (static_cast<std::size_t> (kMaxBuses));
    channelHardwareOutPair_.reserve (static_cast<std::size_t> (kMaxOutputChannels));
    freeChannelIds_.reserve         (static_cast<std::size_t> (kMaxOutputChannels));

    // Auto-create the master bus at BusId{0} per the M5 Session 2 spec.
    // The master always exists; removing it is not supported in M5.
    buses_.emplace_back (BusId { 0 }, BusConfig { 2, "Master" });
    busHardwareOutPair_.push_back (0); // master defaults to physical outputs [0,1]

    // Register the master as a graph node whose main-out is the terminal.
    // busNodeIds_[0] corresponds to buses_[0] (BusId{0}, the master).
    busNodeIds_.reserve (static_cast<std::size_t> (kMaxBuses));
    channelNodeIds_.reserve (static_cast<std::size_t> (kMaxOutputChannels));
    busNodeIds_.push_back (graph_.addNode (MixerNodeKind::Bus));
    graph_.setMainOut (busNodeIds_.front(), graph_.terminalNode());
}

OutputMixer::~OutputMixer() = default;

OutputChannelId OutputMixer::addChannel (SignalType type)
{
    if (channels_.size() >= static_cast<std::size_t> (kMaxOutputChannels))
    {
        // Fail loud per CLAUDE.md rule 8 — losing a registered channel silently
        // means the operator's gain/pan settings vanish without warning.
        jassertfalse;
        return OutputChannelId { 0 }; // sentinel — channel id 0 is unused.
    }

    // Slice 5a: prefer a freed id before minting a fresh one so phrase
    // add/remove churn doesn't burn through kMaxOutputChannels. The freed
    // id's sendMatrix row was zeroed by removeChannel, so re-applying the
    // master-unity default below puts the reused channel at addChannel's
    // canonical starting state regardless of its previous use.
    OutputChannelId id { 0 };
    if (! freeChannelIds_.empty())
    {
        id = OutputChannelId { freeChannelIds_.back() };
        freeChannelIds_.pop_back();
    }
    else
    {
        id = OutputChannelId { nextOutputChannelId_++ };
    }

    channels_.push_back (ChannelEntry { id, type, nullptr });

    // Default master direct level — newly registered channels are audible at
    // unity into the master without explicit routing configuration.
    const std::size_t masterIdx = sendMatrixIndex (id, BusId { 0 });
    if (masterIdx < sendMatrix_.size())
        sendMatrix_[masterIdx] = 1.0f;

    channelNodeIds_.push_back (graph_.addNode (MixerNodeKind::Channel));
    channelHardwareOutPair_.push_back (0); // new channels default to pair 0

    return id;
}

void OutputMixer::removeChannel (OutputChannelId id)
{
    // Locate the id in channels_; unknown ids are a silent no-op (the UI
    // never asks for unknown ids in steady state — defensive only).
    std::size_t idx = channels_.size();
    for (std::size_t i = 0; i < channels_.size(); ++i)
    {
        if (channels_[i].id == id) { idx = i; break; }
    }
    if (idx >= channels_.size()) return;

    // Zero the freed channel's row of sendMatrix_ so a re-minted id starts at
    // addChannel's defaults (unity into master, 0 into every aux) rather than
    // inheriting the removed channel's send levels.
    const auto channelValue = id.value();
    if (channelValue >= 1 && channelValue <= kMaxOutputChannels)
    {
        const std::size_t row = static_cast<std::size_t> (channelValue - 1);
        const std::size_t cols = static_cast<std::size_t> (kMaxBuses);
        for (std::size_t b = 0; b < cols; ++b)
            sendMatrix_[row * cols + b] = 0.0f;
    }

    // Tear down the graph node so it stops appearing in evaluationOrder().
    graph_.removeNode (channelNodeIds_[idx]);

    // Swap-erase from the three parallel vectors. Order isn't observable
    // through the public API; iteration uses channels_'s indices directly.
    const std::size_t last = channels_.size() - 1;
    if (idx != last)
    {
        channels_[idx]               = std::move (channels_[last]);
        channelNodeIds_[idx]         = channelNodeIds_[last];
        channelHardwareOutPair_[idx] = channelHardwareOutPair_[last];
    }
    channels_.pop_back();
    channelNodeIds_.pop_back();
    channelHardwareOutPair_.pop_back();

    freeChannelIds_.push_back (channelValue);
}

void OutputMixer::setChannelStrip (OutputChannelId id,
                                   std::unique_ptr<ChannelStrip<SignalType::Audio>> strip)
{
    for (auto& entry : channels_)
    {
        if (entry.id == id)
        {
            entry.strip = std::move (strip);
            return;
        }
    }
}

BusId OutputMixer::addBus (BusConfig config)
{
    if (buses_.size() >= static_cast<std::size_t> (kMaxBuses))
    {
        // Fail loud per CLAUDE.md rule 8 — silently routing further routes into
        // the master column on overflow would scribble unrelated sends into the
        // master and be debug-hostile. Sentinel preserved for release builds.
        jassertfalse;
        return BusId { 0 }; // sentinel — master always exists, safe fallback.
    }

    const BusId id { nextBusId_++ };
    const BusKind busKind = config.kind; // capture before std::move in emplace_back
    buses_.emplace_back (id, std::move (config));
    busHardwareOutPair_.push_back (0); // new buses default to pair 0
    // Forward the stashed effect-chain host to the newly registered bus
    // so post-`setEffectChainHost` `addBus` calls get the same wiring as
    // pre-existing buses. Null is a valid value (M5 inline path).
    buses_.back().setEffectChainHost (effectChainHost_);

    const auto graphKind = (busKind == BusKind::FxReturn) ? MixerNodeKind::FxReturn
                                                           : MixerNodeKind::Bus;
    const auto node = graph_.addNode (graphKind);
    busNodeIds_.push_back (node);
    graph_.setMainOut (node, busNodeIds_.front()); // aux bus -> master by default

    return id;
}

void OutputMixer::setBusEffectChain (BusId id, EffectChain chain)
{
    for (auto& bus : buses_)
    {
        if (bus.id() == id)
        {
            bus.setEffectChain (std::move (chain));
            return;
        }
    }
}

void OutputMixer::setEffectChainHost (IEffectChainHost* host) noexcept
{
    effectChainHost_ = host;
    for (auto& bus : buses_)
        bus.setEffectChainHost (host);
}

Bus* OutputMixer::busForId (BusId id) noexcept
{
    for (auto& bus : buses_)
        if (bus.id() == id)
            return &bus;
    return nullptr;
}

void OutputMixer::routeChannelToBus (OutputChannelId channel, BusId bus, float sendLevel)
{
    const std::size_t idx = sendMatrixIndex (channel, bus);
    if (idx >= sendMatrix_.size()) return;

    // Confirm both ids actually exist in the registries — silently drop
    // configuration attempts against unknown ids rather than scribble into
    // a matrix cell that the audio thread might later read as a real send.
    const bool channelKnown = std::any_of (channels_.begin(), channels_.end(),
        [channel] (const ChannelEntry& e) { return e.id == channel; });
    if (! channelKnown) return;

    const bool busKnown = std::any_of (buses_.begin(), buses_.end(),
        [bus] (const Bus& b) { return b.id() == bus; });
    if (! busKnown) return;

    sendMatrix_[idx] = std::clamp (sendLevel, 0.0f, 1.0f);
}

bool OutputMixer::routeBusToBus (BusId from, BusId to)
{
    // Master's main-out is fixed to the terminal (Step 4 always drains it to
    // the physical outputs); never let it be re-pointed at an aux bus, which
    // would make the graph disagree with the DSP.
    if (from.value() == 0) return false;

    // Invariant: BusId values are dense and 0-based (master == BusId{0},
    // addBus assigns sequentially), so BusId.value() indexes busNodeIds_
    // directly. The bounds check below rejects any out-of-range id.
    const auto fi = static_cast<std::size_t> (from.value());
    const auto ti = static_cast<std::size_t> (to.value());
    if (fi >= busNodeIds_.size() || ti >= busNodeIds_.size()) return false;
    return graph_.setMainOut (busNodeIds_[fi], busNodeIds_[ti]);
}

bool OutputMixer::setBusMainOutToHardwareOutput (BusId bus)
{
    // Single-arg overload preserves slice-2's "park on pair 0" behaviour
    // for both master and aux buses. The pair-indexed overload below is the
    // canonical setter; this one is kept as a convenience for callers that
    // don't care about the pair.
    return setBusMainOutToHardwareOutput (bus, 0);
}

bool OutputMixer::setBusMainOutToHardwareOutput (BusId bus, int pairIndex)
{
    const auto bi = static_cast<std::size_t> (bus.value());
    if (bi >= busNodeIds_.size()) return false;

    // Master is already permanently routed to the terminal by the ctor, so
    // setMainOut on the master node is effectively a no-op at the graph
    // layer — but we still record the pair index so renderBuffer reads it
    // in Step 4. For aux buses, the setMainOut call is the meaningful work.
    const bool graphOk = graph_.setMainOut (
        busNodeIds_[bi], graph_.terminalNode (MixerTerminal::HardwareOutput));
    if (! graphOk && bus.value() != 0) return false;

    busHardwareOutPair_[bi] = std::max (pairIndex, 0);
    return true;
}

int OutputMixer::busHardwareOutPair (BusId id) const noexcept
{
    const auto bi = static_cast<std::size_t> (id.value());
    if (bi >= busHardwareOutPair_.size()) return 0;
    return busHardwareOutPair_[bi];
}

bool OutputMixer::setChannelMainOutToHardwareOutput (OutputChannelId channel, int pairIndex)
{
    for (std::size_t i = 0; i < channels_.size(); ++i)
    {
        if (channels_[i].id == channel)
        {
            channelHardwareOutPair_[i] = std::max (pairIndex, 0);
            return true;
        }
    }
    return false;
}

int OutputMixer::channelMainOutHardwareOutPair (OutputChannelId id) const noexcept
{
    for (std::size_t i = 0; i < channels_.size(); ++i)
        if (channels_[i].id == id)
            return channelHardwareOutPair_[i];
    return 0;
}

bool OutputMixer::busMainOutToBusWouldCycle (BusId from, BusId to) const noexcept
{
    const auto fi = static_cast<std::size_t> (from.value());
    const auto ti = static_cast<std::size_t> (to.value());
    if (fi >= busNodeIds_.size() || ti >= busNodeIds_.size()) return false;
    return graph_.wouldMainOutCycle (busNodeIds_[fi], busNodeIds_[ti]);
}

bool OutputMixer::renameBus (BusId id, std::string newName)
{
    if (id.value() == 0) return false; // master name is canonical
    for (auto& bus : buses_)
    {
        if (bus.id() == id)
        {
            bus.setName (std::move (newName));
            return true;
        }
    }
    return false;
}

int OutputMixer::busCount() const noexcept
{
    return static_cast<int> (buses_.size());
}

OutputMixer::MainOutDest OutputMixer::busMainOut (BusId id) const noexcept
{
    const auto bi = static_cast<std::size_t> (id.value());
    if (bi >= busNodeIds_.size()) return MainOutDest::Bus;   // unknown — defensive default
    const auto dest = graph_.mainOutOf (busNodeIds_[bi]);
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput))
        return MainOutDest::HardwareOutput;
    return MainOutDest::Bus;
}

BusId OutputMixer::busMainOutBus (BusId id) const noexcept
{
    const auto bi = static_cast<std::size_t> (id.value());
    if (bi >= busNodeIds_.size()) return BusId{ 0 };
    const auto destNode = graph_.mainOutOf (busNodeIds_[bi]);
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
        if (busNodeIds_[i] == destNode) return buses_[i].id();
    return BusId{ 0 };
}

float OutputMixer::sendLevelFor (OutputChannelId channel, BusId bus) const noexcept
{
    const std::size_t idx = sendMatrixIndex (channel, bus);
    if (idx >= sendMatrix_.size()) return 0.0f;
    return sendMatrix_[idx];
}

void OutputMixer::renderBuffer (const float* const* inputChannelData,
                                int                 numInputChannels,
                                float* const*       outputChannelData,
                                int                 numOutputChannels,
                                int                 numSamples) const noexcept
{
    // M5 auto-registration policy: OutputMixer comes up empty. The empty-
    // channel early-return is the hot path in the default app config; tests
    // and M6+ code register channels for the produced-mix path.
    if (channels_.empty()) return;
    if (outputChannelData == nullptr || numOutputChannels <= 0) return;
    if (numSamples <= 0) return;

    const int clampedSamples = std::min (numSamples,
                                         static_cast<int> (kMaxBlockSamples));

    // Step 1 — for each registered output channel, scratch-mix its source
    // signal through ChannelStrip<Audio>::process. The source is the input
    // device channel at the same 0-based index (M5 proxy for Constituent
    // rendering; M6+ replaces). Channels without an input source go silent.
    for (std::size_t i = 0; i < channels_.size(); ++i)
    {
        const auto& entry        = channels_[i];
        float* const leftScratch =
            channelScratch_.data()
                + i * kMaxBlockSamples * static_cast<std::size_t> (kStripChannelCount);
        float* const rightScratch = leftScratch + kMaxBlockSamples;

        // Zero the scratch unconditionally — defensive against the prior
        // buffer's residue when the current source is silent.
        std::memset (leftScratch,  0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
        std::memset (rightScratch, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));

        // Non-Audio channels skip DSP entirely — their strips are stubs
        // until M9 (Midi) / M12 (Video) / M13 (File).
        if (entry.signalType != SignalType::Audio) continue;

        // Source = input device channel at the matching 0-based index.
        const std::int64_t channelIndex = entry.id.value() - 1;
        if (channelIndex < 0 || channelIndex >= numInputChannels) continue;
        const float* const src = inputChannelData[channelIndex];
        if (src == nullptr) continue;

        // Copy source into both scratch channels (mono → stereo) so the
        // strip's equal-power pan has something to work with.
        std::memcpy (leftScratch,  src,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
        std::memcpy (rightScratch, src,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));

        // Apply the per-channel ChannelStrip<Audio> if attached. Without a
        // strip the scratch carries the unity-gain source — M5 acceptable;
        // operators expecting gain/pan attach a strip via setChannelStrip.
        if (entry.strip != nullptr)
        {
            float* stripChannels[kStripChannelCount] = { leftScratch, rightScratch };
            entry.strip->process (stripChannels, kStripChannelCount, clampedSamples);
        }
    }

    // Step 2 — for each (channel, bus) with send level > 0, accumulate the
    // scaled scratch into the target bus's mixBuffer. Master bus (BusId{0})
    // is included; channels with master send level 1.0 (the addChannel
    // default) land in the master directly.
    for (std::size_t ci = 0; ci < channels_.size(); ++ci)
    {
        const auto& entry        = channels_[ci];
        const float* const leftScratch =
            channelScratch_.data()
                + ci * kMaxBlockSamples * static_cast<std::size_t> (kStripChannelCount);
        const float* const rightScratch = leftScratch + kMaxBlockSamples;

        for (const auto& bus : buses_)
        {
            const float level = sendLevelFor (entry.id, bus.id());
            if (level <= 0.0f) continue;

            float* const busLeft  = bus.mixBufferChannel (0);
            float* const busRight = bus.mixBufferChannel (1);
            if (busLeft == nullptr || busRight == nullptr) continue;

            for (int s = 0; s < clampedSamples; ++s)
            {
                busLeft[s]  += leftScratch[s]  * level;
                busRight[s] += rightScratch[s] * level;
            }
        }
    }

    // Step 3 — walk evaluationOrder() (topologically sorted, sources before
    // destinations, terminal last). For each non-master bus node, process it
    // into its graph main-out destination's mix buffer. This supports
    // subgroup routing (busA -> busB -> master) while preserving the default
    // "all aux buses -> master" behaviour when no routeBusToBus calls have
    // been made. Bus::process handles both the M5 inline path and the M7
    // effect-chain path, and zeros its own mix buffer internally.
    //
    // RT-safety: evaluationOrder() returns a const& to a pre-built vector —
    // no allocation. busNodeIds_ lookup is a read-only linear scan (max 64
    // entries). No graph mutators are called here.
    const MixerNodeId masterNode   = busNodeIds_.empty() ? MixerNodeId{}
                                                         : busNodeIds_.front();
    const MixerNodeId terminalNode = graph_.terminalNode();

    // Resolves a pair index into a pair of physical output buffer pointers
    // with bounds-checked fallback. Returns the number of usable channels
    // for `bus.process`: 2 on a real stereo pair, 1 on a degenerate mono
    // output (Bus::process writes only the left half), 0 if no output is
    // available at all (the caller skips bus.process entirely).
    const auto resolveHardwarePair = [outputChannelData, numOutputChannels]
                                     (int pairIndex, float** dst) noexcept -> int
    {
        const int safePair = pairIndex < 0 ? 0 : pairIndex;
        const int leftCh   = safePair * 2;
        const int rightCh  = leftCh + 1;
        if (rightCh < numOutputChannels)
        {
            dst[0] = outputChannelData[leftCh];
            dst[1] = outputChannelData[rightCh];
            return 2;
        }
        if (numOutputChannels >= 2)
        {
            // Requested pair exceeds available output channels — fall back
            // to pair 0 so the bus is still audible rather than dropped.
            dst[0] = outputChannelData[0];
            dst[1] = outputChannelData[1];
            return 2;
        }
        if (numOutputChannels >= 1)
        {
            // Degenerate mono output — write left only, drop right (matches
            // pre-slice-3 behaviour for the single-output renderInputGraph
            // path; right is not addressable here).
            dst[0] = outputChannelData[0];
            dst[1] = nullptr;
            return 1;
        }
        dst[0] = nullptr;
        dst[1] = nullptr;
        return 0;
    };

    for (const MixerNodeId nodeId : graph_.evaluationOrder())
    {
        // Find which bus (if any) this node corresponds to.
        std::size_t busIdx = busNodeIds_.size(); // sentinel = not found
        for (std::size_t bi = 0; bi < busNodeIds_.size(); ++bi)
        {
            if (busNodeIds_[bi] == nodeId) { busIdx = bi; break; }
        }

        if (busIdx >= busNodeIds_.size()) continue; // channel or terminal node
        if (busIdx == 0) continue;                  // master handled in Step 4

        const Bus& bus = buses_[busIdx];

        // Resolve destination:
        //   - master node          -> master's mix buffer (subgroup-into-master)
        //   - terminal (HardwareOutput) -> direct into physical outputs at this
        //     bus's recorded pair index, bypassing master entirely (the real
        //     "Direct out" semantic — slice-2 folded this through master,
        //     which only worked on 2-output devices).
        //   - another bus node     -> that bus's mix buffer (subgroup)
        const MixerNodeId destNode = graph_.mainOutOf (nodeId);
        float* destPtrs[2] = { nullptr, nullptr };

        // destChannelCount tracks how many output channels the destination
        // exposes; matters for the hardware-direct path's mono degenerate
        // case. Subgroup destinations (master / other bus) are always
        // 2-channel stereo per the hard invariant.
        int destChannelCount = 2;

        if (destNode == terminalNode)
        {
            destChannelCount = resolveHardwarePair (busHardwareOutPair_[busIdx], destPtrs);
        }
        else if (destNode == masterNode)
        {
            destPtrs[0] = buses_.front().mixBufferChannel (0);
            destPtrs[1] = buses_.front().mixBufferChannel (1);
        }
        else
        {
            // Find the destination bus by its node id.
            for (std::size_t di = 0; di < busNodeIds_.size(); ++di)
            {
                if (busNodeIds_[di] == destNode)
                {
                    destPtrs[0] = buses_[di].mixBufferChannel (0);
                    destPtrs[1] = buses_[di].mixBufferChannel (1);
                    break;
                }
            }
        }

        if (destChannelCount > 0)
        {
            // Bus::process: accumulates mix buffer into destPtrs, then zeros
            // its own mix buffer. Handles effect-chain dispatch internally.
            bus.process (destPtrs, destChannelCount, clampedSamples);
        }
    }

    // Step 4 — master bus writes its accumulated mixBuffer additively into
    // the physical output channels at its recorded pair index. On a
    // 2-channel device the pair is always 0; on multi-output interfaces the
    // operator can park master on any pair. A degenerate mono output drops
    // the right half (matches pre-slice-3 behaviour).
    float* masterDest[2] = { nullptr, nullptr };
    const int masterChannelCount =
        resolveHardwarePair (busHardwareOutPair_.empty() ? 0 : busHardwareOutPair_.front(),
                             masterDest);
    if (masterChannelCount > 0)
    {
        for (const auto& bus : buses_)
        {
            if (bus.id() == BusId { 0 })
            {
                bus.process (masterDest, masterChannelCount, clampedSamples);
                break;
            }
        }
    }
}

std::size_t OutputMixer::sendMatrixIndex (OutputChannelId channel, BusId bus) const noexcept
{
    const std::int64_t channelValue = channel.value();
    const std::int64_t busValue     = bus.value();

    // Channel ids start at 1 (id 0 is reserved sentinel); map to a 0-based
    // matrix row. Buses use their value directly because master is BusId{0}.
    if (channelValue < 1 || channelValue > kMaxOutputChannels) return sendMatrix_.size();
    if (busValue < 0     || busValue     >= kMaxBuses)        return sendMatrix_.size();

    const std::size_t row = static_cast<std::size_t> (channelValue - 1);
    const std::size_t col = static_cast<std::size_t> (busValue);
    return row * static_cast<std::size_t> (kMaxBuses) + col;
}

OutputMixerGraphState OutputMixer::exportGraphState() const
{
    OutputMixerGraphState state;

    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = busMainOutSnapshot (graph_, busNodeIds_[i], busNodeIds_, buses_,
                                                 busHardwareOutPair_[i]);
        entry.inserts      = bus.effectChain();
        state.buses.push_back (std::move (entry));   // master is index 0 by construction
    }

    // channels_ is a vector — insertion order is already deterministic, so (unlike
    // InputMixer's unordered_map) no sort is needed before export.
    for (std::size_t ci = 0; ci < channels_.size(); ++ci)
    {
        const auto& ce = channels_[ci];
        OutputChannelState entry;
        entry.channelId       = ce.id.value();
        entry.signalType      = ce.signalType;
        entry.hardwareOutPair = channelHardwareOutPair_[ci]; // slice 5a
        if (ce.strip != nullptr) entry.inserts = ce.strip->effectChain();
        for (std::size_t b = 0; b < buses_.size(); ++b)
        {
            const auto busId = buses_[b].id();
            const auto level = sendLevelFor (ce.id, busId);
            // Always emit the master (BusId 0) send, even at 0: addChannel
            // defaults it to 1.0, so a deliberately-zeroed master level must be
            // persisted explicitly to survive import. Aux sends default to 0,
            // so dropping their zeros is safe.
            if (busId.value() == 0 || level > 0.0f)
                entry.sends.push_back ({ busId.value(), level });
        }
        state.channels.push_back (std::move (entry));
    }

    state.nextBusId     = nextBusId_;
    state.nextChannelId = nextOutputChannelId_;
    return state;
}

void OutputMixer::importGraphState (const OutputMixerGraphState& state)
{
    auto busExists = [this] (std::int64_t id)
    {
        for (const auto& bus : buses_) if (bus.id().value() == id) return true;
        return false;
    };

    // Master (BusId 0) already exists from the ctor; reuse it (apply inserts).
    // Create the rest with addBus (dense ids reproduce the persisted busId).
    for (const auto& b : state.buses)
    {
        if (! busExists (b.busId))
        {
            BusConfig config;
            config.channelCount = b.channelCount;
            config.name         = b.name;
            config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
            addBus (config);
        }
        setBusEffectChain (BusId (b.busId), b.inserts);
    }

    // Apply bus subgroup routing once all buses exist. Master main-out is the
    // terminal already, but its hardwareOutPair still needs restoring; aux
    // buses get either routeBusToBus (Kind::Bus) or
    // setBusMainOutToHardwareOutput (Kind::Terminal) with the persisted pair.
    for (const auto& b : state.buses)
    {
        if (b.mainOut.kind == MixerMainOut::Kind::Bus)
        {
            const bool ok = routeBusToBus (BusId (b.busId), BusId (b.mainOut.busId));
            jassert (ok); juce::ignoreUnused (ok);
        }
        else if (b.mainOut.terminal == MixerTerminalKind::HardwareOutput)
        {
            const bool ok = setBusMainOutToHardwareOutput (BusId (b.busId),
                                                            b.mainOut.hardwareOutPair);
            jassert (ok); juce::ignoreUnused (ok);
        }
    }

    for (const auto& c : state.channels)
    {
        const auto created = addChannel (c.signalType);
        if (c.signalType == SignalType::Audio)
        {
            auto strip = std::make_unique<ChannelStrip<SignalType::Audio>>();
            strip->setEffectChain (c.inserts);
            setChannelStrip (created, std::move (strip));
        }
        for (const auto& s : c.sends) routeChannelToBus (created, BusId (s.busId), s.level);
        // Slice 5a: restore per-channel hardware-output pair. addChannel just
        // pushed a default-0 entry; setter overwrites it with the persisted
        // value. (Destination kind isn't persisted in 5a — only the pair.)
        setChannelMainOutToHardwareOutput (created, c.hardwareOutPair);
    }

    nextBusId_           = std::max (nextBusId_, state.nextBusId);
    nextOutputChannelId_ = std::max (nextOutputChannelId_, state.nextChannelId);
}

} // namespace ida
