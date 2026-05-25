#pragma once

#include "ida/Bus.h"
#include "ida/Channel.h"
#include "ida/ChannelDefaults.h"
#include "ida/DirectLayer.h"
#include "ida/InputDescriptor.h"
#include "ida/ITapeSink.h"
#include "ida/MixerGraph.h"
#include "ida/MixerGraphState.h"
#include "ida/MonitorMode.h"
#include "ida/SignalType.h"
#include "ida/TapeId.h"
#include "ida/TapeMode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ida { namespace persistence { class TapeStore; } }

namespace ida
{

class NotificationBus;
class OutputMixer;
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
    /// Non-default — the destructor must remove every auto-created MON
    /// channel from the attached OutputMixer, otherwise OutputMixer's
    /// `setChannelAudioSource` pointers outlive the InputMixer's
    /// `postStrip_` storage they reference (the project-load path
    /// destroys+rebuilds the InputMixer in place, and OutputMixer is owned
    /// by MainComponent so it survives the swap).
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
    /// Bus-to-bus send level (linear 0..1). Mirrors `channelSendLevel` but
    /// addresses a source bus. Returns 0 when either bus is unknown or no
    /// send edge exists. Message-thread accessor.
    float busSendLevel (BusId source, BusId fxReturn) const noexcept;

    /// Slice E2 (2026-05-24) — per-channel pre/post-fader send mode. Mirror
    /// of `OutputMixer::channelSendIsPreFader`: default false = post-fader
    /// (today's behavior; channel's strip applies before the FX-return send
    /// tap). True = pre-fader (the send tap samples the channel's pre-strip
    /// scratch so a muted channel still feeds its FX returns). One toggle
    /// per channel covering all of that channel's sends. Unknown ids return
    /// false. Message-thread accessor.
    bool channelSendIsPreFader (ChannelId) const noexcept;

    /// Message-thread setter for `channelSendIsPreFader`. Unknown ids are a
    /// silent no-op (defensive). Set before the audio device starts; the
    /// audio thread only reads.
    void setChannelSendIsPreFader (ChannelId, bool preFader) noexcept;

    /// Message-thread setter — copies the chain into the named bus (parity
    /// with OutputMixer::setBusEffectChain). No-op if the BusId is unknown.
    void setBusEffectChain (BusId id, EffectChain chain);

    /// Message-thread setter — forwards the audio-thread effect-chain host
    /// to every bus currently registered AND stashes the pointer so any
    /// future `addBus`/`addFxReturn` call wires its new bus up at registration
    /// time (P7 T3a I-2 — mirrors OutputMixer::setEffectChainHost). Pass
    /// `nullptr` to disable dispatch — every bus then falls back to the M5
    /// inline path.
    ///
    /// Set-once before the audio thread starts (same contract as
    /// `setBusEffectChain`). The InputMixer does NOT own the host; lifetime
    /// is the caller's responsibility (test, or MainComponent's
    /// `effectChainHost_` member).
    void setEffectChainHost (IEffectChainHost* host) noexcept;

    /// Message-thread accessor — the live Bus for `id`, or nullptr if unknown.
    /// Same linear-scan idiom as `setBusEffectChain`. The Input Mixer UI uses
    /// this to drive a bus/FX-return strip's fader/mute and read its peak/LUFS
    /// meter.
    /// NOT for the audio thread (the bus is held by value in a reserved vector;
    /// the pointer is stable for the bus's lifetime within this mixer).
    Bus* busForId (BusId id) noexcept;

    /// Renames a bus or FX-return. Writes the new name into `BusConfig::name`.
    /// Returns false for unknown ids (the invalid sentinel `BusId{0}` included
    /// — input-side buses always start at 1; there is no master concept on the
    /// input side, so every real bus is renameable). Message-thread only.
    /// Mirrors `OutputMixer::renameBus`.
    bool renameBus (BusId id, std::string newName);

    /// Message-thread snapshot of the entire routing graph for persistence
    /// (routing-graph Phase 5). Reads buses, FX returns, per-node main-outs,
    /// sends, channel input sources, tape modes, and every node's insert chain.
    /// Message-thread only — never call from the audio thread.
    InputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction of the routing graph from a snapshot.
    /// Replays buses/channels with their persisted ids, then main-outs, sends,
    /// and insert chains. Call on a freshly-constructed mixer; under the
    /// minimal-defaults rule the ctor seeds no buses, so every persisted bus is
    /// minted via addBus to reproduce its dense busId. Message-thread only.
    void importGraphState (const InputMixerGraphState&);

    // Injected non-owning collaborators (set-once on the message thread).
    void setTapeWriter (TapeWriter* writer) noexcept;
    void setOverloadProtection (OverloadProtection* overload) noexcept;
    void setTapeStore (ida::persistence::TapeStore* store) noexcept;

    /// Message-thread setter — binds the DirectLayer this input mixer
    /// historically drove per-channel monitor routes through (pre-V9). V9
    /// Slice 3 collapsed the MON path onto an auto-created OutputMixer
    /// channel (`attachOutputMixer`); the V9-conformance plan's Slice 4
    /// deletes DirectLayer entirely. Until then this setter remains so
    /// callers compile, but `setChannelMonitorMode` no longer touches
    /// `directLayer_`. Mirrors the set-once-non-owning pattern used by
    /// `setTapeSink` / `setNotificationBus`.
    void setDirectLayer (DirectLayer* layer) noexcept;

    /// V9 Slice 3 — wires the InputMixer to an OutputMixer. The MON button's
    /// lifecycle owns an auto-created channel on this OutputMixer; the
    /// InputMixer uses the OutputMixer's `setChannelAudioSource()` seam to
    /// hand off post-strip buffer pointers per whitepaper V9 §5.2 / §7.2.
    ///
    /// Non-owning. The OutputMixer must outlive the InputMixer. Set-once
    /// before the audio thread starts (same contract as `setDirectLayer`).
    /// When unset, `setChannelMonitorMode(.., On)` is a silent no-op at
    /// the route-lifecycle layer — the per-channel mode is still tracked
    /// so a later `attachOutputMixer` + replay can engage routes, but no
    /// OutputMixer channel is minted in the meantime.
    void attachOutputMixer (OutputMixer* output) noexcept;
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

    // V9 Slice 3 — per-channel MON toggle (whitepaper V9 §5.2 / §7.2). Set on
    // the message thread; the InputMixer owns the lifecycle of an auto-created
    // OutputMixer channel that reads this channel's post-strip stereo buffer
    // (the seam exposed by `postStripPointer` + `OutputMixer::setChannelAudioSource`).
    // Unknown channel id = silent no-op (defensive, mirrors
    // `setChannelSendIsPreFader`). Default mode for every newly-added channel
    // is `MonitorMode::Off` — the operator opts in explicitly. Idempotent: a
    // second On call while already On does not mint a second OutputMixer
    // channel. Allocates on the OutputMixer's `addChannel` path; call before
    // the audio device starts, or while bracketed.
    void setChannelMonitorMode (ChannelId, MonitorMode);

    /// Message-thread accessor. Unknown id reads as `MonitorMode::Off`.
    MonitorMode channelMonitorMode (ChannelId) const noexcept;

    /// Message-thread accessor — the OutputChannelId of the auto-created
    /// monitor channel on the attached OutputMixer, or `std::nullopt` when
    /// MON is Off, the InputMixer has no OutputMixer attached, or the input
    /// ChannelId is unknown. Diagnostic / test seam.
    std::optional<OutputChannelId> channelMonitorOutputChannel (ChannelId) const noexcept;

    /// Message-thread accessor. Returns a stable pointer to the channel's
    /// post-strip output buffer for the requested side (0=L, 1=R), or
    /// `nullptr` if no channel with that id exists or `side` is out of
    /// range. The pointer remains valid until `removeChannel(id)` or
    /// destruction. Audio-thread readers (OutputMixer) cache the pointer
    /// once and re-read into it every block — `renderInputGraph` copies
    /// the strip's stereo output into this buffer in-place every call,
    /// so the data behind the pointer is fresh per block.
    ///
    /// V9 Slice 2 of the whitepaper-conformance plan introduced this seam;
    /// Slice 3 uses it to wire MON-on to an auto-created OutputMixer
    /// channel via `OutputMixer::setChannelAudioSource(monChannelId, L, R)`,
    /// replacing the DirectLayer's bypass-the-OutputMixer write path.
    const float* postStripPointer (ChannelId id, int side) const noexcept;

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
    /// Slice E2: per-channel pre-fader send mode. `char` (1/0) so reads from
    /// the audio thread are POD memory, not a `std::vector<bool>` proxy. An
    /// unset entry (channel never had its flag flipped) reads as
    /// post-fader (0) — same default as freshly minted channels.
    std::unordered_map<std::int64_t, char> channelPreFaderSends_;
    /// V9 Slice 3 — per-channel MON state. An entry is present only while
    /// MON is On for that channel (and the entry's `outputChannelId` is the
    /// auto-created OutputMixer channel reading this input's post-strip
    /// buffer). MON → Off removes the entry entirely. `outputChannelId`
    /// is `std::optional` because OutputChannelId has no "invalid" sentinel
    /// and a successfully-minted channel always carries a real id; an entry
    /// without an id encodes "MON was requested but no OutputMixer was
    /// attached at the time" — a later `attachOutputMixer` + replay path
    /// would consult these entries to engage the channels.
    struct MonitorRouteState
    {
        MonitorMode                    mode { MonitorMode::Off };
        std::optional<OutputChannelId> outputChannelId;
    };
    std::unordered_map<std::int64_t, MonitorRouteState> channelMonitorRoutes_;

    /// V9 Slice 2 — per-channel post-strip stereo output. Allocated on
    /// `addChannel` (message thread) and freed on `removeChannel`. The
    /// inner vector pair is the L/R buffer; pointers are stable for the
    /// channel's lifetime (the outer `unordered_map`'s `value_type` slot
    /// never moves once inserted, and the inner `std::vector<float>`'s
    /// `data()` pointer is stable as long as nobody resizes — and we
    /// never resize after the initial `assign` in `addChannel`).
    /// `renderInputGraph` memcpys the strip's L/R output into [0] and [1]
    /// of the entry every block. Audio-thread reads through the pointer
    /// returned by `postStripPointer(id, side)` are safe.
    std::unordered_map<std::int64_t, std::array<std::vector<float>, 2>> postStrip_;

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
    ida::persistence::TapeStore* tapeStore_ { nullptr };
    NotificationBus* notificationBus_ { nullptr };
    ITapeSink* tapeSink_ { nullptr };
    DirectLayer* directLayer_ { nullptr };
    /// V9 Slice 3 — non-owning, set-once. The OutputMixer the MON button
    /// auto-creates channels on. Lifetime is the caller's responsibility;
    /// the OutputMixer must outlive the InputMixer (see `~InputMixer`
    /// which sweeps every live MON channel from this OutputMixer).
    OutputMixer* outputMixer_ { nullptr };

    /// Stashed `IEffectChainHost*` (P7 T3a I-2) — null = no audio-thread
    /// dispatch, every bus falls back to the M5 inline path. Forwarded to
    /// every bus by `setEffectChainHost`, and to any new bus on `addBus`
    /// / `addFxReturn`. Lifetime is the caller's responsibility. Mirrors
    /// the OutputMixer::effectChainHost_ shape.
    IEffectChainHost* effectChainHost_ { nullptr };

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

    // Slice E2: pre-strip (pre-fader) scratch for `renderInputGraph`. Same
    // shape as `scratchLeft_/scratchRight_` (one stereo pair, reused per
    // channel as the loop iterates). `renderInputGraph` copies the source
    // into here BEFORE calling ChannelStrip::process on the post-fader
    // scratch, then picks per-channel which scratch feeds the send tap.
    // Allocated once in the ctor; 64 KB total at default kMaxScratchSamples.
    std::vector<float> scratchLeftPre_;
    std::vector<float> scratchRightPre_;

    // Per-tape summing scratch for renderInputGraph — kMaxTapes rows of
    // kMaxScratchSamples, pre-allocated in the constructor. Each pooled tape's
    // slot accumulates every node routed to it; the touched slots are delivered
    // once per block via tapeSink_. Indexed by tape-terminal slot
    // (tapeTerminals_ order). RT-safe: never resized after construction.
    std::vector<std::vector<float>> tapeMixLeft_;
    std::vector<std::vector<float>> tapeMixRight_;
    std::vector<char>               tapeTouched_;
};

} // namespace ida
