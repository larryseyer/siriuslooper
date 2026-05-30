#pragma once
#include "ida/TapeRecord.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ida {

/// Decoded stereo PCM block. Lives on the worker/message thread — allocation is fine here.
struct PcmBlock {
    std::vector<float> left;
    std::vector<float> right;

    int numFrames() const noexcept { return static_cast<int>(left.size()); }
};

/// Media-agnostic codec interface: one implementation per TapeCodecId tag.
/// All methods are worker-thread-callable; no audio-thread allocation or locks.
class IPayloadCodec {
public:
    virtual ~IPayloadCodec() = default;

    virtual TapeCodecId codecId() const noexcept = 0;

    /// Encode one stereo block → independently-decodable payload bytes.
    virtual std::vector<std::byte> encode(const float* left, const float* right,
                                          int numFrames, double sampleRate) const = 0;

    /// Decode payload bytes → PCM. Returns false on malformed input (no throw).
    virtual bool decode(const std::byte* payload, std::size_t len, PcmBlock& out) const = 0;
};

/// Registry: maps TapeCodecId → IPayloadCodec. Add a new medium by registering
/// a new codec; the container/flush/recovery machinery never changes.
class TapeCodecRegistry {
public:
    /// Register a codec keyed by codec->codecId(). Replaces any prior entry for
    /// the same id. Silently ignores a null pointer.
    void registerCodec(std::shared_ptr<IPayloadCodec> codec);

    /// Returns the codec registered for id, or nullptr if none is registered.
    IPayloadCodec* codecFor(TapeCodecId id) const noexcept;

private:
    std::unordered_map<std::uint16_t, std::shared_ptr<IPayloadCodec>> codecs_;
};

} // namespace ida
