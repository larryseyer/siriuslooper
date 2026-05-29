#pragma once

#include "ida/Channel.h"      // OutputChannelId

#include <unordered_map>

namespace ida { class OttoHost; class OutputMixer; }

namespace ida::app
{

/// S6 (2026-05-29) — post-import OTTO strip rebind. Iterates every channel in
/// `mix`; for channels whose `channelOttoSource() >= 0`, binds the OTTO output
/// L/R pointers via `OttoHost::getOttoOutputLeft/Right(ottoSource)` AND inserts
/// `(ottoSource → OutputChannelId)` into `ottoMap`.
///
/// Idempotent: calling twice produces the same map and rebinds the same
/// pointers (the underlying setters are write-only). Safe to call on a mixer
/// with no OTTO channels — the map is left unchanged.
///
/// `host` MUST be prepared (`host.prepare(sr, blockSize)` already called)
/// BEFORE this is invoked; OTTO's per-output buffer pointers are stable for
/// the OttoHost's lifetime but null pre-prepare. Channels whose source pointer
/// returns null are skipped.
///
/// Existing entries in `ottoMap` are overwritten in-place if their key
/// (ottoSource) matches a freshly-rebound channel — this is the
/// "operator imports a session into a session that already has OTTO strips"
/// path. Existing entries whose key has no corresponding `ottoSource>=0`
/// channel in the imported state are NOT removed by this helper; the caller
/// is responsible for clearing the map BEFORE calling rebind if the import
/// is a full replace.
void rebindOttoChannelsAfterImport (OutputMixer& mix,
                                    OttoHost& host,
                                    std::unordered_map<int, OutputChannelId>& ottoMap);

} // namespace ida::app
