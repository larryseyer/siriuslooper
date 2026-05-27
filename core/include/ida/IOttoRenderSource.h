#pragma once

namespace ida
{

/// Audio-thread-callable port for sources that need to be driven once per
/// audio buffer so their per-output sample data is fresh for downstream
/// readers. Today's only implementer is `ida::OttoHost`, whose
/// `renderBlock` pumps OTTO's `PlayerManager::processGlobalMixer` so the
/// 32 per-output stereo pair pointers exposed by `OttoHost::getOttoOutput
/// {Left,Right}` are populated and stable for the rest of the audio block.
///
/// The interface lives in `core/` (JUCE-free, OTTO-free) so the
/// `audio/` layer can name it without taking a dependency on
/// `otto-bridge/` — pure dependency-inversion port, mirrors the
/// `IOttoTransportListener` precedent from M-OTTO-3b.
///
/// Implementations must obey the audio-thread RT-safety contract: no
/// allocation, no locks, no I/O, no throw. `AudioCallback` invokes
/// `renderBlock(numSamples)` once per buffer, near the top of the
/// callback (before any consumer that reads per-output OTTO audio).
class IOttoRenderSource
{
public:
    virtual ~IOttoRenderSource() = default;

    /// Render one audio block. Called from the audio thread. Must be
    /// RT-safe and noexcept. `numSamples` is the buffer size the audio
    /// device delivered for this callback; implementations clamp
    /// internally if needed (e.g. `OttoHost` defers to OTTO's own
    /// max-block-size clamp).
    virtual void renderBlock (int numSamples) noexcept = 0;
};

} // namespace ida
