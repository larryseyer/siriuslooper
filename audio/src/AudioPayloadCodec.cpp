#include "ida/AudioPayloadCodec.h"
#include "ida/TapeRecord.h"

#include <cstring>

namespace ida {

// ============================================================================
// FlacAudioCodec
// ============================================================================

std::vector<std::byte> FlacAudioCodec::encode(const float* left, const float* right,
                                               int numFrames, double sampleRate) const
{
    juce::MemoryBlock block;
    {
        // MemoryOutputStream backed by `block` — outlives the writer.
        auto stream = std::make_unique<juce::MemoryOutputStream>(block, false);

        juce::FlacAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> streamBase = std::move(stream);

        // 24-bit stereo FLAC at compression level 3 — mirrors FlacTapeSink::writerFor.
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(sampleRate)
                                 .withNumChannels(2)
                                 .withBitsPerSample(24)
                                 .withQualityOptionIndex(3);

        // createWriterFor takes ownership of streamBase (std::exchange semantics).
        auto writer = fmt.createWriterFor(streamBase, options);
        if (writer == nullptr)
            return {};

        const float* channels[2] { left, right };
        writer->writeFromFloatArrays(channels, 2, numFrames);

        // Destroy the writer — finalizes (writes FLAC streaminfo + padding) and
        // flushes to the MemoryOutputStream, which writes to `block`.
    } // writer destroyed here; streamBase already exchanged into writer, also gone

    const auto* data = static_cast<const std::byte*>(block.getData());
    return std::vector<std::byte>(data, data + block.getSize());
}

bool FlacAudioCodec::decode(const std::byte* payload, std::size_t len, PcmBlock& out) const
{
    if (len == 0)
        return false;

    // MemoryInputStream takes a copy of the bytes — each call is stateless.
    auto* stream = new juce::MemoryInputStream(payload, len, true);

    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(stream, true));
    if (reader == nullptr)
        return false;

    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    if (numSamples <= 0)
        return false;

    juce::AudioBuffer<float> buf(2, numSamples);
    reader->read(&buf, 0, numSamples, 0, true, true);

    const auto n = static_cast<std::size_t>(numSamples);
    out.left .assign(buf.getReadPointer(0), buf.getReadPointer(0) + n);
    out.right.assign(buf.getReadPointer(1), buf.getReadPointer(1) + n);
    return true;
}

// ============================================================================
// PcmAudioCodec
// ============================================================================

// Payload layout:
//   [u32 numFrames LE] [float32 L0 LE] [float32 R0 LE] [float32 L1 LE] ...
//
// The u32 header is written/read with writeLE32/readLE32 for consistency with
// the rest of the T0a byte-layer (same endianness contract).

std::vector<std::byte> PcmAudioCodec::encode(const float* left, const float* right,
                                              int numFrames, double /*sampleRate*/) const
{
    static constexpr std::size_t kHeaderBytes   = 4;          // u32 numFrames
    static constexpr std::size_t kBytesPerFrame = 8;          // 2 × float32

    const auto n    = static_cast<std::size_t>(numFrames);
    const auto size = kHeaderBytes + n * kBytesPerFrame;

    std::vector<std::byte> out(size);
    writeLE32(out.data(), static_cast<std::uint32_t>(numFrames));

    std::byte* cursor = out.data() + kHeaderBytes;
    for (std::size_t i = 0; i < n; ++i)
    {
        std::uint32_t lBits, rBits;
        std::memcpy(&lBits, &left[i],  4);
        std::memcpy(&rBits, &right[i], 4);
        writeLE32(cursor,     lBits);
        writeLE32(cursor + 4, rBits);
        cursor += kBytesPerFrame;
    }
    return out;
}

bool PcmAudioCodec::decode(const std::byte* payload, std::size_t len, PcmBlock& out) const
{
    static constexpr std::size_t kHeaderBytes   = 4;
    static constexpr std::size_t kBytesPerFrame = 8;

    if (len < kHeaderBytes)
        return false;

    const auto numFrames = static_cast<int>(readLE32(payload));
    if (numFrames < 0)
        return false;

    const auto n = static_cast<std::size_t>(numFrames);
    if (len < kHeaderBytes + n * kBytesPerFrame)
        return false;

    out.left .resize(n);
    out.right.resize(n);

    const std::byte* cursor = payload + kHeaderBytes;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint32_t lBits = readLE32(cursor);
        const std::uint32_t rBits = readLE32(cursor + 4);
        std::memcpy(&out.left [i], &lBits, 4);
        std::memcpy(&out.right[i], &rBits, 4);
        cursor += kBytesPerFrame;
    }
    return true;
}

} // namespace ida
