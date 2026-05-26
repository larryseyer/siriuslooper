#pragma once

#include "ida/ChannelDefaults.h"
#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"
#include "ida/TapeId.h"

#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// One entry in a file-input playlist. Path is absolute. `durationFrames`
/// is cached when the entry's reader is first opened (left empty until
/// then). `missing` is set at load time (file absent on disk) or at the
/// worker-thread's advance step (file moved/deleted mid-session).
struct FileInputEntry
{
    PlaylistEntryId entryId { 0 };
    std::string path;
    std::optional<int> durationFrames;
    bool missing { false };
};

/// Light, free-standing metadata describing a single file-input source.
/// White-paper V9 §6.6 / §7.2: a file input is an ordered playlist of 1+
/// files, edited live during playback. Parallel to InputDescriptor: heavy,
/// immutable tape data lives elsewhere (engine); this descriptor is the
/// thin pointer + presentation state. `windowOpacity` is the persisted
/// translucency of the floating player window (clamped to [0.5, 1.0] on
/// read; default 0.92).
struct FileInputDescriptor
{
    TapeId tapeId { 0 };
    std::string displayName;
    std::vector<FileInputEntry> entries;
    LoopScope loopScope { LoopScope::Off };
    float windowOpacity { 0.92f };
    ChannelDefaults defaults {};
};

} // namespace ida
