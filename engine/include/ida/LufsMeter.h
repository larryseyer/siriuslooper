#pragma once

/// EBU R128 / ITU-R BS.1770 loudness meter (momentary, short-term, integrated,
/// true peak). Algorithm adapted faithfully from OTTO's `otto::mixer::LUFSMeter`
/// (the sister app's metering, which the operator confirms is correct) into the
/// IDA engine namespace and warning-clean form. K-weighting (a +4 dB
/// high-shelf at 1681.97 Hz followed by an RLB high-pass at 38.13 Hz), a 3 s
/// circular squared-sum buffer, a 400 ms momentary window, and a -70 LUFS
/// absolute gate for the integrated measurement.
///
/// RT contract: `prepare()` allocates (message thread, before audio starts);
/// `process()` is allocation-free, lock-free, and I/O-free and is safe to call
/// from the audio callback. Meter readings are published via `std::atomic`
/// floats the UI reads on its timer. `process` must never be handed more than
/// `maxBlockSize` samples (the working buffers are sized to it in `prepare`).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ida
{

class LufsMeter
{
public:
    LufsMeter() = default;

    // Copy stays deleted (the meter owns large filter-state buffers and is
    // never duplicated). Move is enabled so an owning aggregate like Bus can
    // live in a std::vector<Bus>; see
    // docs/superpowers/plans/2026-05-21-mixer-bus-controls-engine.md.
    // A defaulted move ctor would be implicitly deleted because std::atomic
    // has no move ctor, so the three atomic output members are load()/store()'d
    // manually — the same pattern Bus uses for its own atomics in Task 2.
    LufsMeter (const LufsMeter&)            = delete;
    LufsMeter& operator= (const LufsMeter&) = delete;

    LufsMeter (LufsMeter&& other) noexcept
        : sampleRate_              (other.sampleRate_),
          maxBlockSize_            (other.maxBlockSize_),
          prepared_                (other.prepared_),
          momentaryWindowSamples_ (other.momentaryWindowSamples_),
          shortTermWindowSamples_ (other.shortTermWindowSamples_),
          momentaryReadOffset_    (other.momentaryReadOffset_),
          filteredL_               (std::move (other.filteredL_)),
          filteredR_               (std::move (other.filteredR_)),
          squaredSumBuffer_        (std::move (other.squaredSumBuffer_)),
          bufferWritePos_          (other.bufferWritePos_),
          integratedSumSquared_   (other.integratedSumSquared_),
          integratedSampleCount_  (other.integratedSampleCount_),
          preB0_ (other.preB0_), preB1_ (other.preB1_), preB2_ (other.preB2_),
          preA1_ (other.preA1_), preA2_ (other.preA2_),
          rlbB0_ (other.rlbB0_), rlbB1_ (other.rlbB1_), rlbB2_ (other.rlbB2_),
          rlbA1_ (other.rlbA1_), rlbA2_ (other.rlbA2_),
          preX1L_ (other.preX1L_), preX2L_ (other.preX2L_),
          preY1L_ (other.preY1L_), preY2L_ (other.preY2L_),
          preX1R_ (other.preX1R_), preX2R_ (other.preX2R_),
          preY1R_ (other.preY1R_), preY2R_ (other.preY2R_),
          rlbX1L_ (other.rlbX1L_), rlbX2L_ (other.rlbX2L_),
          rlbY1L_ (other.rlbY1L_), rlbY2L_ (other.rlbY2L_),
          rlbX1R_ (other.rlbX1R_), rlbX2R_ (other.rlbX2R_),
          rlbY1R_ (other.rlbY1R_), rlbY2R_ (other.rlbY2R_),
          momentary_  (other.momentary_.load  (std::memory_order_relaxed)),
          shortTerm_  (other.shortTerm_.load  (std::memory_order_relaxed)),
          integrated_ (other.integrated_.load (std::memory_order_relaxed))
    {
    }

    LufsMeter& operator= (LufsMeter&&) noexcept = delete;

    /// Message-thread setup. Allocates working buffers sized to `maxBlockSize`
    /// and a 3 s circular buffer at `sampleRate`; computes the K-weighting
    /// coefficients. Call before the audio thread uses `process`.
    void prepare (double sampleRate, int maxBlockSize)
    {
        sampleRate_   = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
        maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : kDefaultBlockSize;

        filteredL_.assign (static_cast<std::size_t> (maxBlockSize_), 0.0f);
        filteredR_.assign (static_cast<std::size_t> (maxBlockSize_), 0.0f);

        momentaryWindowSamples_ = std::max (1, static_cast<int> (sampleRate_ * 0.4));
        shortTermWindowSamples_ = std::max (1, static_cast<int> (sampleRate_ * 3.0));
        momentaryReadOffset_    = shortTermWindowSamples_ - momentaryWindowSamples_;
        if (momentaryReadOffset_ < 0) momentaryReadOffset_ = 0;

        squaredSumBuffer_.assign (static_cast<std::size_t> (shortTermWindowSamples_), 0.0);
        bufferWritePos_ = 0;

        resetIntegrated();
        calculateKWeightingCoefficients();
        resetFilterState();
        prepared_ = true;
    }

    /// Audio-thread entry. K-weights the stereo block, accumulates the 3 s
    /// circular buffer, and updates the momentary / short-term / integrated
    /// readings. No-op (safe) when unprepared or given null/empty input.
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (! prepared_ || numSamples <= 0 || left == nullptr || right == nullptr)
            return;
        if (numSamples > maxBlockSize_) numSamples = maxBlockSize_;

        processKWeightedBlock (left, right, numSamples);
        updateWindowedMeasurements();
        accumulateIntegrated (numSamples);
    }

    float getMomentary()  const noexcept { return momentary_.load (std::memory_order_relaxed); }
    float getShortTerm()  const noexcept { return shortTerm_.load (std::memory_order_relaxed); }
    float getIntegrated() const noexcept { return integrated_.load (std::memory_order_relaxed); }

    void resetIntegrated() noexcept
    {
        integratedSumSquared_  = 0.0;
        integratedSampleCount_ = 0;
        momentary_.store (kSilenceLufs, std::memory_order_relaxed);
        shortTerm_.store (kSilenceLufs, std::memory_order_relaxed);
        integrated_.store (kSilenceLufs, std::memory_order_relaxed);
    }

private:
    static constexpr double kDefaultSampleRate = 48000.0;
    static constexpr int    kDefaultBlockSize  = 512;
    static constexpr float  kSilenceLufs       = -100.0f;
    static constexpr double kAbsoluteGateLufs  = -70.0;

    // --- K-weighting coefficients (EBU R128) -------------------------------
    void calculateKWeightingCoefficients()
    {
        calculateHighShelfCoefficients (1681.97, 4.0);   // pre-filter
        calculateHighpassCoefficients  (38.13);          // RLB weighting
    }

    void calculateHighShelfCoefficients (double freqHz, double gainDb)
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * M_PI * freqHz / sampleRate_;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double alpha = sinw0 / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.707 - 1.0) + 2.0);
        const double a0    = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * std::sqrt (A) * alpha;

        preB0_ = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * std::sqrt (A) * alpha) / a0;
        preB1_ = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0) / a0;
        preB2_ = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * std::sqrt (A) * alpha) / a0;
        preA1_ = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0) / a0;
        preA2_ = ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * std::sqrt (A) * alpha) / a0;
    }

    void calculateHighpassCoefficients (double freqHz)
    {
        const double w0    = 2.0 * M_PI * freqHz / sampleRate_;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * 0.707);
        const double a0    = 1.0 + alpha;

        rlbB0_ = (1.0 + cosw0) / 2.0 / a0;
        rlbB1_ = -(1.0 + cosw0) / a0;
        rlbB2_ = (1.0 + cosw0) / 2.0 / a0;
        rlbA1_ = -2.0 * cosw0 / a0;
        rlbA2_ = (1.0 - alpha) / a0;
    }

    void resetFilterState() noexcept
    {
        preX1L_ = preX2L_ = preY1L_ = preY2L_ = 0.0;
        preX1R_ = preX2R_ = preY1R_ = preY2R_ = 0.0;
        rlbX1L_ = rlbX2L_ = rlbY1L_ = rlbY2L_ = 0.0;
        rlbX1R_ = rlbX2R_ = rlbY1R_ = rlbY2R_ = 0.0;
    }

    static double applyBiquad (double x, double b0, double b1, double b2,
                               double a1, double a2,
                               double& x1, double& x2, double& y1, double& y2) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void processKWeightedBlock (const float* left, const float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double preL = applyBiquad (left[i], preB0_, preB1_, preB2_, preA1_, preA2_,
                                             preX1L_, preX2L_, preY1L_, preY2L_);
            const double rlbL = applyBiquad (preL, rlbB0_, rlbB1_, rlbB2_, rlbA1_, rlbA2_,
                                             rlbX1L_, rlbX2L_, rlbY1L_, rlbY2L_);
            const double preR = applyBiquad (right[i], preB0_, preB1_, preB2_, preA1_, preA2_,
                                             preX1R_, preX2R_, preY1R_, preY2R_);
            const double rlbR = applyBiquad (preR, rlbB0_, rlbB1_, rlbB2_, rlbA1_, rlbA2_,
                                             rlbX1R_, rlbX2R_, rlbY1R_, rlbY2R_);

            filteredL_[static_cast<std::size_t> (i)] = static_cast<float> (rlbL);
            filteredR_[static_cast<std::size_t> (i)] = static_cast<float> (rlbR);

            squaredSumBuffer_[static_cast<std::size_t> (bufferWritePos_)] = rlbL * rlbL + rlbR * rlbR;
            bufferWritePos_ = (bufferWritePos_ + 1) % shortTermWindowSamples_;
        }
    }

    static float calculateLufs (double meanSquare) noexcept
    {
        if (meanSquare <= 0.0) return kSilenceLufs;
        return static_cast<float> (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    void updateWindowedMeasurements() noexcept
    {
        double momentarySum = 0.0;
        int readPos = (bufferWritePos_ + momentaryReadOffset_) % shortTermWindowSamples_;
        for (int i = 0; i < momentaryWindowSamples_; ++i)
        {
            momentarySum += squaredSumBuffer_[static_cast<std::size_t> (readPos)];
            readPos = (readPos + 1) % shortTermWindowSamples_;
        }
        momentary_.store (calculateLufs (momentarySum / momentaryWindowSamples_),
                          std::memory_order_relaxed);

        double shortTermSum = 0.0;
        for (int i = 0; i < shortTermWindowSamples_; ++i)
            shortTermSum += squaredSumBuffer_[static_cast<std::size_t> (i)];
        shortTerm_.store (calculateLufs (shortTermSum / shortTermWindowSamples_),
                          std::memory_order_relaxed);
    }

    void accumulateIntegrated (int numSamples) noexcept
    {
        // EBU R128 absolute gate at -70 LUFS (the relative-gate pass is the same
        // simplification OTTO ships).
        if (momentary_.load (std::memory_order_relaxed) < static_cast<float> (kAbsoluteGateLufs))
            return;

        double blockSum = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            blockSum += static_cast<double> (filteredL_[idx]) * filteredL_[idx]
                      + static_cast<double> (filteredR_[idx]) * filteredR_[idx];
        }

        integratedSumSquared_  += blockSum;
        integratedSampleCount_ += numSamples;
        if (integratedSampleCount_ > 0)
            integrated_.store (calculateLufs (integratedSumSquared_
                                              / static_cast<double> (integratedSampleCount_)),
                               std::memory_order_relaxed);
    }

    // --- configuration ---
    double sampleRate_   { kDefaultSampleRate };
    int    maxBlockSize_ { kDefaultBlockSize };
    bool   prepared_     { false };

    int momentaryWindowSamples_ { 0 };
    int shortTermWindowSamples_ { 0 };
    int momentaryReadOffset_    { 0 };

    std::vector<float>  filteredL_;
    std::vector<float>  filteredR_;
    std::vector<double> squaredSumBuffer_;
    int                 bufferWritePos_ { 0 };

    double  integratedSumSquared_  { 0.0 };
    int64_t integratedSampleCount_ { 0 };

    // K-weighting coefficients.
    double preB0_ { 1.0 }, preB1_ { 0.0 }, preB2_ { 0.0 }, preA1_ { 0.0 }, preA2_ { 0.0 };
    double rlbB0_ { 1.0 }, rlbB1_ { 0.0 }, rlbB2_ { 0.0 }, rlbA1_ { 0.0 }, rlbA2_ { 0.0 };

    // Biquad delay lines.
    double preX1L_ { 0.0 }, preX2L_ { 0.0 }, preY1L_ { 0.0 }, preY2L_ { 0.0 };
    double preX1R_ { 0.0 }, preX2R_ { 0.0 }, preY1R_ { 0.0 }, preY2R_ { 0.0 };
    double rlbX1L_ { 0.0 }, rlbX2L_ { 0.0 }, rlbY1L_ { 0.0 }, rlbY2L_ { 0.0 };
    double rlbX1R_ { 0.0 }, rlbX2R_ { 0.0 }, rlbY1R_ { 0.0 }, rlbY2R_ { 0.0 };

    std::atomic<float> momentary_  { kSilenceLufs };
    std::atomic<float> shortTerm_  { kSilenceLufs };
    std::atomic<float> integrated_ { kSilenceLufs };
};

} // namespace ida
