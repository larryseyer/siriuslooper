// Tests for sirius::Lmc — the Logical Master Clock at its local-monotonic tier
// (white paper Part IV). The clock source is injectable, so these tests drive a
// controlled fake clock and confirm the LMC's epoch handling, its exact
// rational time, and that it never runs backwards.
#include "sirius/Lmc.h"
#include "sirius/MonotonicClock.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

using sirius::DisciplineTier;
using sirius::Lmc;
using sirius::Rational;

namespace
{
    /// A monotonic clock whose reading the test controls directly.
    class FakeClock final : public sirius::MonotonicClock
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
