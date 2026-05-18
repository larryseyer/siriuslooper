#pragma once

#include "sirius/Channel.h"
#include "sirius/SignalType.h"

namespace sirius
{

/// V3 §2.2 / V7 alignment plan M2: the output-side mixer. Symmetric to
/// `InputMixer`; sits between Constituent rendering and the physical
/// output layer, owns the output-channel registry, and (in M3+)
/// dispatches the audio-thread render through per-channel strips and
/// session-level effect buses.
///
/// Session 2 declares the surface only. Every body in OutputMixer.cpp
/// asserts false (V7 alignment plan M2 Risks note line 257).
/// `ConstituentRef`, `DirectLayerRef`, `ChannelStripConfig`,
/// `OutputDestination`, `ConstituentRenders`, `DirectInputs`,
/// `OutputBuffers`, snapshot types — none exist yet; their places are
/// held by `// M3:` comments.
class OutputMixer
{
public:
    OutputMixer();
    ~OutputMixer();

    // Channel registry (typically auto-created from active Constituents) ----
    ChannelId addChannel (/* M3: ConstituentRef|DirectLayerRef, */ SignalType);
    // M3: void setChannelStrip (ChannelId, ChannelStripConfig);

    // Bus and send/return ---------------------------------------------------
    // M3: BusId addBus (BusConfig, EffectChain);
    // M3: void routeChannelToBus (ChannelId, BusId, SendLevel);

    // Output routing --------------------------------------------------------
    void routeChannelToOutput (ChannelId /* M3: , OutputDestination */);

    // Mix snapshots (Constituent subtype — lands with the MixSnapshot work)
    // M3+: SnapshotId captureSnapshot (string name);
    // M3+: void recallSnapshot (SnapshotId, TransitionType, Duration);

    // Audio-thread interface (real-time safe in M3+) -----------------------
    void renderBuffer (/* M3: const ConstituentRenders&, const DirectInputs&, OutputBuffers& */);
};

} // namespace sirius
