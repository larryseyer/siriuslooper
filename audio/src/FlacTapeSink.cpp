#include "ida/FlacTapeSink.h"

namespace ida
{

FlacTapeSink::FlacTapeSink (juce::File tapesDir, double sampleRate, std::size_t queueCapacity)
    : tapesDir_ (std::move (tapesDir)),
      sampleRate_ (sampleRate),
      queue_ (queueCapacity)
{
    if (! tapesDir_.isDirectory())
        tapesDir_.createDirectory();
    worker_ = std::thread (&FlacTapeSink::workerLoop, this);
}

FlacTapeSink::~FlacTapeSink()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    drainQueue();        // flush anything enqueued just before exit
    writers_.clear();    // unique_ptr<AudioFormatWriter> dtor flushes + finalizes
}

void FlacTapeSink::setSampleRate (double sampleRate) noexcept
{
    sampleRate_.store (sampleRate, std::memory_order_release);
}

void FlacTapeSink::closeTape (TapeId id)
{
    Message msg;
    msg.kind = MessageKind::CloseTape;
    msg.tapeId = id.value();
    if (! queue_.push (msg))
    {
        // droppedBlocks_ conflates audio-block drops and tape-close drops (known
        // minor; a separate counter is deferred). More importantly: a dropped
        // CloseTape leaves the writer open indefinitely — file-integrity risk.
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
        jassertfalse; // tape-close request dropped (queue full) — writer left open
    }
    wakeCv_.notify_all();
}

std::uint64_t FlacTapeSink::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load (std::memory_order_relaxed);
}

void FlacTapeSink::deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                     int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr || right == nullptr) return;
    // Oversized blocks must not be silently half-recorded. Real device buffers are
    // far below this ceiling; tripping it means a contract/sizing mismatch (e.g. a
    // device block larger than kFlacSinkMaxFramesPerMessage). Drop the whole block
    // loudly rather than truncate it.
    if (numSamples > kFlacSinkMaxFramesPerMessage)
    {
        jassertfalse;
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    const int n = numSamples;

    Message msg;
    msg.kind = MessageKind::Audio;
    msg.tapeId = tape.value();
    msg.numFrames = n;
    for (int i = 0; i < n; ++i)
    {
        msg.samples[static_cast<std::size_t> (i) * 2]     = left[i];
        msg.samples[static_cast<std::size_t> (i) * 2 + 1] = right[i];
    }
    if (! queue_.push (msg))
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
    // No wakeCv_ notify on the audio thread (no lock on the hot path); the
    // worker's wait_for timeout bounds latency.
}

void FlacTapeSink::workerLoop()
{
    using namespace std::chrono_literals;
    while (! shouldExit_.load (std::memory_order_acquire))
    {
        {
            std::unique_lock lk (wakeMutex_);
            wakeCv_.wait_for (lk, 20ms, [this]
            {
                return shouldExit_.load (std::memory_order_acquire) || ! queue_.empty();
            });
        }
        drainQueue();
        // Periodic durability flush: push already-emitted FLAC frames to disk.
        // rawStream is a non-owning observer of the FileOutputStream the writer
        // holds; it remains valid for the writer's lifetime.
        // Flush granularity is bounded by the FLAC encoder's block size
        // (~85 ms at 48 kHz) — flushing the FileOutputStream pushes
        // already-emitted frames to disk; forcing sub-block emission requires
        // direct libFLAC access (deferred). This is the honest durability bound.
        for (auto& [id, ot] : writers_)
            if (ot.rawStream != nullptr) ot.rawStream->flush();
    }
}

void FlacTapeSink::drainQueue()
{
    Message msg;
    while (queue_.pop (msg))
    {
        if (msg.kind == MessageKind::CloseTape) finalizeTape (msg.tapeId);
        else                                    writeAudio (msg);
    }
}

juce::AudioFormatWriter* FlacTapeSink::writerFor (std::int64_t tapeId)
{
    auto it = writers_.find (tapeId);
    if (it != writers_.end()) return it->second.writer.get();

    const double sr = sampleRate_.load (std::memory_order_acquire);
    if (sr <= 0.0) return nullptr;     // no rate yet — drop until set

    const auto file = tapesDir_.getChildFile ("tape-" + juce::String (tapeId) + ".flac");
    file.deleteFile();                 // fresh stream per session run
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk())
    {
        juce::Logger::writeToLog ("FlacTapeSink: cannot open " + file.getFullPathName());
        return nullptr;
    }

    // Capture a non-owning raw pointer BEFORE transferring ownership to the
    // writer. The pointer remains valid for the writer's lifetime because the
    // writer is the sole owner of the same underlying object. Used for the
    // periodic durability flush in workerLoop (see below).
    juce::FileOutputStream* rawStream = stream.get();

    // The new JUCE API takes a std::unique_ptr<OutputStream>& — on success it
    // exchanges the pointer out (writer owns it); on failure the unique_ptr
    // still holds the stream (no leak, no double-free). Cast FileOutputStream
    // unique_ptr to the base OutputStream unique_ptr via a local.
    std::unique_ptr<juce::OutputStream> streamBase = std::move (stream);
    // 24-bit stereo FLAC at compression level 3. All FLAC levels are lossless;
    // the level only trades encoder CPU against file size. Level 3 is pinned
    // (rather than inheriting libFLAC's implicit default of 5) for the iOS path:
    // capture is continuous and may run many tapes at once on a single worker
    // thread, so on a phone (iPhone 11 / A13 is the slowest target) the battery
    // and thermal headroom of a lower level matters more than the ~1-2% extra
    // size that levels 5-8 would buy. JUCE maps qualityOptionIndex directly to
    // FLAC__stream_encoder_set_compression_level.
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sr)
                             .withNumChannels (2)
                             .withBitsPerSample (24)
                             .withQualityOptionIndex (3);
    auto writer = flacFormat_.createWriterFor (streamBase, options);
    if (writer == nullptr)
    {
        juce::Logger::writeToLog ("FlacTapeSink: FLAC writer create failed for tape "
                                  + juce::String (tapeId));
        // createWriterFor unconditionally nulls streamBase via std::exchange before
        // returning nullptr, so this path is reached with an already-released pointer.
        // JUCE's FlacAudioFormatWriter unique_ptr overload leaks the FileOutputStream
        // on encoder-init failure (a JUCE bug, not ours) — but this path is
        // effectively unreachable given a valid open stream + valid params.
        // Do NOT add a manual delete here: that would double-free on the success
        // path's exchange semantics.
        return nullptr;
    }

    auto* w = writer.get();
    writers_.emplace (tapeId, OpenTape { std::move (writer), rawStream });
    return w;
}

void FlacTapeSink::writeAudio (const Message& msg)
{
    auto* writer = writerFor (msg.tapeId);
    if (writer == nullptr) return;

    // De-interleave the POD payload into the two channel pointers the writer wants.
    float left[kFlacSinkMaxFramesPerMessage];
    float right[kFlacSinkMaxFramesPerMessage];
    for (int i = 0; i < msg.numFrames; ++i)
    {
        left[i]  = msg.samples[static_cast<std::size_t> (i) * 2];
        right[i] = msg.samples[static_cast<std::size_t> (i) * 2 + 1];
    }
    const float* channels[2] { left, right };
    writer->writeFromFloatArrays (channels, 2, msg.numFrames);
}

void FlacTapeSink::finalizeTape (std::int64_t tapeId)
{
    auto it = writers_.find (tapeId);
    if (it == writers_.end()) return;
    writers_.erase (it); // unique_ptr<AudioFormatWriter> dtor flushes + finalizes
}

} // namespace ida
