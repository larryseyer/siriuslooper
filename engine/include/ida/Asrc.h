#pragma once

#include <cstddef>

namespace ida
{

/// Continuous async sample-rate conversion at a membrane (white paper Part 5.3).
///
/// The membranes are where conceptual time meets the physical reality of an
/// audio interface running at "48 kHz" that is actually running at some
/// slightly different — and slowly drifting — measured rate. This is a
/// *variable-rate* resampler: the input:output ratio can be nudged continuously
/// as the drift is measured, so the conversion tracks the real device clock
/// rather than a nominal one. It wraps libsoxr's variable-rate path; one
/// instance handles one mono stream.
///
/// All allocation happens in the constructor. `setIoRatio`, `process`, and
/// `flush` do not allocate, so once an Asrc is constructed it is safe to drive
/// from the audio thread.
class Asrc
{
public:
    /// Resampling quality. Higher quality is more transparent on sustained
    /// content but costs more CPU and adds more processing latency — the
    /// trade-off the white paper's Part 5.3 describes.
    enum class Quality
    {
        Quick,    ///< cubic interpolation — cheapest, audibly rough
        Low,      ///< 16-bit, larger rolloff
        Medium,   ///< 16-bit, medium rolloff
        High,     ///< 20-bit — the realistic membrane default
        VeryHigh  ///< 28-bit — the transparent reference, most expensive
    };

    /// Creates a variable-rate resampler. `maxIoRatio` is the widest
    /// input:output ratio the resampler will ever be set to — clock drift is
    /// tiny, but headroom is cheap. The resampler starts at a 1:1 ratio. Throws
    /// std::invalid_argument if `maxIoRatio` is less than 1, std::runtime_error
    /// if the underlying resampler fails to initialise.
    Asrc (double maxIoRatio, Quality quality);
    ~Asrc();

    Asrc (const Asrc&) = delete;
    Asrc& operator= (const Asrc&) = delete;

    /// Moves to a new input:output ratio, slewing smoothly to it over
    /// `slewLengthSamples` output samples — pass 0 for an instant change. White
    /// paper Part 4.4: the clock re-engages via gradual rate slewing, never an
    /// instantaneous re-lock. Throws std::invalid_argument if the ratio is not
    /// positive or is outside the range the resampler was created for.
    void setIoRatio (double ioRatio, std::size_t slewLengthSamples);

    struct ProcessResult
    {
        std::size_t inputConsumed;   ///< input samples actually consumed
        std::size_t outputGenerated; ///< output samples actually produced
    };

    /// Consumes up to `inputCount` samples from `input` and writes up to
    /// `outputCapacity` resampled samples to `output`. Does not allocate.
    ProcessResult process (const float* input, std::size_t inputCount,
                           float* output, std::size_t outputCapacity);

    /// Signals end-of-input and drains the resampler's internal buffer. Returns
    /// the number of samples written to `output`. Does not allocate.
    std::size_t flush (float* output, std::size_t outputCapacity);

    /// The resampler's current processing delay, in output samples — the
    /// latency the plan flags for measurement against the <30 ms trust budget
    /// of white paper Part 14.8.
    double delaySamples() const;

    double maxIoRatio() const noexcept { return maxIoRatio_; }

private:
    double maxIoRatio_;
    void* handle_; ///< soxr_t — kept opaque so soxr.h stays out of this header
};

} // namespace ida
