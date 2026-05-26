#pragma once

#include <cstdint>

namespace ida
{

/// Advance policy for a file-input playlist when the currently-active
/// entry reaches end-of-file. White-paper V9 §6.6 (playlist clause) +
/// glossary "Playlist scope". Underlying type is pinned to `uint8_t` so
/// the wire size is stable when this enum lands in session JSON (Task 9).
enum class LoopScope : std::uint8_t
{
    Off,    ///< Advance to next entry; stop at end of list.
    Track,  ///< Rewind same entry to 0.
    List    ///< Advance to next entry, wrapping last → first.
};

} // namespace ida
