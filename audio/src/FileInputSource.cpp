#include "ida/FileInputSource.h"

#include <algorithm>
#include <cmath>

namespace ida
{

FileInputSource::FileInputSource (double deviceSampleRate)
    : deviceSampleRate_ (deviceSampleRate)
{
    formatManager_.registerBasicFormats();   // WAV + AIFF
    formatManager_.registerFormat (new juce::FlacAudioFormat(), false);

    ringL_.assign ((size_t) kRingFrames, 0.f);
    ringR_.assign ((size_t) kRingFrames, 0.f);

    // Hoist the worker's per-tick buffer to a member: 2 channels x the
    // maximum chunk the worker ever requests. Eliminates the per-tick
    // allocation that the Task 6 implementation incurred.
    scratch_.setSize (2, 2048, false, true, false);

    workerThread_.addTimeSliceClient (this);
    workerThread_.startThread();
}

FileInputSource::~FileInputSource()
{
    workerThread_.removeTimeSliceClient (this);
    workerThread_.stopThread (200);
}

//=================================================================
// Single-reader API (Task 4 surface).
//=================================================================

bool FileInputSource::openReader (const std::string& path)
{
    juce::File file { juce::String (path) };
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader {
        formatManager_.createReaderFor (file) };
    if (reader == nullptr)
        return false;

    // Success path: replace the legacy reader (preserves Task 4's
    // "preserves prior reader on failure" guard — failure returned above
    // before touching state). Then mirror into the playlist so the worker
    // and introspection methods see a unified view.
    currentReader_ = std::move (reader);

    {
        std::lock_guard<std::mutex> lock (listMutex_);
        entries_.clear();
        readerSource_.reset();
        resampler_.reset();
        currentEntryIdValue_.store (-1);

        const PlaylistEntryId id { nextEntryId_++ };
        entries_.push_back ({ id, path, false });
        if (openEntryReader_locked (id))
            currentEntryIdValue_.store (id.value());
    }
    return true;
}

void FileInputSource::closeReader()
{
    currentReader_.reset();
    std::lock_guard<std::mutex> lock (listMutex_);
    readerSource_.reset();
    resampler_.reset();
    currentEntryIdValue_.store (-1);
}

int FileInputSource::currentReaderDurationFrames() const noexcept
{
    // Prefer the playlist's reader-source (Task 7 path); fall back to the
    // legacy single-reader pointer so the "openReader failure preserves
    // prior reader" guard remains queryable.
    if (readerSource_ != nullptr)
        if (auto* r = readerSource_->getAudioFormatReader())
            return static_cast<int> (r->lengthInSamples);

    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->lengthInSamples) : 0;
}

double FileInputSource::currentReaderSampleRate() const noexcept
{
    if (readerSource_ != nullptr)
        if (auto* r = readerSource_->getAudioFormatReader())
            return r->sampleRate;

    return currentReader_ != nullptr ? currentReader_->sampleRate : 0.0;
}

int FileInputSource::currentReaderNumChannels() const noexcept
{
    if (readerSource_ != nullptr)
        if (auto* r = readerSource_->getAudioFormatReader())
            return static_cast<int> (r->numChannels);

    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->numChannels) : 0;
}

//=================================================================
// Audio-thread (Task 5 — unchanged).
//=================================================================

void FileInputSource::pullInto (juce::AudioBuffer<float>& dest, int numFrames) noexcept
{
    jassert (dest.getNumChannels() == 2);   // stereo-only invariant (whitepaper §6.1)

    const int writePos = writePos_.load (std::memory_order_acquire);
    const int readPos  = readPos_ .load (std::memory_order_relaxed);
    const int available = (writePos - readPos + kRingFrames) % kRingFrames;

    const int toPull = juce::jmin (numFrames, available);
    int rp = readPos;
    for (int i = 0; i < toPull; ++i)
    {
        dest.setSample (0, i, ringL_[(size_t) rp]);
        if (dest.getNumChannels() > 1)
            dest.setSample (1, i, ringR_[(size_t) rp]);
        rp = (rp + 1) % kRingFrames;
    }
    readPos_.store (rp, std::memory_order_release);

    if (toPull < numFrames)
    {
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            for (int i = toPull; i < numFrames; ++i)
                dest.setSample (ch, i, 0.f);
        underruns_.fetch_add (1, std::memory_order_relaxed);
    }
}

void FileInputSource::testPushRing (const juce::AudioBuffer<float>& src)
{
    const int n = src.getNumSamples();
    jassert (n <= kRingFrames);

    int wp = writePos_.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        ringL_[(size_t) wp] = src.getSample (0, i);
        ringR_[(size_t) wp] = src.getNumChannels() > 1
                              ? src.getSample (1, i) : src.getSample (0, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
}

//=================================================================
// Transport (Task 6).
//=================================================================

void FileInputSource::play()  noexcept { isPlaying_.store (true);  }
void FileInputSource::pause() noexcept { isPlaying_.store (false); }

void FileInputSource::stop() noexcept
{
    isPlaying_.store (false);
    seek (0);
    playheadFrames_.store (0);
}

void FileInputSource::seek (std::int64_t frame) noexcept { seekRequest_.store (frame); }

//=================================================================
// Playlist (Task 7).
//=================================================================

PlaylistEntryId FileInputSource::addEntry (const std::string& path)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    const PlaylistEntryId id { nextEntryId_++ };
    const bool missing = ! juce::File (juce::String (path)).existsAsFile();
    entries_.push_back ({ id, path, missing });
    return id;
}

bool FileInputSource::removeEntry (PlaylistEntryId id)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    if (currentEntryIdValue_.load() == id.value() && isPlaying_.load())
        return false;

    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    entries_.erase (it);
    return true;
}

bool FileInputSource::reorderEntry (PlaylistEntryId id, int newIndex)
{
    std::lock_guard<std::mutex> lock (listMutex_);
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    Entry moved = *it;
    entries_.erase (it);
    newIndex = juce::jlimit (0, (int) entries_.size(), newIndex);
    entries_.insert (entries_.begin() + newIndex, moved);
    return true;
}

void FileInputSource::setLoopScope (LoopScope scope) noexcept { loopScope_.store (scope); }

PlaylistEntryId FileInputSource::currentEntry() const noexcept
{
    return PlaylistEntryId { currentEntryIdValue_.load() };
}

bool FileInputSource::entryMissing (PlaylistEntryId id) const
{
    std::lock_guard<std::mutex> lock (listMutex_);
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    return it != entries_.end() && it->missing;
}

int FileInputSource::entryCount() const
{
    std::lock_guard<std::mutex> lock (listMutex_);
    return (int) entries_.size();
}

bool FileInputSource::openEntryReader_locked (PlaylistEntryId id)
{
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [id] (const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    juce::File file { juce::String (it->path) };
    if (! file.existsAsFile())   { it->missing = true; return false; }

    std::unique_ptr<juce::AudioFormatReader> reader {
        formatManager_.createReaderFor (file) };
    if (reader == nullptr)       { it->missing = true; return false; }

    const double fileSr = reader->sampleRate;

    // AudioFormatReaderSource takes ownership when deleteSourceWhenDeleted=true.
    readerSource_ = std::make_unique<juce::AudioFormatReaderSource> (
        reader.release(), true);
    readerSource_->setLooping (false);
    readerSource_->prepareToPlay (2048, deviceSampleRate_);

    if (std::abs (fileSr - deviceSampleRate_) > 0.5)
    {
        resampler_ = std::make_unique<juce::ResamplingAudioSource> (
            readerSource_.get(), false, 2);
        // ResamplingAudioSource scales by `ratio` — to play a 44.1 k file at
        // a 48 k device rate, the source must be advanced at 44.1/48 input
        // samples per output sample.
        resampler_->setResamplingRatio (fileSr / deviceSampleRate_);
        resampler_->prepareToPlay (2048, deviceSampleRate_);
    }
    else
    {
        resampler_.reset();
    }
    return true;
}

PlaylistEntryId FileInputSource::nextEntryId_locked (PlaylistEntryId current) const
{
    auto it = std::find_if (entries_.begin(), entries_.end(),
                            [current] (const Entry& e) { return e.id == current; });
    if (it == entries_.end()) return PlaylistEntryId { -1 };

    // Skip missing entries to the right.
    auto next = std::next (it);
    while (next != entries_.end() && next->missing) ++next;
    if (next != entries_.end()) return next->id;

    // Off + last entry: stop. List + last entry: wrap to first non-missing.
    if (loopScope_.load() == LoopScope::List)
    {
        auto first = std::find_if (entries_.begin(), entries_.end(),
                                   [] (const Entry& e) { return ! e.missing; });
        if (first != entries_.end() && first->id != current) return first->id;
    }
    return PlaylistEntryId { -1 };
}

//=================================================================
// Worker tick (Task 7 — playlist-aware).
//=================================================================

int FileInputSource::useTimeSlice()
{
    // Apply pending seek.
    const auto seekReq = seekRequest_.exchange (-1);
    if (seekReq >= 0)
    {
        if (readerSource_ != nullptr)
            readerSource_->setNextReadPosition (seekReq);
        playheadFrames_.store (seekReq);
    }

    if (! isPlaying_.load())
        return 50;   // sleep 50 ms

    // Open the first valid entry if no reader-source is active yet. The
    // legacy single-reader path (`currentReader_` set, `readerSource_`
    // null) also passes through here on first play; we rebuild the
    // playlist-style stack from `entries_[0]` so the unified worker path
    // takes over (the legacy reader stays put as the prior-reader fallback).
    if (readerSource_ == nullptr)
    {
        std::lock_guard<std::mutex> lock (listMutex_);
        for (auto& e : entries_)
        {
            if (! e.missing && openEntryReader_locked (e.id))
            {
                currentEntryIdValue_.store (e.id.value());
                break;
            }
            // openEntryReader_locked already flagged it missing if open failed.
        }
        if (readerSource_ == nullptr)
        {
            isPlaying_.store (false);
            return 50;
        }
    }

    // Ring headroom.
    const int writePos = writePos_.load (std::memory_order_relaxed);
    const int readPos  = readPos_ .load (std::memory_order_acquire);
    const int free     = (readPos - writePos - 1 + kRingFrames) % kRingFrames;
    if (free < 256)
        return 5;   // ring well-fed, check back soon

    const int chunk = juce::jmin (free, 2048);
    juce::AudioSourceChannelInfo info { &scratch_, 0, chunk };

    if (resampler_ != nullptr) resampler_->getNextAudioBlock (info);
    else                       readerSource_->getNextAudioBlock (info);

    int wp = writePos;
    for (int i = 0; i < chunk; ++i)
    {
        ringL_[(size_t) wp] = scratch_.getSample (0, i);
        ringR_[(size_t) wp] = scratch_.getNumChannels() > 1
                              ? scratch_.getSample (1, i)
                              : scratch_.getSample (0, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
    playheadFrames_.fetch_add (chunk);

    // EOF? Advance per LoopScope.
    if (readerSource_->getNextReadPosition() >= readerSource_->getTotalLength())
    {
        const auto scope = loopScope_.load();
        if (scope == LoopScope::Track)
        {
            readerSource_->setNextReadPosition (0);
            playheadFrames_.store (0);
        }
        else
        {
            std::lock_guard<std::mutex> lock (listMutex_);
            const auto curId = PlaylistEntryId { currentEntryIdValue_.load() };
            auto next = nextEntryId_locked (curId);
            if (next.value() < 0)
            {
                // End of list (Off) or no non-missing successor (List).
                isPlaying_.store (false);
                readerSource_.reset();
                resampler_.reset();
                currentEntryIdValue_.store (-1);
            }
            else
            {
                openEntryReader_locked (next);
                currentEntryIdValue_.store (next.value());
                playheadFrames_.store (0);
            }
        }
    }

    return 1;
}

} // namespace ida
