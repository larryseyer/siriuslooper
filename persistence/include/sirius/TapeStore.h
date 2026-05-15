#pragma once

#include <juce_core/juce_core.h>

namespace sirius::persistence
{

/// A content-addressed blob store for tape files (white paper Part 7.8). Each
/// tape lives on disk under a filename derived from a SHA-256 of its bytes, so
/// two sessions that captured the same take share the same file with no
/// possibility of a collision and no possibility of one session's edits
/// silently altering another's data — files, once written, are immutable.
///
/// The store deliberately does not know about TapeId, Constituent, or anything
/// from the structure layer. It is the data-layer half of the session
/// directory; the structure-layer manifest that maps a TapeId to its content
/// hash lives in session.json (SessionFormat).
class TapeStore
{
public:
    /// Opens (creating if needed) the store rooted at `directory`. Throws
    /// std::runtime_error if the directory cannot be created or is not writable.
    explicit TapeStore (juce::File directory);

    /// Writes `bytes` and returns the content hash that identifies it. The
    /// write is idempotent: storing identical content twice produces the same
    /// hash and leaves the existing file untouched (rule: tape data integrity
    /// is sacred, white paper Part 13.3). Throws std::runtime_error if the
    /// write fails.
    juce::String store (const juce::MemoryBlock& bytes);

    /// Whether a tape with the given hash is present in the store.
    bool exists (const juce::String& contentHash) const;

    /// Reads the tape with the given hash into `out`. Returns true on success,
    /// false if no such tape exists. Throws std::runtime_error on read failure
    /// of a present file (a corruption signal, not an absent-file signal).
    bool read (const juce::String& contentHash, juce::MemoryBlock& out) const;

    /// The file the given content hash maps to, whether or not it exists. Used
    /// by external archival tools that need to enumerate the data layer.
    juce::File fileFor (const juce::String& contentHash) const;

    const juce::File& directory() const noexcept { return directory_; }

private:
    juce::File directory_;
};

} // namespace sirius::persistence
