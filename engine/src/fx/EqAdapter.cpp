#include "EqAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cstring>

namespace ida
{

EqAdapter::EqAdapter()
{
    // T3a ships default-config EQ with the master enable flipped on so the
    // adapter actually runs DSP. The remaining defaults (HP at 20 Hz, all
    // shelves at 0 dB, LP at 20 kHz) produce a flat response — a freshly
    // inserted EQ passes audio unchanged until the operator dials something
    // in. PlayerEQ internally bypasses each stage when its band is at the
    // bypass-equivalent value, so the flat default is essentially a no-op
    // on the audio path beyond the master gate.
    cfg_.eqEnabled = true;
}

void EqAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerEQ::prepare
    // allocates the cascaded filter stages; updateCoefficients computes the
    // initial IIR coefficients from cfg_.
    eq_.prepare (sampleRate, maxBlockSize);
    eq_.updateCoefficients (cfg_);
    prepared_ = true;
}

void EqAdapter::reset() noexcept
{
    // PlayerEQ::reset clears the cascaded ProcessorDuplicator filter states
    // — bounded by the number of stages (≤ 4 HP + 4 LP + 3 single-stage
    // shelves/peak = 11 IIR state clears). No allocation, no logging,
    // no throw on this path.
    eq_.reset();
}

bool EqAdapter::process (const float* const* inChannels,
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

    // IDA is stereo-only (hard invariant — CLAUDE.md). PlayerEQ
    // itself requires numChannels >= 2 (it returns early on a mono
    // buffer, leaving the output unchanged). Treat anything narrower
    // than stereo as a miss so the caller's dry passthrough takes over
    // instead of half-processing the channels.
    if (numChannels < 2)
        return false;

    // PlayerEQ processes in-place via juce::dsp::ProcessContextReplacing.
    // The adapter's contract allows in-place invocation (inChannels and
    // outChannels may alias). If they don't alias, copy in -> out first,
    // then process out in-place. std::memcpy is bounded, allocation-free,
    // lock-free.
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
    // dimensions). PlayerEQ then builds an AudioBlock from the buffer and
    // runs the IIR cascades in-place via ProcessContextReplacing.
    juce::AudioBuffer<float> buffer (outChannels, numChannels, numSamples);
    eq_.process (buffer);
    return true;
}

} // namespace ida
