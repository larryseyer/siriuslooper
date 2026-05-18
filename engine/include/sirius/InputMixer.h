#pragma once

#include "sirius/Channel.h"
#include "sirius/InputDescriptor.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

namespace sirius
{

/// V3 §2.1 / V7 alignment plan M2: the input-side mixer. Sits between
/// physical input registration and the tape/direct-layer split, owns the
/// channel registry, and (in M3+) is responsible for dispatching audio-
/// thread work into per-channel processing chains.
///
/// Session 2 declares the surface only; every body in InputMixer.cpp
/// asserts false so any accidental call site in the M1 audio path is
/// loud, not silent (V7 alignment plan M2 Risks note line 257).
/// `ChannelConfig`, `BusId`, `InputBuffers`, `OutputDestinations`, and
/// `ChannelDefaults` aren't real types yet — M3 designs them; their
/// places in these signatures are held by `// M3:` comments rather than
/// speculative type names.
class InputMixer
{
public:
    InputMixer();
    ~InputMixer();

    // Input-layer registry --------------------------------------------------
    void registerInput (InputId, const InputDescriptor&);
    void setInputRawDirect (InputId, bool enabled);
    void setInputEnabled (InputId, bool enabled);
    // M3: void setInputDefaults (InputId, ChannelDefaults);

    // Channel registry ------------------------------------------------------
    ChannelId addChannel (InputId source, SignalType /* M3: ChannelConfig */);
    void removeChannel (ChannelId);
    // M3: void setChannelProcessing (ChannelId, ProcessingChain);
    void setChannelTapeMode (ChannelId, TapeMode);
    // M3: void setChannelDirectRouting (ChannelId, DirectRouting);
    // M3: void setChannelDestinations (ChannelId, span<TapeId|BusId>);

    // Bus registry ----------------------------------------------------------
    // M3: BusId addBus (BusConfig);
    // M3: void routeChannelToBus (ChannelId, BusId, SendLevel);

    // Audio-thread interface (real-time safe in M3+) -----------------------
    void processBuffer (/* M3: const InputBuffers&, OutputDestinations& */);
};

} // namespace sirius
