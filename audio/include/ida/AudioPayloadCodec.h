#pragma once

#include "ida/IPayloadCodec.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace ida {

/// FLAC-per-block codec: each encode() call produces one self-contained
/// 24-bit stereo FLAC stream (file header + one block) via
/// juce::MemoryOutputStream + FlacAudioFormat at compression level 3.
/// Each payload is independently decodable — no cross-block encoder state.
class FlacAudioCodec final : public IPayloadCodec
{
public:
    TapeCodecId codecId() const noexcept override { return TapeCodecId::AudioFlac; }

    std::vector<std::byte> encode(const float* left, const float* right,
                                  int numFrames, double sampleRate) const override;

    bool decode(const std::byte* payload, std::size_t len, PcmBlock& out) const override;
};

/// PCM codec: layout is [u32 numFrames] then interleaved float32 LE (L0,R0,L1,R1,...).
/// Bit-exact: encode→decode recovers every sample unchanged.
class PcmAudioCodec final : public IPayloadCodec
{
public:
    TapeCodecId codecId() const noexcept override { return TapeCodecId::AudioPcm; }

    std::vector<std::byte> encode(const float* left, const float* right,
                                  int numFrames, double sampleRate) const override;

    bool decode(const std::byte* payload, std::size_t len, PcmBlock& out) const override;
};

} // namespace ida
