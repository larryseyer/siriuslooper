#pragma once

#include "ida/IdaProject.h"
#include "ida/TapeId.h"

#include <juce_core/juce_core.h>

#include <string>

namespace ida::persistence
{

/// The on-disk file name for one tape: `tape_<x>.idatape`, where `x` is the
/// 1-based `TapeId::value()` (spec §2.2). The container format (FLAC or PCM) is
/// a tier decision made elsewhere; the extension stays `.idatape` so existing
/// record containers keep their identity. The grouper (project folder) carries
/// the project identity, so the file name itself stays minimal.
std::string tapeFileName (TapeId id);

/// The project's tape directory: `<appSupportRoot>/<project.folderId()>`. This is
/// the grouper folder that holds the project's tapes directly (spec §2.2,
/// replacing the flat `…/IDA/tapes/`). `appSupportRoot` is injected (the app
/// passes `idaAppSupportDirectory()`; tests pass a temp dir).
juce::File projectTapesDir (const juce::File& appSupportRoot, const IdaProject& project);

/// The fully-qualified path of one tape:
/// `<appSupportRoot>/<project.folderId()>/tape_<x>.idatape`. Every tape path is a
/// child of its project folder — no tape ever lives outside an owning project
/// (the structural expression of §2.1 "no orphan tapes").
juce::File tapeFileFor (const juce::File& appSupportRoot, const IdaProject& project, TapeId id);

} // namespace ida::persistence
