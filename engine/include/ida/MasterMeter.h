#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ida {

/// Lock-free meter snapshot publisher driven from the IDA master mix point.
/// publish() runs on the audio thread (alloc/lock/log-free).
/// snapshot() runs on the message thread (atomic load).
class MasterMeter
{
public:
    struct Snapshot { float leftDb; float rightDb; float peakDb; float lufs; };

    MasterMeter();

    /// Message-thread. Sizes integrators for the current sampleRate. Must
    /// be called once before the first publish().
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /// Audio-thread. Computes per-block left/right RMS + peak + LUFS and
    /// stores them into the atomic snapshot. Zero allocations.
    void publish (const juce::AudioBuffer<float>& masterStereo) noexcept;

    /// Any-thread (atomic load).
    Snapshot snapshot() const noexcept;

private:
    std::atomic<Snapshot> snapshot_;
    double sampleRate_ { 48000.0 };
};

} // namespace ida
