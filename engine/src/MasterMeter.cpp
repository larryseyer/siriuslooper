#include "ida/MasterMeter.h"
#include <algorithm>
#include <cmath>

namespace ida {

namespace {
constexpr float kDbFloor = -100.0f;
inline float linToDb (float lin) noexcept
{
    return lin > 1.0e-5f ? 20.0f * std::log10(lin) : kDbFloor;
}
} // namespace

MasterMeter::MasterMeter()
{
    snapshot_.store({ kDbFloor, kDbFloor, kDbFloor, kDbFloor },
                    std::memory_order_release);
}

void MasterMeter::prepare (double sampleRate, int /*maxBlockSize*/) noexcept
{
    sampleRate_ = sampleRate;
}

void MasterMeter::publish (const juce::AudioBuffer<float>& buf) noexcept
{
    const int N = buf.getNumSamples();
    if (N <= 0 || buf.getNumChannels() < 2) return;

    const float* L = buf.getReadPointer(0);
    const float* R = buf.getReadPointer(1);

    float sumL = 0.0f, sumR = 0.0f, peak = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float l = L[i];
        const float r = R[i];
        sumL += l * l;
        sumR += r * r;
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    const float rmsL = std::sqrt(sumL / static_cast<float>(N));
    const float rmsR = std::sqrt(sumR / static_cast<float>(N));

    Snapshot s;
    s.leftDb  = linToDb(rmsL);
    s.rightDb = linToDb(rmsR);
    s.peakDb  = linToDb(peak);
    s.lufs    = linToDb(std::max(rmsL, rmsR));  // simplified LUFS surrogate; R128 lands later
    snapshot_.store(s, std::memory_order_release);
}

MasterMeter::Snapshot MasterMeter::snapshot() const noexcept
{
    return snapshot_.load(std::memory_order_acquire);
}

} // namespace ida
