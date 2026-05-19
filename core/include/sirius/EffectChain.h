#pragma once

#include "sirius/ArchivalMode.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/VersionPinningRecord.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sirius
{

/// One slot in an effect chain — a single hosted plugin instance, with the
/// state it should be restored to and the bypass flag. The state blob is an
/// opaque byte string (base64-encoded for JSON safety) produced by the host
/// runtime when it serializes a plugin; the data model does not interpret it.
///
/// `archivalMode` selects the per-instance strategy for handling plug-in
/// non-determinism (white paper §15.6). Default is `VersionPinning` per V7
/// plan line 563. `persistedSnapshot` is the frozen identity at the moment
/// of the last `serializeSession`; populated by `populateVersionPinningRecords`
/// on save and compared by `verifyVersionPinningOnLoad` on load.
struct EffectChainEntry
{
    PluginDescriptor descriptor;
    std::string      displayName; ///< chain-local name, defaults to descriptor.name
    std::string      stateBase64; ///< plugin state as base64; empty when not yet captured
    bool             bypassed { false };
    ArchivalMode     archivalMode { ArchivalMode::VersionPinning };
    std::optional<VersionPinningRecord> persistedSnapshot;

    bool operator== (const EffectChainEntry& other) const noexcept
    {
        return descriptor == other.descriptor
            && displayName == other.displayName
            && stateBase64 == other.stateBase64
            && bypassed == other.bypassed
            && archivalMode == other.archivalMode
            && persistedSnapshot == other.persistedSnapshot;
    }
    bool operator!= (const EffectChainEntry& other) const noexcept { return ! (*this == other); }
};

/// An ordered chain of hosted plugin effects attached to a Constituent (white
/// paper Part 7.7: "Effects are applied per-Constituent and are replaceable").
/// Like Constituent itself, edits are copy-on-write: every mutating call
/// returns a new chain, sharing nothing mutable. The chain is JUCE-free
/// structural data; instantiation, processing, and parameter automation live
/// in the host runtime.
class EffectChain
{
public:
    EffectChain() = default;

    bool        empty()      const noexcept { return entries_.empty(); }
    std::size_t size()       const noexcept { return entries_.size(); }
    const std::vector<EffectChainEntry>& entries() const noexcept { return entries_; }
    const EffectChainEntry& at (std::size_t index) const;

    /// Returns a chain with `entry` appended at the end. Pure: the receiver is
    /// untouched.
    EffectChain withAppended (EffectChainEntry entry) const;

    /// Returns a chain with the slot at `index` replaced. Throws
    /// std::out_of_range if `index` is past the end.
    EffectChain withReplaced (std::size_t index, EffectChainEntry entry) const;

    /// Returns a chain with the slot at `index` removed. Throws
    /// std::out_of_range if `index` is past the end.
    EffectChain withRemoved (std::size_t index) const;

    /// Returns a chain with the slots at `fromIndex` and `toIndex` swapped —
    /// the building block of reordering. Throws std::out_of_range if either
    /// index is past the end.
    EffectChain withMoved (std::size_t fromIndex, std::size_t toIndex) const;

    bool operator== (const EffectChain& other) const noexcept { return entries_ == other.entries_; }
    bool operator!= (const EffectChain& other) const noexcept { return entries_ != other.entries_; }

private:
    std::vector<EffectChainEntry> entries_;
};

} // namespace sirius
