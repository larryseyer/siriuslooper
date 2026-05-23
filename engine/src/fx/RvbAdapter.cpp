#include "RvbAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>

namespace sirius
{

RvbAdapter::RvbAdapter()
{
    // T3d ships default-config RVB with the master enable flipped on so
    // the adapter actually runs DSP. IR preset selection is the T4/T5 GUI
    // story; slice 3 wires the path resolution.
    cfg_.irEnabled = true;
}

void RvbAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerIRConvolution
    // ::prepare allocates two pre-delay buffers (200 ms each), initialises
    // both convolvers in the double-buffer, and STARTS A BACKGROUND THREAD
    // for non-blocking IR processing. updateParameters then clamps + stores
    // all real-time + processing params from cfg_; with no IR loaded yet
    // it short-circuits the IR-reprocessing branch (guarded by rawIR_
    // emptiness), so this is the safe order: prepare → updateParameters →
    // (slice 3) requestIRLoad.
    conv_.prepare (sampleRate, maxBlockSize);
    conv_.updateParameters (cfg_);
    prepared_ = true;
}

void RvbAdapter::reset() noexcept
{
    // PlayerIRConvolution::reset resets both convolvers (clears FFT
    // overlap-add state) and zeroes both pre-delay ring buffers. Bounded —
    // no allocation, no logging, no throw. IR + params survive the reset;
    // only convolution state and pre-delay history clear.
    conv_.reset();
}

bool RvbAdapter::process (const float* const* inChannels,
                          float* const*       outChannels,
                          int                 numChannels,
                          int                 numSamples) noexcept
{
    // Adapter not prepared yet — miss path. Per the interface contract,
    // leave outChannels untouched and let the caller treat it as a dry
    // passthrough.
    if (! prepared_)
        return false;

    // Defensive null / non-positive guards. Same shape as
    // OutOfProcessEffectChainHost::pumpSlot — return false as a miss
    // rather than crash on malformed input.
    if (inChannels == nullptr || outChannels == nullptr
        || numChannels <= 0 || numSamples <= 0)
        return false;

    // Sirius is stereo-only (hard invariant — CLAUDE.md). PlayerIRConvolution
    // requires numChannels >= 2 (early-exits silently otherwise). Treat
    // anything narrower than stereo as a miss so the caller's dry
    // passthrough takes over instead of leaving outChannels in whatever
    // partial state.
    if (numChannels < 2)
        return false;

    // PlayerIRConvolution processes in-place on its juce::AudioBuffer<float>
    // argument. The adapter contract allows in-place invocation
    // (inChannels and outChannels may alias). If they don't alias, copy
    // in -> out first, then process out in-place. std::memcpy is bounded,
    // allocation-free, lock-free.
    if (inChannels != outChannels)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (inChannels[ch] != outChannels[ch])
                std::memcpy (outChannels[ch],
                             inChannels[ch],
                             static_cast<std::size_t> (numSamples) * sizeof (float));
        }
    }

    // Wrap raw pointers in a juce::AudioBuffer that REFERS TO the existing
    // memory (no allocation — this constructor just stores the pointer and
    // dimensions). PlayerIRConvolution::process then either (a) silently
    // early-exits on !loaded_ while the background worker is still
    // installing the IR, leaving outChannels at its memcpy-from-input
    // state (i.e. dry passthrough until the IR is ready), or (b) post-load
    // overwrites outChannels with the 100 % wet convolution + pre-delay.
    juce::AudioBuffer<float> buffer (outChannels, numChannels, numSamples);
    conv_.process (buffer);
    return true;
}

} // namespace sirius
