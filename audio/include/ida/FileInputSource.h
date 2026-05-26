#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ida
{

/// Per-file-input engine: owns the disk-reader stack, an SPSC ring, and a
/// worker-thread transport loop driving a playlist of 1+ audio files.
/// White-paper V9 §6.6 / §7.2. Lives in audio/ (not engine/) because it
/// depends on juce_audio_formats.
class FileInputSource : public juce::TimeSliceClient
{
public:
    explicit FileInputSource (double deviceSampleRate);
    ~FileInputSource() override;

    FileInputSource (const FileInputSource&) = delete;
    FileInputSource& operator= (const FileInputSource&) = delete;

    //=================================================================
    // Single-reader API (Task 4 surface — retained for tests & callers
    // that only need one file open; internally now routes through the
    // playlist so introspection methods cover both paths uniformly).
    //=================================================================

    /// Clears the playlist, appends `path`, and opens its reader.
    /// Returns true on success. On failure the prior reader survives
    /// (the playlist is rolled back). Message thread only.
    bool openReader (const std::string& path);

    /// Closes the current reader. Safe to call when no reader is open.
    void closeReader();

    /// Introspection on the currently-open reader. All return 0 / 0.0 / 0
    /// when no reader is open. Prefer the playlist's reader-source when
    /// active (Task 7 path); fall back to the legacy `currentReader_`
    /// (preserves Task 4's "openReader failure leaves prior reader intact"
    /// guard, which only the legacy pointer can express).
    int    currentReaderDurationFrames() const noexcept;
    double currentReaderSampleRate()     const noexcept;
    int    currentReaderNumChannels()    const noexcept;

    //=================================================================
    // Audio-thread (Task 5 surface — unchanged).
    //=================================================================

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

    //=================================================================
    // Transport (Task 6).
    //=================================================================

    void play()  noexcept;
    void pause() noexcept;
    void stop()  noexcept;   ///< pause + seek to 0
    void seek (std::int64_t frame) noexcept;

    bool         isPlaying()      const noexcept { return isPlaying_.load(); }
    std::int64_t playheadFrames() const noexcept { return playheadFrames_.load(); }

    //=================================================================
    // Playlist (Task 7). Message-thread only for mutations. The worker
    // holds `listMutex_` for the duration of a reader-open, which does
    // synchronous disk I/O (file existence check + format decode of the
    // header — typically <10 ms for WAV/FLAC; longer on slow disks or
    // during the first-tick bootstrap of a long playlist with many
    // missing entries, where the worker walks the list trying each in
    // turn). UI mutations may briefly block on this. The audio thread
    // NEVER touches `listMutex_`.
    //=================================================================

    /// Appends a playlist entry. Returns the new entry's stable id.
    PlaylistEntryId addEntry    (const std::string& path);

    /// Removes an entry by id. Refuses (returns false) when `id` is the
    /// currently-playing entry and playback is active. Returns false if
    /// the id is unknown.
    bool            removeEntry (PlaylistEntryId id);

    /// Moves `id` to `newIndex` (clamped). Returns false if the id is
    /// unknown. Safe to call during playback — the worker's "current
    /// entry" lookup is by id, so the active reader continues
    /// uninterrupted regardless of position changes.
    bool            reorderEntry (PlaylistEntryId id, int newIndex);

    void            setLoopScope (LoopScope scope) noexcept;
    LoopScope       loopScope()    const noexcept { return loopScope_.load(); }

    /// Id of the entry the worker is currently reading from. Returns a
    /// sentinel id of -1 when no entry is active.
    PlaylistEntryId currentEntry() const noexcept;

    /// True if `id` has been flagged missing (file absent on disk or
    /// unreadable). Lazily set by the worker the first time it tries
    /// to open the entry.
    bool            entryMissing (PlaylistEntryId id) const;

    int             entryCount() const;

    //=================================================================
    // juce::TimeSliceClient
    //=================================================================
    int useTimeSlice() override;

private:
    /// Per-entry record. The worker walks this vector under
    /// `listMutex_`; the audio thread never touches it.
    struct Entry
    {
        PlaylistEntryId id;
        std::string     path;
        bool            missing { false };
    };

    /// Open the reader for the entry with the given id and (re)build the
    /// `readerSource_`/`resampler_` stack. Caller must hold `listMutex_`.
    /// Returns false (and marks the entry missing) on file-not-found or
    /// unreadable-format.
    bool openEntryReader_locked (PlaylistEntryId id);

    /// Successor of `current` honoring LoopScope::List wrap and
    /// missing-entry skip. Returns a sentinel id of -1 when no
    /// successor exists (Off + last entry or all-missing). Caller must
    /// hold `listMutex_`.
    PlaylistEntryId nextEntryId_locked (PlaylistEntryId current) const;

    static constexpr int kRingFrames = 12000;  // 250 ms stereo @ 48 kHz

    double deviceSampleRate_;
    juce::AudioFormatManager formatManager_;

    /// Legacy single-reader pointer. Kept alongside the playlist's
    /// `readerSource_` so the "openReader failure preserves prior
    /// reader" guard (Task 4 test) keeps the prior reader's metadata
    /// queryable. Routing for `current*` introspection prefers
    /// `readerSource_` when present (playlist path), falls back to this.
    std::unique_ptr<juce::AudioFormatReader> currentReader_;

    std::vector<float> ringL_, ringR_;
    std::atomic<int>   writePos_  { 0 };
    std::atomic<int>   readPos_   { 0 };
    std::atomic<int>   underruns_ { 0 };

    juce::TimeSliceThread     workerThread_ { "FileInputSource worker" };
    std::atomic<bool>         isPlaying_      { false };
    std::atomic<std::int64_t> playheadFrames_ { 0 };
    std::atomic<std::int64_t> seekRequest_    { -1 };  // -1 = none

    // Playlist state. `currentEntryIdValue_` is an atomic mirror of the
    // worker's "current entry" id so currentEntry() is lock-free for the
    // UI thread.
    mutable std::mutex                            listMutex_;
    std::vector<Entry>                            entries_;
    std::int64_t                                  nextEntryId_ { 1 };
    std::atomic<std::int64_t>                     currentEntryIdValue_ { -1 };
    std::atomic<LoopScope>                        loopScope_ { LoopScope::Off };

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
    std::unique_ptr<juce::ResamplingAudioSource>   resampler_;

    /// Per-tick scratch buffer for the worker — hoisted to a member so
    /// useTimeSlice() does not allocate. Sized once on construction to
    /// `2 channels × 2048 frames` (the maximum the worker requests per
    /// tick via `juce::jmin (free, 2048)`).
    juce::AudioBuffer<float> scratch_;
};

} // namespace ida
