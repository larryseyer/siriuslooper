#pragma once

#include <atomic>

namespace ida {

/// Lock-free meter snapshot publisher driven from the IDA master mix point.
/// publish() runs on the audio thread (alloc/lock/log-free).
/// snapshot() runs on the message thread (atomic load).
class MasterMeter
{
public:
    struct Snapshot { float leftDb; float rightDb; float peakDb; float lufs; };

    MasterMeter();

    /// Message-thread. Records sample-rate scaffolding for the future R128
    /// LUFS integrator. The current `publish()` does not yet read it;
    /// kept so the audio-thread API doesn't break when R128 lands.
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /// Audio-thread. Computes per-block left/right RMS + peak + LUFS-surrogate
    /// from raw planar stereo pointers and stores them into the atomic
    /// snapshot. Zero allocations. `numSamples` must be ≥ 0; `left` and
    /// `right` must not be nullptr when numSamples > 0.
    void publish (const float* left, const float* right, int numSamples) noexcept;

    /// Any-thread (atomic load).
    Snapshot snapshot() const noexcept;

private:
    std::atomic<Snapshot> snapshot_;
    double sampleRate_ { 48000.0 };  // reserved for R128 LUFS integrator
};

} // namespace ida
