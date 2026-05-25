#pragma once

namespace ida
{

/// Per-channel monitoring toggle (whitepaper V9 §7.2, operator design lock
/// 2026-05-24).
///
/// V9 collapses the previous tri-state (Off / Raw / Processed) to a single
/// toggle: a channel either monitors or it doesn't. When `On`, the input
/// is tapped post-strip (after gain, mute, pan, and inserts) and routed to
/// an auto-created OutputMixer channel — so the operator hears the same
/// processed signal that the tape records, and adjusting any OutputMixer
/// control (level, pan, sends, inserts) applies to the monitor path. The
/// strip's mute is still the operator-facing kill-switch regardless of
/// where in the chain the tap sits.
///
/// The earlier "Raw" sub-millisecond pre-strip option is gone — V9 treats
/// pro-DAW Control-Room monitoring as a single conceptual gesture rather
/// than three. Latency comes from the processing chain the operator chose
/// to apply; if they want less, they remove inserts.
///
/// Wire-stable tokens (used by SessionFormat):
///     Off → "Off"
///     On  → "On"
///
/// V8 sessions used "Raw" / "Processed" tokens; both read as `On` for
/// back-compat (SessionFormat handles the coercion).
enum class MonitorMode
{
    Off = 0,
    On
};

} // namespace ida
