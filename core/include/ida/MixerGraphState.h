#pragma once

#include "ida/EffectChain.h"
#include "ida/MonitorMode.h"
#include "ida/SignalType.h"
#include "ida/TapeMode.h"

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace ida
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
    Kind              kind            { Kind::Terminal };
    MixerTerminalKind terminal        { MixerTerminalKind::Tape }; // valid when kind == Terminal
    std::int64_t      tapeId          { 1 };                       // valid when terminal == Tape (1 = primary)
    std::int64_t      busId           { 0 };                       // valid when kind == Bus
    int               hardwareOutPair { 0 };                       // valid when terminal == HardwareOutput; 0 = outputs [0,1]

    bool operator== (const MixerMainOut& o) const noexcept
    {
        if (kind != o.kind || terminal != o.terminal || busId != o.busId)
            return false;
        const bool isTape = (kind == Kind::Terminal && terminal == MixerTerminalKind::Tape);
        if (isTape && tapeId != o.tapeId) return false;
        const bool isHw   = (kind == Kind::Terminal && terminal == MixerTerminalKind::HardwareOutput);
        return ! isHw || hardwareOutPair == o.hardwareOutPair;
    }
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

/// Default bus width — IDA audio is stereo-only (white paper §6.1), so a
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
    // Operator-set fader / pan / width / mute, mirroring the engine atomics on
    // `Bus`. Defaults equal `Bus`'s defaults so a session that never wrote
    // these fields loads with no audible change.
    float                  gainLinear   { 1.0f };
    bool                   muted        { false };
    float                  pan          { 0.5f };
    float                  width        { 1.0f };
    /// V9 §7.2 (2026-05-25): per-input-side-node MON toggle. `Off` means the
    /// bus is silent in the room (it still writes wherever main-out routes);
    /// `On` mints an OutputMixer channel reading this bus's post-processing
    /// buffer (see `Bus::postProcessingPointer`). Default `Off` per the
    /// explicit-opt-in rule.
    /// No `monitorOutputPair` sibling: bus MON inherits master routing (same
    /// as the V9 channel-MON path), so the disk-back-compat int that
    /// `InputChannelState` keeps for V8 sessions has no analogue here.
    MonitorMode            monitorMode  { MonitorMode::Off };

    bool operator== (const MixerBusState& o) const noexcept
    {
        return busId == o.busId && channelCount == o.channelCount && name == o.name
            && kind == o.kind && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts
            && std::bit_cast<std::uint32_t> (gainLinear) == std::bit_cast<std::uint32_t> (o.gainLinear)
            && muted == o.muted
            && std::bit_cast<std::uint32_t> (pan) == std::bit_cast<std::uint32_t> (o.pan)
            && std::bit_cast<std::uint32_t> (width) == std::bit_cast<std::uint32_t> (o.width)
            && monitorMode == o.monitorMode;
    }
    bool operator!= (const MixerBusState& o) const noexcept { return ! (*this == o); }
};

/// Input-side channel: input source id, device source, tape mode, single
/// main-out (tape / hardware-output / bus), sends into FX returns, insert chain.
/// Slice E2 (2026-05-24) adds `preFaderSends` — one toggle per channel
/// covering all of that channel's sends. Default false = post-fader (the
/// channel's gain + mute applies before the send tap, today's behavior).
/// True = pre-fader (sends bypass the strip so a muted channel still feeds
/// its FX returns; reverb-on-cans / live-cue use cases).
struct InputChannelState
{
    std::int64_t           channelId         { 0 };
    SignalType             signalType        { SignalType::Audio };
    std::int64_t           inputSourceId     { 0 };
    MixerChannelSource     source;
    TapeMode               tapeMode          { TapeMode::NoTape };
    MixerMainOut           mainOut;
    std::vector<MixerSend> sends;
    EffectChain            inserts;
    bool                   preFaderSends     { false };
    /// V9 (whitepaper §7.2): per-channel direct-layer monitor mode.
    /// Two states: `Off` (no monitoring) / `On` (post-strip tap into an
    /// auto-created OutputMixer channel — `InputMixer::attachOutputMixer`).
    /// Default `Off` per the explicit-opt-in rule (whitepaper §7.3).
    MonitorMode            monitorMode       { MonitorMode::Off };
    /// V8 disk back-compat only — unused at runtime in V9. Kept in the
    /// struct so older session files load without schema migration; export
    /// writes `0`. The V9 MON path does not pick an output pair (the
    /// auto-created OutputMixer channel inherits master's routing). See
    /// `SessionFormat.cpp` deserialize and `InputMixer::importGraphState`.
    int                    monitorOutputPair { 0 };

    bool operator== (const InputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && inputSourceId == o.inputSourceId && source == o.source
            && tapeMode == o.tapeMode && mainOut == o.mainOut && sends == o.sends
            && inserts == o.inserts && preFaderSends == o.preFaderSends
            && monitorMode == o.monitorMode
            && (monitorMode == MonitorMode::Off || monitorOutputPair == o.monitorOutputPair);
    }
    bool operator!= (const InputChannelState& o) const noexcept { return ! (*this == o); }
};

/// Output-side channel: routes into buses via the send matrix (no single
/// main-out, no input source / tape mode). Slice 5a adds per-channel
/// hardware-output pair (mirror of MixerBusState's mainOut.hardwareOutPair)
/// so phrase channels can route direct to a stereo output pair, bypassing
/// master and aux buses. The destination kind (Bus vs HardwareOutput) is
/// not persisted in 5a — only the pair index round-trips.
/// Output-channel main-out destination kind (slice E3, 2026-05-24). Bus =
/// the channel's main-out is a bus (master or aux); HardwareOutput = the
/// channel routes direct to a physical pair (slice 5a). FX-return sends are
/// stored separately in `sends` and are independent of the main-out.
enum class OutputChannelMainOutKind { Bus, HardwareOutput };

struct OutputChannelState
{
    std::int64_t             channelId       { 0 };
    SignalType               signalType      { SignalType::Audio };
    std::vector<MixerSend>   sends;
    EffectChain              inserts;
    int                      hardwareOutPair { 0 };
    bool                     preFaderSends   { false };
    OutputChannelMainOutKind mainOutKind     { OutputChannelMainOutKind::Bus };
    std::int64_t             mainOutBus      { 0 }; // valid when mainOutKind == Bus
    /// S6 (2026-05-29) — channel-provenance marker for persistence + import-time
    /// rebind. -1 = phrase channel (the default); 0..31 = the OTTO output index
    /// this channel was minted from via MainComponent::addOttoOutputStrip;
    /// -2 reserved for the future S7 OTTO Stereo Mix sentinel. The engine never
    /// reads this at runtime — pure transport metadata round-tripped through
    /// exportGraphState/importGraphState so MainComponent's post-import rebind
    /// pass can identify OTTO channels and rebind their buffer pointers via
    /// OttoHost::getOttoOutputLeft/Right.
    int                      ottoSource      { -1 };

    bool operator== (const OutputChannelState& o) const noexcept
    {
        return channelId == o.channelId && signalType == o.signalType
            && sends == o.sends && inserts == o.inserts
            && hardwareOutPair == o.hardwareOutPair
            && preFaderSends == o.preFaderSends
            && mainOutKind == o.mainOutKind
            && (mainOutKind == OutputChannelMainOutKind::HardwareOutput
                    || mainOutBus == o.mainOutBus)
            && ottoSource == o.ottoSource;
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

} // namespace ida
