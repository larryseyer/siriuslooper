#pragma once

#include <cstdint>

namespace sirius
{

/// A monotonic, never-decreasing time source, in nanoseconds.
///
/// This is the substrate the Logical Master Clock disciplines against. It is an
/// interface so the LMC can be exercised in tests against a controlled fake
/// clock — the white paper's "exact by construction" discipline extends to the
/// time source being injectable, not hard-wired.
class MonotonicClock
{
public:
    virtual ~MonotonicClock() = default;

    /// The current reading, in nanoseconds. Successive calls never decrease.
    /// The epoch is unspecified — only differences are meaningful.
    virtual std::int64_t nowNanos() const = 0;
};

/// The local CPU monotonic clock — std::chrono::steady_clock. This is the
/// tier-5 discipline source (white paper Part 4.2): when nothing external is
/// reachable, local CPU monotonic *is* the LMC, self-consistent within a
/// session, which is sufficient for solo work.
class SteadyMonotonicClock final : public MonotonicClock
{
public:
    std::int64_t nowNanos() const override;
};

} // namespace sirius
