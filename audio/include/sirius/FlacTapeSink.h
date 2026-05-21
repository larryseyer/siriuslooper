#pragma once

#include "sirius/ITapeSink.h"
#include "sirius/LockFreeSpscQueue.h"
#include "sirius/TapeId.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace sirius
{

/// Per-message ceiling on the inline stereo payload. 4096 stereo float32 frames
/// (32 KB) covers any realistic audio block; renderInputGraph already clamps a
/// block to kMaxScratchSamples, which is <= this. A POD message so the audio
/// thread constructs it on the stack and the SPSC queue value-copies it.
inline constexpr int kFlacSinkMaxFramesPerMessage = 4096;

/// Real, RT-safe ITapeSink: writes one append-only FLAC stream per TapeId on a
/// worker thread (tape subsystem slice 3). The audio thread only copies the
/// block into a POD message and pushes it onto a single SPSC queue — no alloc,
/// no lock, no I/O, noexcept (docs/RT_SAFETY_CONTRACT.md). The worker thread is
/// the sole owner of the FLAC writers, so the writer map needs no lock; tape
/// close flows as an ordered control message through the same queue.
///
/// Files are <tapesDir>/tape-<id>.flac. The tape is immutable as written;
/// SHA-256 content-addressing is a session-close archival step handled
/// elsewhere (NOT here). FLAC block emission (~one FLAC block ≈ 85 ms at 48 kHz)
/// bounds worst-case crash loss; finer per-tier flushing needs direct libFLAC
/// (deferred).
class FlacTapeSink : public ITapeSink
{
public:
    /// `tapesDir` is created if absent. `sampleRate` may be 0 at construction
    /// (set later via setSampleRate before audio starts); `queueCapacity` slots
    /// must cover the worst-case touched-tapes-per-block burst.
    FlacTapeSink (juce::File tapesDir, double sampleRate, std::size_t queueCapacity);

    /// Stops the worker, drains remaining messages, finalizes every open FLAC
    /// writer (each produces a valid, complete file).
    ~FlacTapeSink() override;

    FlacTapeSink (const FlacTapeSink&) = delete;
    FlacTapeSink& operator= (const FlacTapeSink&) = delete;

    /// Message-thread, before the audio device starts (or on device change while
    /// the audio callback is detached). The worker reads it when lazily creating
    /// a writer. Changing it after a tape's writer exists does not retro-rewrite
    /// that file's header — set it before capture.
    void setSampleRate (double sampleRate) noexcept;

    /// Message-thread: enqueue an ordered request to finalize and close tape
    /// `id`'s FLAC writer. No-op if the tape has no open writer. Used by tape
    /// removal (slice 4) and explicit teardown.
    void closeTape (TapeId id);

    /// Diagnostics (message thread): blocks dropped because the queue was full.
    std::uint64_t droppedBlockCount() const noexcept;

    // --- audio thread ---
    void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                           int numSamples) noexcept override;

private:
    enum class MessageKind : std::uint8_t { Audio, CloseTape };

    struct Message
    {
        MessageKind kind { MessageKind::Audio };
        std::int64_t tapeId { 0 };
        int numFrames { 0 };
        // Interleaved L,R,L,R… so a single copy fills it.
        std::array<float, static_cast<std::size_t> (kFlacSinkMaxFramesPerMessage) * 2> samples {};
    };

    void workerLoop();
    void drainQueue();
    void writeAudio (const Message& msg);
    void finalizeTape (std::int64_t tapeId);
    juce::AudioFormatWriter* writerFor (std::int64_t tapeId); // worker-thread only

    juce::File tapesDir_;
    std::atomic<double> sampleRate_;
    LockFreeSpscQueue<Message> queue_;
    std::atomic<std::uint64_t> droppedBlocks_ { 0 };

    std::atomic<bool> shouldExit_ { false };
    std::condition_variable wakeCv_;
    std::mutex wakeMutex_;
    std::thread worker_;

    // Worker-thread-only state — never touched by the audio or message thread.
    juce::FlacAudioFormat flacFormat_;
    struct OpenTape
    {
        std::unique_ptr<juce::AudioFormatWriter> writer;
        /// Non-owning observer of the FileOutputStream the writer holds.
        /// Captured before unique_ptr ownership is transferred to the writer;
        /// valid for the writer's lifetime. Used for periodic durability flush.
        juce::FileOutputStream* rawStream { nullptr };
    };
    std::unordered_map<std::int64_t, OpenTape> writers_;
};

} // namespace sirius
