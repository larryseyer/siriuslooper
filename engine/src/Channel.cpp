#include "sirius/Channel.h"

#include "sirius/ProcessingChain.h"

namespace sirius
{

Channel::Channel (ChannelId id_,
                  SignalType signalType_,
                  InputId source_,
                  TapeMode tapeMode_)
    : id (id_),
      signalType (signalType_),
      source (source_),
      tapeMode (tapeMode_),
      processing (makeProcessingChain (signalType_))
{
}

} // namespace sirius
