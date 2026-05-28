#include "ida/MasterMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdlib>

// Operator-new override + alloc counters live in IdaMasterSpectrumTests.cpp (shared TU).
extern thread_local std::atomic<size_t> g_allocCount;
extern thread_local bool g_counting;

TEST_CASE("MasterMeter publishes peak from a known signal", "[ida-master-meter]") {
    ida::MasterMeter meter;
    meter.prepare(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    buf.setSample(0, 100, 0.5f);  // 0.5 peak on L only

    meter.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());

    const auto snap = meter.snapshot();
    CHECK(snap.peakDb > -7.0f);
    CHECK(snap.peakDb < -5.0f);
    CHECK(snap.leftDb  > snap.rightDb);
}

TEST_CASE("MasterMeter::publish is alloc-free under load", "[ida-master-meter][rt-safety]") {
    ida::MasterMeter meter;
    meter.prepare(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.2f);

    meter.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());  // warm-up

    constexpr int N = 10'000;
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting = true;
    for (int i = 0; i < N; ++i) meter.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());
    g_counting = false;

    REQUIRE(g_allocCount.load(std::memory_order_relaxed) == 0u);
}
