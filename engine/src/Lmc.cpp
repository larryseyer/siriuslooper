#include "sirius/Lmc.h"

#include "sirius/MonotonicClock.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace sirius
{

namespace
{
    constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
}

Lmc::Lmc (std::shared_ptr<const MonotonicClock> clock)
    : clock_ (std::move (clock))
{
    if (clock_ == nullptr)
        throw std::invalid_argument ("ida::Lmc: clock must not be null");

    epochNanos_ = clock_->nowNanos();
}

Rational Lmc::nowSeconds() const
{
    const std::int64_t elapsedNanos = clock_->nowNanos() - epochNanos_;
    return Rational (elapsedNanos, kNanosPerSecond);
}

void Lmc::advanceBySamples (std::int64_t numSamples, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || numSamples <= 0)
        return;

    sampleRateHz_.store (static_cast<std::int64_t> (std::llround (sampleRate)),
                         std::memory_order_release);
    sampleCount_.fetch_add (numSamples, std::memory_order_release);
}

Rational Lmc::nowSecondsFromSamples() const noexcept
{
    const std::int64_t rate = sampleRateHz_.load (std::memory_order_acquire);
    if (rate <= 0)
        return Rational (0);

    const std::int64_t samples = sampleCount_.load (std::memory_order_acquire);
    return Rational (samples, rate);
}

std::int64_t Lmc::sampleCount() const noexcept
{
    return sampleCount_.load (std::memory_order_acquire);
}

} // namespace sirius
