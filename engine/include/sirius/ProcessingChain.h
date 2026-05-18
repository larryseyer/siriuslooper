#pragma once

#include "sirius/SignalType.h"

#include <memory>

namespace sirius
{

/// Per-channel processing applied during `InputMixer::processBuffer`. M3 ships
/// the type shape with no-op bodies for every modality; real bodies arrive
/// per-modality:
///
///   - AudioChain  — real DSP (gain/pan) lands with M5's `ChannelStrip<SignalType::Audio>`
///                   per V7 alignment plan amendment §3.
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

class AudioChain final : public ProcessingChain
{
public:
    SignalType signalType() const noexcept override { return SignalType::Audio; }
    // M5 adds: void process (juce::AudioBuffer<float>& inOut) noexcept;
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
/// SignalType.
std::unique_ptr<ProcessingChain> makeProcessingChain (SignalType type);

} // namespace sirius
