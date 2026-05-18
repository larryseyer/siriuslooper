#include "sirius/AudioCallback.h"

#include "sirius/Lmc.h"

#include <algorithm>
#include <cstring>

namespace sirius
{

AudioCallback::AudioCallback (EngineConfig config) noexcept
    : config_ (config)
{
}

void AudioCallback::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int                 numInputChannels,
    float* const*       outputChannelData,
    int                 numOutputChannels,
    int                 numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    const bool monitoring = monitoringEnabled_.load (std::memory_order_acquire);

    const int passThroughChannels =
        monitoring ? std::min (numInputChannels, numOutputChannels) : 0;

    for (int ch = 0; ch < passThroughChannels; ++ch)
    {
        auto* dst = outputChannelData[ch];
        auto* src = inputChannelData[ch];
        if (dst != nullptr && src != nullptr)
            std::memcpy (dst, src, static_cast<std::size_t> (numSamples) * sizeof (float));
        else if (dst != nullptr)
            std::memset (dst, 0, static_cast<std::size_t> (numSamples) * sizeof (float));
    }

    for (int ch = passThroughChannels; ch < numOutputChannels; ++ch)
    {
        auto* dst = outputChannelData[ch];
        if (dst != nullptr)
            std::memset (dst, 0, static_cast<std::size_t> (numSamples) * sizeof (float));
    }

    // Sample-clock to LMC (white paper §4.4): a single fetch_add + store on
    // the LMC's atomic pair. Re-loading the rate per buffer rather than
    // capturing it at start handles devices that change rate mid-session.
    if (lmc_ != nullptr)
        lmc_->advanceBySamples (numSamples,
                                currentSampleRate_.load (std::memory_order_acquire));
}

void AudioCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device == nullptr)
    {
        currentSampleRate_.store (0.0, std::memory_order_release);
        currentBufferSize_.store (0,   std::memory_order_release);
        return;
    }

    currentSampleRate_.store (device->getCurrentSampleRate(), std::memory_order_release);
    currentBufferSize_.store (device->getCurrentBufferSizeSamples(), std::memory_order_release);
}

void AudioCallback::audioDeviceStopped()
{
    currentSampleRate_.store (0.0, std::memory_order_release);
    currentBufferSize_.store (0,   std::memory_order_release);
}

} // namespace sirius
