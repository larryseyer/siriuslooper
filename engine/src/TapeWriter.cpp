#include "sirius/TapeWriter.h"

#include <juce_core/juce_core.h>

#include <stdexcept>

namespace sirius
{

struct OpenFile { std::unique_ptr<juce::FileOutputStream> stream; };

TapeWriter::TapeWriter (std::filesystem::path partialDir,
                        std::chrono::milliseconds flushInterval,
                        std::size_t queueCapacity)
    : partialDir_ (std::move (partialDir)),
      flushInterval_ (flushInterval),
      queue_ (queueCapacity)
{
    if (! std::filesystem::exists (partialDir_))
    {
        std::error_code ec;
        std::filesystem::create_directories (partialDir_, ec);
        if (ec)
            throw std::runtime_error ("TapeWriter: cannot create partial directory: "
                                      + partialDir_.string() + " — " + ec.message());
    }

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
    for (auto& [_, of] : openFiles_)
        if (of && of->stream) of->stream->flush();
}

bool TapeWriter::tryEnqueue (const TapeWriteMessage& msg) noexcept
{
    return queue_.push (msg);
}

std::filesystem::path TapeWriter::flushChannel (ChannelId channelId)
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

std::filesystem::path TapeWriter::partialPathFor (ChannelId channelId) const
{
    return partialDir_ / (std::to_string (channelId.value()) + ".tape.partial");
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
            if (it != openFiles_.end() && it->second && it->second->stream)
            {
                it->second->stream->flush();
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
            if (it == openFiles_.end() || ! it->second || ! it->second->stream)
            {
                const auto path = partialPathFor (msg.id);
                auto fresh = std::make_unique<juce::FileOutputStream> (
                    juce::File (juce::String (path.string())));
                if (! fresh->openedOk())
                {
                    juce::Logger::writeToLog ("TapeWriter: cannot open partial file: "
                                              + juce::String (path.string()));
                    errorCounts_[channelKey]++;
                    continue;
                }
                auto of = std::make_unique<OpenFile>();
                of->stream = std::move (fresh);
                it = openFiles_.emplace (channelKey, std::move (of)).first;
            }
            stream = it->second->stream.get();
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
