#pragma once

#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/ChannelStrip.h"
#include "sirius/EffectChain.h"
#include "sirius/SignalType.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sirius
{

/// V3 §2.2 / V7 alignment plan M5: the output-side mixer. Symmetric to
/// `InputMixer`; sits between Constituent rendering and the physical
/// output layer, owns the output-channel registry + per-channel strips,
/// and (in M5 Session 3) dispatches the audio-thread render through
/// per-channel strips, send/return architecture, and session-level
/// effect buses including the master bus.
///
/// Session 2 (this file) establishes the configuration surfaces and the
/// data structures so Session 3 can plug them in. The audio-thread entry
/// point `renderBuffer` remains stubbed in S2; S3 fills the body.
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
    /// covers any realistic Sirius session; raises require re-auditing
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

    /// Sets the send level from `channel` into `bus`. `sendLevel` is linear
    /// in [0, 1]; values outside the range are clamped. Routing to
    /// `BusId{0}` (master) sets the channel's master direct level; newly
    /// registered channels default to 1.0 into the master so they're
    /// audible at unity without explicit configuration. No-op if either id
    /// is unknown.
    void routeChannelToBus (OutputChannelId channel, BusId bus, float sendLevel);

    /// Message-thread accessor for the send-level matrix entry. Returns 0
    /// if either id is unknown. Primary use: tests + S3 audio-thread
    /// traversal that reads send levels into the mix.
    float sendLevelFor (OutputChannelId channel, BusId bus) const noexcept;

    // Output routing ----------------------------------------------------------
    // M5+: physical-output routing — kept as a stub for compatibility with
    // the M2 skeleton. Session 3 may delete or repurpose this when the
    // OutputDestination type lands.
    void routeChannelToOutput (OutputChannelId channel);

    // Mix snapshots (Constituent subtype — lands with the MixSnapshot work)
    // M6+: SnapshotId captureSnapshot (string name);
    // M6+: void recallSnapshot (SnapshotId, TransitionType, Duration);

    // Audio-thread interface (real-time safe in M5 Session 3+) ---------------

    /// Audio-thread render entry. M5 Session 2 leaves the body stubbed;
    /// Session 3 fills it with the channel-strip → send-matrix → bus-process
    /// → master-bus traversal. The signature stays JUCE-free for the same
    /// reason `ChannelStrip::process` does.
    void renderBuffer (float* const* output, int numChannels, int numSamples) noexcept;

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

    std::vector<ChannelEntry> channels_;
    std::vector<Bus>          buses_;

    /// Dense send-level matrix sized `kMaxOutputChannels * kMaxBuses` in
    /// the constructor. 32 × 64 = 2048 floats = 8 KB total — small enough
    /// to fit comfortably in L1 for the S3 audio-thread traversal.
    std::vector<float>        sendMatrix_;

    std::int64_t nextOutputChannelId_ { 1 };
    std::int64_t nextBusId_ { 1 }; // BusId{0} is the master, auto-created.
};

} // namespace sirius
