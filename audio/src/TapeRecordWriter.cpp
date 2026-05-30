#include "ida/TapeRecordWriter.h"
#include "ida/AudioPayloadCodec.h"

#include <algorithm>
#include <cmath>

namespace ida
{

TapeRecordWriter::TapeRecordWriter (juce::File tapesDir, double sampleRate,
                                    std::size_t queueCapacity,
                                    TapeCodecId audioCodec, int flushIntervalMs)
    : tapesDir_      (std::move (tapesDir)),
      sampleRate_    (sampleRate),
      audioCodec_    (audioCodec),
      flushIntervalMs_ (std::clamp (flushIntervalMs, kMinFlushIntervalMs, kMaxFlushIntervalMs)),
      queue_         (queueCapacity)
{
    // Register both built-in codecs; the configured audioCodec_ selects which
    // one the worker uses per block. Both are available for decode (T0b).
    registry_.registerCodec (std::make_shared<FlacAudioCodec>());
    registry_.registerCodec (std::make_shared<PcmAudioCodec>());

    if (! tapesDir_.isDirectory())
        tapesDir_.createDirectory();

    worker_ = std::thread (&TapeRecordWriter::workerLoop, this);
}

TapeRecordWriter::~TapeRecordWriter()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    drainQueue();       // flush anything enqueued just before exit
    openTapes_.clear(); // each unique_ptr<FileOutputStream> dtor drains the userspace buffer
                        // and closes the file handle (no fsync)
}

void TapeRecordWriter::setSampleRate (double sampleRate) noexcept
{
    sampleRate_.store (sampleRate, std::memory_order_release);
}

void TapeRecordWriter::closeTape (TapeId id)
{
    Message msg;
    msg.kind   = MessageKind::CloseTape;
    msg.tapeId = id.value();
    if (! queue_.push (msg))
    {
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
        jassertfalse; // tape-close request dropped (queue full) — stream left open
    }
    wakeCv_.notify_all();
}

std::uint64_t TapeRecordWriter::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load (std::memory_order_relaxed);
}

int TapeRecordWriter::flushIntervalMs() const noexcept
{
    return flushIntervalMs_;
}

void TapeRecordWriter::deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                         int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr || right == nullptr) return;

    // Oversized blocks must not be silently half-recorded. Real device buffers
    // are far below this ceiling; tripping it means a contract/sizing mismatch.
    if (numSamples > kTapeRecordWriterMaxFramesPerMessage)
    {
        jassertfalse;
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    Message msg;
    msg.kind      = MessageKind::Audio;
    msg.tapeId    = tape.value();
    msg.numFrames = numSamples;
    for (int i = 0; i < numSamples; ++i)
    {
        msg.samples[static_cast<std::size_t> (i) * 2]     = left[i];
        msg.samples[static_cast<std::size_t> (i) * 2 + 1] = right[i];
    }
    if (! queue_.push (msg))
        droppedBlocks_.fetch_add (1, std::memory_order_relaxed);
    // No wakeCv_ notify on the audio thread (no lock on the hot path); the
    // worker's wait_for timeout bounds latency.
}

juce::File TapeRecordWriter::tapeFile (TapeId id) const
{
    return tapesDir_.getChildFile ("tape-" + juce::String (id.value()) + ".idatape");
}

void TapeRecordWriter::workerLoop()
{
    const auto interval = std::chrono::milliseconds (flushIntervalMs_);
    while (! shouldExit_.load (std::memory_order_acquire))
    {
        {
            std::unique_lock lk (wakeMutex_);
            wakeCv_.wait_for (lk, interval, [this]
            {
                return shouldExit_.load (std::memory_order_acquire) || ! queue_.empty();
            });
        }
        drainQueue();
        // Periodic durability flush: push already-written bytes to disk.
        for (auto& [id, ot] : openTapes_)
            if (ot.stream != nullptr) ot.stream->flush();
    }
}

void TapeRecordWriter::drainQueue()
{
    Message msg;
    while (queue_.pop (msg))
    {
        if (msg.kind == MessageKind::CloseTape)
            finalizeTape (msg.tapeId);
        else
            writeAudio (msg);
    }
}

TapeRecordWriter::OpenTape* TapeRecordWriter::openTapeFor (std::int64_t tapeId)
{
    auto it = openTapes_.find (tapeId);
    if (it != openTapes_.end()) return &it->second;

    const double sr = sampleRate_.load (std::memory_order_acquire);
    if (sr <= 0.0) return nullptr; // no sample rate yet — drop until set

    const auto file = tapeFile (TapeId { tapeId });
    file.deleteFile(); // fresh stream per session run

    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk())
    {
        juce::Logger::writeToLog ("TapeRecordWriter: cannot open " + file.getFullPathName());
        return nullptr;
    }

    // Write the 12-byte file header before any records.
    std::array<std::byte, kTapeFileHeaderBytes> headerBuf {};
    writeFileHeader (headerBuf.data());
    stream->write (headerBuf.data(), kTapeFileHeaderBytes);

    auto [ins, ok] = openTapes_.emplace (tapeId, OpenTape { std::move (stream), sr, 0, 0 });
    return ok ? &ins->second : nullptr;
}

void TapeRecordWriter::writeAudio (const Message& msg)
{
    // Worker-thread function — allocation is fine here.
    OpenTape* ot = openTapeFor (msg.tapeId);
    if (ot == nullptr) return;

    IPayloadCodec* codec = registry_.codecFor (audioCodec_);
    if (codec == nullptr)
    {
        juce::Logger::writeToLog ("TapeRecordWriter: no codec registered for id "
                                  + juce::String (static_cast<int> (audioCodec_)));
        return;
    }

    const double sr     = ot->sampleRate; // captured at tape-open time; constant for all records
    const int    n      = msg.numFrames;

    // De-interleave the POD payload.
    float left[kTapeRecordWriterMaxFramesPerMessage];
    float right[kTapeRecordWriterMaxFramesPerMessage];
    for (int i = 0; i < n; ++i)
    {
        left[i]  = msg.samples[static_cast<std::size_t> (i) * 2];
        right[i] = msg.samples[static_cast<std::size_t> (i) * 2 + 1];
    }

    const auto payload = codec->encode (left, right, n, sr);

    // Timestamp policy v1: lmcTs = framesWritten / round(sampleRate).
    // conceptualTs == lmcTs at capture; the real conceptual↔LMC mapping is a
    // T0b render-time TempoMap concern, not this class's responsibility.
    const auto srInt = static_cast<std::int64_t> (std::llround (sr));
    const auto lmcTs = Rational { static_cast<std::int64_t> (ot->framesWritten), srInt };

    TapeRecordHeader hdr;
    hdr.seq          = ot->nextSeq;
    hdr.type         = TapeRecordType::Audio;
    hdr.codec        = audioCodec_;
    hdr.conceptualTs = lmcTs;
    hdr.lmcTs        = lmcTs;

    encodeRecord (hdr, payload.data(), payload.size(), encodeBuffer_);
    ot->stream->write (encodeBuffer_.data(), encodeBuffer_.size());

    ot->framesWritten += static_cast<std::uint64_t> (n);
    ++ot->nextSeq;
}

void TapeRecordWriter::finalizeTape (std::int64_t tapeId)
{
    auto it = openTapes_.find (tapeId);
    if (it == openTapes_.end()) return;

    if (it->second.stream != nullptr)
    {
        it->second.stream->flush();
        // unique_ptr dtor closes the file; flush before erase for safety.
    }
    openTapes_.erase (it);
}

} // namespace ida
