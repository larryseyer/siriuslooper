#pragma once

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

} // namespace sirius
