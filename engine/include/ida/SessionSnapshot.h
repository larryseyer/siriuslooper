#pragma once

#include "ida/Constituent.h"
#include "ida/INotificationSink.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sirius
{

class OutOfProcessEffectChainHost;

/// Locates one EffectChainEntry within the host's slot map.
struct SlotLocation
{
    std::int64_t busId;
    std::size_t  slotIndex;
};

/// Callback that translates an `(owningConstituent, entryIndex)`
/// position within the session graph to the corresponding host slot.
/// Returns nullopt if the entry is not currently configured in the
/// host (e.g. nested chains the engine hasn't wired yet).
using SlotLookup = std::function<std::optional<SlotLocation> (
    const Constituent& owningConstituent, std::size_t entryIndex)>;

/// Save-side helper (white paper §15.6).
///
/// Walks `root` and returns a copy-on-write Constituent tree whose
/// `EffectChainEntry` slots (those with `archivalMode == VersionPinning`)
/// carry a freshly computed `persistedSnapshot` populated from live host
/// state. Always-refresh policy: any prior `persistedSnapshot` is
/// replaced; if the new record differs from the old, posts a
/// `PluginVersionRepinned` (Info / PluginEvent) message to `sink`.
///
/// The record's descriptor + state blob come from the live host via
/// `lookup` → `descriptorForSlot` / `stateBlobForSlot`. When the slot
/// isn't configured (lookup returns nullopt) or the host can't produce a
/// blob, the helper falls back to a descriptor-only record with an empty
/// state blob — preserving the engine's ability to save partially-wired
/// trees.
///
/// Pure: does not mutate `root` — the input tree's existing
/// `shared_ptr<const Constituent>` references stay observably unchanged.
/// Subtrees with no VersionPinning changes are shared with the input.
std::shared_ptr<const Constituent> populateVersionPinningRecords (
    std::shared_ptr<const Constituent> root,
    const OutOfProcessEffectChainHost& host,
    SlotLookup                         lookup,
    INotificationSink&                 sink);

/// Load-side helper (white paper §15.6).
///
/// Walks `loadedRoot` (the Constituent tree produced by
/// `deserializeSession`). For each `EffectChainEntry` whose
/// `persistedSnapshot` is populated, builds a fresh record from live host
/// state and compares the two via `VersionPinningRecord::matches` (which
/// excludes `oversamplingRate`). On mismatch, posts one
/// `Warning / PluginEvent` drift message to `sink` whose 8-char state-hash
/// prefixes lead so they survive the 128-byte notification truncation.
///
/// Per white paper line 1500 + V7 line 566: warn, do not refuse. The
/// session still loads.
void verifyVersionPinningOnLoad (
    const Constituent&                 loadedRoot,
    const OutOfProcessEffectChainHost& host,
    SlotLookup                         lookup,
    INotificationSink&                 sink);

} // namespace sirius
