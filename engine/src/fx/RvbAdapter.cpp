#include "RvbAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>
#include <string>

namespace ida
{
namespace
{

// Default IR preset (filename without extension; matches OTTO's
// `PlayerEffectsConfig::irPresetName` convention). A short bright plate is
// the universal pro-audio neutral starting point — flat enough to add
// dimension without obscuring the source, short enough (~1.13 s tail) to
// suit vocal/snare-bus insert duty. Verified present at
// ${OTTO_ASSETS_DIR}/IR/Plate Bright 1.13.wav.
constexpr const char* kDefaultIRPresetName = "Plate Bright 1.13";
constexpr const char* kIRFileExtension     = ".wav";

/// Compose the absolute path to the default IR from the CMake-injected
/// asset-bundle root. Returns empty when IDA_OTTO_ASSETS_DIR is not
/// defined; PlayerIRConvolution::loadIRFile gracefully fails on empty /
/// non-existent paths so the adapter degrades to silent passthrough
/// rather than crashing.
std::string resolveDefaultIRPath()
{
#ifdef IDA_OTTO_ASSETS_DIR
    return std::string (IDA_OTTO_ASSETS_DIR) + "/IR/"
         + kDefaultIRPresetName + kIRFileExtension;
#else
    return {};
#endif
}

} // namespace

RvbAdapter::RvbAdapter()
{
    // T3d ships default-config RVB with the master enable flipped on so
    // the adapter actually runs DSP, and the default plate IR pinned in
    // cfg_ so a future param-diff path through updateParameters compares
    // consistently. Operator-facing IR preset selection lands with T4/T5.
    cfg_.irEnabled    = true;
    cfg_.irPresetName = kDefaultIRPresetName;
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

    // Hand the default-IR path to OTTO's background worker. requestIRLoad
    // is documented audio-thread-safe (lock-guarded queue + condvar
    // notify); we're on the message thread here, which is even safer.
    // The audio thread sees `loaded_ == false` until the worker finishes
    // wav-decode + decay/EQ/time-stretch processing + double-buffer
    // install, during which window `conv_.process(...)` early-exits and
    // outChannels retains its memcpy-from-input state (dry passthrough).
    // Empty / missing path silently no-ops inside loadIRFile — the
    // adapter stays in pre-load silent-passthrough indefinitely, which
    // is the right failure mode when assets aren't bundled.
    conv_.requestIRLoad (resolveDefaultIRPath());

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

    // IDA is stereo-only (hard invariant — CLAUDE.md). PlayerIRConvolution
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

} // namespace ida
