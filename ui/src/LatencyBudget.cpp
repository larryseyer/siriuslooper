#include "ida/LatencyBudget.h"

#include <algorithm>
#include <stdexcept>

namespace ida
{

LatencyBand bandOf (double latencyMs)
{
    if (latencyMs < 10.0)  return LatencyBand::Proprioceptive;
    if (latencyMs < 30.0)  return LatencyBand::Causal;
    if (latencyMs < 100.0) return LatencyBand::Response;
    return LatencyBand::Waiting;
}

LatencyBudget::LatencyBudget (std::size_t capacity)
    : capacity_ (capacity)
{
    if (capacity == 0)
        throw std::invalid_argument ("ida::LatencyBudget: capacity must be at least 1");
    samples_.reserve (capacity);
}

void LatencyBudget::record (double latencyMs)
{
    if (latencyMs < 0.0)
        throw std::invalid_argument ("ida::LatencyBudget: latency must not be negative");

    if (samples_.size() < capacity_)
    {
        samples_.push_back (latencyMs);
    }
    else
    {
        samples_[nextWriteIndex_] = latencyMs;
        nextWriteIndex_ = (nextWriteIndex_ + 1) % capacity_;
    }
}

void LatencyBudget::clear() noexcept
{
    samples_.clear();
    nextWriteIndex_ = 0;
}

double LatencyBudget::meanMs() const noexcept
{
    if (samples_.empty()) return 0.0;
    double sum = 0.0;
    for (double s : samples_) sum += s;
    return sum / static_cast<double> (samples_.size());
}

double LatencyBudget::worstMs() const noexcept
{
    if (samples_.empty()) return 0.0;
    return *std::max_element (samples_.begin(), samples_.end());
}

double LatencyBudget::fractionWithinBudget() const noexcept
{
    if (samples_.empty()) return 1.0;
    std::size_t within = 0;
    for (double s : samples_)
        if (s < latencyBudgetMs) ++within;
    return static_cast<double> (within) / static_cast<double> (samples_.size());
}

} // namespace ida
