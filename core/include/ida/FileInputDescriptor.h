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
/// `entryId` has no default — the engine allocates one at registration;
/// callers always supply an explicit value.
struct FileInputEntry
{
    PlaylistEntryId entryId;
    std::string path;
    std::optional<std::int64_t> durationFrames;
    bool missing { false };
};

/// Light, free-standing metadata describing a single file-input source.
/// White-paper V9 §6.6 / §7.2: a file input is an ordered playlist of 1+
/// files, edited live during playback. Parallel to InputDescriptor: heavy,
/// immutable tape data lives elsewhere (engine); this descriptor is the
/// thin pointer + presentation state. `tapeId` is `std::optional` because
/// the engine assigns it at registerFileInput time — the descriptor exists
/// as configuration before the binding happens; sentinel-zero would be
/// ambiguous given that TapeId(0)/(1) are real tape ids elsewhere.
/// `windowOpacity` is the persisted translucency of the floating player
/// window (clamped to [0.5, 1.0] on read; default 0.92).
struct FileInputDescriptor
{
    std::optional<TapeId> tapeId;
    std::string displayName;
    std::vector<FileInputEntry> entries;
    LoopScope loopScope { LoopScope::Off };
    float windowOpacity { 0.92f };
    ChannelDefaults defaults {};
};

} // namespace ida
