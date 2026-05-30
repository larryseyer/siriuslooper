#include "ida/TapeRecord.h"

#include <cstring>

namespace ida {

// ---------------------------------------------------------------------------
// Little-endian write helpers
// ---------------------------------------------------------------------------

void writeLE16(std::byte* dst, std::uint16_t v) noexcept
{
    dst[0] = static_cast<std::byte>(v & 0xFFu);
    dst[1] = static_cast<std::byte>((v >> 8) & 0xFFu);
}

void writeLE32(std::byte* dst, std::uint32_t v) noexcept
{
    dst[0] = static_cast<std::byte>(v & 0xFFu);
    dst[1] = static_cast<std::byte>((v >> 8)  & 0xFFu);
    dst[2] = static_cast<std::byte>((v >> 16) & 0xFFu);
    dst[3] = static_cast<std::byte>((v >> 24) & 0xFFu);
}

void writeLE64(std::byte* dst, std::uint64_t v) noexcept
{
    dst[0] = static_cast<std::byte>(v & 0xFFu);
    dst[1] = static_cast<std::byte>((v >> 8)  & 0xFFu);
    dst[2] = static_cast<std::byte>((v >> 16) & 0xFFu);
    dst[3] = static_cast<std::byte>((v >> 24) & 0xFFu);
    dst[4] = static_cast<std::byte>((v >> 32) & 0xFFu);
    dst[5] = static_cast<std::byte>((v >> 40) & 0xFFu);
    dst[6] = static_cast<std::byte>((v >> 48) & 0xFFu);
    dst[7] = static_cast<std::byte>((v >> 56) & 0xFFu);
}

// ---------------------------------------------------------------------------
// Little-endian read helpers
// ---------------------------------------------------------------------------

std::uint16_t readLE16(const std::byte* src) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[0]))
      | (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[1])) << 8));
}

std::uint32_t readLE32(const std::byte* src) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(src[0]))
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(src[1])) << 8)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(src[2])) << 16)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(src[3])) << 24);
}

std::uint64_t readLE64(const std::byte* src) noexcept
{
    return static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[0]))
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[1])) << 8)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[2])) << 16)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[3])) << 24)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[4])) << 32)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[5])) << 40)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[6])) << 48)
         | (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[7])) << 56);
}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320)
// crc32("123456789") == 0xCBF43926
// ---------------------------------------------------------------------------

// 256-entry table for reflected CRC-32 computation.
static constexpr std::array<std::uint32_t, 256> buildCrcTable() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256u; ++i)
    {
        std::uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        table[i] = crc;
    }
    return table;
}

static constexpr auto kCrcTable = buildCrcTable();

std::uint32_t crc32(const std::byte* data, std::size_t len) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
    {
        const std::uint8_t idx = std::to_integer<std::uint8_t>(data[i])
                                 ^ static_cast<std::uint8_t>(crc & 0xFFu);
        crc = kCrcTable[idx] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// File header
// ---------------------------------------------------------------------------

// Magic: exactly "IDATAPE\0" (8 bytes).
static constexpr std::array<char, 8> kMagic = {
    'I', 'D', 'A', 'T', 'A', 'P', 'E', '\0'
};

void writeFileHeader(std::byte* dst) noexcept
{
    // magic[8]
    for (int i = 0; i < 8; ++i)
        dst[i] = static_cast<std::byte>(kMagic[static_cast<std::size_t>(i)]);
    // formatVersion u16 = 1 (little-endian)
    writeLE16(dst + 8, kTapeFormatVersion);
    // reserved u16 = 0
    writeLE16(dst + 10, 0u);
}

bool readFileHeader(const std::byte* src, std::size_t n, std::uint16_t& versionOut) noexcept
{
    if (n < kTapeFileHeaderBytes)
        return false;
    for (int i = 0; i < 8; ++i)
    {
        if (std::to_integer<char>(src[i]) != kMagic[static_cast<std::size_t>(i)])
            return false;
    }
    versionOut = readLE16(src + 8);
    return true;
}

// ---------------------------------------------------------------------------
// Record encode / decode
// ---------------------------------------------------------------------------

std::size_t encodeRecord(const TapeRecordHeader& h,
                         const std::byte* payload,
                         std::size_t payloadLen,
                         std::vector<std::byte>& out)
{
    const std::size_t bodyLen = kRecordHeaderBytes + payloadLen;
    const std::size_t total   = 4u + bodyLen + 4u;
    out.resize(total);

    std::byte* p = out.data();

    // u32 bodyLen (little-endian)
    writeLE32(p, static_cast<std::uint32_t>(bodyLen));
    p += 4;

    // Body start — remember position for CRC computation.
    std::byte* const bodyStart = p;

    // 44-byte record header fields (in spec order)
    writeLE64(p, h.seq);                                           p += 8;
    writeLE16(p, static_cast<std::uint16_t>(h.type));             p += 2;
    writeLE16(p, static_cast<std::uint16_t>(h.codec));            p += 2;
    writeLE64(p, static_cast<std::uint64_t>(h.conceptualTs.numerator()));   p += 8;
    writeLE64(p, static_cast<std::uint64_t>(h.conceptualTs.denominator())); p += 8;
    writeLE64(p, static_cast<std::uint64_t>(h.lmcTs.numerator()));          p += 8;
    writeLE64(p, static_cast<std::uint64_t>(h.lmcTs.denominator()));        p += 8;

    // Payload
    if (payloadLen > 0 && payload != nullptr)
        std::memcpy(p, payload, payloadLen);
    p += payloadLen;

    // Trailing CRC of the body.
    const std::uint32_t checksum = crc32(bodyStart, bodyLen);
    writeLE32(p, checksum);

    return total;
}

bool decodeRecordBody(const std::byte* body,
                      std::size_t bodyLen,
                      TapeRecordHeader& hOut,
                      const std::byte*& payloadOut,
                      std::size_t& payloadLenOut) noexcept
{
    if (bodyLen < kRecordHeaderBytes)
        return false;

    const std::byte* p = body;

    hOut.seq   = readLE64(p);                                           p += 8;
    hOut.type  = static_cast<TapeRecordType>(readLE16(p));              p += 2;
    hOut.codec = static_cast<TapeCodecId>(readLE16(p));                 p += 2;

    const auto conceptualNum = static_cast<std::int64_t>(readLE64(p)); p += 8;
    const auto conceptualDen = static_cast<std::int64_t>(readLE64(p)); p += 8;
    const auto lmcNum        = static_cast<std::int64_t>(readLE64(p)); p += 8;
    const auto lmcDen        = static_cast<std::int64_t>(readLE64(p)); p += 8;

    hOut.conceptualTs = Rational{ conceptualNum, conceptualDen };
    hOut.lmcTs        = Rational{ lmcNum, lmcDen };

    payloadOut    = body + kRecordHeaderBytes;
    payloadLenOut = bodyLen - kRecordHeaderBytes;
    return true;
}

} // namespace ida
