#pragma once

#include "ida/TapeRecordReader.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ida {

/// Per-phrase-channel decode-ahead reader. A worker decodes tape records via a
/// TapeRecordReader into a lock-free stereo PCM ring; the audio thread pulls
/// frames wait-free (underrun zero-fills). See plan Task 5 for the sample model.
class TapePrefetcher {
public:
    TapePrefetcher() = default;
    ~TapePrefetcher();

    TapePrefetcher (const TapePrefetcher&) = delete;
    TapePrefetcher& operator= (const TapePrefetcher&) = delete;

    /// Open the tape file. framesPerRecord is the writer's fixed block size;
    /// loopLengthSamples==0 means non-looping forward read. Message thread.
    bool open (const juce::File& file, TapeCodecRegistry& registry,
               int framesPerRecord, std::int64_t loopLengthSamples);

    /// Size the ring (message thread, before start()).
    void prepare (int ringFrames);

    /// Start / stop the worker thread.
    void start();
    void stop();

    /// Steer the decode-ahead target (the next tape sample the audio thread will
    /// consume). Lock-free; the worker observes it and refills. Any thread.
    void setTargetSample (std::int64_t tapeSample) noexcept;

    /// Audio thread: pull n stereo frames from the ring into l/r. Returns the
    /// number of real frames written; zero-fills [returned, n). Wait-free.
    int pull (float* l, float* r, int n) noexcept;

    /// Test hook: run one synchronous decode-into-ring pass (no worker thread).
    void serviceForTest();

private:
    void workerLoop();
    void fillRing();                       // decode from nextDecodeSample_ until ring full

    std::unique_ptr<TapeRecordReader> reader_;
    TapeCodecRegistry*                registry_ { nullptr };
    int                               framesPerRecord_ { 0 };
    std::int64_t                      loopLengthSamples_ { 0 };

    // Stereo ring: parallel L/R float buffers, SPSC head/tail.
    std::vector<float>          ringL_, ringR_;
    std::atomic<std::size_t>    head_ { 0 };   // consumer (audio thread)
    std::atomic<std::size_t>    tail_ { 0 };   // producer (worker)
    std::int64_t                nextDecodeSample_ { 0 };  // worker-private cursor

    std::atomic<std::int64_t>   targetSample_ { 0 };
    std::atomic<bool>           running_ { false };
    std::thread                 worker_;
    std::mutex                  cvMutex_;
    std::condition_variable     cv_;
};

} // namespace ida
