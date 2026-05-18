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
    buses_.emplace_back (id, std::move (config));
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

    // Step 3 — for each non-master bus in registration order, accumulate
    // its mixBuffer into the master bus's mixBuffer at unity, then zero
    // the non-master bus's mixBuffer for the next buffer. (M7 will run
    // each bus's effectChain BETWEEN the read and the master accumulate;
    // M5 holds the chain config-only per spec line 387, so a direct add
    // is equivalent.) Master is always buses_[0] per the constructor's
    // emplace_back, accessed via mixBufferChannel which is const-callable
    // because mixBuffer_ inside Bus is `mutable`.
    float* masterLeft  = nullptr;
    float* masterRight = nullptr;
    for (const auto& bus : buses_)
    {
        if (bus.id() == BusId { 0 })
        {
            masterLeft  = bus.mixBufferChannel (0);
            masterRight = bus.mixBufferChannel (1);
            break;
        }
    }

    for (const auto& bus : buses_)
    {
        if (bus.id() == BusId { 0 }) continue; // skip master itself

        float* const busLeft  = bus.mixBufferChannel (0);
        float* const busRight = bus.mixBufferChannel (1);
        if (busLeft == nullptr || busRight == nullptr) continue;

        if (masterLeft != nullptr && masterRight != nullptr)
        {
            for (int s = 0; s < clampedSamples; ++s)
            {
                masterLeft[s]  += busLeft[s];
                masterRight[s] += busRight[s];
            }
        }

        // Zero the non-master bus's mixBuffer so next buffer starts fresh.
        // Bus::process would do this if invoked; doing it inline avoids a
        // throwaway dummy-output call.
        std::memset (busLeft,  0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
        std::memset (busRight, 0,
                     static_cast<std::size_t> (clampedSamples) * sizeof (float));
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
