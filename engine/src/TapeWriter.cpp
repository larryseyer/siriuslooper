#include "ida/TapeWriter.h"

#include "ida/NotificationBus.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace ida
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
        assert (flushRequestForChannel_ == -1
                && "TapeWriter::flushChannel called concurrently — see header doc");
        flushRequestForChannel_ = channelId.value();
    }
    flushRequestPending_.store (true, std::memory_order_release);
    wakeCv_.notify_all();

    std::unique_lock lk (stateMutex_);
    flushCompleteCv_.wait (lk, [this]
    {
        return flushRequestForChannel_ == -1;
    });

    return partialPathFor (channelId);
}

void TapeWriter::touchParamsPartial (ChannelId channelId)
{
    std::scoped_lock lk (stateMutex_);
    const std::filesystem::path path = partialDir_
        / (std::to_string (channelId.value()) + ".params.partial");
    if (! std::filesystem::exists (path))
    {
        std::ofstream ofs (path); // create empty file
    }
}

void TapeWriter::setNotificationBus (NotificationBus* bus) noexcept
{
    notificationBus_ = bus;
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
                    || ! queue_.empty()
                    || flushRequestPending_.load (std::memory_order_acquire);
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
        flushRequestPending_.store (false, std::memory_order_release);
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
                    // M6 Session 2 — truthfulness post on the writer thread.
                    // Not RT-safe-constrained (writer thread, not audio
                    // thread); the post itself is allocation-free regardless.
                    // Channel id in the message so the S3 UI surface can
                    // disambiguate concurrent failures across channels.
                    if (notificationBus_ != nullptr)
                    {
                        char notifyMsg[128];
                        std::snprintf (notifyMsg, sizeof (notifyMsg),
                                       "tape flush failed (ch %lld) — cannot open partial file",
                                       static_cast<long long> (channelKey));
                        notificationBus_->post (NotificationLevel::Error,
                                                Category::DiskPressure, notifyMsg);
                    }
                    continue;
                }
                auto of = std::make_unique<OpenFile>();
                of->stream = std::move (fresh);
                it = openFiles_.emplace (channelKey, std::move (of)).first;
            }
            stream = it->second->stream.get();
        }

        if (stream == nullptr) continue;

        const bool ok = stream->write (msg.samples.data(), msg.payloadByteCount);
        if (! ok)
        {
            juce::Logger::writeToLog ("TapeWriter: write failed for channel "
                                      + juce::String (channelKey));
            std::scoped_lock lk (stateMutex_);
            errorCounts_[channelKey]++;
            // M6 Session 2 — truthfulness post on the writer thread (see
            // the openedOk failure branch above for the rationale). Channel
            // id in the message so the S3 UI surface can disambiguate.
            if (notificationBus_ != nullptr)
            {
                char notifyMsg[128];
                std::snprintf (notifyMsg, sizeof (notifyMsg),
                               "tape flush failed (ch %lld) — write error, see logs",
                               static_cast<long long> (channelKey));
                notificationBus_->post (NotificationLevel::Error,
                                        Category::DiskPressure, notifyMsg);
            }
        }
    }
}

} // namespace ida
