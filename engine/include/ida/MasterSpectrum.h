#pragma once

#include <atomic>
#include <memory>
#include <vector>

// juce::dsp::FFT is the only JUCE type this header touches and we keep it as
// a forward declaration so the engine's PUBLIC surface stays JUCE-free (per
// engine/CMakeLists.txt: juce_audio_basics + juce_dsp are PRIVATE link deps
// for IdaEngine). The dtor MUST be defined out-of-line in the .cpp so the
// unique_ptr<FFT> instantiates against a complete type — textbook PIMPL-shape
// pattern. See the T2 MasterMeter follow-on (f854478) for the same rule
// applied to keeping juce_audio_basics out of MasterMeter's public surface.
namespace juce { namespace dsp { class FFT; } }

namespace ida
{

/// Lock-free FFT-spectrum snapshot publisher driven from the IDA master mix
/// point. `publish()` runs on the audio thread (alloc/lock/log-free); per-bin
/// `binDb()` reads run on any thread via atomic load.
///
/// Mirror of MasterMeter (S3a T2): same shape, same RT-safety contract, same
/// "raw planar pointers + numSamples" public API so the engine header has no
/// dependency on juce_audio_basics.
class MasterSpectrum
{
public:
    MasterSpectrum();

    /// Out-of-line so `unique_ptr<juce::dsp::FFT>` instantiates against a
    /// complete FFT type — see the FFT forward-declaration note above.
    ~MasterSpectrum();

    MasterSpectrum (const MasterSpectrum&) = delete;
    MasterSpectrum& operator= (const MasterSpectrum&) = delete;
    MasterSpectrum (MasterSpectrum&&) = delete;
    MasterSpectrum& operator= (MasterSpectrum&&) = delete;

    /// Message-thread. Allocates window + scratch + atomic bin vector and
    /// constructs the FFT engine. Sized once at device-start; the audio thread
    /// never resizes. `numBins` is the number of output magnitude bins (half
    /// the FFT size); FFT size is `numBins * 2`, which must be a power of two
    /// for the JUCE FFT order calculation to be exact. Sensible call:
    /// `prepare(sampleRate, maxBlockSize, 256)` → FFT size 512.
    void prepare (double sampleRate, int maxBlockSize, int numBins) noexcept;

    /// Audio-thread. Windows the mono'd (L+R)/2 of the supplied stereo block
    /// into the pre-allocated scratch, runs the FFT, and stores per-bin
    /// magnitude (dB) into the atomic vector. `left` and `right` must be
    /// non-null when `numSamples > 0`. Zero allocations, no locks, no I/O.
    /// numSamples may be smaller than the FFT size — the scratch fills
    /// progressively across blocks and runs the FFT only when full.
    void publish (const float* left, const float* right, int numSamples) noexcept;

    /// Any-thread (atomic load).
    int   numBins() const noexcept;

    /// Any-thread (atomic load). Out-of-range → returns the dB floor.
    float binDb  (int bin) const noexcept;

private:
    int    numBins_   { 0 };
    int    fftSize_   { 0 };
    int    hopFill_   { 0 };
    double sampleRate_ { 48000.0 };  // reserved for future bin→Hz mapping

    std::vector<float>              window_;   // Hann coefficients, size fftSize_
    std::vector<float>              scratch_;  // mono accumulator, size 2*fftSize_ (real-in / complex-out)
    std::vector<std::atomic<float>> bins_;     // size numBins_; bin magnitude in dB

    std::unique_ptr<juce::dsp::FFT> fft_;
};

} // namespace ida
