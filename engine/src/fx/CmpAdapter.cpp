#include "CmpAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>

namespace ida
{

CmpAdapter::CmpAdapter()
{
    // T3b ships default-config CMP with the master enable flipped on so the
    // adapter actually runs DSP. The remaining defaults (-12 dB threshold,
    // 4:1 ratio, 10 ms attack, 100 ms release, 0 dB makeup, mix=1.0 fully
    // wet, sidechain HPF on at 100 Hz Butterworth × 4 stages) produce a
    // conservative compressor that reduces peaks above -12 dB. Unlike EQ's
    // flat-default no-op, a freshly inserted CMP slot immediately
    // compresses — the operator dials parameters from there.
    cfg_.compEnabled = true;
}

void CmpAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerCompressor::
    // prepare allocates the dry-copy buffer (for the parallel-mix dry
    // blend) and the sidechain detector buffer, and initializes the
    // 4-stage Butterworth HPF coefficients. updateParameters then
    // pre-computes envelope coefficients (attack/release exponentials)
    // from cfg_. Without that call the envelope follower has zero
    // coefficients and degenerates into a sample-accurate peak limiter
    // (instant attack / release) instead of the operator-expected 10 ms /
    // 100 ms behavior.
    comp_.prepare (sampleRate, maxBlockSize);
    comp_.updateParameters (cfg_);
    prepared_ = true;
}

void CmpAdapter::reset() noexcept
{
    // PlayerCompressor::reset clears the dry+sidechain buffers, both
    // envelope followers, and HPF state. Bounded — no allocation, no
    // logging, no throw. Compressor parameters survive the reset.
    comp_.reset();
}

bool CmpAdapter::process (const float* const* inChannels,
                          float* const*       outChannels,
                          int                 numChannels,
                          int                 numSamples) noexcept
{
    // Adapter not prepared yet — miss path. Per the interface contract,
    // leave outChannels untouched and let the caller treat it as a dry
    // passthrough (the caller is responsible for outChannels holding the
    // dry signal already when invoking in-place).
    if (! prepared_)
        return false;

    // Defensive null / non-positive guards. Same shape as
    // OutOfProcessEffectChainHost::pumpSlot — return false as a miss
    // rather than crash on malformed input.
    if (inChannels == nullptr || outChannels == nullptr
        || numChannels <= 0 || numSamples <= 0)
        return false;

    // IDA is stereo-only (hard invariant — CLAUDE.md). PlayerCompressor
    // requires numChannels >= 2 (it returns early on a mono buffer,
    // leaving the output unchanged). Treat anything narrower than stereo
    // as a miss so the caller's dry passthrough takes over instead of
    // half-processing the channels.
    if (numChannels < 2)
        return false;

    // PlayerCompressor processes in-place on its juce::AudioBuffer<float>
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
    // dimensions). PlayerCompressor then runs the parallel-compression
    // path in-place on the buffer.
    juce::AudioBuffer<float> buffer (outChannels, numChannels, numSamples);
    comp_.process (buffer);
    return true;
}

} // namespace ida
