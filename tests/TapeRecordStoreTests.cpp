// Tests for the T0a tape record byte-layer primitives. These pin the
// on-disk format contract: correct little-endian encoding, standard
// CRC-32 (IEEE 802.3), file-header magic / version, and full
// encode→decode round-trips for every header field and the payload.
#include "ida/TapeRecord.h"
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <vector>

using namespace ida;

// ---------------------------------------------------------------------------
// 1. File header round-trip + negative cases
// ---------------------------------------------------------------------------
TEST_CASE("writeFileHeader/readFileHeader round-trips version==1", "[tape-record]")
{
    std::array<std::byte, kTapeFileHeaderBytes> buf{};
    writeFileHeader(buf.data());

    std::uint16_t ver = 0;
    REQUIRE(readFileHeader(buf.data(), buf.size(), ver));
    REQUIRE(ver == 1);
}

TEST_CASE("readFileHeader returns false for wrong magic", "[tape-record]")
{
    std::array<std::byte, kTapeFileHeaderBytes> buf{};
    writeFileHeader(buf.data());
    // corrupt the first byte
    buf[0] = std::byte{0xFF};

    std::uint16_t ver = 0;
    REQUIRE_FALSE(readFileHeader(buf.data(), buf.size(), ver));
}

TEST_CASE("readFileHeader returns false when n < kTapeFileHeaderBytes", "[tape-record]")
{
    std::array<std::byte, kTapeFileHeaderBytes> buf{};
    writeFileHeader(buf.data());

    std::uint16_t ver = 0;
    REQUIRE_FALSE(readFileHeader(buf.data(), kTapeFileHeaderBytes - 1, ver));
}

// ---------------------------------------------------------------------------
// 2. LE helpers: round-trips + exact byte ordering
// ---------------------------------------------------------------------------
TEST_CASE("writeLE16/readLE16 round-trips", "[tape-record]")
{
    std::array<std::byte, 2> buf{};
    writeLE16(buf.data(), 0xABCD);
    REQUIRE(readLE16(buf.data()) == 0xABCD);
}

TEST_CASE("writeLE16 stores least-significant byte first", "[tape-record]")
{
    std::array<std::byte, 2> buf{};
    writeLE16(buf.data(), 0x0102);
    REQUIRE(buf[0] == std::byte{0x02});
    REQUIRE(buf[1] == std::byte{0x01});
}

TEST_CASE("writeLE32/readLE32 round-trips", "[tape-record]")
{
    std::array<std::byte, 4> buf{};
    writeLE32(buf.data(), 0xDEADBEEF);
    REQUIRE(readLE32(buf.data()) == 0xDEADBEEF);
}

TEST_CASE("writeLE32 stores bytes little-endian (0x01020304)", "[tape-record]")
{
    std::array<std::byte, 4> buf{};
    writeLE32(buf.data(), 0x01020304);
    REQUIRE(buf[0] == std::byte{0x04});
    REQUIRE(buf[1] == std::byte{0x03});
    REQUIRE(buf[2] == std::byte{0x02});
    REQUIRE(buf[3] == std::byte{0x01});
}

TEST_CASE("writeLE64/readLE64 round-trips", "[tape-record]")
{
    std::array<std::byte, 8> buf{};
    writeLE64(buf.data(), 0x0102030405060708ULL);
    REQUIRE(readLE64(buf.data()) == 0x0102030405060708ULL);
}

TEST_CASE("writeLE64 stores least-significant byte first", "[tape-record]")
{
    std::array<std::byte, 8> buf{};
    writeLE64(buf.data(), 0x0102030405060708ULL);
    REQUIRE(buf[0] == std::byte{0x08});
    REQUIRE(buf[7] == std::byte{0x01});
}

// ---------------------------------------------------------------------------
// 3. CRC-32 known vector
// ---------------------------------------------------------------------------
TEST_CASE("crc32 of '123456789' == 0xCBF43926", "[tape-record]")
{
    // 9 ASCII bytes: the standard CRC-32 check vector.
    static constexpr std::array<std::byte, 9> kVec = {
        std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
        std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
        std::byte{'7'}, std::byte{'8'}, std::byte{'9'}
    };
    REQUIRE(crc32(kVec.data(), kVec.size()) == 0xCBF43926u);
}

// ---------------------------------------------------------------------------
// 4. encodeRecord / decodeRecordBody full round-trip
// ---------------------------------------------------------------------------
TEST_CASE("encodeRecord / decodeRecordBody full round-trip", "[tape-record]")
{
    // Build a non-trivial header.
    TapeRecordHeader hdr;
    hdr.seq          = 0x0102030405060708ULL;
    hdr.type         = TapeRecordType::Audio;
    hdr.codec        = TapeCodecId::AudioFlac;
    hdr.conceptualTs = Rational{ 3, 4 };
    hdr.lmcTs        = Rational{ -7, 8 };

    // Small payload.
    static constexpr std::array<std::byte, 5> kPayload = {
        std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC},
        std::byte{0xDD}, std::byte{0xEE}
    };

    std::vector<std::byte> out;
    const std::size_t total = encodeRecord(hdr, kPayload.data(), kPayload.size(), out);

    // Total = 4 (bodyLen field) + bodyLen + 4 (CRC).
    const std::uint32_t bodyLen = readLE32(out.data());
    REQUIRE(bodyLen == kRecordHeaderBytes + kPayload.size());
    REQUIRE(total == 4u + bodyLen + 4u);
    REQUIRE(out.size() == total);

    // Trailing CRC must match crc32(body).
    const std::byte* body = out.data() + 4;
    const std::uint32_t storedCrc = readLE32(out.data() + 4 + bodyLen);
    REQUIRE(storedCrc == crc32(body, bodyLen));

    // Decode the body back.
    TapeRecordHeader hdrOut;
    const std::byte* payloadOut = nullptr;
    std::size_t      payloadLen = 0;
    REQUIRE(decodeRecordBody(body, bodyLen, hdrOut, payloadOut, payloadLen));

    // Every header field must be bit-exact.
    REQUIRE(hdrOut.seq  == hdr.seq);
    REQUIRE(hdrOut.type == hdr.type);
    REQUIRE(hdrOut.codec == hdr.codec);
    REQUIRE(hdrOut.conceptualTs.numerator()   == hdr.conceptualTs.numerator());
    REQUIRE(hdrOut.conceptualTs.denominator() == hdr.conceptualTs.denominator());
    REQUIRE(hdrOut.lmcTs.numerator()   == hdr.lmcTs.numerator());
    REQUIRE(hdrOut.lmcTs.denominator() == hdr.lmcTs.denominator());

    // Payload pointer must point into body (no copy) and bytes must match.
    REQUIRE(payloadOut != nullptr);
    REQUIRE(payloadLen == kPayload.size());
    REQUIRE(std::memcmp(payloadOut, kPayload.data(), kPayload.size()) == 0);
}

// ---------------------------------------------------------------------------
// 5. Flipping a body byte changes the CRC
// ---------------------------------------------------------------------------
TEST_CASE("flipping any body byte changes the stored CRC", "[tape-record]")
{
    TapeRecordHeader hdr;
    hdr.seq   = 42;
    hdr.codec = TapeCodecId::AudioPcm;

    std::array<std::byte, 3> payload = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}
    };

    std::vector<std::byte> out;
    encodeRecord(hdr, payload.data(), payload.size(), out);

    const std::uint32_t bodyLen    = readLE32(out.data());
    const std::uint32_t storedCrc  = readLE32(out.data() + 4 + bodyLen);

    // Flip byte 0 of the body.
    out[4] = ~out[4];
    const std::uint32_t newCrc = crc32(out.data() + 4, bodyLen);
    REQUIRE(newCrc != storedCrc);
}

// ---------------------------------------------------------------------------
// 6. decodeRecordBody rejects bodyLen < kRecordHeaderBytes
// ---------------------------------------------------------------------------
TEST_CASE("decodeRecordBody returns false when bodyLen < kRecordHeaderBytes", "[tape-record]")
{
    std::array<std::byte, 10> buf{};
    TapeRecordHeader hdrOut;
    const std::byte* payloadOut = nullptr;
    std::size_t      payloadLen = 0;

    // 10 < 44, must return false.
    REQUIRE_FALSE(decodeRecordBody(buf.data(), 10, hdrOut, payloadOut, payloadLen));
}
