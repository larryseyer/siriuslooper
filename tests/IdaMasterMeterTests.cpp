#include "ida/MasterMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdlib>

namespace {
thread_local std::atomic<size_t> g_allocCount { 0 };
thread_local bool g_counting { false };
} // namespace

// Operator-new override is defined in tests/IdaMasterSpectrumTests.cpp
// (Task 4) as a shared TU for [ida-master-meter] + [ida-master-spectrum]
// suites. This file declares EXTERN access only — do NOT redefine the
// operators here (ODR). For this isolated landing (Task 2 lands before
// Task 4), if the operator-new override block is needed RIGHT NOW for
// alloc-counting, place it inline below with the understanding that
// Task 4 will move it to that file:

void* operator new(size_t n) {
    if (g_counting) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](size_t n) {
    if (g_counting) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

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
