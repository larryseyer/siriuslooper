// Tests for sirius::makeInternalFxAdapter — pins the T3a-T3d contract:
// kEq / kCmp / kDly / kRvb all return non-null usable adapters. Each
// landed in lock-step with its own sub-task (T3a-T3d). The smoke-check
// reaches through the factory's IInternalFxAdapter* return to verify
// polymorphic dispatch across the four built-in FX kinds.
#include "sirius/InternalFxFactory.h"
#include "sirius/IInternalFxAdapter.h"
#include "sirius/InternalFxId.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using sirius::InternalFxId;
using sirius::makeInternalFxAdapter;

TEST_CASE ("makeInternalFxAdapter returns a usable adapter for kEq",
           "[internal-fx][factory]")
{
    auto adapter = makeInternalFxAdapter (InternalFxId::kEq);
    REQUIRE (adapter != nullptr);

    // Smoke-check the returned adapter: un-prepared, process must return
    // false without crashing, and must leave the output untouched. This
    // mirrors the EqAdapter miss-contract test but reached through the
    // factory's IInternalFxAdapter* return — verifies the polymorphic
    // dispatch is wired correctly.
    constexpr int kNumSamples  = 64;
    constexpr int kNumChannels = 2;

    std::vector<float> inLeft  (kNumSamples, 0.25f);
    std::vector<float> inRight (kNumSamples, 0.25f);
    std::vector<float> outLeft (kNumSamples, -3.0f);  // sentinel
    std::vector<float> outRight(kNumSamples, -3.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter->process (inPtrs, outPtrs, kNumChannels, kNumSamples);
    CHECK_FALSE (ok);
    for (int i = 0; i < kNumSamples; ++i)
    {
        REQUIRE (outLeft[i]  == -3.0f);
        REQUIRE (outRight[i] == -3.0f);
    }
}

TEST_CASE ("makeInternalFxAdapter returns a usable adapter for kCmp",
           "[internal-fx][factory]")
{
    auto adapter = makeInternalFxAdapter (InternalFxId::kCmp);
    REQUIRE (adapter != nullptr);

    // Smoke-check the returned adapter: un-prepared, process must return
    // false without crashing, and must leave the output untouched. This
    // mirrors the CmpAdapter miss-contract test but reached through the
    // factory's IInternalFxAdapter* return — verifies the polymorphic
    // dispatch is wired correctly through the union of internal-FX kinds.
    constexpr int kNumSamples  = 64;
    constexpr int kNumChannels = 2;

    std::vector<float> inLeft  (kNumSamples, 0.25f);
    std::vector<float> inRight (kNumSamples, 0.25f);
    std::vector<float> outLeft (kNumSamples, -3.0f);  // sentinel
    std::vector<float> outRight(kNumSamples, -3.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter->process (inPtrs, outPtrs, kNumChannels, kNumSamples);
    CHECK_FALSE (ok);
    for (int i = 0; i < kNumSamples; ++i)
    {
        REQUIRE (outLeft[i]  == -3.0f);
        REQUIRE (outRight[i] == -3.0f);
    }
}

TEST_CASE ("makeInternalFxAdapter returns a usable adapter for kDly",
           "[internal-fx][factory]")
{
    auto adapter = makeInternalFxAdapter (InternalFxId::kDly);
    REQUIRE (adapter != nullptr);

    // Smoke-check the returned adapter: un-prepared, process must return
    // false without crashing, and must leave the output untouched. This
    // mirrors the DlyAdapter miss-contract test but reached through the
    // factory's IInternalFxAdapter* return — verifies the polymorphic
    // dispatch is wired correctly through the union of internal-FX kinds.
    constexpr int kNumSamples  = 64;
    constexpr int kNumChannels = 2;

    std::vector<float> inLeft  (kNumSamples, 0.25f);
    std::vector<float> inRight (kNumSamples, 0.25f);
    std::vector<float> outLeft (kNumSamples, -3.0f);  // sentinel
    std::vector<float> outRight(kNumSamples, -3.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter->process (inPtrs, outPtrs, kNumChannels, kNumSamples);
    CHECK_FALSE (ok);
    for (int i = 0; i < kNumSamples; ++i)
    {
        REQUIRE (outLeft[i]  == -3.0f);
        REQUIRE (outRight[i] == -3.0f);
    }
}

TEST_CASE ("makeInternalFxAdapter returns a usable adapter for kRvb",
           "[internal-fx][factory]")
{
    auto adapter = makeInternalFxAdapter (InternalFxId::kRvb);
    REQUIRE (adapter != nullptr);

    // Smoke-check the returned adapter: un-prepared, process must return
    // false without crashing, and must leave the output untouched. This
    // mirrors the RvbAdapter miss-contract test but reached through the
    // factory's IInternalFxAdapter* return — verifies the polymorphic
    // dispatch is wired correctly for the last of the four internal-FX
    // kinds (T3d).
    constexpr int kNumSamples  = 64;
    constexpr int kNumChannels = 2;

    std::vector<float> inLeft  (kNumSamples, 0.25f);
    std::vector<float> inRight (kNumSamples, 0.25f);
    std::vector<float> outLeft (kNumSamples, -3.0f);  // sentinel
    std::vector<float> outRight(kNumSamples, -3.0f);

    const float* inPtrs[kNumChannels]  = { inLeft.data(),  inRight.data() };
    float*       outPtrs[kNumChannels] = { outLeft.data(), outRight.data() };

    const bool ok = adapter->process (inPtrs, outPtrs, kNumChannels, kNumSamples);
    CHECK_FALSE (ok);
    for (int i = 0; i < kNumSamples; ++i)
    {
        REQUIRE (outLeft[i]  == -3.0f);
        REQUIRE (outRight[i] == -3.0f);
    }
}
