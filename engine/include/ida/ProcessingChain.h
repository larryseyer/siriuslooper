#pragma once

#include "ida/SignalType.h"

#include <memory>

namespace sirius
{

/// Per-channel processing applied during `InputMixer::processBuffer`. M3 ships
/// the type shape with no-op bodies for every modality; real bodies arrive
/// per-modality:
///
///   - Audio  — real DSP (gain/pan) shipped in M5 as `ChannelStrip<SignalType::Audio>`
///              (header: `sirius/ChannelStrip.h`); supersedes the M3-era
///              `AudioChain` per plan amendment §3.
///   - MidiChain   — real UMP handling lands with M9.
///   - VideoChain  — real video processing lands with M12.
///   - FileChain   — real file-input processing lands with M13.
///
/// The abstract base anchors the polymorphism Channel needs (it holds a
/// unique_ptr<ProcessingChain>). Each concrete subclass declares its own
/// typed process() entry point — callers down-cast via signalType() before
/// invoking (Audio chains process audio samples, MIDI chains process UMP
/// events, etc.). No common process() lives on the base because the modalities
/// pass fundamentally different payload types.
class ProcessingChain
{
public:
    virtual ~ProcessingChain() = default;
    virtual SignalType signalType() const noexcept = 0;
};

class MidiChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Midi; }
    // M9 adds: void process (UmpStream& inOut) noexcept;
};

class VideoChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Video; }
    // M12 adds: void process (VideoFrame& inOut) noexcept;
};

class FileChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::File; }
    // M13 adds: void process (FileBufferRef& inOut) noexcept;
};

/// Build the concrete ProcessingChain matching `type`. Used by `Channel`'s
/// constructor so callers never need to know which subclass goes with which
/// SignalType. For `SignalType::Audio` this returns a
/// `ChannelStrip<SignalType::Audio>` (M5 supersedes the M3-era `AudioChain`);
/// for the other modalities it still returns their M3-era stub chains
/// (`MidiChain` / `VideoChain` / `FileChain`) until M9 / M12 / M13.
std::unique_ptr<ProcessingChain> makeProcessingChain (SignalType type);

} // namespace sirius
