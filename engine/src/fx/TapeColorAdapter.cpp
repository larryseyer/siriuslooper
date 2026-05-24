#include "TapeColorAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>

namespace ida
{

TapeColorAdapter::TapeColorAdapter()
{
    // TAPECOLOR default-OFF rule (operator design lock 2026-05-24):
    // unlike EqAdapter/CmpAdapter/RvbAdapter/DlyAdapter, this adapter does
    // NOT flip an "enabled" flag in its ctor. `lsfx::TapeColorProcessor`'s
    // default config has `enabled = false`, so a freshly-inserted TAPECOLOR
    // slot is a silent passthrough until the operator turns it on through
    // the (later-slice) param-UI. Slice 1 ships the insert-anywhere
    // plumbing only; the param surface lands separately.
}

void TapeColorAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. TapeColorProcessor
    // ::prepare builds three per-quality oversamplers, sizes the modulation
    // delay-line ring, primes the IR convolution worker thread. IDA is
    // stereo-only per the hard invariant (CLAUDE.md §"HARD INVARIANT: stereo
    // only"); pass 2 channels to match.
    processor_.prepare (sampleRate, maxBlockSize, /*numChannels=*/ 2);
    prepared_ = true;
}

void TapeColorAdapter::reset() noexcept
{
    // Bounded — no allocation, no logging, no throw. Clears DC blocker
    // state, hysteresis solver state, emphasis EQ filter state, modulation
    // delay-line buffer, and the convolution overlap-add state. Config
    // (enabled flag, drive, mix, etc.) survives the reset.
    processor_.reset();
}

bool TapeColorAdapter::process (const float* const* inChannels,
                                float* const*       outChannels,
                                int                 numChannels,
                                int                 numSamples) noexcept
{
    if (! prepared_) return false;

    if (inChannels == nullptr || outChannels == nullptr
        || numChannels <= 0 || numSamples <= 0)
        return false;

    // IDA is stereo-only. TapeColorProcessor is prepared with 2 channels;
    // anything narrower is a miss so the caller's dry-passthrough takes
    // over rather than partially-filling outChannels.
    if (numChannels < 2) return false;

    // Copy in -> out unless the buffers already alias. TapeColorProcessor
    // processes in-place on a juce::AudioBuffer view of the OUTPUT pointers;
    // on a passthrough (cfg.enabled == false) the buffer is left untouched,
    // which means outChannels carries the dry signal — same dry-on-miss
    // contract as RvbAdapter's pre-IR-load window.
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

    // Wrap raw pointers in an AudioBuffer view (constructor stores pointer
    // + dims, no allocation). bpm=0 leaves the wow LFO free-running — IDA
    // doesn't plumb the host transport into the internal-FX dispatch yet
    // (same posture as DlyAdapter's delaySyncEnabled=false stance). A
    // future slice plumbs LMC bpm in here for tempo-locked wow.
    juce::AudioBuffer<float> buffer (outChannels, numChannels, numSamples);
    processor_.process (buffer, /*bpm=*/ 0.0);
    return true;
}

lsfx::tapecolor::TapeColorConfig& TapeColorAdapter::scratchConfig() noexcept
{
    return processor_.scratchConfig();
}

void TapeColorAdapter::commitConfig() noexcept
{
    processor_.commitConfig();
}

const lsfx::tapecolor::TapeColorConfig& TapeColorAdapter::liveConfig() const noexcept
{
    return processor_.liveConfig();
}

} // namespace ida
