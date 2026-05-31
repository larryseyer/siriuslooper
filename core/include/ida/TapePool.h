#pragma once

#include "ida/TapeDescriptor.h"
#include "ida/TapeId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// The project's pool of tapes — an ordered list, possibly empty, unbounded
/// maximum. An empty pool is the legal blank-slate (New Song) state. The single
/// source of truth for "which tapes exist"; the timeline, the Tapes tab, and the
/// input mixer's routing all read it. Message-thread only (no audio-thread
/// access in this slice).
class TapePool
{
public:
    /// Constructs an empty pool — the blank-slate / New Song state. The first
    /// `add` allocates TapeId{1}.
    TapePool() = default;

    /// Constructs from an explicit list (used by deserialization). The list may
    /// be empty; its ids must be unique. nextId_ is seeded one past the max id
    /// (an empty list leaves it at 1). Throws std::invalid_argument on
    /// duplicate ids.
    explicit TapePool (std::vector<TapeDescriptor> tapes);

    /// Appends a new tape with the given name and a freshly allocated id;
    /// returns the new id.
    TapeId add (std::string name);

    /// Removes the tape with the given id. May empty the pool and does not pin
    /// the primary. Returns false (no change) only if the id is unknown.
    bool remove (TapeId id);

    /// Renames the tape with the given id. Returns false if the id is unknown.
    bool rename (TapeId id, std::string name);

    int                                count() const noexcept;
    const TapeDescriptor*              find (TapeId id) const noexcept; // nullptr if absent
    const TapeDescriptor&              at (int index) const;            // throws std::out_of_range
    const std::vector<TapeDescriptor>& tapes() const noexcept;

    /// The primary tape — the first entry, or nullopt when the pool is empty.
    std::optional<TapeId> primary() const noexcept;

private:
    std::vector<TapeDescriptor> tapes_;
    std::int64_t                nextId_ { 1 };
};

} // namespace ida
