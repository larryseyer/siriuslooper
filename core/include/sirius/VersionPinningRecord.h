#pragma once

#include "sirius/PluginDescriptor.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sirius
{

/// The portable, persistable identity of one hosted plug-in instance at
/// the moment it was saved (white paper §15.6). Round-trips through the
/// session format. Compared on reopen against a freshly computed record
/// via `matches(...)`; any mismatch outside `oversamplingRate` raises a
/// `PluginVersionDrift` notification.
struct VersionPinningRecord
{
    std::string   uniqueId;
    std::string   version;
    std::string   stateBlobSha256;
    std::uint32_t oversamplingRate { 1u };
    std::string   declaredInternalStateHash;

    /// Field-by-field equality EXCEPT `oversamplingRate` (placeholder
    /// until oversampling is tracked — comparing it would trigger noise).
    bool matches (const VersionPinningRecord& other) const noexcept;

    /// Byte-exact identity comparison — includes `oversamplingRate`.
    /// Distinct from `matches()` because round-trip tests need bit-equal
    /// equality while drift detection needs semantic equality. Maintain
    /// both bodies in lockstep when adding fields (see .cpp).
    bool operator== (const VersionPinningRecord& other) const noexcept;
    bool operator!= (const VersionPinningRecord& other) const noexcept { return ! (*this == other); }
};

/// Builds the record from the live plug-in instance. The state blob is
/// the empty byte sequence in M8 S1 (no CLAP state integration yet —
/// that arrives in M8 S2); the SHA-256 of zero bytes is well defined
/// and serves as the synthetic-fixture baseline.
///
/// `oversamplingRate` defaults to 1 (no oversampling tracking yet —
/// see plan "Risks and open decisions" note).
/// `declaredInternalStateHash` is empty in M8 S1 (the white paper's
/// optional plug-in-self-reported hash arrives with the CLAP host
/// extension in M8 S2+).
VersionPinningRecord makeVersionPinningRecord (
    const PluginDescriptor& descriptor,
    std::span<const std::byte> stateBlob = {});

} // namespace sirius
