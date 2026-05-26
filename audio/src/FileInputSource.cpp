#include "ida/FileInputSource.h"

namespace ida
{

FileInputSource::FileInputSource (double deviceSampleRate)
    : deviceSampleRate_ (deviceSampleRate)
{
    formatManager_.registerBasicFormats();   // WAV + AIFF
    formatManager_.registerFormat (new juce::FlacAudioFormat(), false);

    ringL_.assign ((size_t) kRingFrames, 0.f);
    ringR_.assign ((size_t) kRingFrames, 0.f);

    workerThread_.addTimeSliceClient (this);
    workerThread_.startThread();
}

FileInputSource::~FileInputSource()
{
    workerThread_.removeTimeSliceClient (this);
    workerThread_.stopThread (200);
}

bool FileInputSource::openReader (const std::string& path)
{
    juce::File file { juce::String (path) };
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader {
        formatManager_.createReaderFor (file) };
    if (reader == nullptr)
        return false;

    // Failure paths above leave currentReader_ intact; success path is
    // the only mutator. Guarded by the "preserves prior reader" test.
    currentReader_ = std::move (reader);
    return true;
}

void FileInputSource::closeReader()
{
    currentReader_.reset();
}

int FileInputSource::currentReaderDurationFrames() const noexcept
{
    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->lengthInSamples) : 0;
}

double FileInputSource::currentReaderSampleRate() const noexcept
{
    return currentReader_ != nullptr ? currentReader_->sampleRate : 0.0;
}

int FileInputSource::currentReaderNumChannels() const noexcept
{
    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->numChannels) : 0;
}

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
    jassert (n <= kRingFrames);   // helper has no overrun guard — keep test pushes bounded

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

void FileInputSource::play()  noexcept { isPlaying_.store (true);  }
void FileInputSource::pause() noexcept { isPlaying_.store (false); }

void FileInputSource::stop() noexcept
{
    isPlaying_.store (false);
    seek (0);
    // Observable-on-return semantics: callers expect playheadFrames()==0
    // immediately after stop(). The worker also honors seekRequest_, so
    // this is consistent if the worker fires mid-call.
    playheadFrames_.store (0);
}

void FileInputSource::seek (std::int64_t frame) noexcept { seekRequest_.store (frame); }

int FileInputSource::useTimeSlice()
{
    // Apply pending seek.
    const auto seekReq = seekRequest_.exchange (-1);
    if (seekReq >= 0 && currentReader_ != nullptr)
        playheadFrames_.store (seekReq);

    if (! isPlaying_.load() || currentReader_ == nullptr)
        return 50;   // sleep 50 ms

    // Acquire-load readPos_ — symmetric to the consumer's release-store.
    const int writePos = writePos_.load (std::memory_order_relaxed);
    const int readPos  = readPos_ .load (std::memory_order_acquire);
    const int free     = (readPos - writePos - 1 + kRingFrames) % kRingFrames;
    if (free < 256)
        return 5;   // ring well-fed, check back soon

    const int chunk = juce::jmin (free, 2048);
    juce::AudioBuffer<float> scratch (juce::jmax (2, (int) currentReader_->numChannels), chunk);

    const auto head = playheadFrames_.load();
    const bool readOk = currentReader_->read (&scratch, 0, chunk, head, true, true);
    juce::ignoreUnused (readOk);

    int wp = writePos;
    for (int i = 0; i < chunk; ++i)
    {
        ringL_[(size_t) wp] = scratch.getSample (0, i);
        ringR_[(size_t) wp] = scratch.getNumChannels() > 1
                              ? scratch.getSample (1, i)
                              : scratch.getSample (0, i);
        wp = (wp + 1) % kRingFrames;
    }
    writePos_.store (wp, std::memory_order_release);
    playheadFrames_.fetch_add (chunk);

    return 1;
}

} // namespace ida
