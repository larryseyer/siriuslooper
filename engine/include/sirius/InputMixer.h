#pragma once

#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/ChannelDefaults.h"
#include "sirius/InputDescriptor.h"
#include "sirius/ITapeSink.h"
#include "sirius/MixerGraph.h"
#include "sirius/MixerGraphState.h"
#include "sirius/SignalType.h"
#include "sirius/TapeId.h"
#include "sirius/TapeMode.h"

#include <cstddef>
#include <cstdint>
#include <string>
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

    static constexpr int kMaxInputChannels = 32;
    static constexpr int kMaxInputBuses    = 64;
    static constexpr int kMaxTapes         = 64;

    BusId addBus (BusConfig config);
    BusId addFxReturn (const std::string& name);

    int     busCount()    const noexcept;
    BusId   busIdAt (int index)   const noexcept;
    BusKind busKindAt (int index) const noexcept;
    bool    channelIsRegisteredInGraph (ChannelId) const noexcept;
    bool    busMainOutIsTape (BusId) const noexcept;

    /// Where a node's single full-level main-out goes. Tape = capture terminal
    /// (primary/default); HardwareOutput = RME-TotalMix direct-out monitoring;
    /// Bus = a subgroup. (Input channels never route to "master" — there is none.)
    enum class MainOutDest { Tape, HardwareOutput, Bus };

    bool setChannelMainOutToBus (ChannelId, BusId);
    bool setChannelMainOutToHardwareOutput (ChannelId);
    bool setChannelMainOutToTape (ChannelId);
    bool setBusMainOutToBus (BusId from, BusId to);
    bool setBusMainOutToHardwareOutput (BusId);
    bool setBusMainOutToTape (BusId);
    MainOutDest channelMainOut (ChannelId) const noexcept;
    MainOutDest busMainOut (BusId) const noexcept;

    /// The specific bus a node's main-out targets, or BusId{0} (invalid sentinel)
    /// when the main-out is not a bus (tape / hardware output) or the node is
    /// unknown. Complements channelMainOut/busMainOut (which return only the
    /// MainOutDest category). Message-thread only.
    BusId channelMainOutBus (ChannelId) const noexcept;
    BusId busMainOutBus (BusId) const noexcept;

    /// True iff routing `from`'s main-out to bus `to` would close a feedback
    /// cycle (so the UI can omit it from the picker). Non-mutating wrapper over
    /// MixerGraph::wouldMainOutCycle. False for unknown ids. Message-thread only.
    bool busMainOutToBusWouldCycle (BusId from, BusId to) const noexcept;

    // Multi-tape terminal registry (tape subsystem slice 2) -----------------
    /// Registers a Tape terminal for a pooled tape. The eventual owner (slice 4)
    /// keeps this in sync with the project TapePool. Returns false on a duplicate
    /// id or when kMaxTapes is exceeded. Message-thread only.
    bool addTape (TapeId);
    /// Unregisters a Tape terminal. Returns false for an unknown id or the
    /// primary tape (TapeId{1} — the permanent default). Nodes routed to the
    /// removed tape fall back to the primary tape. Message-thread only.
    bool removeTape (TapeId);
    int  tapeCount() const noexcept;
    bool hasTape (TapeId) const noexcept;

    /// Routes a node's main-out to a specific pooled tape. Returns false if the
    /// node or the tape is unknown, or the edge is invalid. The no-arg overloads
    /// target the PRIMARY tape, behavior-preserving.
    bool setChannelMainOutToTape (ChannelId, TapeId);
    bool setBusMainOutToTape (BusId, TapeId);

    /// True iff the node's main-out targets this specific pooled tape.
    bool channelMainOutIsTape (ChannelId, TapeId) const noexcept;
    bool busMainOutIsTape (BusId, TapeId) const noexcept;

    bool  setChannelSend (ChannelId, BusId fxReturn, float level);
    bool  setBusSend (BusId source, BusId fxReturn, float level);
    float channelSendLevel (ChannelId, BusId fxReturn) const noexcept;

    /// Message-thread setter — copies the chain into the named bus (parity
    /// with OutputMixer::setBusEffectChain). No-op if the BusId is unknown.
    void setBusEffectChain (BusId id, EffectChain chain);

    /// Message-thread accessor — the live Bus for `id`, or nullptr if unknown.
    /// Same linear-scan idiom as `setBusEffectChain`. The Input Mixer UI uses
    /// this to drive a bus/FX-return strip's fader/mute and read its peak/LUFS
    /// meter.
    /// NOT for the audio thread (the bus is held by value in a reserved vector;
    /// the pointer is stable for the bus's lifetime within this mixer).
    Bus* busForId (BusId id) noexcept;

    /// Message-thread snapshot of the entire routing graph for persistence
    /// (routing-graph Phase 5). Reads buses, FX returns, per-node main-outs,
    /// sends, channel input sources, tape modes, and every node's insert chain.
    /// Message-thread only — never call from the audio thread.
    InputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction of the routing graph from a snapshot.
    /// Replays buses/channels with their persisted ids, then main-outs, sends,
    /// and insert chains. Call on a freshly-constructed mixer; the ctor's RVB/DLY
    /// FX returns are reused (not re-created) when the snapshot carries their ids.
    /// Message-thread only.
    void importGraphState (const InputMixerGraphState&);

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

    /// Injects the per-tape capture sink (tape subsystem slice 2). Set-once on
    /// the message thread before the audio device starts; non-owning. When unset,
    /// renderInputGraph drops tape-routed signal (no capture). Slice 3 binds a
    /// real per-tape sink in MainComponent.
    void setTapeSink (ITapeSink* sink) noexcept;

    // Input-layer registry --------------------------------------------------
    void registerInput (InputId, const InputDescriptor&);
    void setInputRawDirect (InputId, bool enabled);
    void setInputEnabled (InputId, bool enabled);
    void setInputDefaults (InputId, ChannelDefaults defaults);

    // Channel registry ------------------------------------------------------
    ChannelId addChannel (InputId source, SignalType type);
    void removeChannel (ChannelId);
    void setChannelTapeMode (ChannelId, TapeMode);

    // Mixer-strip input source (whitepaper §6.1/§6.2) -----------------------
    /// Assigns which physical device channel(s) feed this mixer channel's
    /// stereo strip. `stereo` true → `leftDeviceChannel`/`rightDeviceChannel`
    /// map to the strip's L/R. `stereo` false → `leftDeviceChannel` is a mono
    /// source presented dual-mono (both sides) and positioned by the strip's
    /// pan; `rightDeviceChannel` is ignored. Message-thread setter, set during
    /// setup before the audio device starts (same contract as registerInput /
    /// addChannel); the audio thread only reads the source map in
    /// `processDeviceInputs`. The channel is always stereo internally — this
    /// never creates a mono channel (the hard stereo-only invariant holds).
    void setChannelInputSource (ChannelId, int leftDeviceChannel,
                                int rightDeviceChannel, bool stereo) noexcept;

    // Audio-thread interface (real-time safe) ------------------------------
    /// Walks the channel, applies its ProcessingChain (no-op in M3), and
    /// if the channel is tape-bearing, memcpys `bytes[0..byteCount]` into a
    /// `TapeWriteMessage` and enqueues on the bound TapeWriter. On
    /// queue-full, calls `OverloadProtection::reportLoad(1.0)` and drops.
    /// No allocations, no locks, no I/O on this path.
    /// NOTE: renderInputGraph is the live audio-callback entry point as of the
    /// tape subsystem and supersedes this method for production capture/metering;
    /// this method remains for direct unit testing of the strip path.
    void processBuffer (ChannelId, const std::byte* bytes, std::size_t byteCount) noexcept;

    /// Audio-thread strip processing + metering for the Input Mixer UI.
    /// For each channel that has an input source (see setChannelInputSource),
    /// gathers its 1–2 source device channels out of `deviceIn` into a stereo
    /// scratch (mono sources duplicated to both sides, dual-mono) and runs
    /// `ChannelStrip<Audio>::process` on it, publishing post-fader peak meters
    /// the UI reads on its timer. The device buffers are never mutated (the
    /// raw-monitor contract). Channels without a source descriptor — and any
    /// source whose device-channel index is outside [0, numDeviceChannels) —
    /// are skipped. No allocations, no locks, no I/O on this path.
    /// NOTE: renderInputGraph is the live audio-callback entry point as of the
    /// tape subsystem and supersedes this method for production capture/metering;
    /// this method remains for direct unit testing of the strip path.
    void processDeviceInputs (const float* const* deviceIn,
                              int numDeviceChannels, int numSamples) noexcept;

    /// Audio-thread render of the full input routing graph. Gathers each channel's
    /// device source through its ChannelStrip, routes each node's main-out to its
    /// destination (bus mix buffer / tape enqueue / direct-out) and its sends into
    /// FX returns, walking the graph's topological evaluation order. Tape delivery
    /// enqueues a stereo-interleaved TapeWriteMessage; hardware-output delivery
    /// accumulates into directOut. RT-safe: no alloc, no locks, no I/O, noexcept.
    /// directOut may be null / 0 channels when no hardware-output route is active.
    void renderInputGraph (const float* const* deviceIn, int numDeviceChannels,
                           float* const* directOut, int numDirectOutChannels,
                           int numSamples) noexcept;

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

    /// Which device channel(s) feed a mixer channel's stereo strip. `stereo`
    /// false → `left` is a mono source duplicated to both sides; `right` is
    /// unused. See setChannelInputSource.
    struct ChannelInputSource
    {
        int left;
        int right;
        bool stereo;
    };

    std::unordered_map<std::int64_t, InputState> inputs_;
    std::unordered_map<std::int64_t, Channel> channels_;
    std::unordered_map<std::int64_t, ChannelInputSource> channelSources_;
    std::int64_t nextChannelId_ { 1 };

    MixerGraph graph_ { { MixerTerminal::Tape, MixerTerminal::HardwareOutput } };
    std::unordered_map<std::int64_t, MixerNodeId> channelNodeIds_;
    std::vector<Bus>          buses_;
    std::vector<MixerNodeId>  busNodeIds_;
    std::int64_t              nextBusId_ { 1 };

    struct TapeTerminal { std::int64_t tapeId; MixerNodeId node; };
    std::vector<TapeTerminal> tapeTerminals_; // [0] = primary (TapeId 1); >= 1

    MixerNodeId nodeForBus (BusId) const noexcept;
    MixerNodeId nodeForChannel (ChannelId) const noexcept;
    MainOutDest classifyMainOut (MixerNodeId dest) const noexcept;
    BusId       busIdForNode (MixerNodeId) const noexcept; // BusId{0} if not a bus node
    MixerNodeId tapeNodeFor (TapeId) const noexcept;        // invalid id if absent
    int         tapeSlotForNode (MixerNodeId) const noexcept; // -1 if not a tape terminal

    void applyChannelMainOut (ChannelId, const MixerMainOut&);
    void applyBusMainOut (BusId, const MixerMainOut&);

    MixerMainOut mainOutSnapshot (MixerNodeId node) const noexcept;
    std::vector<MixerSend> sendSnapshot (MixerNodeId node) const;
    const EffectChain* channelInsertChain (ChannelId) const noexcept;

    void accumulateIntoBus (MixerNodeId busNode, const float* left, const float* right,
                            float level, int numSamples) noexcept;
    void accumulateIntoTape (int slot, const float* left, const float* right,
                             float level, int numSamples) noexcept;

    TapeWriter* tapeWriter_ { nullptr };
    OverloadProtection* overload_ { nullptr };
    sirius::persistence::TapeStore* tapeStore_ { nullptr };
    NotificationBus* notificationBus_ { nullptr };
    ITapeSink* tapeSink_ { nullptr };

    // Audio-thread scratch — pre-allocated in the constructor (sized to
    // `kMaxScratchSamples`, defined at file-scope in InputMixer.cpp). M5
    // Session 1: `processBuffer` copies the inbound byte stream into this
    // float buffer, calls `ChannelStrip<Audio>::process` for Audio channels,
    // and copies the processed result back into the TapeWriteMessage. The
    // source `bytes` pointer is never mutated — DirectLayer's raw routes
    // read the same float buffers from AudioCallback and a write through
    // them would break the raw-monitor contract.
    std::vector<float> processingScratch_;

    // Audio-thread stereo-gather scratch for `processDeviceInputs` — two rows
    // (L/R), each `kMaxScratchSamples`, pre-allocated in the constructor. The
    // gather copies a channel's source device channel(s) here (a mono source
    // is copied to both rows, dual-mono) so `ChannelStrip<Audio>::process`
    // runs on a stereo block. Separate from `processingScratch_` so the tape
    // path and the metering path never share a buffer.
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;

    // Per-tape summing scratch for renderInputGraph — kMaxTapes rows of
    // kMaxScratchSamples, pre-allocated in the constructor. Each pooled tape's
    // slot accumulates every node routed to it; the touched slots are delivered
    // once per block via tapeSink_. Indexed by tape-terminal slot
    // (tapeTerminals_ order). RT-safe: never resized after construction.
    std::vector<std::vector<float>> tapeMixLeft_;
    std::vector<std::vector<float>> tapeMixRight_;
    std::vector<char>               tapeTouched_;
};

} // namespace sirius
