#pragma once

#include "ida/ArchivalMode.h"
#include "ida/InternalFxId.h"
#include "ida/PluginDescriptor.h"
#include "ida/VersionPinningRecord.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// Discriminant for `EffectChainEntry`'s tagged union (the "union slot type"
/// contract in `docs/design/ida-internal-fx.md`). Each slot is one of:
///   - `Empty`    — slot is unallocated; host skips it at render time.
///   - `Internal` — slot identifies one of IDA's four built-in FX by
///                  `InternalFxId`; the host wraps the matching OTTO Player FX.
///   - `Plugin`   — slot carries a `PluginDescriptor` for an externally
///                  hosted VST/CLAP/AU(v3). Unchanged from the pre-union shape.
/// Wire-stable NAMES: SessionFormat emits these as strings ("Empty" /
/// "Internal" / "Plugin"). Renaming a value without updating
/// effectChainEntryToVar / effectChainEntryFromVar breaks every saved
/// session that contains entries of that kind. Numeric values are also
/// pinned (see EffectChainSlotKind tests) — reordering breaks in-memory
/// switches that rely on the enum order.
enum class EffectChainSlotKind : std::uint8_t
{
    Empty    = 0,
    Internal = 1,
    Plugin   = 2,
};

/// One slot in an effect chain — a tagged union over Empty / Internal / Plugin.
/// `kind` is the discriminant; `internalId` is meaningful iff `kind == Internal`;
/// `descriptor` + `stateBase64` + `archivalMode` + `persistedSnapshot` are
/// meaningful iff `kind == Plugin`. Default construction yields an Empty slot
/// — a safer default than Plugin, because an uninitialized entry with
/// `kind == Plugin` and a default `descriptor` would look like a legitimate
/// (but invalid) plugin reference. Use the static factories `makePlugin` and
/// `makeInternal` at construction sites; the field-by-field constructor is
/// retained only because it is implicit in the aggregate-initialization style
/// the persistence layer uses.
///
/// The state blob is an opaque byte string (base64-encoded for JSON safety)
/// produced by the host runtime when it serializes a plugin; the data model
/// does not interpret it.
///
/// `archivalMode` selects the per-instance strategy for handling plug-in
/// non-determinism (white paper §15.6). Default is `VersionPinning` per V7
/// plan line 563. `persistedSnapshot` is the frozen identity at the moment
/// of the last `serializeSession`; populated by `populateVersionPinningRecords`
/// on save and compared by `verifyVersionPinningOnLoad` on load. Both fields
/// are meaningless on `Empty` and `Internal` slots and are persisted only on
/// `Plugin` slots.
struct EffectChainEntry
{
    EffectChainSlotKind kind { EffectChainSlotKind::Empty };
    InternalFxId        internalId { InternalFxId::kEq }; ///< valid iff kind == Internal

    PluginDescriptor descriptor;                          ///< valid iff kind == Plugin
    std::string      displayName;                         ///< chain-local name
    std::string      stateBase64;                         ///< plugin state as base64 (Plugin only)
    bool             bypassed { false };
    ArchivalMode     archivalMode { ArchivalMode::VersionPinning }; ///< Plugin only
    std::optional<VersionPinningRecord> persistedSnapshot;          ///< Plugin only

    /// Factory: construct an Internal slot for a built-in FX.
    static EffectChainEntry makeInternal (InternalFxId id);

    /// Factory: construct a Plugin slot from a descriptor + display name +
    /// state blob (state may be empty when not yet captured).
    static EffectChainEntry makePlugin (PluginDescriptor descriptor,
                                        std::string      displayName,
                                        std::string      stateBase64 = "");

    bool operator== (const EffectChainEntry& other) const noexcept
    {
        if (kind        != other.kind)        return false;
        if (displayName != other.displayName) return false;
        if (bypassed    != other.bypassed)    return false;
        switch (kind)
        {
            case EffectChainSlotKind::Empty:
                return true;
            case EffectChainSlotKind::Internal:
                return internalId == other.internalId;
            case EffectChainSlotKind::Plugin:
                return descriptor        == other.descriptor
                    && stateBase64       == other.stateBase64
                    && archivalMode      == other.archivalMode
                    && persistedSnapshot == other.persistedSnapshot;
        }
        return false; // unreachable — quiets some compilers
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

    /// Hard per-node insert-slot ceiling (routing-graph Phase 4). The cap
    /// lives on this shared type, so it binds every node that owns a chain —
    /// channels, buses, and FX returns alike. `withAppended` throws
    /// std::length_error when the chain is already at this size.
    static constexpr std::size_t kMaxSlots = 8;

    /// True when the chain holds `kMaxSlots` entries — no further append is
    /// possible. UI callers check this before offering "add a slot" so the
    /// cap is not enforced via exception-as-control-flow.
    bool full() const noexcept { return entries_.size() == kMaxSlots; }

    bool        empty()      const noexcept { return entries_.empty(); }
    std::size_t size()       const noexcept { return entries_.size(); }
    const std::vector<EffectChainEntry>& entries() const noexcept { return entries_; }
    const EffectChainEntry& at (std::size_t index) const;

    /// Returns a chain with `entry` appended at the end. Pure: the receiver is
    /// untouched. Throws std::length_error if the chain is already full
    /// (size() == kMaxSlots).
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

} // namespace ida
