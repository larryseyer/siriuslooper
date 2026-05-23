#pragma once

#include "ida/Bus.h"
#include "ida/Channel.h"
#include "ida/ChannelStrip.h"
#include "ida/EffectChain.h"
#include "ida/IEffectChainHost.h"
#include "ida/MixerGraph.h"
#include "ida/MixerGraphState.h"
#include "ida/SignalType.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ida
{

/// V3 §2.2 / V7 alignment plan M5: the output-side mixer. Symmetric to
/// `InputMixer`; sits between Constituent rendering and the physical
/// output layer, owns the output-channel registry + per-channel strips,
/// and (in M5 Session 3) dispatches the audio-thread render through
/// per-channel strips, send/return architecture, and session-level
/// effect buses including the master bus.
///
/// Session 3 (this file) fills the audio-thread render body. The four-step
/// traversal: (1) scratch-mix each registered output channel from its
/// input source through its `ChannelStrip<Audio>`; (2) for each
/// (channel, bus) send level > 0, accumulate the scaled scratch into the
/// target bus's mixBuffer; (3) for each non-master bus in topological
/// evaluation order (sources before destinations, from the routing graph),
/// invoke `Bus::process` which routes the bus mixBuffer into its graph
/// main-out destination — the master bus, a subgroup bus, or the terminal —
/// at unity; (4) the master bus writes its mixBuffer additively into the
/// physical output channels.
///
/// M5 auto-registration policy: OutputMixer comes up EMPTY. MainComponent
/// does not auto-register channels — operator UX for mixer config is
/// M22+. Result: in M5, the OutputMixer path is a true no-op until tests
/// or future code calls `addChannel`. This is intentional. DirectLayer's
/// bypass path provides the default monitoring; OutputMixer is the
/// produced-mix path that becomes meaningful once Constituents render
/// into channels (M6+).
///
/// M5 channel audio source: per channel, the OutputMixer's audio source
/// is the corresponding input device channel (one-to-one, same index as
/// DirectLayer's raw routes). This is a pre-Constituent-rendering proxy
/// — M6+ replaces it with real Constituent renders.
///
/// Threading contract (inherited from M4, see continue.md constraint #6):
/// every configuration mutator (`addChannel`, `setChannelStrip`, `addBus`,
/// `setBusEffectChain`, `routeChannelToBus`) is message-thread only and
/// must complete before the audio thread starts calling `renderBuffer`.
/// There is no atomic-snapshot publish in M5 — operator-facing mutation
/// during a live audio callback is a post-M5 concern.
class OutputMixer
{
public:
    /// Hard ceiling for the pre-allocated send-level matrix. 32 channels
    /// covers any realistic IDA session; raises require re-auditing
    /// RT_SAFETY_CONTRACT §6 for OutputMixer::renderBuffer once S3 lands.
    static constexpr int kMaxOutputChannels = 32;

    /// Hard ceiling for the bus registry. 64 buses comes from V3 (per
    /// continue.md RT-safety note line 165: "the V3 decision caps buses
    /// at 64"). Includes the master bus at `BusId{0}`.
    static constexpr int kMaxBuses = 64;

    OutputMixer();
    ~OutputMixer();

    // Channel registry --------------------------------------------------------

    /// Registers a new output channel. The signal-type argument is held for
    /// the Session 3 audio-thread dispatch (which routes Audio channels
    /// through `ChannelStrip<Audio>` and skips other modalities until their
    /// real-DSP milestones land). Returns the fresh OutputChannelId — the
    /// promoted return type per V7 plan M5 (was a placeholder `ChannelId`
    /// in the M2 skeleton; promoted now per continue.md constraint #1).
    OutputChannelId addChannel (SignalType type);

    /// Message-thread setter — the OutputMixer takes ownership of the strip.
    /// Each registered output channel gets its own strip; the strip is the
    /// per-channel gain/pan/EQ/dynamics processor that S3 invokes from the
    /// audio thread. Calling with an unknown OutputChannelId is a no-op.
    void setChannelStrip (OutputChannelId id,
                          std::unique_ptr<ChannelStrip<SignalType::Audio>> strip);

    // Bus and send/return -----------------------------------------------------

    /// Adds a session-level effect bus and returns its fresh BusId starting
    /// at `BusId{1}` (`BusId{0}` is the master bus, auto-created in the
    /// constructor). Pre-allocates the Bus's mix scratch in its constructor
    /// — no audio-thread allocation. Returns `BusId{0}` if the bus registry
    /// is full (caller asked for more than `kMaxBuses - 1` aux buses); the
    /// master bus is always present so this is a safe sentinel.
    BusId addBus (BusConfig config);

    /// Message-thread setter — copies the chain into the named bus. No-op
    /// if the BusId is unknown.
    void setBusEffectChain (BusId id, EffectChain chain);

    /// Message-thread setter — forwards the audio-thread effect-chain
    /// host to every bus currently registered AND stashes the pointer so
    /// any future `addBus` call wires its new bus up at registration
    /// time (M7 S3). Pass `nullptr` to disable dispatch — every bus
    /// then falls back to the M5 inline path.
    ///
    /// Set-once before the audio thread starts (same contract as
    /// `setBusEffectChain`). The OutputMixer does NOT own the host;
    /// lifetime is the caller's responsibility (test, or future
    /// MainComponent wiring post-M7).
    void setEffectChainHost (IEffectChainHost* host) noexcept;

    /// Sets the send level from `channel` into `bus`. `sendLevel` is linear
    /// in [0, 1]; values outside the range are clamped. Routing to
    /// `BusId{0}` (master) sets the channel's master direct level; newly
    /// registered channels default to 1.0 into the master so they're
    /// audible at unity without explicit configuration. No-op if either id
    /// is unknown.
    void routeChannelToBus (OutputChannelId channel, BusId bus, float sendLevel);

    /// Routes a bus's main-out into another bus (subgroup) via the routing
    /// graph. Returns false if either id is unknown or the assignment would
    /// create a cycle (delegates to MixerGraph::setMainOut). Default main-out
    /// for an aux bus is the master.
    bool routeBusToBus (BusId from, BusId to);

    /// Message-thread accessor for the send-level matrix entry. Returns 0
    /// if either id is unknown. Primary use: tests + S3 audio-thread
    /// traversal that reads send levels into the mix.
    float sendLevelFor (OutputChannelId channel, BusId bus) const noexcept;

    // Persistence (routing-graph Phase 5) -------------------------------------

    /// Message-thread snapshot of the routing graph for persistence (Phase 5).
    /// buses[0] is the master (BusId 0). Message-thread only.
    OutputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction from a snapshot. The master bus already
    /// exists (ctor); its insert chain is applied, never re-added. Call on a
    /// freshly-constructed mixer. Message-thread only.
    void importGraphState (const OutputMixerGraphState&);

    // Mix snapshots (Constituent subtype — lands with the MixSnapshot work)
    // M6+: SnapshotId captureSnapshot (string name);
    // M6+: void recallSnapshot (SnapshotId, TransitionType, Duration);

    // Audio-thread interface (real-time safe in M5 Session 3+) ---------------

    /// Audio-thread render entry. M5 Session 3 body: channel-strip →
    /// send-matrix → bus-process → master-bus → output traversal.
    ///
    /// Per-channel audio source (M5): `inputChannelData[channelIndex]`
    /// where `channelIndex = OutputChannelId.value() - 1`. Channels
    /// without a matching input device channel are silent. M6+ replaces
    /// this with Constituent renders.
    ///
    /// `const noexcept` per V7 plan line 386 + continue.md constraint #4:
    /// `renderBuffer` is a function of state, not a state mutator. All
    /// state mutation lives in message-thread setters; the audio thread
    /// only reads.
    ///
    /// Output write semantics: ADDITIVE into the physical `output[c]`
    /// buffers. DirectLayer (bypass path) and OutputMixer (produced-mix
    /// path) both write additively into the same output buffers; the
    /// caller (AudioCallback) zeroes the outputs once at the start of
    /// the buffer.
    ///
    /// The signature stays JUCE-free for the same reason `ChannelStrip::
    /// process` does.
    void renderBuffer (const float* const* inputChannelData,
                       int                 numInputChannels,
                       float* const*       outputChannelData,
                       int                 numOutputChannels,
                       int                 numSamples) const noexcept;

private:
    struct ChannelEntry
    {
        OutputChannelId id;
        SignalType      signalType;
        std::unique_ptr<ChannelStrip<SignalType::Audio>> strip;
    };

    /// Indexes into `sendMatrix_` for the (channel, bus) pair. Channel
    /// indices are derived from `OutputChannelId.value() - 1` because the
    /// id stream starts at 1 (id 0 is reserved). Bus indices are
    /// `BusId.value()` directly because the master bus is `BusId{0}`.
    /// Returns `kMaxOutputChannels * kMaxBuses` (out of bounds) when
    /// either id is past the corresponding ceiling — callers must
    /// bounds-check before indexing.
    std::size_t sendMatrixIndex (OutputChannelId channel, BusId bus) const noexcept;

    MixerGraph                graph_ { MixerTerminal::HardwareOutput };
    std::vector<MixerNodeId>  channelNodeIds_; // parallel to channels_
    std::vector<MixerNodeId>  busNodeIds_;     // parallel to buses_ (index 0 = master)

    std::vector<ChannelEntry> channels_;
    std::vector<Bus>          buses_;

    /// Dense send-level matrix sized `kMaxOutputChannels * kMaxBuses` in
    /// the constructor. 32 × 64 = 2048 floats = 8 KB total — small enough
    /// to fit comfortably in L1 for the S3 audio-thread traversal.
    std::vector<float>        sendMatrix_;

    /// Per-channel post-strip scratch — sized
    /// `kMaxOutputChannels * kMaxBlockSamples` in the constructor. The
    /// audio-thread `renderBuffer` writes the post-`ChannelStrip` signal
    /// for each registered output channel here, then reads back from it
    /// during the send-matrix accumulation step. `mutable` because
    /// `renderBuffer` is `const` (per V7 plan line 386) but the scratch
    /// is implementation detail the audio thread owns end-to-end. Matches
    /// the `InputMixer::processingScratch_` pattern.
    mutable std::vector<float> channelScratch_;

    std::int64_t nextOutputChannelId_ { 1 };
    std::int64_t nextBusId_ { 1 }; // BusId{0} is the master, auto-created.

    /// Stashed `IEffectChainHost*` (M7 S3) — null = no audio-thread
    /// dispatch, every bus falls back to the M5 inline path. Forwarded
    /// to every bus by `setEffectChainHost`, and to any new bus on
    /// `addBus`. Lifetime is the caller's responsibility.
    IEffectChainHost* effectChainHost_ { nullptr };
};

} // namespace ida
