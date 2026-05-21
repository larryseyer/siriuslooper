#pragma once

#include "sirius/TapeDescriptor.h"
#include "sirius/TapeId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sirius
{

/// The project's pool of tapes — an ordered list, minimum one, unbounded
/// maximum. The single source of truth for "which tapes exist"; the timeline,
/// the Tapes tab, and the input mixer's routing all read it. Message-thread only
/// (no audio-thread access in this slice).
class TapePool
{
public:
    /// Seeds exactly one tape (TapeId{1}, "Tape 1") so the >=1 invariant holds
    /// from construction.
    TapePool();

    /// Constructs from an explicit list (used by deserialization). The list must
    /// be non-empty and its ids unique; nextId_ is seeded one past the max id.
    /// Throws std::invalid_argument on an empty list or duplicate ids.
    explicit TapePool (std::vector<TapeDescriptor> tapes);

    /// Appends a new tape with the given name and a freshly allocated id;
    /// returns the new id.
    TapeId add (std::string name);

    /// Removes the tape with the given id. Returns false (no change) if the id
    /// is unknown OR if removing it would leave the pool empty (the >=1 floor).
    bool remove (TapeId id);

    /// Renames the tape with the given id. Returns false if the id is unknown.
    bool rename (TapeId id, std::string name);

    int                                count() const noexcept;
    const TapeDescriptor*              find (TapeId id) const noexcept; // nullptr if absent
    const TapeDescriptor&              at (int index) const;            // throws std::out_of_range
    const std::vector<TapeDescriptor>& tapes() const noexcept;

    /// The primary tape — the first entry; always valid (>=1 invariant).
    TapeId primary() const noexcept;

private:
    std::vector<TapeDescriptor> tapes_;
    std::int64_t                nextId_ { 1 };
};

} // namespace sirius
