// Tests for ida::Asrc — continuous async sample-rate conversion (white paper
// Part 5.3), wrapping libsoxr's variable-rate path. Beyond correctness, these
// tests carry out the measurement the plan explicitly asks for: libsoxr's
// variable-rate resampling latency, checked against the <30 ms trust budget of
// white paper Part 14.8.
#include "ida/Asrc.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

using ida::Asrc;

TEST_CASE ("ASRC construction validates the maximum ratio", "[asrc]")
{
    CHECK_THROWS_AS (Asrc (0.5, Asrc::Quality::High), std::invalid_argument);
    CHECK_NOTHROW (Asrc (1.0, Asrc::Quality::High));
    CHECK_NOTHROW (Asrc (4.0, Asrc::Quality::High));
}

TEST_CASE ("ASRC rejects a non-positive io ratio", "[asrc]")
{
    Asrc asrc (4.0, Asrc::Quality::High);
    CHECK_THROWS_AS (asrc.setIoRatio (0.0, 0), std::invalid_argument);
    CHECK_THROWS_AS (asrc.setIoRatio (-1.0, 0), std::invalid_argument);
    CHECK_NOTHROW (asrc.setIoRatio (1.0, 0));
}

TEST_CASE ("ASRC produces output at the set rate ratio", "[asrc]")
{
    // io ratio = input rate / output rate. A ratio of 0.5 means the input rate
    // is half the output rate, so the stream is upsampled 2x: feeding N input
    // samples yields about 2N output samples (give or take filter warm-up).
    Asrc asrc (4.0, Asrc::Quality::Medium);
    asrc.setIoRatio (0.5, 0);

    constexpr std::size_t inputLen = 4800;
    std::vector<float> input (inputLen, 0.0f);
    for (std::size_t i = 0; i < inputLen; ++i)
        input[i] = static_cast<float> (std::sin (2.0 * 3.14159265358979 * 100.0
                                                 * static_cast<double> (i) / 24000.0));

    std::vector<float> output (inputLen * 4, 0.0f);
    const auto processed = asrc.process (input.data(), inputLen,
                                         output.data(), output.size());
    std::size_t total = processed.outputGenerated;
    total += asrc.flush (output.data() + total, output.size() - total);

    CHECK (processed.inputConsumed == inputLen);
    CHECK (total > inputLen * 2 - 256);
    CHECK (total < inputLen * 2 + 256);
}

TEST_CASE ("ASRC variable-rate latency stays within the trust budget", "[asrc][conceptual-time]")
{
    // The plan calls this measurement out explicitly: libsoxr's variable-rate
    // (async) path latency must be checked against the <30 ms response budget.
    // Measured by impulse response — feed a single impulse and find where its
    // energy emerges — at the realistic membrane ratio (a 44.1k device clock
    // against a 48k engine) and quality (HQ).
    constexpr double referenceOutputRate = 48000.0;
    Asrc asrc (4.0, Asrc::Quality::High);
    asrc.setIoRatio (44100.0 / 48000.0, 0);

    constexpr std::size_t inputLen = 16384;
    std::vector<float> input (inputLen, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output (inputLen * 2, 0.0f);
    const auto processed = asrc.process (input.data(), inputLen,
                                         output.data(), output.size());
    std::size_t total = processed.outputGenerated;
    total += asrc.flush (output.data() + total, output.size() - total);

    REQUIRE (total > 0);

    std::size_t peak = 0;
    for (std::size_t i = 1; i < total; ++i)
        if (std::fabs (output[i]) > std::fabs (output[peak]))
            peak = i;

    const double reportedDelayMs   = asrc.delaySamples() / referenceOutputRate * 1000.0;
    const double impulseLatencyMs  = static_cast<double> (peak) / referenceOutputRate * 1000.0;

    INFO ("output samples produced: " << total);
    INFO ("impulse peak at index " << peak << ", value " << output[peak]);
    INFO ("libsoxr VR/HQ reported delay: " << reportedDelayMs << " ms");
    INFO ("libsoxr VR/HQ impulse-response latency: " << impulseLatencyMs << " ms");

    // A real impulse came through, not just numerical noise.
    CHECK (std::fabs (output[peak]) > 0.01f);
    // Both the resampler's reported delay and the empirical impulse latency
    // must sit inside the trust budget. If either fails, the plan's fallback
    // (a custom polyphase resampler) is on the table.
    CHECK (reportedDelayMs >= 0.0);
    CHECK (reportedDelayMs < 30.0);
    CHECK (impulseLatencyMs < 30.0);
}
