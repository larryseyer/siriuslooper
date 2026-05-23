#pragma once

#include "ida/Rational.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace ida
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

    // Holds atomics — copy/move would be ill-defined for the audio-thread
    // counter pair below. Construct on the message thread and pass by
    // pointer to anything that needs to call into it.
    Lmc (const Lmc&) = delete;
    Lmc& operator= (const Lmc&) = delete;

    /// Absolute LMC time, in seconds, elapsed since the LMC epoch. Exact:
    /// nanoseconds over 1e9, reduced. Never decreases.
    Rational nowSeconds() const;

    /// The LMC epoch as a raw monotonic-clock reading (nanoseconds). Stable for
    /// the life of the LMC.
    std::int64_t epochNanos() const noexcept { return epochNanos_; }

    /// The discipline tier currently governing the LMC. Always LocalMonotonic
    /// at M2.
    DisciplineTier tier() const noexcept { return DisciplineTier::LocalMonotonic; }

    // -- sample-clock surface (audio thread) -----------------------------------
    /// Advance the sample-clock counter by `numSamples` at `sampleRate` Hz.
    /// Called from the audio thread at the end of each AudioCallback buffer
    /// (white paper §4.4 — when external discipline is absent the device's
    /// hardware-counted sample-clock is the best disciplined local time
    /// source). RT-safe: a single fetch_add plus a store on atomics, no
    /// allocation, no locking. `sampleRate <= 0` or `numSamples <= 0` is a
    /// no-op (silent device or empty buffer).
    ///
    /// The contract is monotone time *within a single device-session*: JUCE
    /// delivers `audioDeviceAboutToStart` once per device lifetime and a
    /// true rate change is a stop+start cycle, not a per-buffer rate jump.
    /// A discontinuous rate change between calls would cause
    /// nowSecondsFromSamples to step (forward or backward) because the
    /// counter is reinterpreted; reconciling that step into a continuous
    /// LMC is the §4.3 calibration / §4.4 slewing story that lands with
    /// the ensemble layer in M8.
    void advanceBySamples (std::int64_t numSamples, double sampleRate) noexcept;

    /// Exact rational seconds elapsed in sample-clock time since the LMC
    /// epoch — sampleCount / sampleRateHz. Returns Rational (0) before any
    /// audio buffers have been fed. Never decreases.
    Rational nowSecondsFromSamples() const noexcept;

    /// Samples accumulated so far by advanceBySamples. Zero before any
    /// audio buffers fed.
    std::int64_t sampleCount() const noexcept;

private:
    std::shared_ptr<const MonotonicClock> clock_;
    std::int64_t epochNanos_;

    // Sample-rate is stored as integer Hz (rounded once per advanceBySamples
    // call) so nowSecondsFromSamples returns an exact Rational without any
    // floating-point floor on the LMC side. Standard rates (44100, 48000,
    // 88200, 96000, 176400, 192000) are integer Hz; sub-ppm rate error on
    // exotic devices is below the LMC's calibration tolerance anyway.
    std::atomic<std::int64_t> sampleCount_  { 0 };
    std::atomic<std::int64_t> sampleRateHz_ { 0 };
};

} // namespace ida
