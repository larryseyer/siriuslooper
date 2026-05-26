#pragma once

namespace ida
{

/// Advance policy for a file-input playlist when the currently-active
/// entry reaches end-of-file. White-paper V9 §6.6 (playlist clause) +
/// glossary "Playlist scope".
enum class LoopScope
{
    Off,    ///< Advance to next entry; stop at end of list.
    Track,  ///< Rewind same entry to 0.
    List    ///< Advance to next entry, wrapping last → first.
};

} // namespace ida
