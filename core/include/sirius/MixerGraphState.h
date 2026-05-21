#pragma once

#include "sirius/EffectChain.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace sirius
{

/// Routing-graph Phase 5 — a JUCE-free, engine-free plain-data snapshot of one
/// mixer's routing graph, for persistence. The engine exports a live mixer into
/// this and imports it back; persistence serializes it to/from JSON. Core stays
/// engine-free, so the engine enums BusKind / MixerTerminal are mirrored here as
/// MixerBusKind / MixerTerminalKind and translated at the engine export/import seam.

enum class MixerBusKind { Bus, FxReturn };
enum class MixerTerminalKind { Tape, HardwareOutput };

/// A node's single main-out destination: a terminal (tape / hardware-output) or
/// another bus (subgroup / master).
struct MixerMainOut
{
    enum class Kind { Terminal, Bus };
    Kind              kind     { Kind::Terminal };
    MixerTerminalKind terminal { MixerTerminalKind::Tape }; // valid when kind == Terminal
    std::int64_t      busId    { 0 };                       // valid when kind == Bus

    bool operator== (const MixerMainOut& o) const noexcept
    { return kind == o.kind && terminal == o.terminal && busId == o.busId; }
    bool operator!= (const MixerMainOut& o) const noexcept { return ! (*this == o); }
};

/// A leveled send into another bus (an FX return on the input side; any bus on
/// the output-channel send matrix).
struct MixerSend
{
    std::int64_t busId { 0 };
    float        level { 0.0f };

    bool operator== (const MixerSend& o) const noexcept
    {
        // level is a stored snapshot value; exact equality is the round-trip
        // contract (float -> JSON -> float is lossless). Compare raw bits to
        // make that intent explicit and silence -Wfloat-equal.
        return busId == o.busId
            && std::bit_cast<std::uint32_t> (level) == std::bit_cast<std::uint32_t> (o.level);
    }
    bool operator!= (const MixerSend& o) const noexcept { return ! (*this == o); }
};

/// Which device channel(s) feed an input channel's stereo strip (RME model).
struct MixerChannelSource
{
    int  left   { 0 };
    int  right  { 1 };
    bool stereo { true };

    bool operator== (const MixerChannelSource& o) const noexcept
    { return left == o.left && right == o.right && stereo == o.stereo; }
    bool operator!= (const MixerChannelSource& o) const noexcept { return ! (*this == o); }
};

/// Default bus width — Sirius audio is stereo-only (white paper §6.1), so a
/// bus defaults to a stereo pair. Mirrors engine BusConfig::channelCount.
inline constexpr int kDefaultBusChannelCount = 2;

/// One bus or FX return: identity, config, graph main-out, sends, insert chain.
/// Shared by both mixers. On the output side, buses[0] is the master (busId 0)
/// and bus sends are empty (output routing uses main-out + the channel matrix).
struct MixerBusState
{
    std::int64_t           busId        { 0 };
    int                    channelCount { kDefaultBusChannelCount };
    std::string            name;
    MixerBusKind           kind         { MixerBusKind::Bus };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const MixerBusState& o) const noexcept
    {
        return busId == o.busId && channelCount == o.channelCount && name == o.name
            && kind == o.kind && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts;
    }
    bool operator!= (const MixerBusState& o) const noexcept { return ! (*this == o); }
};

/// Input-side channel: input source id, device source, tape mode, single
/// main-out (tape / hardware-output / bus), sends into FX returns, insert chain.
struct InputChannelState
{
    std::int64_t           channelId     { 0 };
    SignalType             signalType    { SignalType::Audio };
    std::int64_t           inputSourceId { 0 };
    MixerChannelSource     source;
    TapeMode               tapeMode      { TapeMode::NoTape };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const InputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && inputSourceId == o.inputSourceId && source == o.source
            && tapeMode == o.tapeMode && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts;
    }
    bool operator!= (const InputChannelState& o) const noexcept { return ! (*this == o); }
};

/// Output-side channel: routes into buses via the send matrix (no single
/// main-out, no input source / tape mode).
struct OutputChannelState
{
    std::int64_t           channelId  { 0 };
    SignalType             signalType { SignalType::Audio };
    std::vector<MixerSend> sends;
    EffectChain            inserts;

    bool operator== (const OutputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && sends == o.sends && inserts == o.inserts;
    }
    bool operator!= (const OutputChannelState& o) const noexcept { return ! (*this == o); }
};

struct InputMixerGraphState
{
    std::vector<MixerBusState>     buses;
    std::vector<InputChannelState> channels;
    std::int64_t                   nextBusId     { 1 };
    std::int64_t                   nextChannelId { 1 };

    bool operator== (const InputMixerGraphState& o) const noexcept
    {
        return buses == o.buses && channels == o.channels
            && nextBusId == o.nextBusId && nextChannelId == o.nextChannelId;
    }
    bool operator!= (const InputMixerGraphState& o) const noexcept { return ! (*this == o); }
};

struct OutputMixerGraphState
{
    std::vector<MixerBusState>      buses;     // buses[0] == master (busId 0)
    std::vector<OutputChannelState> channels;
    std::int64_t                    nextBusId     { 1 };
    std::int64_t                    nextChannelId { 1 };

    bool operator== (const OutputMixerGraphState& o) const noexcept
    {
        return buses == o.buses && channels == o.channels
            && nextBusId == o.nextBusId && nextChannelId == o.nextChannelId;
    }
    bool operator!= (const OutputMixerGraphState& o) const noexcept { return ! (*this == o); }
};

} // namespace sirius
