#pragma once

#include "ida/Channel.h"
#include "ida/LockFreeSpscQueue.h"
#include "ida/Rational.h"

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

namespace ida::persistence { class TapeStore; }

namespace sirius
{

class NotificationBus;
struct OpenFile;

/// Per-message ceiling on the inline sample payload. 32 KB → 4096 stereo
/// float32 samples → headroom for any reasonable EngineConfig buffer × 2
/// channels at the upper end (M3 spec, brainstorm 2026-05-18). The message
/// is a POD so the audio thread can construct it on the stack and the
/// LockFreeSpscQueue can value-copy it through `push`.
inline constexpr std::size_t kMaxTapeWriteMessageBytes = 32 * 1024;

/// Audio-thread → writer-thread handoff. Self-contained: the audio thread
/// memcpys processed bytes into `samples[0..payloadByteCount]` and enqueues.
/// No pointers into shared memory; ownership is trivial. Default values are
/// chosen so a zeroed message is harmless if a consumer races a producer.
///
/// NOTE: `payloadByteCount` is bytes, not samples — M3 has no per-channel
/// sample-rate context yet; M4 wires it.
struct TapeWriteMessage
{
    ChannelId id { 0 };
    Rational lmcTime { 0 };
    std::size_t payloadByteCount { 0 };
    std::array<std::byte, kMaxTapeWriteMessageBytes> samples {};
};

/// Owns one worker thread and one bounded SPSC queue. The audio thread is
/// the sole producer (calls `tryEnqueue`); the worker is the sole consumer
/// (drains in a loop, writes to per-channel `<channelId>.tape.partial` files
/// inside `partialDir`, flushing at the caller-supplied interval).
///
/// Real-time-safety contract (docs/RT_SAFETY_CONTRACT.md): `tryEnqueue`
/// never allocates, never blocks, never does I/O. Queue-full returns
/// `false`; the audio-thread caller reports overload via OverloadProtection.
///
/// Error handling: I/O failures on the writer thread (disk full, permission
/// denied) are caught, counted per channel, logged via juce::Logger, and
/// surfaced via the same OverloadProtection.reportLoad(1.0) mechanism
/// (semantically "engine can't keep up"). The channel keeps trying on
/// subsequent buffers — recoverable, not fatal.
///
/// Tier policy: the caller converts CapabilityTier → flushInterval before
/// constructing, keeping the engine layer free of the app-layer CapabilityTier
/// header. A helper `tapeWriterFlushInterval(CapabilityTier)` lives in the
/// app layer alongside CapabilityTier itself.
class TapeWriter
{
public:
    /// Constructs the queue with `queueCapacity` slots and starts the
    /// worker thread. `partialDir` is the per-session working directory;
    /// per-channel partial files live at `partialDir / <channelId>.tape.partial`.
    /// `flushInterval` is how often the worker thread drains pending messages.
    TapeWriter (std::filesystem::path partialDir,
                std::chrono::milliseconds flushInterval,
                std::size_t queueCapacity);

    /// Signals shutdown, notifies the worker, joins, and drains any
    /// remaining queue entries before returning. No in-flight samples
    /// are lost (the worker is given a final flush pass).
    ~TapeWriter();

    TapeWriter (const TapeWriter&) = delete;
    TapeWriter& operator= (const TapeWriter&) = delete;

    /// Audio-thread entry. Returns true on enqueue, false on queue-full.
    /// Wait-free. Caller is responsible for reporting overload on false.
    [[nodiscard]] bool tryEnqueue (const TapeWriteMessage& msg) noexcept;

    /// Worker-thread cooperative drain trigger used by
    /// `InputMixer::finalizeChannel` before the channel's partial file
    /// is finalized. Blocks the caller until the worker has flushed
    /// every pending message for `channelId` and closed the file handle.
    ///
    /// PRECONDITION: caller serializes calls — concurrent flushChannel
    /// invocations from multiple threads have undefined behavior (the
    /// completion-tracking is single-slot). In practice the only caller is
    /// InputMixer::finalizeChannel on the message thread, and the operator
    /// finalizes channels one at a time. If concurrent finalization
    /// becomes a requirement (e.g. M22 batch-disarm), upgrade the slot
    /// to a per-channel completion map.
    ///
    /// In NDEBUG/release builds where the assert is elided, violating this
    /// precondition causes the second caller to block until the first's
    /// flush completes (loud — finalize blocks — preferable to silent data
    /// loss).
    ///
    /// RETURNS: the partial-file path. NOTE: the file may not exist on
    /// disk if no messages were ever enqueued for this channel. Caller
    /// must check existence (std::filesystem::exists or equivalent)
    /// before reading. M3 Session 3's InputMixer::finalizeChannel will
    /// add the existence check.
    std::filesystem::path flushChannel (ChannelId channelId);

    /// Ensure the JSONL params partial file exists for `channelId` — used by
    /// NonDestructive mode to record the channel's "I have a params tape"
    /// state even when no events have been emitted yet. Safe to call from
    /// the message thread; the file is touched via the writer thread's
    /// state mutex. NOT RT-safe — never call from the audio thread.
    void touchParamsPartial (ChannelId channelId);

    /// Per-channel error counter (incremented on I/O failure). Read
    /// from the message thread for diagnostics.
    std::uint32_t errorCountForChannel (ChannelId channelId) const;

    /// M6 Session 2 — attach the engine→UI truthfulness channel. On flush
    /// failure (open failure or write failure inside `writePendingMessages`)
    /// the worker thread posts an `Error/DiskPressure` notification alongside
    /// the existing per-channel error counter bump and juce::Logger write.
    /// Set-once on the message thread before the worker is doing real work;
    /// non-owning. The bus must outlive this TapeWriter. The worker thread
    /// is NOT the audio thread — the post here can technically allocate (it
    /// doesn't, but the contract is more permissive) since RT-safety isn't
    /// the concern; the value is operator-visible truthfulness.
    void setNotificationBus (NotificationBus* bus) noexcept;

private:
    void workerLoop();
    void writePendingMessages();
    std::filesystem::path partialPathFor (ChannelId channelId) const;

    std::filesystem::path partialDir_;
    std::chrono::milliseconds flushInterval_;
    LockFreeSpscQueue<TapeWriteMessage> queue_;

    std::atomic<bool> shouldExit_ { false };
    std::atomic<bool> flushRequestPending_ { false };
    std::condition_variable wakeCv_;
    std::mutex wakeMutex_;
    std::thread worker_;

    mutable std::mutex stateMutex_;
    std::unordered_map<std::int64_t, std::unique_ptr<OpenFile>> openFiles_;
    std::unordered_map<std::int64_t, std::uint32_t> errorCounts_;
    std::int64_t flushRequestForChannel_ { -1 };
    std::condition_variable flushCompleteCv_;

    // M6 Session 2 — non-owning truthfulness sink. Read by the worker
    // thread from `writePendingMessages` on I/O failure; written set-once
    // on the message thread before the worker has any failure to report.
    // std::atomic isn't necessary — the pointer is set before the worker
    // ever reads it, matching the same set-once pattern used for
    // `tapeWriter_` / `overload_` in InputMixer.
    NotificationBus* notificationBus_ { nullptr };
};

} // namespace sirius
