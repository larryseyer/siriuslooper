#include "sirius/TapeWriter.h"

#include <stdexcept>

namespace sirius
{

TapeWriter::TapeWriter (juce::File partialDir,
                        std::chrono::milliseconds flushInterval,
                        std::size_t queueCapacity)
    : partialDir_ (std::move (partialDir)),
      flushInterval_ (flushInterval),
      queue_ (queueCapacity)
{
    if (! partialDir_.exists() && ! partialDir_.createDirectory())
        throw std::runtime_error ("TapeWriter: cannot create partial directory: "
                                  + partialDir_.getFullPathName().toStdString());

    worker_ = std::thread (&TapeWriter::workerLoop, this);
}

TapeWriter::~TapeWriter()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    // Final drain — anything the audio thread enqueued just before shutdown.
    writePendingMessages();

    std::scoped_lock lk (stateMutex_);
    for (auto& [_, stream] : openFiles_)
        if (stream) stream->flush();
}

bool TapeWriter::tryEnqueue (const TapeWriteMessage& msg) noexcept
{
    return queue_.push (msg);
}

juce::File TapeWriter::flushChannel (ChannelId channelId)
{
    {
        std::scoped_lock lk (stateMutex_);
        flushRequestForChannel_ = channelId.value();
    }
    wakeCv_.notify_all();

    std::unique_lock lk (stateMutex_);
    flushCompleteCv_.wait (lk, [this, channelId]
    {
        return flushRequestForChannel_ == -1
            || flushRequestForChannel_ != channelId.value();
    });

    return partialPathFor (channelId);
}

std::uint32_t TapeWriter::errorCountForChannel (ChannelId channelId) const
{
    std::scoped_lock lk (stateMutex_);
    const auto it = errorCounts_.find (channelId.value());
    return it == errorCounts_.end() ? 0u : it->second;
}

juce::File TapeWriter::partialPathFor (ChannelId channelId) const
{
    return partialDir_.getChildFile (
        juce::String (channelId.value()) + ".tape.partial");
}

void TapeWriter::workerLoop()
{
    while (! shouldExit_.load (std::memory_order_acquire))
    {
        {
            std::unique_lock lk (wakeMutex_);
            wakeCv_.wait_for (lk, flushInterval_, [this]
            {
                return shouldExit_.load (std::memory_order_acquire)
                    || ! queue_.empty();
            });
        }
        writePendingMessages();

        std::int64_t flushTarget = -1;
        {
            std::scoped_lock lk (stateMutex_);
            flushTarget = flushRequestForChannel_;
        }
        if (flushTarget >= 0)
        {
            std::scoped_lock lk (stateMutex_);
            auto it = openFiles_.find (flushTarget);
            if (it != openFiles_.end() && it->second)
            {
                it->second->flush();
                it->second.reset(); // close the stream
                openFiles_.erase (it);
            }
            flushRequestForChannel_ = -1;
            flushCompleteCv_.notify_all();
        }
    }
}

void TapeWriter::writePendingMessages()
{
    TapeWriteMessage msg;
    while (queue_.pop (msg))
    {
        const auto channelKey = msg.id.value();
        juce::FileOutputStream* stream = nullptr;
        {
            std::scoped_lock lk (stateMutex_);
            auto it = openFiles_.find (channelKey);
            if (it == openFiles_.end() || ! it->second)
            {
                const auto path = partialPathFor (msg.id);
                auto fresh = std::make_unique<juce::FileOutputStream> (path);
                if (! fresh->openedOk())
                {
                    juce::Logger::writeToLog ("TapeWriter: cannot open partial file: "
                                              + path.getFullPathName());
                    errorCounts_[channelKey]++;
                    continue;
                }
                it = openFiles_.emplace (channelKey, std::move (fresh)).first;
            }
            stream = it->second.get();
        }

        if (stream == nullptr) continue;

        const bool ok = stream->write (msg.samples.data(), msg.sampleCount);
        if (! ok)
        {
            juce::Logger::writeToLog ("TapeWriter: write failed for channel "
                                      + juce::String (channelKey));
            std::scoped_lock lk (stateMutex_);
            errorCounts_[channelKey]++;
        }
    }
}

} // namespace sirius
