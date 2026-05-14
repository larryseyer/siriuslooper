#pragma once

#include "sirius/Rational.h"

#include <cstdint>
#include <memory>

namespace sirius
{

class MonotonicClock;

/// The discipline source currently governing the Logical Master Clock, in the
/// strict quality hierarchy of white paper Part 4.2. M2 implements only the
/// local-monotonic tier; the higher tiers and distributed election arrive with
/// the ensemble layer (M8).
enum class DisciplineTier
{
    GpsAtomic,       ///< Tier 1 — GPS-disciplined / atomic
    Ptp,             ///< Tier 2 — PTP / IEEE 1588 grandmaster
    Ntp,             ///< Tier 3 — NTP stratum 1-2
    AbletonLink,     ///< Tier 4 — Ableton Link peer consensus
    LocalMonotonic   ///< Tier 5 — local CPU monotonic, self-consistent
};

/// The Logical Master Clock — a software construct above all hardware clocks
/// (white paper Part IV). It is the absolute-time reference the membrane
/// renders numerical time against.
///
/// At M2 the LMC runs at the local-monotonic tier: when nothing external is
/// reachable, the local CPU monotonic clock *is* the LMC, self-consistent
/// within a session. The LMC's epoch is the moment it is created — session
/// start — and `nowSeconds()` is exact rational time elapsed since then, so
/// nothing downstream inherits floating-point drift from the clock itself.
class Lmc
{
public:
    /// Creates an LMC disciplined by the given monotonic clock. The clock's
    /// reading at construction becomes the LMC epoch. Throws
    /// std::invalid_argument if `clock` is null.
    explicit Lmc (std::shared_ptr<const MonotonicClock> clock);

    /// Absolute LMC time, in seconds, elapsed since the LMC epoch. Exact:
    /// nanoseconds over 1e9, reduced. Never decreases.
    Rational nowSeconds() const;

    /// The LMC epoch as a raw monotonic-clock reading (nanoseconds). Stable for
    /// the life of the LMC.
    std::int64_t epochNanos() const noexcept { return epochNanos_; }

    /// The discipline tier currently governing the LMC. Always LocalMonotonic
    /// at M2.
    DisciplineTier tier() const noexcept { return DisciplineTier::LocalMonotonic; }

private:
    std::shared_ptr<const MonotonicClock> clock_;
    std::int64_t epochNanos_;
};

} // namespace sirius
