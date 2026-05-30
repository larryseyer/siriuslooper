#include "ida/TapePrefetcher.h"

#include "ida/IPayloadCodec.h"
#include "ida/TapeRecord.h"

#include <algorithm>
#include <chrono>

namespace ida {

TapePrefetcher::~TapePrefetcher() { stop(); }

bool TapePrefetcher::open (const juce::File& file, TapeCodecRegistry& registry,
                           int framesPerRecord, std::int64_t loopLengthSamples)
{
    registry_          = &registry;
    framesPerRecord_   = framesPerRecord;
    loopLengthSamples_ = loopLengthSamples;
    TapeTruncationReport report;
    reader_ = TapeRecordReader::open (file, registry, report, /*recover=*/false);
    return reader_ != nullptr;
}

void TapePrefetcher::prepare (int ringFrames)
{
    ringL_.assign (static_cast<std::size_t> (ringFrames) + 1, 0.0f);
    ringR_.assign (static_cast<std::size_t> (ringFrames) + 1, 0.0f);
    head_.store (0); tail_.store (0);
}

void TapePrefetcher::setTargetSample (std::int64_t tapeSample) noexcept
{
    targetSample_.store (tapeSample, std::memory_order_release);
    cv_.notify_one();
}

namespace {
inline std::size_t ringAvail (std::size_t head, std::size_t tail, std::size_t cap) noexcept
{ return (tail + cap - head) % cap; }
}

int TapePrefetcher::pull (float* l, float* r, int n) noexcept
{
    const std::size_t cap = ringL_.size();
    if (cap == 0) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); return 0; }
    std::size_t head = head_.load (std::memory_order_relaxed);
    const std::size_t tail = tail_.load (std::memory_order_acquire);
    const std::size_t avail = ringAvail (head, tail, cap);
    const int got = static_cast<int> (std::min<std::size_t> (avail, static_cast<std::size_t> (n)));
    for (int i = 0; i < got; ++i)
    {
        l[i] = ringL_[head]; r[i] = ringR_[head];
        head = (head + 1) % cap;
    }
    head_.store (head, std::memory_order_release);
    for (int i = got; i < n; ++i) { l[i] = 0.0f; r[i] = 0.0f; }
    return got;
}

void TapePrefetcher::fillRing()
{
    if (reader_ == nullptr || framesPerRecord_ <= 0) return;
    const std::size_t cap = ringL_.size();
    if (cap == 0) return;

    PcmBlock block; TapeRecordHeader hdr;
    for (;;)
    {
        std::size_t tail = tail_.load (std::memory_order_relaxed);
        const std::size_t head = head_.load (std::memory_order_acquire);
        const std::size_t space = (cap - 1) - ringAvail (head, tail, cap);
        if (space < static_cast<std::size_t> (framesPerRecord_)) break;

        std::int64_t s = nextDecodeSample_;
        if (loopLengthSamples_ > 0) s %= loopLengthSamples_;
        const std::uint64_t rec = static_cast<std::uint64_t> (s / framesPerRecord_);
        const int off          = static_cast<int> (s % framesPerRecord_);
        if (rec >= reader_->recordCount()) break;        // nothing more to decode
        if (! reader_->readAudioRecord (rec, block, hdr)) break;

        const int nframes = block.numFrames();
        for (int i = off; i < nframes; ++i)
        {
            ringL_[tail] = block.left[static_cast<std::size_t> (i)];
            ringR_[tail] = block.right[static_cast<std::size_t> (i)];
            tail = (tail + 1) % cap;
        }
        tail_.store (tail, std::memory_order_release);
        nextDecodeSample_ += (nframes - off);
    }
}

void TapePrefetcher::serviceForTest()
{
    nextDecodeSample_ = targetSample_.load (std::memory_order_acquire);
    fillRing();
}

void TapePrefetcher::workerLoop()
{
    while (running_.load (std::memory_order_acquire))
    {
        const std::int64_t target = targetSample_.load (std::memory_order_acquire);
        if (target != nextDecodeSample_ && ringAvail (head_.load(), tail_.load(), ringL_.size()) == 0)
            nextDecodeSample_ = target;    // honor a seek once the ring has drained
        fillRing();
        std::unique_lock<std::mutex> lk (cvMutex_);
        cv_.wait_for (lk, std::chrono::milliseconds (5));
    }
}

void TapePrefetcher::start()
{
    if (running_.exchange (true)) return;
    nextDecodeSample_ = targetSample_.load (std::memory_order_acquire);
    worker_ = std::thread ([this] { workerLoop(); });
}

void TapePrefetcher::stop()
{
    if (! running_.exchange (false)) return;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

} // namespace ida
