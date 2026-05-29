#include "OttoStripRebind.h"

#include "ida/OttoHost.h"
#include "ida/OutputMixer.h"

namespace ida::app
{

void rebindOttoChannelsAfterImport (OutputMixer& mix,
                                    OttoHost& host,
                                    std::unordered_map<int, OutputChannelId>& ottoMap)
{
    const int total = mix.channelCount();
    for (int i = 0; i < total; ++i)
    {
        const auto chId = mix.channelIdAt (i);
        if (chId.value() == 0) continue;

        const int ottoSource = mix.channelOttoSource (chId);
        if (ottoSource < 0) continue;        // phrase channel — leave alone

        const float* const leftSrc  = host.getOttoOutputLeft  (ottoSource);
        const float* const rightSrc = host.getOttoOutputRight (ottoSource);
        if (leftSrc == nullptr || rightSrc == nullptr) continue;  // host not prepared

        mix.setChannelAudioSource (chId, leftSrc, rightSrc);
        ottoMap.insert_or_assign (ottoSource, chId);
    }
}

} // namespace ida::app
