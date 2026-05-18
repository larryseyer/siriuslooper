#include "sirius/InputMixer.h"

#include <cassert>

namespace sirius
{

InputMixer::InputMixer() = default;
InputMixer::~InputMixer() = default;

void InputMixer::registerInput (InputId, const InputDescriptor&)
{
    assert (false && "InputMixer::registerInput — M3-M5 stub");
}

void InputMixer::setInputRawDirect (InputId, bool)
{
    assert (false && "InputMixer::setInputRawDirect — M3-M5 stub");
}

void InputMixer::setInputEnabled (InputId, bool)
{
    assert (false && "InputMixer::setInputEnabled — M3-M5 stub");
}

ChannelId InputMixer::addChannel (InputId, SignalType)
{
    assert (false && "InputMixer::addChannel — M3-M5 stub");
    return ChannelId { 0 };
}

void InputMixer::removeChannel (ChannelId)
{
    assert (false && "InputMixer::removeChannel — M3-M5 stub");
}

void InputMixer::setChannelTapeMode (ChannelId, TapeMode)
{
    assert (false && "InputMixer::setChannelTapeMode — M3-M5 stub");
}

void InputMixer::processBuffer()
{
    assert (false && "InputMixer::processBuffer — M3-M5 stub");
}

} // namespace sirius
