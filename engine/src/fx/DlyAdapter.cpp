#include "DlyAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>

namespace sirius
{

DlyAdapter::DlyAdapter()
{
    // T3c ships default-config DLY with the master enable flipped on so the
    // adapter actually runs DSP. The second flip — delaySyncEnabled=false
    // — picks free-running mode so PlayerDelay drives off `delayTimeMs`
    // (250 ms default) rather than the tempo-sync path that needs a live
    // bpm > 0. The internal-FX adapter surface has no transport hookup
    // today (`prepare(sr, blk)`), and the sync path silently no-ops when
    // bpm <= 0, leaving the DelayLine at its initial 0-sample delay and
    // emitting silence (100 % wet of nothing). A later slice plumbs bpm
    // into the engine and lets the operator flip sync back on.
    cfg_.delayEnabled     = true;
    cfg_.delaySyncEnabled = false;
}

void DlyAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerDelay::prepare
    // allocates two stereo delay lines (sized to kMaxDelaySamples = 192000
    // = 4 sec @ 48 kHz) and four IIR feedback filters (hi-cut + lo-cut per
    // channel). updateParameters then computes the IIR coefficients from
    // cfg_ AND sets the delay time. The bpm argument is unused on the
    // free-running path (delaySyncEnabled=false in ctor) — pass 120.0 as
    // a sane sentinel that survives a future ctor change that flips sync
    // back on. Without this updateParameters call, setDelay would never
    // be invoked and popSample(0) would always return zero — the impulse
    // test would silently see "no echo ever appears."
    dly_.prepare (sampleRate, maxBlockSize);
    dly_.updateParameters (cfg_, 120.0);
    prepared_ = true;
}

void DlyAdapter::reset() noexcept
{
    // PlayerDelay::reset clears both delay-line buffers and all four IIR
    // feedback filters, and zeroes samplesSinceLastTap_. Bounded — no
    // allocation, no logging, no throw. Delay-time/feedback/filter
    // parameters survive the reset; only state buffers and the tap
    // counter reset.
    dly_.reset();
}

bool DlyAdapter::process (const float* const* inChannels,
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

    // IDA is stereo-only (hard invariant — CLAUDE.md). PlayerDelay
    // requires numChannels >= 2 (it returns early on a mono buffer,
    // leaving the output unchanged). Treat anything narrower than stereo
    // as a miss so the caller's dry passthrough takes over instead of
    // half-processing the channels.
    if (numChannels < 2)
        return false;

    // PlayerDelay processes in-place on its juce::AudioBuffer<float>
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
    // dimensions). PlayerDelay then runs the wet-only tap loop in-place:
    // read from delay lines, filter feedback, push input+feedback back to
    // the lines, overwrite the buffer with the wet tap.
    juce::AudioBuffer<float> buffer (outChannels, numChannels, numSamples);
    dly_.process (buffer);
    return true;
}

} // namespace sirius
