#pragma once

#include "sirius/Constituent.h"
#include "sirius/INotificationSink.h"

#include <memory>

namespace sirius
{

/// Save-side helper (white paper §15.6).
///
/// Walks `root` and returns a copy-on-write Constituent tree whose
/// `EffectChainEntry` slots (those with `archivalMode == VersionPinning`)
/// carry a freshly populated `persistedSnapshot` reflecting the live
/// state of each slot. Pure: does not mutate `root` — the input tree's
/// existing `shared_ptr<const Constituent>` references stay observably
/// unchanged. Subtrees with no VersionPinning changes are shared with
/// the input.
///
/// For the M8 S1 baseline the snapshot is built from the entry's own
/// descriptor with an empty state blob (no CLAP state integration yet —
/// that arrives in M8 S2 per V7 plan lines 597-598). The
/// `OutOfProcessEffectChainHost&` parameter is deliberately absent from
/// the signature in M8 S1 because the helper has no use for it; M8 S2
/// adds it back atomically with the body that consumes it (real CLAP
/// state hashing + live descriptor query).
std::shared_ptr<const Constituent> populateVersionPinningRecords (
    std::shared_ptr<const Constituent> root);

/// Load-side helper (white paper §15.6).
///
/// Walks `loadedRoot` (the Constituent tree produced by
/// `deserializeSession`). For each `EffectChainEntry` whose
/// `persistedSnapshot` is populated, builds a fresh record from the
/// entry's current descriptor and compares the two via
/// `VersionPinningRecord::matches` (which excludes `oversamplingRate`).
/// On mismatch, posts one `Warning / PluginEvent` to `sink` naming the
/// unique id + expected/found versions. The notification surface is
/// the existing engine→UI truthfulness channel; the Preparation pane's
/// notification history renders it without any new UI code.
///
/// Per white paper line 1500 + V7 line 566: warn, do not refuse. The
/// session still loads.
void verifyVersionPinningOnLoad (const Constituent& loadedRoot,
                                 INotificationSink& sink);

} // namespace sirius
