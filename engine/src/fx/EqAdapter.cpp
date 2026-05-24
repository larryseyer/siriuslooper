#include "EqAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cstddef>
#include <cstring>

namespace ida
{

EqAdapter::EqAdapter()
{
    // Default both halves of the double-buffer to "enabled + flat" so a
    // freshly-inserted EQ adapter passes audio unchanged until the
    // operator dials something in. PlayerEQ internally bypasses each
    // band at its bypass-equivalent value (HP 20 Hz, shelves 0 dB, LP
    // 20 kHz), so the master gate is the only path the signal sees on
    // the default config.
    cfgs_[0].eqEnabled = true;
    cfgs_[1] = cfgs_[0];
}

void EqAdapter::prepare (double sampleRate, int maxBlockSize)
{
    // Message-thread setup — allocation is allowed here. PlayerEQ::prepare
    // allocates the cascaded filter stages; updateCoefficients computes the
    // initial IIR coefficients from the live config.
    eq_.prepare (sampleRate, maxBlockSize);
    const int live = liveIndex_.load (std::memory_order_relaxed);
    eq_.updateCoefficients (cfgs_[static_cast<std::size_t> (live)]);
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

otto::effects::PlayerEffectsConfig& EqAdapter::scratchConfig() noexcept
{
    // The stale half (the one NOT pointed to by liveIndex_). Relaxed
    // load: the message thread is the only writer to liveIndex_, so the
    // value it reads back is its own most recent store — no acquire
    // ordering needed against itself.
    const int live = liveIndex_.load (std::memory_order_relaxed);
    return cfgs_[static_cast<std::size_t> (1 - live)];
}

void EqAdapter::commitConfig() noexcept
{
    // Mirror of MasterBus.h:217-240 — flip the live index with release
    // so the audio thread's acquire-load picks up the new config without
    // a mutex, then refresh the EQ's coefficient state against the new
    // live half. PlayerEQ::updateCoefficients allocates juce::dsp::IIR
    // Coefficients heap objects; this is fine on the message thread but
    // is why commitConfig() is not audio-thread safe. Finally, copy the
    // new live snapshot back to the now-stale half so the next
    // scratchConfig() starts from the current state.
    const int oldLive = liveIndex_.load (std::memory_order_relaxed);
    const int newLive = 1 - oldLive;
    liveIndex_.store (newLive, std::memory_order_release);
    if (prepared_)
        eq_.updateCoefficients (cfgs_[static_cast<std::size_t> (newLive)]);
    cfgs_[static_cast<std::size_t> (oldLive)] = cfgs_[static_cast<std::size_t> (newLive)];
}

const otto::effects::PlayerEffectsConfig& EqAdapter::liveConfig() const noexcept
{
    // Acquire-load: paired with the release store in commitConfig() so
    // the message-thread writes to the live half are visible here.
    const int live = liveIndex_.load (std::memory_order_acquire);
    return cfgs_[static_cast<std::size_t> (live)];
}

void EqAdapter::setEqConfig (const EqConfig& cfg) noexcept
{
    // Map IDA's typed surface into PlayerEffectsConfig's EQ fields, then
    // commit via the scratch/commit pattern. Compressor + IR + Delay
    // fields are untouched in the scratch half so calling setEqConfig
    // doesn't bleed into the other internal-FX surfaces (those round-
    // trip via the matching live half on the next commitConfig).
    auto& scratch = scratchConfig();
    scratch.eqEnabled = cfg.enabled;

    scratch.eqHPFreq  = cfg.hpFreq;
    scratch.eqHPSlope = (cfg.hpSlopeDbPerOct == 24
                            ? otto::effects::FilterSlope::Slope24dB
                            : otto::effects::FilterSlope::Slope12dB);

    scratch.eqLowGain = cfg.lowGain;
    scratch.eqLowFreq = cfg.lowFreq;
    scratch.eqLowQ    = cfg.lowQ;

    scratch.eqMidGain = cfg.midGain;
    scratch.eqMidFreq = cfg.midFreq;
    scratch.eqMidQ    = cfg.midQ;

    scratch.eqHighGain = cfg.highGain;
    scratch.eqHighFreq = cfg.highFreq;
    scratch.eqHighQ    = cfg.highQ;

    scratch.eqLPFreq  = cfg.lpFreq;
    scratch.eqLPSlope = (cfg.lpSlopeDbPerOct == 24
                            ? otto::effects::FilterSlope::Slope24dB
                            : otto::effects::FilterSlope::Slope12dB);

    commitConfig();
}

EqConfig EqAdapter::eqConfig() const noexcept
{
    const auto& live = liveConfig();
    EqConfig out;
    out.enabled = live.eqEnabled;

    out.hpFreq          = live.eqHPFreq;
    out.hpSlopeDbPerOct = (live.eqHPSlope == otto::effects::FilterSlope::Slope24dB ? 24 : 12);

    out.lowGain  = live.eqLowGain;
    out.lowFreq  = live.eqLowFreq;
    out.lowQ     = live.eqLowQ;

    out.midGain  = live.eqMidGain;
    out.midFreq  = live.eqMidFreq;
    out.midQ     = live.eqMidQ;

    out.highGain = live.eqHighGain;
    out.highFreq = live.eqHighFreq;
    out.highQ    = live.eqHighQ;

    out.lpFreq          = live.eqLPFreq;
    out.lpSlopeDbPerOct = (live.eqLPSlope == otto::effects::FilterSlope::Slope24dB ? 24 : 12);

    return out;
}

} // namespace ida
