#include "ida/PlaybackResolver.h"
#include "ida/RenderPipeline.h"

#include <chrono>
#include <cmath>
#include <vector>

namespace ida {

PlaybackResolver::~PlaybackResolver() { stop(); }

void PlaybackResolver::resolveOnce()
{
    if (pipeline_ == nullptr || publisher_ == nullptr || ! playhead_) return;

    ActiveReadsSnapshot snap;             // stack POD; worker thread, alloc is fine
    const TransportPlayhead ph = playhead_();
    if (ph.isPlaying && sampleRate_ > 0.0)
    {
        // Exact identity-calibrated LMC time: seconds -> samples/sr (Rational has
        // no fromDouble, and this matches Task 2's playhead = playedSamples/sr).
        const std::int64_t lmcNum = std::llround (ph.positionInSeconds * sampleRate_);
        const std::int64_t lmcDen = std::llround (sampleRate_);
        const Rational lmc = (lmcDen > 0) ? Rational (lmcNum, lmcDen) : Rational (0);

        const std::vector<ActiveRead> reads = pipeline_->activeReadsAt (lmc);
        for (const auto& ar : reads)
        {
            const int slot = slotFor_ ? slotFor_ (ar.loop) : -1;
            if (slot < 0) continue;       // no phrase channel for this loop -> drop
            const std::int64_t tapeSample =
                static_cast<std::int64_t> (std::llround (ar.tapePosition.toDouble() * sampleRate_));
            if (steer_) steer_ (slot, tapeSample);
            snap.add ({ slot, tapeSample, /*active=*/true });
        }
    }
    publisher_->publish (snap);
}

void PlaybackResolver::workerLoop()
{
    while (running_.load (std::memory_order_acquire))
    {
        resolveOnce();
        std::unique_lock<std::mutex> lk (cvMutex_);
        cv_.wait_for (lk, std::chrono::milliseconds (kResolveIntervalMs));
    }
}

void PlaybackResolver::start()
{
    if (running_.exchange (true)) return;
    worker_ = std::thread ([this] { workerLoop(); });
}

void PlaybackResolver::stop()
{
    if (! running_.exchange (false)) return;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

} // namespace ida
