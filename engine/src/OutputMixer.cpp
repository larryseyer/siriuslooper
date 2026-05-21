#include "sirius/OutputMixer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace sirius
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

    // Auto-create the master bus at BusId{0} per the M5 Session 2 spec.
    // The master always exists; removing it is not supported in M5.
    buses_.emplace_back (BusId { 0 }, BusConfig { 2, "Master" });

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

    const OutputChannelId id { nextOutputChannelId_++ };
    channels_.push_back (ChannelEntry { id, type, nullptr });

    // Default master direct level — newly registered channels are audible at
    // unity into the master without explicit routing configuration.
    const std::size_t masterIdx = sendMatrixIndex (id, BusId { 0 });
    if (masterIdx < sendMatrix_.size())
        sendMatrix_[masterIdx] = 1.0f;

    channelNodeIds_.push_back (graph_.addNode (MixerNodeKind::Channel));

    return id;
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
    const auto fi = static_cast<std::size_t> (from.value());
    const auto ti = static_cast<std::size_t> (to.value());
    if (fi >= busNodeIds_.size() || ti >= busNodeIds_.size()) return false;
    return graph_.setMainOut (busNodeIds_[fi], busNodeIds_[ti]);
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
    const MixerNodeId masterNode = busNodeIds_.empty() ? MixerNodeId{}
                                                       : busNodeIds_.front();

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

        // Resolve destination: master node or terminal -> master mix buffer;
        // otherwise the destination bus's mix buffer.
        const MixerNodeId destNode = graph_.mainOutOf (nodeId);
        float* destPtrs[2] = { nullptr, nullptr };

        if (destNode == masterNode || destNode == graph_.terminalNode())
        {
            if (! buses_.empty())
            {
                destPtrs[0] = buses_.front().mixBufferChannel (0);
                destPtrs[1] = buses_.front().mixBufferChannel (1);
            }
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

        // Bus::process: accumulates mix buffer into destPtrs, then zeros
        // its own mix buffer. Handles effect-chain dispatch internally.
        bus.process (destPtrs, 2, clampedSamples);
    }

    // Step 4 — master bus writes its accumulated mixBuffer additively into
    // the physical output channels. Use Bus::process for this so the M7
    // effect-chain integration has a single, well-named call site to grow
    // into. (Bus::process is const + handles zeroing internally.)
    for (const auto& bus : buses_)
    {
        if (bus.id() == BusId { 0 })
        {
            bus.process (outputChannelData,
                         std::min (numOutputChannels, 2),
                         clampedSamples);
            break;
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

} // namespace sirius
