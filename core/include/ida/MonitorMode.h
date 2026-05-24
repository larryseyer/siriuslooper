#pragma once

namespace ida
{

/// Per-channel direct-layer monitoring mode (operator design lock 2026-05-24,
/// whitepaper §7.1 line 691 — "A channel can write to tape, feed direct, both,
/// or neither — independent per-channel choices.").
///
/// The direct layer is the parallel signal path that carries an input straight
/// to an output-mixer channel without touching the tape (whitepaper §7). The
/// per-channel mode below is the operator's explicit "feed direct" choice; it
/// is independent of the channel's main-out destination (tape / bus).
///
/// * `Off`       — default. The channel does not feed the direct layer; the
///                 operator does not hear it on the output unless an Output-
///                 Mixer phrase channel renders the same source from tape.
/// * `Raw`       — sub-millisecond zero-processing monitoring. The signal is
///                 tapped pre-strip (before gain, mute, pan, inserts). The
///                 operator hears the pristine instrument exactly as the
///                 preamp captured it (whitepaper §7.2). The strip's mute
///                 still kills the monitor — mute is the operator-facing
///                 kill-switch regardless of where in the chain the tap sits.
/// * `Processed` — post-strip monitoring with the channel's gain, EQ, CMP,
///                 and inserts applied. Adds a few ms of latency (the cost of
///                 the processing chain). The 90% default when the operator
///                 turns Monitor on (whitepaper §7.2 — processed monitoring
///                 is the right choice when the processing IS the monitor).
///
/// Wire-stable tokens (used by SessionFormat):
///     Off       → "Off"
///     Raw       → "Raw"
///     Processed → "Processed"
enum class MonitorMode
{
    Off = 0,
    Raw,
    Processed
};

} // namespace ida
