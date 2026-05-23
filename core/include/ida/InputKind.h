#pragma once

#include "ida/SignalType.h"

namespace sirius
{

/// The kind of signal a tape carries. One value per category in the
/// white paper §6.2 enumeration of input sources. This descriptor lives
/// alongside a TapeId so that UI and routing layers can ask "what kind
/// of input is this?" without depending on the (kind-specific) payload
/// type the underlying Tape<T> is parameterized on. Tape<T> itself does
/// not know about InputKind — the descriptor is free-standing metadata.
enum class InputKind
{
    Audio,
    Video,
    Midi,
    Control,
    ParameterAutomation,
    Transport,
    System
};

/// Project the seven `InputKind` cases onto the four-case `SignalType`
/// the V3 mixer architecture uses (V3 transition guide §2.4 / V7
/// alignment plan M2 Risks note).
///
/// `Audio`, `Midi`, and `Video` map one-to-one. The remaining cases —
/// `Control`, `ParameterAutomation`, `Transport`, `System` — collapse to
/// `SignalType::File`: their tapes are JSONL streams in the Sirius
/// Archive Format until M11's SAF design forces a split.
constexpr SignalType signalTypeOf (InputKind kind) noexcept
{
    switch (kind)
    {
        case InputKind::Audio: return SignalType::Audio;
        case InputKind::Midi:  return SignalType::Midi;
        case InputKind::Video: return SignalType::Video;
        case InputKind::Control:
        case InputKind::ParameterAutomation:
        case InputKind::Transport:
        case InputKind::System:
            return SignalType::File;
    }
    return SignalType::File;
}

} // namespace sirius
