#include "sirius/MonotonicClock.h"

#include <chrono>

namespace sirius
{

std::int64_t SteadyMonotonicClock::nowNanos() const
{
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds> (since).count();
}

} // namespace sirius
