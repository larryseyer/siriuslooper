#pragma once
#include "ida/Rational.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ida {

enum class TapeRecordType : std::uint16_t { Audio = 1 };
enum class TapeCodecId    : std::uint16_t { None = 0, AudioFlac = 1, AudioPcm = 2 };

inline constexpr std::uint16_t kTapeFormatVersion   = 1;
inline constexpr std::size_t   kTapeFileHeaderBytes  = 12;
// body = seq(8) + type(2) + codec(2) + conceptualNum(8) + conceptualDen(8)
//              + lmcNum(8) + lmcDen(8) = 44
inline constexpr std::size_t   kRecordHeaderBytes    = 44;

struct TapeRecordHeader {
    std::uint64_t  seq          { 0 };
    TapeRecordType type         { TapeRecordType::Audio };
    TapeCodecId    codec        { TapeCodecId::AudioFlac };
    Rational       conceptualTs { Rational{ 0 } };
    Rational       lmcTs        { Rational{ 0 } };
};

void          writeLE16(std::byte* dst, std::uint16_t v) noexcept;
void          writeLE32(std::byte* dst, std::uint32_t v) noexcept;
void          writeLE64(std::byte* dst, std::uint64_t v) noexcept;
std::uint16_t readLE16(const std::byte* src) noexcept;
std::uint32_t readLE32(const std::byte* src) noexcept;
std::uint64_t readLE64(const std::byte* src) noexcept;

std::uint32_t crc32(const std::byte* data, std::size_t len) noexcept;

/// Writes kTapeFileHeaderBytes bytes into dst: 8-byte magic + u16 version + u16 reserved.
void writeFileHeader(std::byte* dst) noexcept;

/// Validates the file header at src[0..n). Returns false if n < kTapeFileHeaderBytes
/// or the magic is wrong. On success sets versionOut to the stored format version.
bool readFileHeader(const std::byte* src, std::size_t n, std::uint16_t& versionOut) noexcept;

/// Encodes one record into out (resized to fit). Layout:
///   [u32 bodyLen][body: 44-byte header fields + payload][u32 crc32(body)]
/// Returns total bytes written (== out.size()).
std::size_t encodeRecord(const TapeRecordHeader& h,
                         const std::byte* payload, std::size_t payloadLen,
                         std::vector<std::byte>& out);

/// Decodes only the body bytes (caller has already extracted bodyLen and the
/// trailing CRC). Sets hOut, points payloadOut into body (no copy), and sets
/// payloadLenOut = bodyLen - kRecordHeaderBytes. Returns false if
/// bodyLen < kRecordHeaderBytes.
bool decodeRecordBody(const std::byte* body, std::size_t bodyLen,
                      TapeRecordHeader& hOut,
                      const std::byte*& payloadOut,
                      std::size_t& payloadLenOut) noexcept;

} // namespace ida
