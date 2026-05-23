#include "ida/Asrc.h"

#include <soxr.h>

#include <stdexcept>
#include <string>

namespace ida
{

namespace
{
    unsigned long recipeFor (Asrc::Quality quality)
    {
        switch (quality)
        {
            case Asrc::Quality::Quick:    return SOXR_QQ;
            case Asrc::Quality::Low:      return SOXR_LQ;
            case Asrc::Quality::Medium:   return SOXR_MQ;
            case Asrc::Quality::High:     return SOXR_HQ;
            case Asrc::Quality::VeryHigh: return SOXR_VHQ;
        }
        return SOXR_HQ;
    }
}

Asrc::Asrc (double maxIoRatio, Quality quality)
    : maxIoRatio_ (maxIoRatio), handle_ (nullptr)
{
    if (maxIoRatio_ < 1.0)
        throw std::invalid_argument ("ida::Asrc: maxIoRatio must be at least 1");

    soxr_io_spec_t ioSpec = soxr_io_spec (SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    // SOXR_VR selects the variable-rate path: the ratio can be changed on the
    // fly with soxr_set_io_ratio.
    soxr_quality_spec_t qualitySpec = soxr_quality_spec (recipeFor (quality), SOXR_VR);
    soxr_runtime_spec_t runtimeSpec = soxr_runtime_spec (1); // single-threaded — RT-safe

    // For a variable-rate resampler the rates passed to soxr_create define the
    // maximum I/O ratio that will ever be requested.
    soxr_error_t error = nullptr;
    handle_ = soxr_create (maxIoRatio_, 1.0, 1 /* mono */,
                           &error, &ioSpec, &qualitySpec, &runtimeSpec);

    if (handle_ == nullptr || error != nullptr)
        throw std::runtime_error (std::string ("ida::Asrc: soxr_create failed: ")
                                  + (error != nullptr ? error : "unknown error"));

    // Start at a 1:1 ratio (instant — third argument 0).
    error = soxr_set_io_ratio (static_cast<soxr_t> (handle_), 1.0, 0);
    if (error != nullptr)
    {
        soxr_delete (static_cast<soxr_t> (handle_));
        throw std::runtime_error (std::string ("ida::Asrc: soxr_set_io_ratio failed: ")
                                  + error);
    }
}

Asrc::~Asrc()
{
    if (handle_ != nullptr)
        soxr_delete (static_cast<soxr_t> (handle_));
}

void Asrc::setIoRatio (double ioRatio, std::size_t slewLengthSamples)
{
    if (ioRatio <= 0.0)
        throw std::invalid_argument ("ida::Asrc: ioRatio must be positive");

    const soxr_error_t error =
        soxr_set_io_ratio (static_cast<soxr_t> (handle_), ioRatio, slewLengthSamples);

    if (error != nullptr)
        throw std::invalid_argument (std::string ("ida::Asrc: ioRatio out of range: ")
                                     + error);
}

Asrc::ProcessResult Asrc::process (const float* input, std::size_t inputCount,
                                   float* output, std::size_t outputCapacity)
{
    std::size_t inputConsumed = 0;
    std::size_t outputGenerated = 0;

    soxr_process (static_cast<soxr_t> (handle_),
                  input, inputCount, &inputConsumed,
                  output, outputCapacity, &outputGenerated);

    return { inputConsumed, outputGenerated };
}

std::size_t Asrc::flush (float* output, std::size_t outputCapacity)
{
    std::size_t outputGenerated = 0;

    // A null input signals end-of-input; soxr then drains its internal buffer.
    soxr_process (static_cast<soxr_t> (handle_),
                  nullptr, 0, nullptr,
                  output, outputCapacity, &outputGenerated);

    return outputGenerated;
}

double Asrc::delaySamples() const
{
    return soxr_delay (static_cast<soxr_t> (handle_));
}

} // namespace ida
