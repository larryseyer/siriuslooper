#include "ida/WetCaptureWriter.h"

#include "ida/NotificationBus.h"
#include "ida/TapeStore.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cstdio>
#include <stdexcept>

namespace ida
{

struct WetOpenFile { std::unique_ptr<juce::FileOutputStream> stream; };

WetCaptureWriter::WetCaptureWriter (std::filesystem::path     partialDir,
                                    std::chrono::milliseconds flushInterval,
                                    std::size_t               queueCapacity)
    : partialDir_ (std::move (partialDir)),
      flushInterval_ (flushInterval),
      queue_ (queueCapacity)
{
    if (! std::filesystem::exists (partialDir_))
    {
        std::error_code ec;
        std::filesystem::create_directories (partialDir_, ec);
        if (ec)
            throw std::runtime_error ("WetCaptureWriter: cannot create partial directory: "
                                      + partialDir_.string() + " — " + ec.message());
    }

    worker_ = std::thread (&WetCaptureWriter::workerLoop, this);
}

WetCaptureWriter::~WetCaptureWriter()
{
    {
        std::scoped_lock lk (wakeMutex_);
        shouldExit_ = true;
    }
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    writePendingMessages(); // final drain

    std::scoped_lock lk (stateMutex_);
    for (auto& [_, of] : openFiles_)
        if (of && of->stream) of->stream->flush();
}

bool WetCaptureWriter::tryEnqueueWet (ChannelId           id,
                                      Rational            lmcTime,
                                      const float* const* channels,
                                      int                 numChannels,
                                      int                 numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return false;

    const std::size_t floatCount = static_cast<std::size_t> (numChannels)
                                 * static_cast<std::size_t> (numSamples);
    const std::size_t byteCount = floatCount * sizeof (float);
    if (byteCount > kMaxTapeWriteMessageBytes)
        return false; // oversized buffer — drop (parallel to dry per-message cap)

    WetWriteMessage msg;
    msg.id = id;
    msg.lmcTime = lmcTime;
    msg.payloadByteCount = byteCount;

    // Interleave planar → [ch0 s0, ch1 s0, ch0 s1, ...] directly into the POD.
    auto* dst = reinterpret_cast<float*> (msg.samples.data());
    for (int s = 0; s < numSamples; ++s)
        for (int c = 0; c < numChannels; ++c)
            *dst++ = channels[c][s];

    return queue_.push (msg);
}

std::filesystem::path WetCaptureWriter::flushChannel (ChannelId channelId)
{
    {
        std::scoped_lock lk (stateMutex_);
        assert (flushRequestForChannel_ == -1
                && "WetCaptureWriter::flushChannel called concurrently — see header doc");
        flushRequestForChannel_ = channelId.value();
    }
    flushRequestPending_.store (true, std::memory_order_release);
    wakeCv_.notify_all();

    std::unique_lock lk (stateMutex_);
    flushCompleteCv_.wait (lk, [this] { return flushRequestForChannel_ == -1; });

    return partialPathFor (channelId);
}

juce::String WetCaptureWriter::finalizeToStore (ChannelId channelId, persistence::TapeStore& store)
{
    const auto path = flushChannel (channelId);
    if (! std::filesystem::exists (path))
        return {};

    juce::File partial (juce::String (path.string()));
    juce::MemoryBlock bytes;
    if (! partial.loadFileAsData (bytes) || bytes.getSize() == 0)
        return {};

    const juce::String hash = store.store (bytes);
    partial.deleteFile();
    return hash;
}

void WetCaptureWriter::setNotificationBus (NotificationBus* bus) noexcept
{
    notificationBus_ = bus;
}

std::uint32_t WetCaptureWriter::errorCountForChannel (ChannelId channelId) const
{
    std::scoped_lock lk (stateMutex_);
    const auto it = errorCounts_.find (channelId.value());
    return it == errorCounts_.end() ? 0u : it->second;
}

std::filesystem::path WetCaptureWriter::partialPathFor (ChannelId channelId) const
{
    return partialDir_ / (std::to_string (channelId.value()) + ".wet.tape.partial");
}

void WetCaptureWriter::workerLoop()
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
                it->second.reset();
                openFiles_.erase (it);
            }
            flushRequestForChannel_ = -1;
            flushCompleteCv_.notify_all();
        }
        flushRequestPending_.store (false, std::memory_order_release);
    }
}

void WetCaptureWriter::writePendingMessages()
{
    WetWriteMessage msg;
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
                    juce::Logger::writeToLog ("WetCaptureWriter: cannot open partial file: "
                                              + juce::String (path.string()));
                    errorCounts_[channelKey]++;
                    if (notificationBus_ != nullptr)
                    {
                        char notifyMsg[128];
                        std::snprintf (notifyMsg, sizeof (notifyMsg),
                                       "wet flush failed (ch %lld) — cannot open partial file",
                                       static_cast<long long> (channelKey));
                        notificationBus_->post (NotificationLevel::Error,
                                                Category::DiskPressure, notifyMsg);
                    }
                    continue;
                }
                auto of = std::make_unique<WetOpenFile>();
                of->stream = std::move (fresh);
                it = openFiles_.emplace (channelKey, std::move (of)).first;
            }
            stream = it->second->stream.get();
        }

        if (stream == nullptr) continue;

        const bool ok = stream->write (msg.samples.data(), msg.payloadByteCount);
        if (! ok)
        {
            juce::Logger::writeToLog ("WetCaptureWriter: write failed for channel "
                                      + juce::String (channelKey));
            std::scoped_lock lk (stateMutex_);
            errorCounts_[channelKey]++;
            if (notificationBus_ != nullptr)
            {
                char notifyMsg[128];
                std::snprintf (notifyMsg, sizeof (notifyMsg),
                               "wet flush failed (ch %lld) — write error, see logs",
                               static_cast<long long> (channelKey));
                notificationBus_->post (NotificationLevel::Error,
                                        Category::DiskPressure, notifyMsg);
            }
        }
    }
}

} // namespace ida
