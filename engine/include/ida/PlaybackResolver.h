#pragma once

#include "ida/ActiveReadsSnapshot.h"
#include "ida/TransportPlayhead.h"
#include "ida/ConstituentId.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace ida {

class RenderPipeline;

/// Off-audio-thread resolver: playhead -> activeReadsAt -> pre-resolved snapshot.
/// All collaborators are injected so it is testable in isolation. The worker
/// runs activeReadsAt (which allocates + walks the tree) off the audio thread and
/// publishes a lock-free snapshot the audio thread consumes.
class PlaybackResolver {
public:
    static constexpr int kResolveIntervalMs = 10;

    ~PlaybackResolver();

    void setPipeline (RenderPipeline* p) noexcept             { pipeline_ = p; }
    void setPublisher (ActiveReadsPublisher* p) noexcept      { publisher_ = p; }
    void setSampleRate (double sr) noexcept                   { sampleRate_ = sr; }
    void setPlayheadProvider (std::function<TransportPlayhead()> f) { playhead_ = std::move (f); }
    void setSlotForConstituent (std::function<int(ConstituentId)> f) { slotFor_ = std::move (f); }
    void setSteerPrefetcher (std::function<void(int, std::int64_t)> f) { steer_ = std::move (f); }

    void start();
    void stop();

    /// Synchronous one-shot resolve+publish (test + start() warmup).
    void resolveOnceForTest() { resolveOnce(); }

private:
    void resolveOnce();
    void workerLoop();

    RenderPipeline*       pipeline_  { nullptr };
    ActiveReadsPublisher* publisher_ { nullptr };
    double                sampleRate_ { 48000.0 };
    std::function<TransportPlayhead()>      playhead_;
    std::function<int(ConstituentId)>       slotFor_;
    std::function<void(int, std::int64_t)>  steer_;

    std::atomic<bool>       running_ { false };
    std::thread             worker_;
    std::mutex              cvMutex_;
    std::condition_variable cv_;
};

} // namespace ida
