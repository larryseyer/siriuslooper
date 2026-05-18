#pragma once

#include "sirius/Channel.h"
#include "sirius/ChannelDefaults.h"
#include "sirius/InputDescriptor.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sirius { namespace persistence { class TapeStore; } }

namespace sirius
{

class NotificationBus;
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
    void setTapeStore (sirius::persistence::TapeStore* store) noexcept;
    /// M6 Session 2 — attach the engine→UI truthfulness channel. When bound,
    /// the queue-full branch of `processBuffer` posts a `Warning/CpuPressure`
    /// notification alongside the existing `OverloadProtection::reportLoad`
    /// call. Set-once on the message thread before the audio device starts;
    /// non-owning. `NotificationBus::post` is `noexcept` and allocation-free,
    /// so this preserves the audio-thread contract on `processBuffer`.
    void setNotificationBus (NotificationBus* bus) noexcept;

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

    /// Message-thread accessor for a channel's ProcessingChain. Returns
    /// nullptr if the ChannelId is unknown. Callers down-cast via
    /// `dynamic_cast` (or `static_cast` after checking `signalType()`) to
    /// reach the per-modality strip surface — e.g. setting gain/pan on
    /// `ChannelStrip<SignalType::Audio>`. Never call from the audio thread.
    ProcessingChain* processingChainFor (ChannelId) noexcept;

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
    sirius::persistence::TapeStore* tapeStore_ { nullptr };
    NotificationBus* notificationBus_ { nullptr };

    // Audio-thread scratch — pre-allocated in the constructor (sized to
    // `kMaxScratchSamples`, defined at file-scope in InputMixer.cpp). M5
    // Session 1: `processBuffer` copies the inbound byte stream into this
    // float buffer, calls `ChannelStrip<Audio>::process` for Audio channels,
    // and copies the processed result back into the TapeWriteMessage. The
    // source `bytes` pointer is never mutated — DirectLayer's raw routes
    // read the same float buffers from AudioCallback and a write through
    // them would break the raw-monitor contract.
    std::vector<float> processingScratch_;
};

} // namespace sirius
