#pragma once

namespace juce { class MidiBuffer; }

namespace ida
{

/// Audio-thread-callable port for sources that need to be driven once per
/// audio buffer so their per-output sample data is fresh for downstream
/// readers. Today's only implementer is `ida::OttoHost`, whose
/// `renderBlock` runs OTTO's processBlock housekeeping prefix +
/// per-channel `processGlobalMixer` + housekeeping suffix so the 32
/// per-output stereo pair pointers exposed by `OttoHost::getOttoOutput
/// {Left,Right}` are populated and stable for the rest of the audio block.
///
/// The interface lives in `core/` (JUCE-free in the public surface
/// except for the forward-declared `juce::MidiBuffer&` below — a deliberate
/// minimal bleed so the audio thread can pass MIDI through without an
/// extra hop. The forward declaration costs no include; the implementing
/// translation unit pulls in `<juce_audio_basics/juce_audio_basics.h>`).
///
/// Implementations must obey the audio-thread RT-safety contract: no
/// allocation, no locks, no I/O, no throw. `AudioCallback` invokes
/// `renderBlock` once per buffer, near the top of the callback (before
/// any consumer that reads per-output OTTO audio).
class IOttoRenderSource
{
public:
    virtual ~IOttoRenderSource() = default;

    /// Render one audio block. Called from the audio thread. Must be
    /// RT-safe and noexcept. `numSamples` is the buffer size the audio
    /// device delivered for this callback; implementations clamp
    /// internally if needed. `midiMessages` carries any MIDI the audio
    /// callback received this block (host MIDI input, file-input MIDI,
    /// etc.) and may be augmented in-place with the source's own
    /// generated MIDI events (e.g. OTTO's pattern-generated NoteOn/Off).
    virtual void renderBlock (int numSamples,
                              juce::MidiBuffer& midiMessages) noexcept = 0;
};

} // namespace ida
