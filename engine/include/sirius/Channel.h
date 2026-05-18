#pragma once

#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <cstdint>

namespace sirius
{

/// Identifies a single physical or virtual input source the InputMixer
/// has registered. Strong-typed wrapper so call sites cannot accidentally
/// swap an `InputId` for a `ChannelId`. Follows the house pattern set by
/// `TapeId` and `ConstituentId`.
///
/// Lives in engine/ for now because no core type references it yet —
/// promote to core/ in M3+ if it spreads into persistence or UI layers.
class InputId
{
public:
    explicit constexpr InputId (std::int64_t value) noexcept : value_ (value) {}

    std::int64_t value() const noexcept { return value_; }

    bool operator== (const InputId& other) const noexcept { return value_ == other.value_; }
    bool operator!= (const InputId& other) const noexcept { return value_ != other.value_; }

private:
    std::int64_t value_;
};

/// Identifies a single channel within an InputMixer or OutputMixer.
/// Strong-typed for the same reasons as `InputId`; the mixer surface
/// hands out fresh ChannelIds from `add_channel`.
class ChannelId
{
public:
    explicit constexpr ChannelId (std::int64_t value) noexcept : value_ (value) {}

    std::int64_t value() const noexcept { return value_; }

    bool operator== (const ChannelId& other) const noexcept { return value_ == other.value_; }
    bool operator!= (const ChannelId& other) const noexcept { return value_ != other.value_; }

private:
    std::int64_t value_;
};

/// A first-class channel inside the V3 mixer architecture (V7 alignment
/// plan M2 line 210). A channel pairs an input source with a signal
/// modality, a tape-routing decision, and (M3+) a typed processing chain
/// plus its destination list.
///
/// Session 2 only declares the type shape — the processing chain,
/// destination list, and the audio-thread dispatch all land in M3-M5.
/// The fields present here are the ones that have concrete types today.
struct Channel
{
    ChannelId id;
    SignalType signalType;
    InputId source;
    TapeMode tapeMode;

    // M3: ProcessingChain processing;       — typed by signalType
    // M3: std::vector<Destination> dests;   — TapeId | BusId
    // M3: DirectRouting directRouting;
};

} // namespace sirius
