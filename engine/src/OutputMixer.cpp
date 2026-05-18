#include "sirius/OutputMixer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <utility>

namespace sirius
{

OutputMixer::OutputMixer()
    : sendMatrix_ (static_cast<std::size_t> (kMaxOutputChannels)
                       * static_cast<std::size_t> (kMaxBuses),
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

void OutputMixer::routeChannelToOutput (OutputChannelId)
{
    // Placeholder retained from the M2 skeleton. M5 Session 2 doesn't define
    // OutputDestination; Session 3 (or a later milestone) decides whether
    // physical-output routing is its own surface or folded into the send
    // matrix as a special bus id. No-op for now.
}

void OutputMixer::renderBuffer (float* const* output, int numChannels, int numSamples) noexcept
{
    // M5 Session 2 stub — Session 3 fills this with the channel-strip →
    // send-matrix → bus-process → master-bus traversal. The early-return
    // shape mirrors `InputMixer::processBuffer`'s defensive guards.
    (void) output;
    (void) numChannels;
    (void) numSamples;
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
