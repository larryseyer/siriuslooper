#pragma once

namespace ida
{

/// The tape-routing decision a `Channel` makes for itself (V3 transition
/// guide §2.1, `set_channel_tape_mode`).
///
/// V3 clarifies that tape topology is *channel-driven*, not input-driven:
/// one input may produce zero, one, two, or many tapes depending on how
/// its channels are configured (V3 transition guide §3.3).
///
///   - `CommitToTape`   — one tape allocated; the channel's processed
///                        signal is what gets written. Effects are baked.
///   - `NonDestructive` — one tape allocated *plus* a parallel parameter
///                        tape; the dry signal is stored and the
///                        processing delta is captured separately so the
///                        processing chain remains editable post-capture.
///   - `NoTape`         — no tape allocation; the channel exists for
///                        direct-layer routing only.
enum class TapeMode
{
    CommitToTape,
    NonDestructive,
    NoTape
};

} // namespace ida
