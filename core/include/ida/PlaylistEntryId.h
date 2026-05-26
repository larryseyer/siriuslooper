#pragma once

#include <cstdint>

namespace ida
{

/// Stable identifier for an entry within a file-input playlist. Allocated
/// monotonically by InputMixer when the entry is registered. Survives
/// reorder operations (lookup is by id, not by position), so the worker
/// thread's "current entry" remains valid across UI-thread list edits.
/// Same house pattern as InputId / ChannelId / BusId.
class PlaylistEntryId
{
public:
    explicit constexpr PlaylistEntryId (std::int64_t value) noexcept : value_ (value) {}

    constexpr std::int64_t value() const noexcept { return value_; }

    constexpr bool operator== (const PlaylistEntryId& other) const noexcept
    {
        return value_ == other.value_;
    }
    constexpr bool operator!= (const PlaylistEntryId& other) const noexcept
    {
        return !(*this == other);
    }

private:
    std::int64_t value_;
};

} // namespace ida
