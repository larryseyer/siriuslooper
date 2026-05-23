#pragma once

#include <cstddef>
#include <vector>

namespace sirius
{

/// The four perceptual latency bands of white paper 14.8. The whole UI exists
/// inside Causal at worst; the audio path inside Proprioceptive. Buckets are
/// useful for grouping diagnostics — "1.4% of frames slipped to Response" is
/// the kind of phrasing the UI can announce to the performer.
enum class LatencyBand
{
    Proprioceptive, ///< < 10 ms — action feels like one's own
    Causal,         ///< 10–30 ms — action feels like consequence
    Response,       ///< 30–100 ms — action feels like response
    Waiting         ///< > 100 ms — action feels like waiting
};

/// The boundary above which a visual/tactile response leaves the "causal
/// coupling" range and the performer starts to feel the lag (white paper 14.8).
/// The performance gesture surface must keep its measured response below this.
constexpr double latencyBudgetMs = 30.0;

/// Maps a measured latency in milliseconds to its perceptual band.
LatencyBand bandOf (double latencyMs);

/// A simple rolling tracker for visual/tactile response latency. The audio
/// path's <30 ms budget is enforced upstream by the LMC and conceptual-time
/// architecture; this class is the visual/tactile equivalent: every frame's
/// measured latency is recorded, and the UI can read back the budget-meets
/// fraction to honestly announce its own performance (white paper Part 13.3,
/// rule 3 — degradation is announced, not silent).
///
/// The tracker is a ring buffer: it keeps the last `capacity` samples and
/// computes its summary statistics over that window, so the readouts respond
/// to the recent past rather than averaging history forever.
class LatencyBudget
{
public:
    /// `capacity` is the number of samples retained for the rolling window.
    /// Throws std::invalid_argument if zero.
    explicit LatencyBudget (std::size_t capacity = 120);

    /// Records a measured latency, in milliseconds. Throws std::invalid_argument
    /// if negative — a latency cannot be negative, and clamping would hide the
    /// fact that the caller's clock arithmetic is wrong.
    void record (double latencyMs);

    /// Removes every retained sample.
    void clear() noexcept;

    /// The number of samples currently held — at most `capacity`.
    std::size_t size() const noexcept { return samples_.size(); }
    bool        empty() const noexcept { return samples_.empty(); }
    std::size_t capacity() const noexcept { return capacity_; }

    /// Mean latency over the retained window, or 0 if no samples.
    double meanMs() const noexcept;

    /// Maximum latency over the retained window, or 0 if no samples.
    double worstMs() const noexcept;

    /// Fraction of retained samples that landed within `latencyBudgetMs`.
    /// Returns 1.0 when no samples have been recorded yet — silent absence is
    /// not degradation.
    double fractionWithinBudget() const noexcept;

    /// Whether every retained sample landed within `latencyBudgetMs`.
    bool meetsBudget() const noexcept { return fractionWithinBudget() >= 1.0; }

    const std::vector<double>& samples() const noexcept { return samples_; }

private:
    std::vector<double> samples_;
    std::size_t capacity_;
    std::size_t nextWriteIndex_ { 0 };
};

} // namespace sirius
