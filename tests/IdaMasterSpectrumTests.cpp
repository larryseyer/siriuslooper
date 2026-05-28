#include "ida/MasterSpectrum.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <cstdlib>

// Shared TU: this file owns the operator-new override + alloc counters for
// the [ida-master-meter] and [ida-master-spectrum] test suites. Both files
// link into the same IdaTests binary; defining the overrides here (and ONLY
// here) avoids the ODR multiple-definition trap. IdaMasterMeterTests.cpp
// declares the counters via `extern` and uses the same overrides.
thread_local std::atomic<size_t> g_allocCount { 0 };
thread_local bool g_counting { false };

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

TEST_CASE("MasterSpectrum publishes nonzero bin energy at the sine frequency",
          "[ida-master-spectrum]") {
    ida::MasterSpectrum spec;
    constexpr int kNumBins = 256;
    spec.prepare(48000.0, 256, kNumBins);

    juce::AudioBuffer<float> buf(2, 256);
    const float freq = 1000.0f;
    for (int i = 0; i < 256; ++i) {
        const float s = static_cast<float>(std::sin(2.0 * 3.14159265 * freq * i / 48000.0));
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
    }

    // Need at least fftSize_/numSamples = 512/256 = 2 publish calls to fill
    // the accumulator and trigger an FFT. Four blocks is comfortable headroom.
    for (int b = 0; b < 4; ++b) {
        spec.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());
    }

    bool sawSignal = false;
    for (int bin = 0; bin < kNumBins; ++bin) {
        if (spec.binDb(bin) > -40.0f) { sawSignal = true; break; }
    }
    REQUIRE(sawSignal);
}

TEST_CASE("MasterSpectrum::publish is alloc-free under load",
          "[ida-master-spectrum][rt-safety]") {
    ida::MasterSpectrum spec;
    spec.prepare(48000.0, 256, 256);

    juce::AudioBuffer<float> buf(2, 256);
    for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.1f);

    // Warm-up — primes the accumulator and runs the first FFT, so any
    // implicit one-shot allocation (none expected, but defensive) lands
    // outside the counted region.
    spec.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());

    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting = true;
    constexpr int N = 1000;
    for (int i = 0; i < N; ++i) {
        spec.publish(buf.getReadPointer(0), buf.getReadPointer(1), buf.getNumSamples());
    }
    g_counting = false;

    REQUIRE(g_allocCount.load(std::memory_order_relaxed) == 0u);
}
