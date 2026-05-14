#include "sirius/Lmc.h"

#include "sirius/MonotonicClock.h"

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
        throw std::invalid_argument ("sirius::Lmc: clock must not be null");

    epochNanos_ = clock_->nowNanos();
}

Rational Lmc::nowSeconds() const
{
    const std::int64_t elapsedNanos = clock_->nowNanos() - epochNanos_;
    return Rational (elapsedNanos, kNanosPerSecond);
}

} // namespace sirius
