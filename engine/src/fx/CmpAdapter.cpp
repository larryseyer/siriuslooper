#include "CmpAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <cstring>

namespace ida
{

CmpAdapter::CmpAdapter()
{
    // Default both halves of the double-buffer to "enabled + conservative
    // compressor" so a freshly-inserted CMP adapter immediately
    // compresses peaks above the default -12 dB threshold (4:1 ratio,
    // 10 ms attack, 100 ms release, 0 dB makeup, mix=1.0 fully wet,
    // sidechain HPF on at 100 Hz). Operator dials from there.
    cfgs_[0].compEnabled = true;
    cfgs_[1] = cfgs_[0];
}

void CmpAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerCompressor::
    // prepare allocates the dry-copy buffer (for the parallel-mix dry
    // blend) and the sidechain detector buffer, and initializes the
    // 4-stage Butterworth HPF coefficients. updateParameters then
    // pre-computes envelope coefficients (attack/release exponentials)
    // from the live config. Without that call the envelope follower has
    // zero coefficients and degenerates into a sample-accurate peak
    // limiter (instant attack / release) instead of the operator-
    // expected 10 ms / 100 ms behavior.
    comp_.prepare (sampleRate, maxBlockSize);
    const int live = liveIndex_.load (std::memory_order_relaxed);
    comp_.updateParameters (cfgs_[static_cast<std::size_t> (live)]);
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

otto::effects::PlayerEffectsConfig& CmpAdapter::scratchConfig() noexcept
{
    const int live = liveIndex_.load (std::memory_order_relaxed);
    return cfgs_[static_cast<std::size_t> (1 - live)];
}

void CmpAdapter::commitConfig() noexcept
{
    // Mirror of EqAdapter::commitConfig / MasterBus.h:217-240. Flip with
    // release so the audio thread's acquire-load picks up the new
    // config, refresh the compressor's pre-computed envelope coefficients
    // against the new live half, then copy forward so subsequent
    // scratchConfig() calls start from current state.
    const int oldLive = liveIndex_.load (std::memory_order_relaxed);
    const int newLive = 1 - oldLive;
    liveIndex_.store (newLive, std::memory_order_release);
    if (prepared_)
        comp_.updateParameters (cfgs_[static_cast<std::size_t> (newLive)]);
    cfgs_[static_cast<std::size_t> (oldLive)] = cfgs_[static_cast<std::size_t> (newLive)];
}

const otto::effects::PlayerEffectsConfig& CmpAdapter::liveConfig() const noexcept
{
    const int live = liveIndex_.load (std::memory_order_acquire);
    return cfgs_[static_cast<std::size_t> (live)];
}

void CmpAdapter::setCmpConfig (const CmpConfig& cfg) noexcept
{
    auto& scratch = scratchConfig();
    scratch.compEnabled      = cfg.enabled;
    scratch.compThreshold    = cfg.threshold;
    scratch.compRatio        = cfg.ratio;
    scratch.compAttack       = cfg.attackMs;
    scratch.compRelease      = cfg.releaseMs;
    scratch.compMakeup       = cfg.makeupDb;
    scratch.compMix          = cfg.mix;
    scratch.compSidechainHPF = cfg.sidechainHpf;
    commitConfig();
}

CmpConfig CmpAdapter::cmpConfig() const noexcept
{
    const auto& live = liveConfig();
    CmpConfig out;
    out.enabled      = live.compEnabled;
    out.threshold    = live.compThreshold;
    out.ratio        = live.compRatio;
    out.attackMs     = live.compAttack;
    out.releaseMs    = live.compRelease;
    out.makeupDb     = live.compMakeup;
    out.mix          = live.compMix;
    out.sidechainHpf = live.compSidechainHPF;
    return out;
}

} // namespace ida
