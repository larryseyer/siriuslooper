#include "ida/ProcessingChain.h"

#include "ida/ChannelStrip.h"

#include <cassert>

namespace sirius
{

std::unique_ptr<ProcessingChain> makeProcessingChain (SignalType type)
{
    switch (type)
    {
        case SignalType::Audio: return std::make_unique<ChannelStrip<SignalType::Audio>>();
        case SignalType::Midi:  return std::make_unique<MidiChain>();
        case SignalType::Video: return std::make_unique<VideoChain>();
        case SignalType::File:  return std::make_unique<FileChain>();
    }
    // The switch is exhaustive over the closed enum; this is unreachable but
    // silences compiler warnings on toolchains that don't notice exhaustion.
    assert (false && "makeProcessingChain — unhandled SignalType");
    return nullptr;
}

} // namespace sirius
