#pragma once

#include <string>

namespace ida
{

/// The top-level persistence unit that owns an arrangement and ALL of its tapes
/// (spec §2.2). Tapes are project-scoped on disk; the project's `folderId` is the
/// grouper that holds them.
///
/// Two identities are deliberately decoupled:
///   * `folderId()` — `yyyymmddhhmmss-<sanitized-name>`, set ONCE at creation and
///     immutable. It is the on-disk grouper and the stable id; renaming a project
///     never changes it (avoids folder churn / broken tape paths).
///   * `displayName()` — the user-facing project name; mutable via `setDisplayName`.
///
/// JUCE-free by design (core/ is JUCE-free): the creation timestamp is supplied by
/// the caller as a 14-character `yyyymmddhhmmss` string, so this type is fully
/// deterministic and unit-testable without reading a wall clock. Production wires
/// the timestamp from the clock at construction time (the app layer).
class IdaProject
{
public:
    /// Builds a project. `displayName` is stored verbatim and ALSO sanitized into
    /// the folder id; `timestampDigits` must be the 14-char `yyyymmddhhmmss`
    /// creation stamp. The folder id is `timestampDigits + "-" + sanitize(displayName)`.
    /// An empty / all-illegal display name sanitizes to "untitled".
    static IdaProject create (std::string displayName, std::string timestampDigits);

    /// Sanitizes a display name to a filesystem-safe folder-name fragment:
    /// lowercase ASCII letters/digits pass; every other char becomes '_', runs of
    /// '_' collapse to one, leading/trailing '_' are trimmed; empty -> "untitled".
    /// Exposed for tests and for any caller that needs the fragment alone.
    static std::string sanitizeName (const std::string& displayName);

    const std::string& folderId() const noexcept         { return folderId_; }
    const std::string& displayName() const noexcept      { return displayName_; }
    const std::string& createdTimestamp() const noexcept { return createdTimestamp_; }

    /// Changes the user-facing name only. The folder id is NOT touched (§2.2).
    void setDisplayName (std::string displayName);

private:
    IdaProject (std::string folderId, std::string displayName, std::string createdTimestamp);

    std::string folderId_;
    std::string displayName_;
    std::string createdTimestamp_;
};

} // namespace ida
