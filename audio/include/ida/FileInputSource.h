#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ida
{

/// Per-file-input engine: owns the disk-reader stack and (later) the
/// SPSC ring + worker-thread transport loop. White-paper V9 §6.6 / §7.2.
/// Lives in audio/ (not engine/) because it depends on juce_audio_formats.
class FileInputSource : public juce::TimeSliceClient
{
public:
    explicit FileInputSource (double deviceSampleRate);
    ~FileInputSource() override;

    FileInputSource (const FileInputSource&) = delete;
    FileInputSource& operator= (const FileInputSource&) = delete;

    /// Opens `path` and replaces any previously-open reader. Returns
    /// true on success. Message thread only.
    bool openReader (const std::string& path);

    /// Closes the current reader. Safe to call when no reader is open.
    void closeReader();

    /// Introspection on the currently-open reader. All return 0 / 0.0 / 0
    /// when no reader is open.
    int    currentReaderDurationFrames() const noexcept;
    double currentReaderSampleRate()     const noexcept;
    int    currentReaderNumChannels()    const noexcept;

    /// Audio-thread entry point. Pops `numFrames` of stereo samples into
    /// `dest`. If the ring has fewer than `numFrames` available, fills
    /// the tail with silence and increments the underrun counter.
    /// noexcept, no allocation, no locks, no I/O.
    void pullInto (juce::AudioBuffer<float>& dest, int numFrames) noexcept;

    /// Test-only helper. Pushes the contents of `src` into the ring as
    /// if the worker had produced them. NOT for production use.
    void testPushRing (const juce::AudioBuffer<float>& src);

    /// Diagnostic counter. Bumped each time pullInto saw fewer than the
    /// requested frames.
    int underrunCount() const noexcept { return underruns_.load(); }

    /// Transport commands. Message-thread only. Worker picks up on next tick.
    void play()  noexcept;
    void pause() noexcept;
    void stop()  noexcept;   ///< pause + seek to 0
    void seek (std::int64_t frame) noexcept;

    bool         isPlaying()      const noexcept { return isPlaying_.load(); }
    std::int64_t playheadFrames() const noexcept { return playheadFrames_.load(); }

    /// juce::TimeSliceClient
    int useTimeSlice() override;

private:
    static constexpr int kRingFrames = 12000;  // 250 ms stereo @ 48 kHz

    double deviceSampleRate_;
    juce::AudioFormatManager formatManager_;
    std::unique_ptr<juce::AudioFormatReader> currentReader_;

    std::vector<float> ringL_, ringR_;
    std::atomic<int>   writePos_  { 0 };
    std::atomic<int>   readPos_   { 0 };
    std::atomic<int>   underruns_ { 0 };

    juce::TimeSliceThread     workerThread_ { "FileInputSource worker" };
    std::atomic<bool>         isPlaying_      { false };
    std::atomic<std::int64_t> playheadFrames_ { 0 };
    std::atomic<std::int64_t> seekRequest_    { -1 };  // -1 = none
};

} // namespace ida
