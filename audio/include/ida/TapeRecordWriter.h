#pragma once

#include "ida/ITapeSink.h"
#include "ida/IPayloadCodec.h"
#include "ida/LockFreeSpscQueue.h"
#include "ida/TapeId.h"
#include "ida/TapeRecord.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ida
{

/// Per-message ceiling on the inline stereo payload. 4096 stereo float32 frames
/// (32 KB) covers any realistic audio block. Blocks exceeding this are dropped
/// loudly in deliverTapeBlock — a contract violation that does not occur with
/// realistic device buffer sizes. A POD message so the audio thread constructs
/// it on the stack and the SPSC queue value-copies it.
inline constexpr int kTapeRecordWriterMaxFramesPerMessage = 4096;

/// Clamp bounds for the flushIntervalMs constructor argument (whitepaper §17.8).
inline constexpr int kMinFlushIntervalMs = 1;
inline constexpr int kMaxFlushIntervalMs = 5000;

/// RT-safe ITapeSink: writes one append-only .idatape stream per TapeId on a
/// worker thread. The audio thread only copies the block into a POD message and
/// pushes it onto a single SPSC queue — no alloc, no lock, no I/O, noexcept
/// (docs/RT_SAFETY_CONTRACT.md). The worker thread owns all file I/O, codec
/// calls, and the open-tape map; no locking is required on worker state.
///
/// Files are <tapesDir>/tape-<id>.idatape. Each file begins with the 12-byte
/// kTapeFileHeaderBytes file header, then N records of the form:
///   [u32 bodyLen][44-byte record header + codec payload][u32 crc32(body)]
///
/// Timestamp policy v1: lmcTs = Rational(framesWritten, round(sampleRate))
/// from the per-tape cumulative frame count (exact). conceptualTs == lmcTs at
/// capture. The real conceptual↔LMC mapping is a T0b render-time TempoMap
/// concern and does not belong in this class.
class TapeRecordWriter final : public ITapeSink
{
public:
    /// `tapesDir` is created if absent. `sampleRate` may be updated before
    /// audio starts via setSampleRate. `queueCapacity` must cover the
    /// worst-case burst of audio-thread pushes between worker drains.
    /// `audioCodec` selects which registered codec is used for every block.
    /// `flushIntervalMs` is the CV wait_for period; smaller values reduce crash
    /// loss at the cost of more frequent fsync overhead.
    TapeRecordWriter (juce::File tapesDir, double sampleRate, std::size_t queueCapacity,
                      TapeCodecId audioCodec, int flushIntervalMs);

    /// Stops the worker, drains remaining messages, flushes and closes every
    /// open stream so every .idatape file is a valid complete record container.
    ~TapeRecordWriter() override;

    TapeRecordWriter (const TapeRecordWriter&) = delete;
    TapeRecordWriter& operator= (const TapeRecordWriter&) = delete;

    /// Message-thread, before the audio device starts (or on device change while
    /// the audio callback is detached). The worker reads the new rate when lazily
    /// opening a stream. Changing it after a tape's stream exists does not retro-
    /// rewrite that file's header.
    void setSampleRate (double sampleRate) noexcept;

    /// Message-thread: enqueue an ordered request to flush and close tape `id`'s
    /// stream. No-op if the tape has no open stream.
    ///
    /// SINGLE-PRODUCER INVARIANT: this queue has exactly one producer. Calling
    /// closeTape while the audio callback is live means two threads push onto a
    /// single-producer SPSC queue simultaneously, corrupting the queue and
    /// producing undefined behaviour that will silently drop or mangle records.
    /// The caller MUST detach the audio callback before calling closeTape.
    void closeTape (TapeId id);

    /// Diagnostics (message thread): blocks dropped because queue was full or
    /// block exceeded kTapeRecordWriterMaxFramesPerMessage.
    std::uint64_t droppedBlockCount() const noexcept;

    /// The flush interval passed at construction (milliseconds).
    int flushIntervalMs() const noexcept;

    // --- audio thread ---
    void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                           int numSamples) noexcept override;

    /// Returns <tapesDir>/tape-<id>.idatape. Test helper; safe to call from any
    /// thread (reads only immutable state).
    juce::File tapeFile (TapeId id) const;

private:
    enum class MessageKind : std::uint8_t { Audio, CloseTape };

    struct Message
    {
        MessageKind kind     { MessageKind::Audio };
        std::int64_t tapeId { 0 };
        int numFrames        { 0 };
        // Interleaved L,R,L,R… — deliverTapeBlock fills [0, numFrames*2) before push;
        // the worker only reads that range. Not zero-initialised: skipping init saves
        // 32 KB per audio block on the hot path. CloseTape control messages carry this
        // unused payload — a deliberate trade-off to keep the SPSC element a single
        // trivially-copyable POD.
        std::array<float, static_cast<std::size_t> (kTapeRecordWriterMaxFramesPerMessage) * 2> samples;
    };

    struct OpenTape
    {
        std::unique_ptr<juce::FileOutputStream> stream;
        double        sampleRate   { 0.0 }; // captured at open time; all records in
                                             // this file share this rate
        std::uint64_t nextSeq      { 0 };
        std::uint64_t framesWritten { 0 };
    };

    void workerLoop();
    void drainQueue();
    void writeAudio (const Message& msg);
    void finalizeTape (std::int64_t tapeId);
    OpenTape* openTapeFor (std::int64_t tapeId); // worker-thread only

    juce::File                tapesDir_;
    std::atomic<double>       sampleRate_;
    TapeCodecId               audioCodec_;
    int                       flushIntervalMs_;
    TapeCodecRegistry         registry_;
    LockFreeSpscQueue<Message> queue_;
    std::atomic<std::uint64_t> droppedBlocks_ { 0 };

    std::atomic<bool>       shouldExit_ { false };
    std::condition_variable wakeCv_;
    std::mutex              wakeMutex_;
    std::thread             worker_;

    // Worker-thread-only state. Never touched by the audio or message thread.
    std::unordered_map<std::int64_t, OpenTape> openTapes_;
    // Reusable encode buffer: avoids a fresh heap allocation per block on the
    // worker. The worker is the sole user; no synchronization required.
    std::vector<std::byte> encodeBuffer_;
};

} // namespace ida
