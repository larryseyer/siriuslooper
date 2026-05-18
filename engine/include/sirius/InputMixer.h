#pragma once

#include "sirius/Channel.h"
#include "sirius/ChannelDefaults.h"
#include "sirius/InputDescriptor.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace sirius
{

class OverloadProtection;
class TapeWriter;

/// V3 §2.1 / V7 alignment plan M3: the input-side mixer. Sits between
/// physical input registration and the tape/direct-layer split, owns the
/// channel registry, and dispatches audio-thread work into per-channel
/// processing chains.
///
/// Collaborators (TapeWriter, OverloadProtection) are injected via set-once
/// setters on the message thread. The audio-thread entry point (processBuffer)
/// is allocation-free, lock-free, and I/O-free per docs/RT_SAFETY_CONTRACT.md.
class InputMixer
{
public:
    InputMixer();
    ~InputMixer();

    // Injected non-owning collaborators (set-once on the message thread).
    void setTapeWriter (TapeWriter* writer) noexcept;
    void setOverloadProtection (OverloadProtection* overload) noexcept;

    // Input-layer registry --------------------------------------------------
    void registerInput (InputId, const InputDescriptor&);
    void setInputRawDirect (InputId, bool enabled);
    void setInputEnabled (InputId, bool enabled);
    void setInputDefaults (InputId, ChannelDefaults defaults);

    // Channel registry ------------------------------------------------------
    ChannelId addChannel (InputId source, SignalType type);
    void removeChannel (ChannelId);
    void setChannelTapeMode (ChannelId, TapeMode);

    // Audio-thread interface (real-time safe) ------------------------------
    /// Walks the channel, applies its ProcessingChain (no-op in M3), and
    /// if the channel is tape-bearing, memcpys `bytes[0..byteCount]` into a
    /// `TapeWriteMessage` and enqueues on the bound TapeWriter. On
    /// queue-full, calls `OverloadProtection::reportLoad(1.0)` and drops.
    /// No allocations, no locks, no I/O on this path.
    void processBuffer (ChannelId, const std::byte* bytes, std::size_t byteCount) noexcept;

    // Finalize a channel's recording — Session 3 wires the full flow.
    void finalizeChannel (ChannelId);

private:
    struct InputState
    {
        InputDescriptor descriptor;
        bool rawDirectMonitor;
        bool enabled;
        ChannelDefaults defaults;
    };

    std::unordered_map<std::int64_t, InputState> inputs_;
    std::unordered_map<std::int64_t, Channel> channels_;
    std::int64_t nextChannelId_ { 1 };

    TapeWriter* tapeWriter_ { nullptr };
    OverloadProtection* overload_ { nullptr };
};

} // namespace sirius
