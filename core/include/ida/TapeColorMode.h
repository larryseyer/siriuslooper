#pragma once

namespace ida
{

/// Per-tape TAPECOLOR mode (operator design lock 2026-05-24,
/// `[[project_tapecolor_placement]]` memory). The tape itself carries the
/// mode; an Output-Mixer slot is the separate "Mode B" path (5th internal FX,
/// insert-anywhere). The two modes can coexist on the same signal — legal,
/// silent, no UI warning.
///
/// * `None`        — default. Tape is bit-clean on write, bit-clean on read.
/// * `BeforeWrite` — color baked into the FLAC at record time. The on-disk
///                   tape is permanently colored; reading replays as-is.
/// * `AfterRead`   — clean disk, color applied on playback only. Toggling
///                   the color or its parameters changes playback without
///                   re-rendering the tape. This is the default mode the
///                   operator lands in when they turn TAPECOLOR on for a
///                   tape.
///
/// Wire-stable tokens (used by SessionFormat):
///     None        → "None"
///     BeforeWrite → "BeforeWrite"
///     AfterRead   → "AfterRead"
enum class TapeColorMode
{
    None = 0,
    BeforeWrite,
    AfterRead
};

} // namespace ida
