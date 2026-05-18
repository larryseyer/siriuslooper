#include "sirius/OutputMixer.h"

#include <cassert>

namespace sirius
{

OutputMixer::OutputMixer() = default;
OutputMixer::~OutputMixer() = default;

ChannelId OutputMixer::addChannel (SignalType)
{
    assert (false && "OutputMixer::addChannel — M3-M5 stub");
    return ChannelId { 0 };
}

void OutputMixer::routeChannelToOutput (ChannelId)
{
    assert (false && "OutputMixer::routeChannelToOutput — M3-M5 stub");
}

void OutputMixer::renderBuffer()
{
    assert (false && "OutputMixer::renderBuffer — M3-M5 stub");
}

} // namespace sirius
