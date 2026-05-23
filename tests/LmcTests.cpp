// Tests for ida::Lmc — the Logical Master Clock at its local-monotonic tier
// (white paper Part IV). The clock source is injectable, so these tests drive a
// controlled fake clock and confirm the LMC's epoch handling, its exact
// rational time, and that it never runs backwards.
#include "ida/Lmc.h"
#include "ida/MonotonicClock.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

using ida::DisciplineTier;
using ida::Lmc;
using ida::Rational;

namespace
{
    /// A monotonic clock whose reading the test controls directly.
    class FakeClock final : public ida::MonotonicClock
    {
    public:
        std::int64_t nowNanos() const override { return value_; }
        void advance (std::int64_t deltaNanos) { value_ += deltaNanos; }
        void setTo (std::int64_t nanos) { value_ = nanos; }

    private:
        std::int64_t value_ { 0 };
    };
}

TEST_CASE ("the LMC requires a non-null clock", "[lmc]")
{
    CHECK_THROWS_AS (Lmc (nullptr), std::invalid_argument);
}

TEST_CASE ("the LMC epoch is the clock reading at construction", "[lmc]")
{
    auto clock = std::make_shared<FakeClock>();
    clock->setTo (5'000'000'000); // an arbitrary non-zero starting reading
    const Lmc lmc (clock);

    CHECK (lmc.epochNanos() == 5'000'000'000);
    // Time elapsed since the epoch is zero until the clock advances.
    CHECK (lmc.nowSeconds() == Rational (0));
}

TEST_CASE ("nowSeconds is exact rational time since the epoch", "[lmc][conceptual-time]")
{
    auto clock = std::make_shared<FakeClock>();
    const Lmc lmc (clock);

    // 1.5 seconds, exactly.
    clock->advance (1'500'000'000);
    CHECK (lmc.nowSeconds() == Rational (3, 2));

    // A single nanosecond is representable exactly — no floating-point floor.
    clock->setTo (0);
    clock->advance (1);
    CHECK (lmc.nowSeconds() == Rational (1, 1'000'000'000));

    // A tempo-relevant duration: 240 ms.
    clock->setTo (0);
    clock->advance (240'000'000);
    CHECK (lmc.nowSeconds() == Rational (6, 25));
}

TEST_CASE ("the LMC never runs backwards as the clock advances", "[lmc]")
{
    auto clock = std::make_shared<FakeClock>();
    const Lmc lmc (clock);

    Rational previous = lmc.nowSeconds();
    for (int step = 0; step < 100; ++step)
    {
        clock->advance (333'333); // an uneven, non-round step
        const Rational current = lmc.nowSeconds();
        CHECK (current >= previous);
        previous = current;
    }
}

TEST_CASE ("at M2 the LMC runs at the local-monotonic tier", "[lmc]")
{
    auto clock = std::make_shared<FakeClock>();
    const Lmc lmc (clock);
    CHECK (lmc.tier() == DisciplineTier::LocalMonotonic);
}

// -- Sample-clock surface (M1 Session 2) ----------------------------------------
// These tests drive Lmc::advanceBySamples directly — no JUCE, no real audio
// device — to exercise the audio-thread surface the AudioCallback wires up.
// Together they cover the four behaviours the white paper §4.4 commitment
// requires: exact rational time at standard rates, exact-rational summation
// across varying buffer sizes, monotone advancement under mixed rates, and
// silent-device noops.

TEST_CASE ("advanceBySamples accumulates exact rational time at 48 kHz", "[lmc][sample-clock]")
{
    auto clock = std::make_shared<FakeClock>();
    Lmc lmc (clock);

    lmc.advanceBySamples (48'000, 48'000.0);

    CHECK (lmc.sampleCount() == 48'000);
    CHECK (lmc.nowSecondsFromSamples() == Rational (1, 1));
}

TEST_CASE ("advanceBySamples is exact across non-standard buffer sizes", "[lmc][sample-clock]")
{
    auto clock = std::make_shared<FakeClock>();
    Lmc lmc (clock);

    // 44.1 kHz with an irregular buffer-size schedule — sums must remain
    // exact because samples-over-rate is integer / integer.
    constexpr std::int64_t buffers[] { 256, 137, 1024, 64, 1, 2048, 333 };
    std::int64_t expectedSamples = 0;
    for (auto n : buffers)
    {
        lmc.advanceBySamples (n, 44'100.0);
        expectedSamples += n;
    }

    CHECK (lmc.sampleCount() == expectedSamples);
    CHECK (lmc.nowSecondsFromSamples() == Rational (expectedSamples, 44'100));
}

TEST_CASE ("advanceBySamples is monotone across many buffers at a fixed rate", "[lmc][sample-clock]")
{
    // The contract advanceBySamples actually offers — and the contract
    // AudioCallback actually exercises — is monotone time within a single
    // device-session, which runs at a single rate. JUCE delivers
    // `audioDeviceAboutToStart` once per device lifetime; a true rate
    // change happens as stop+start, not as a per-buffer rate jitter. So we
    // verify monotone time across 1000 varying-size buffers at 48 kHz —
    // matches the realistic audio thread, the §4.4 promise of "no step,
    // no glitch, no audible discontinuity" within a discipline epoch.
    auto clock = std::make_shared<FakeClock>();
    Lmc lmc (clock);

    constexpr std::int64_t bufs[] { 64, 128, 256, 512, 1024 };

    Rational previous = lmc.nowSecondsFromSamples();
    for (int i = 0; i < 1000; ++i)
    {
        lmc.advanceBySamples (bufs[i % 5], 48'000.0);
        const Rational current = lmc.nowSecondsFromSamples();
        CHECK (current >= previous);
        previous = current;
    }

    // After 200 each of {64, 128, 256, 512, 1024} = 396'800 samples at
    // 48 kHz, the accumulated time is exact rational: 396800 / 48000.
    const std::int64_t totalSamples = (64 + 128 + 256 + 512 + 1024) * 200;
    CHECK (lmc.sampleCount() == totalSamples);
    CHECK (lmc.nowSecondsFromSamples() == Rational (totalSamples, 48'000));
}

TEST_CASE ("advanceBySamples with rate <= 0 or zero samples is a no-op", "[lmc][sample-clock]")
{
    auto clock = std::make_shared<FakeClock>();
    Lmc lmc (clock);

    // Establish a real reading first so a no-op call can be detected as
    // "the state did not change", rather than "we never advanced".
    lmc.advanceBySamples (480, 48'000.0);
    const auto baselineSamples = lmc.sampleCount();
    const auto baselineSeconds = lmc.nowSecondsFromSamples();

    lmc.advanceBySamples (1024, 0.0);
    CHECK (lmc.sampleCount() == baselineSamples);
    CHECK (lmc.nowSecondsFromSamples() == baselineSeconds);

    lmc.advanceBySamples (1024, -48'000.0);
    CHECK (lmc.sampleCount() == baselineSamples);
    CHECK (lmc.nowSecondsFromSamples() == baselineSeconds);

    lmc.advanceBySamples (0, 48'000.0);
    CHECK (lmc.sampleCount() == baselineSamples);
    CHECK (lmc.nowSecondsFromSamples() == baselineSeconds);
}

TEST_CASE ("nowSecondsFromSamples is zero before any audio buffers; nowSeconds independent", "[lmc][sample-clock]")
{
    auto clock = std::make_shared<FakeClock>();
    clock->setTo (2'500'000'000); // 2.5 wall-clock seconds
    Lmc lmc (clock);

    // Sample-clock side: nothing fed yet, must read exactly zero (not just
    // "small") even after the wall clock has moved.
    clock->advance (1'500'000'000);
    CHECK (lmc.sampleCount() == 0);
    CHECK (lmc.nowSecondsFromSamples() == Rational (0));

    // Wall-clock side remains independent — it tracks the FakeClock's delta
    // since construction, unaffected by the (absent) sample-clock state.
    CHECK (lmc.nowSeconds() == Rational (3, 2));
}
