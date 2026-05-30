#include "ida/IPayloadCodec.h"

namespace ida {

void TapeCodecRegistry::registerCodec(std::shared_ptr<IPayloadCodec> codec)
{
    if (!codec)
        return;

    const auto key = static_cast<std::uint16_t>(codec->codecId());
    codecs_[key]   = std::move(codec);
}

IPayloadCodec* TapeCodecRegistry::codecFor(TapeCodecId id) const noexcept
{
    const auto it = codecs_.find(static_cast<std::uint16_t>(id));
    if (it == codecs_.end())
        return nullptr;

    return it->second.get();
}

} // namespace ida
