#pragma once

#include "sirius/ProcessingChain.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <cstdint>
#include <memory>

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
/// modality, a tape-routing decision, and a per-`SignalType` processing
/// chain. The destinations vector lands in M3+ when bus routing exists.
///
/// Constructor builds the matching `ProcessingChain` via
/// `makeProcessingChain(signalType)` so callers never need to know which
/// chain subclass goes with which modality. The chain is held by
/// `unique_ptr` because each subclass has its own state; M5 begins to
/// populate that state with real DSP for AudioChain.
struct Channel
{
    Channel (ChannelId id_,
             SignalType signalType_,
             InputId source_,
             TapeMode tapeMode_);

    ChannelId id;
    SignalType signalType;
    InputId source;
    TapeMode tapeMode;
    std::unique_ptr<ProcessingChain> processing;

    // M3+: std::vector<Destination> destinations; — TapeId | BusId
};

} // namespace sirius
