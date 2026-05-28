#include "ida/MasterSpectrum.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

// Guards against a future toolchain regression where std::atomic<float>
// becomes a struct-with-mutex. Mirror of the static_assert T2's follow-on
// added to MasterMeter.cpp (f854478). The bins_ vector publishes audio-
// thread snapshots through these atomics, so the RT-safety claim is
// load-bearing.
static_assert(std::atomic<float>::is_always_lock_free,
              "MasterSpectrum: std::atomic<float> must be lock-free on all "
              "target platforms. See RT_SAFETY_CONTRACT.md.");

namespace ida {

namespace {
constexpr float kDbFloor = -100.0f;
constexpr float kTwoPi   = 6.283185307179586f;

inline float linToDb (float lin) noexcept
{
    return lin > 1.0e-7f ? 20.0f * std::log10(lin) : kDbFloor;
}

/// Returns the integer log2 of n if n is a positive power of two, else -1.
/// Used to derive the FFT order from fftSize_ at prepare() time (message
/// thread — the audio-thread publish() never recomputes it).
inline int log2OfPow2 (int n) noexcept
{
    if (n <= 0) return -1;
    int order = 0;
    int v = n;
    while ((v & 1) == 0) { v >>= 1; ++order; }
    return v == 1 ? order : -1;
}
} // namespace

MasterSpectrum::MasterSpectrum() = default;
MasterSpectrum::~MasterSpectrum() = default;

void MasterSpectrum::prepare (double sampleRate, int /*maxBlockSize*/, int numBins) noexcept
{
    // All-or-nothing prepare contract: validate ALL inputs (including the
    // derived power-of-two FFT size) before mutating any member. binDb() is
    // documented "any-thread"; if we stored numBins_ and then bailed without
    // sizing bins_, a concurrent UI poll would OOB-read the unresized vector.
    if (numBins <= 0) return;
    const int newFftSize = numBins * 2;
    const int order      = log2OfPow2(newFftSize);
    if (order < 0) return;

    sampleRate_ = sampleRate;
    numBins_    = numBins;
    fftSize_    = newFftSize;
    hopFill_    = 0;

    // Hann window — pre-computed once on the message thread.
    window_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    for (int i = 0; i < fftSize_; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(fftSize_ - 1);
        window_[static_cast<std::size_t>(i)] =
            0.5f * (1.0f - std::cos(kTwoPi * phase));
    }

    // Scratch holds real input + complex output: juce::dsp::FFT::perform-
    // FrequencyOnlyForwardTransform writes the magnitudes back into the
    // same buffer and expects 2*fftSize floats of headroom.
    scratch_.assign(static_cast<std::size_t>(2 * fftSize_), 0.0f);

    // std::atomic is not move/copy-constructible, so we can't use assign()
    // here. The vector is constructed in place with the desired size — each
    // atomic value-initializes to zero. This allocation runs on the message
    // thread (audioDeviceAboutToStart), not the audio thread.
    bins_ = std::vector<std::atomic<float>>(static_cast<std::size_t>(numBins_));
    for (auto& bin : bins_)
        bin.store(kDbFloor, std::memory_order_release);

    fft_ = std::make_unique<juce::dsp::FFT>(order);
}

void MasterSpectrum::publish (const float* left, const float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || fft_ == nullptr || fftSize_ <= 0) return;
    if (left == nullptr || right == nullptr) return;

    // Accumulate into the windowed scratch. We may need multiple publish()
    // calls before the FFT input is full (fftSize is typically 512, blocks
    // are often 256 or smaller). The whole accumulator is bounded — once
    // hopFill_ reaches fftSize_ we run the FFT and reset to zero.
    int i = 0;
    while (i < numSamples)
    {
        const int remaining   = fftSize_ - hopFill_;
        const int chunkToCopy = std::min(remaining, numSamples - i);

        for (int k = 0; k < chunkToCopy; ++k)
        {
            const float mono = 0.5f * (left[i + k] + right[i + k]);
            scratch_[static_cast<std::size_t>(hopFill_ + k)] =
                mono * window_[static_cast<std::size_t>(hopFill_ + k)];
        }
        hopFill_ += chunkToCopy;
        i        += chunkToCopy;

        if (hopFill_ == fftSize_)
        {
            // Zero the complex half before running the transform — the JUCE
            // FFT reads/writes the trailing fftSize_ floats as imaginary
            // input / spectrum output.
            for (int k = fftSize_; k < 2 * fftSize_; ++k)
                scratch_[static_cast<std::size_t>(k)] = 0.0f;

            fft_->performFrequencyOnlyForwardTransform(scratch_.data());

            // Magnitudes for the first fftSize_/2 = numBins_ bins are written
            // by JUCE into scratch_[0 .. numBins_-1]. Convert to dB and
            // publish atomically. Normalize by fftSize_ so amplitude doesn't
            // scale with the transform size.
            const float invN = 1.0f / static_cast<float>(fftSize_);
            for (int bin = 0; bin < numBins_; ++bin)
            {
                const float mag = scratch_[static_cast<std::size_t>(bin)] * invN;
                bins_[static_cast<std::size_t>(bin)]
                    .store(linToDb(mag), std::memory_order_release);
            }

            hopFill_ = 0;
        }
    }
}

int MasterSpectrum::numBins() const noexcept
{
    return numBins_;
}

float MasterSpectrum::binDb (int bin) const noexcept
{
    if (bin < 0 || bin >= numBins_) return kDbFloor;
    return bins_[static_cast<std::size_t>(bin)].load(std::memory_order_acquire);
}

} // namespace ida
