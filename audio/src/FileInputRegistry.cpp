#include "ida/FileInputRegistry.h"

#include <algorithm>
#include <utility>

namespace ida
{

FileInputRegistry::FileInputRegistry (double deviceSampleRate)
    : deviceSampleRate_ (deviceSampleRate)
{
}

FileInputRegistry::~FileInputRegistry() = default;

InputId FileInputRegistry::registerFileInput (const FileInputDescriptor& desc)
{
    const auto idValue = nextFileInputId_++;
    const InputId id { idValue };

    descriptors_.emplace (idValue, desc);

    auto src = std::make_unique<FileInputSource> (deviceSampleRate_);

    // Apply the descriptor's loopScope first so the worker honours it as
    // soon as entries are added.
    src->setLoopScope (desc.loopScope);

    // Walk the descriptor's entries and register each with the source. We
    // also rewrite the descriptor's entry ids to match what the source
    // allocates so subsequent lookups (remove/reorder) line up.
    auto& storedDesc = descriptors_.at (idValue);
    for (auto& entry : storedDesc.entries)
        entry.entryId = src->addEntry (entry.path);

    sources_.emplace (idValue, std::move (src));
    return id;
}

void FileInputRegistry::unregisterFileInput (InputId id)
{
    // FileInputSource's dtor joins its worker thread. Erasing from
    // sources_ here triggers that join cleanly.
    sources_.erase (id.value());
    descriptors_.erase (id.value());
}

const FileInputDescriptor* FileInputRegistry::fileInputDescriptor (InputId id) const
{
    const auto it = descriptors_.find (id.value());
    return it == descriptors_.end() ? nullptr : &it->second;
}

const std::unordered_map<std::int64_t, FileInputDescriptor>&
    FileInputRegistry::allFileInputDescriptors() const
{
    return descriptors_;
}

PlaylistEntryId FileInputRegistry::addFileInputEntry (InputId id, const std::string& path)
{
    auto* src = source_ (id);
    if (src == nullptr)
        return PlaylistEntryId { -1 };

    const auto entryId = src->addEntry (path);

    auto& desc = descriptors_.at (id.value());
    desc.entries.push_back (FileInputEntry { entryId, path, std::nullopt, false });

    return entryId;
}

bool FileInputRegistry::removeFileInputEntry (InputId id, PlaylistEntryId entry)
{
    auto* src = source_ (id);
    if (src == nullptr)
        return false;

    if (! src->removeEntry (entry))
        return false;

    auto& desc = descriptors_.at (id.value());
    desc.entries.erase (
        std::remove_if (desc.entries.begin(), desc.entries.end(),
                        [entry] (const FileInputEntry& e) { return e.entryId == entry; }),
        desc.entries.end());
    return true;
}

bool FileInputRegistry::reorderFileInput (InputId id, PlaylistEntryId entry, int newIndex)
{
    auto* src = source_ (id);
    if (src == nullptr)
        return false;

    if (! src->reorderEntry (entry, newIndex))
        return false;

    // Mirror the reorder in the descriptor's entries vector so the UI's
    // view matches the source's worker view.
    auto& desc = descriptors_.at (id.value());
    const auto it = std::find_if (desc.entries.begin(), desc.entries.end(),
                                  [entry] (const FileInputEntry& e) { return e.entryId == entry; });
    if (it == desc.entries.end())
        return true;  // source accepted; descriptor was already out of sync — nothing to mirror.

    FileInputEntry moved = *it;
    desc.entries.erase (it);

    const int clamped = std::clamp (newIndex, 0, static_cast<int> (desc.entries.size()));
    desc.entries.insert (desc.entries.begin() + clamped, std::move (moved));
    return true;
}

void FileInputRegistry::playFileInput (InputId id)
{
    if (auto* src = source_ (id))
        src->play();
}

void FileInputRegistry::pauseFileInput (InputId id)
{
    if (auto* src = source_ (id))
        src->pause();
}

void FileInputRegistry::stopFileInput (InputId id)
{
    if (auto* src = source_ (id))
        src->stop();
}

void FileInputRegistry::seekFileInput (InputId id, std::int64_t frame)
{
    if (auto* src = source_ (id))
        src->seek (frame);
}

void FileInputRegistry::setFileInputLoopScope (InputId id, LoopScope scope)
{
    auto* src = source_ (id);
    if (src == nullptr)
        return;

    src->setLoopScope (scope);
    descriptors_.at (id.value()).loopScope = scope;
}

void FileInputRegistry::setFileInputWindowOpacity (InputId id, float opacity)
{
    const auto it = descriptors_.find (id.value());
    if (it == descriptors_.end())
        return;

    it->second.windowOpacity = std::clamp (opacity, 0.5f, 1.0f);
}

void FileInputRegistry::setFileInputAlwaysOnTop (InputId id, bool onTop)
{
    if (auto it = descriptors_.find (id.value()); it != descriptors_.end())
        it->second.alwaysOnTop = onTop;
}

FileInputRegistry::FileInputTransportState
    FileInputRegistry::fileInputTransportState (InputId id) const
{
    const auto* src = source_ (id);
    if (src == nullptr)
        return {};

    FileInputTransportState state;
    state.isPlaying      = src->isPlaying();
    state.currentEntry   = src->currentEntry();
    state.playheadFrames = src->playheadFrames();
    state.loopScope      = src->loopScope();
    return state;
}

FileInputPullCallable FileInputRegistry::resolveFileInputPull (InputId id) noexcept
{
    if (auto* src = source_ (id))
        return FileInputPullCallable { &FileInputSource::pullIntoStatic, src };
    return FileInputPullCallable {};
}

void FileInputRegistry::onOttoTransport (const TransportSnapshot& snapshot)
{
    // M-OTTO-3b — first OttoHost subscriber. Stub policy: record only.
    // Per-input "follow transport" semantics (auto-play armed inputs on
    // Started, etc.) are downstream UX work; the operator's mixer-first
    // roadmap parks transport behaviour until after the mixers ship.
    // Wiring the receiver now keeps that later slice purely behavioural.
    lastOttoTransport_ = snapshot;
}

FileInputSource* FileInputRegistry::source_ (InputId id)
{
    const auto it = sources_.find (id.value());
    return it == sources_.end() ? nullptr : it->second.get();
}

const FileInputSource* FileInputRegistry::source_ (InputId id) const
{
    const auto it = sources_.find (id.value());
    return it == sources_.end() ? nullptr : it->second.get();
}

} // namespace ida
