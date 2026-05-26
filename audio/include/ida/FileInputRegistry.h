#pragma once

#include "ida/Channel.h"              // ida::InputId
#include "ida/FileInputDescriptor.h"  // ida::FileInputDescriptor
#include "ida/FileInputSource.h"      // ida::FileInputSource
#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace ida
{

/// Owns the FileInputSource instances behind each registered file input plus
/// the persisted descriptor for each. Lives in audio/ (not engine/) because
/// FileInputSource depends on juce_audio_formats, which the engine layer is
/// deliberately kept free of. The API shape mirrors the file-input spec's
/// "InputMixer surface" — the registry is the single source of truth the app
/// layer queries / mutates from the UI thread; Task 11 wires its sources into
/// the audio callback alongside the InputMixer.
class FileInputRegistry
{
public:
    explicit FileInputRegistry (double deviceSampleRate);
    ~FileInputRegistry();

    FileInputRegistry (const FileInputRegistry&) = delete;
    FileInputRegistry& operator= (const FileInputRegistry&) = delete;

    /// Allocates a new InputId, stores the descriptor (copied), constructs a
    /// FileInputSource at the registry's device sample rate, and applies the
    /// descriptor's loopScope + entries to the source. Returns the allocated
    /// InputId.
    InputId registerFileInput (const FileInputDescriptor& desc);

    /// Drops the descriptor + the FileInputSource (whose dtor stops its
    /// worker thread cleanly). No-op if `id` is unknown.
    void unregisterFileInput (InputId id);

    /// Read-only descriptor access. Nullptr if `id` is unknown.
    const FileInputDescriptor* fileInputDescriptor (InputId id) const;

    /// Read-only view of all registered descriptors (UI iteration). Stable
    /// reference until the registry is mutated.
    const std::unordered_map<std::int64_t, FileInputDescriptor>&
        allFileInputDescriptors() const;

    /// Playlist mutation forwarded to the underlying FileInputSource AND
    /// the descriptor's cached entries vector (so the descriptor reflects
    /// reality without a separate query). Returns the new entry id; -1 if
    /// `id` is unknown.
    PlaylistEntryId addFileInputEntry (InputId id, const std::string& path);

    /// Returns false if `id` is unknown OR the source refused removal
    /// (currently-playing entry). On success the descriptor's entries
    /// vector is updated to match.
    bool removeFileInputEntry (InputId id, PlaylistEntryId entry);

    /// Returns false if `id` is unknown OR the source refused reorder.
    /// Mirrors the descriptor's entries order on success.
    bool reorderFileInput (InputId id, PlaylistEntryId entry, int newIndex);

    /// Transport. No-ops if `id` is unknown.
    void playFileInput  (InputId id);
    void pauseFileInput (InputId id);
    void stopFileInput  (InputId id);
    void seekFileInput  (InputId id, std::int64_t frame);
    void setFileInputLoopScope (InputId id, LoopScope scope);

    /// Window opacity. Clamped to [0.5, 1.0] before storing in the
    /// descriptor. No-op if `id` is unknown. (FileInputSource doesn't know
    /// about opacity — descriptor-only state.)
    void setFileInputWindowOpacity (InputId id, float opacity);

    /// Snapshot of live transport state for the UI poll path (30 Hz).
    /// Returns a default-constructed state (isPlaying=false, sentinel
    /// PlaylistEntryId(-1), playheadFrames=0, loopScope=Off) if `id` is
    /// unknown.
    struct FileInputTransportState
    {
        bool            isPlaying     { false };
        PlaylistEntryId currentEntry  { -1 };  ///< sentinel: -1 = no active entry
        std::int64_t    playheadFrames { 0 };
        LoopScope       loopScope     { LoopScope::Off };
    };
    FileInputTransportState fileInputTransportState (InputId id) const;

private:
    FileInputSource*       source_ (InputId id);
    const FileInputSource* source_ (InputId id) const;

    double deviceSampleRate_;
    std::unordered_map<std::int64_t, FileInputDescriptor>           descriptors_;
    std::unordered_map<std::int64_t, std::unique_ptr<FileInputSource>> sources_;

    /// Separate range from device-input ids so the app layer can tell them
    /// apart at a glance during debugging (per spec).
    std::int64_t nextFileInputId_ { 100000 };
};

} // namespace ida
