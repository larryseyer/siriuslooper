#pragma once

#include "sirius/Channel.h"
#include "sirius/IWetCaptureSink.h"
#include "sirius/LockFreeSpscQueue.h"
#include "sirius/Rational.h"
#include "sirius/TapeWriter.h" // kMaxTapeWriteMessageBytes (shared per-message cap)

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace juce { class String; }
namespace sirius::persistence { class TapeStore; }

namespace sirius
{

class NotificationBus;
struct WetOpenFile;

/// Audio-thread → writer-thread handoff for wet (post-effects) audio. Same POD
/// shape as `TapeWriteMessage`; `samples` holds interleaved float32
/// (`[ch0 s0, ch1 s0, ch0 s1, ...]`) up to `payloadByteCount` bytes.
struct WetWriteMessage
{
    ChannelId   id { 0 };
    Rational    lmcTime { 0 };
    std::size_t payloadByteCount { 0 };
    alignas (alignof (float)) std::array<std::byte, kMaxTapeWriteMessageBytes> samples {};
};

/// Captures wet audio to per-channel `<channelId>.wet.tape.partial` files,
/// then finalizes them into the content-addressed `TapeStore`. Structurally
/// parallel to `TapeWriter` (M3): the audio thread is the sole producer
/// (`tryEnqueueWet`), the worker thread is the sole consumer. No changes to
/// `TapeWriter` — the shared concurrency primitive is `LockFreeSpscQueue`.
///
/// RT-safety: `tryEnqueueWet` never allocates, blocks, or does I/O. Queue-full
/// or an oversized buffer returns `false`.
class WetCaptureWriter : public IWetCaptureSink
{
public:
    /// `partialDir` is the per-session working directory; per-channel partials
    /// live at `partialDir / <channelId>.wet.tape.partial`. `flushInterval` is
    /// how often the worker drains. Starts the worker thread.
    WetCaptureWriter (std::filesystem::path     partialDir,
                      std::chrono::milliseconds flushInterval,
                      std::size_t               queueCapacity);

    /// Signals shutdown, joins the worker, and drains any remaining queue
    /// entries before returning.
    ~WetCaptureWriter() override;

    WetCaptureWriter (const WetCaptureWriter&) = delete;
    WetCaptureWriter& operator= (const WetCaptureWriter&) = delete;

    [[nodiscard]] bool tryEnqueueWet (ChannelId           id,
                                      Rational            lmcTime,
                                      const float* const* channels,
                                      int                 numChannels,
                                      int                 numSamples) noexcept override;

    /// Blocks until the worker has flushed every pending message for
    /// `channelId` and closed the file. Returns the partial path (which may not
    /// exist on disk if nothing was enqueued — caller checks). Caller serializes
    /// calls (single-slot completion tracking), matching `TapeWriter`.
    std::filesystem::path flushChannel (ChannelId channelId);

    /// Message-thread helper: `flushChannel`, read the partial into memory,
    /// store it via `TapeStore` (content-addressed), delete the partial, and
    /// return the content hash. Returns an empty string if nothing was captured
    /// (partial absent or empty). NOT RT-safe — never call from the audio thread.
    juce::String finalizeToStore (ChannelId channelId, persistence::TapeStore& store);

    /// Per-channel I/O error counter (incremented on the worker thread).
    std::uint32_t errorCountForChannel (ChannelId channelId) const;

    /// Attach the engine→UI truthfulness channel. On worker-thread flush
    /// failure, posts `Error / DiskPressure`. Set-once on the message thread
    /// before the worker has real work; non-owning; the bus must outlive this.
    void setNotificationBus (NotificationBus* bus) noexcept;

private:
    void workerLoop();
    void writePendingMessages();
    std::filesystem::path partialPathFor (ChannelId channelId) const;

    std::filesystem::path                 partialDir_;
    std::chrono::milliseconds             flushInterval_;
    LockFreeSpscQueue<WetWriteMessage>    queue_;

    std::atomic<bool>        shouldExit_ { false };
    std::atomic<bool>        flushRequestPending_ { false };
    std::condition_variable  wakeCv_;
    std::mutex               wakeMutex_;
    std::thread              worker_;

    mutable std::mutex                                            stateMutex_;
    std::unordered_map<std::int64_t, std::unique_ptr<WetOpenFile>> openFiles_;
    std::unordered_map<std::int64_t, std::uint32_t>               errorCounts_;
    std::int64_t                                                  flushRequestForChannel_ { -1 };
    std::condition_variable                                       flushCompleteCv_;

    NotificationBus* notificationBus_ { nullptr };
};

} // namespace sirius
