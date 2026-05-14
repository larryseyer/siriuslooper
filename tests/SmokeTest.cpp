// M0 harness smoke test: confirms the test framework compiles, links, and runs
// under CTest in CI. M1 replaces this with the conceptual-time core's
// golden-value suite.
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("test harness runs", "[smoke]")
{
    CHECK (true);
}
