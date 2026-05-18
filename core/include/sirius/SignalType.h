#pragma once

namespace sirius
{

/// Signal modality tracked alongside every channel, tape, and Constituent
/// (white paper V7 §2.4; V3 transition guide §2.4). The four cases mirror
/// the four channel-strip variants V3 defines — `AudioChannelStrip`,
/// `MidiChannelStrip`, `VideoChannelStrip`, `FileInputChannelStrip`.
///
/// MIDI is intentionally first-class from day one: V7 §6.12 specifies UMP
/// (MIDI 2.0) storage on tape and a MIDI-1.0-upcast policy at the
/// membrane. Code that interprets `SignalType::Midi` must not assume the
/// underlying payload is a 3-byte (status, data1, data2) message — see
/// V3 transition guide line 229 for the "UMP from day one" constraint.
enum class SignalType
{
    Audio,
    Midi,
    Video,
    File
};

} // namespace sirius
