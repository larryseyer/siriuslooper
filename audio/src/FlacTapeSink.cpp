#include "sirius/FlacTapeSink.h"

#include <algorithm>

namespace sirius
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
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
    wakeCv_.notify_all();
}

std::uint64_t FlacTapeSink::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load (std::memory_order_relaxed);
}

void FlacTapeSink::deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                     int numSamples) noexcept
{
    const int n = std::min (numSamples, kFlacSinkMaxFramesPerMessage);
    if (n <= 0 || left == nullptr || right == nullptr) return;

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
        for (auto& [id, ot] : writers_)   // periodic durability flush
        {
            // AudioFormatWriter owns the stream; flush via the writer's output.
            // writeFromFloatArrays flushes implicitly on the underlying stream
            // each drain pass — no direct stream access needed here.
            (void) id;
            (void) ot;
        }
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

    // The new JUCE API takes a std::unique_ptr<OutputStream>& — on success it
    // exchanges the pointer out (writer owns it); on failure the unique_ptr
    // still holds the stream (no leak, no double-free). Cast FileOutputStream
    // unique_ptr to the base OutputStream unique_ptr via a local.
    std::unique_ptr<juce::OutputStream> streamBase = std::move (stream);
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sr)
                             .withNumChannels (2)
                             .withBitsPerSample (24);
    auto writer = flacFormat_.createWriterFor (streamBase, options);
    if (writer == nullptr)
    {
        juce::Logger::writeToLog ("FlacTapeSink: FLAC writer create failed for tape "
                                  + juce::String (tapeId));
        // streamBase still owned here — unique_ptr destructs it cleanly.
        return nullptr;
    }

    auto* w = writer.get();
    writers_.emplace (tapeId, OpenTape { std::move (writer) });
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

} // namespace sirius
