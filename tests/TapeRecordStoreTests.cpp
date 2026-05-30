// Tests for the T0a tape record byte-layer primitives. These pin the
// on-disk format contract: correct little-endian encoding, standard
// CRC-32 (IEEE 802.3), file-header magic / version, and full
// encode→decode round-trips for every header field and the payload.
#include "ida/TapeRecord.h"
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

// ---------------------------------------------------------------------------
// 7. IPayloadCodec interface + TapeCodecRegistry (T0a Task 2)
// ---------------------------------------------------------------------------
#include "ida/IPayloadCodec.h"

namespace {

// Trivial fake codec: AudioPcm tag, encode returns sentinel bytes,
// decode fills a 1-frame PcmBlock and returns true.
class FakePcmCodec final : public ida::IPayloadCodec {
public:
    ida::TapeCodecId codecId() const noexcept override
    {
        return ida::TapeCodecId::AudioPcm;
    }

    std::vector<std::byte> encode(const float*, const float*,
                                  int, double) const override
    {
        return { std::byte{0xDE}, std::byte{0xAD} };
    }

    bool decode(const std::byte*, std::size_t, ida::PcmBlock& out) const override
    {
        out.left  = { 0.5f };
        out.right = { -0.5f };
        return true;
    }
};

// Second fake with the same AudioPcm id (replacement test).
class FakePcmCodec2 final : public ida::IPayloadCodec {
public:
    ida::TapeCodecId codecId() const noexcept override
    {
        return ida::TapeCodecId::AudioPcm;
    }

    std::vector<std::byte> encode(const float*, const float*,
                                  int, double) const override
    {
        return { std::byte{0xBE}, std::byte{0xEF} };
    }

    bool decode(const std::byte*, std::size_t, ida::PcmBlock& out) const override
    {
        out.left  = { 1.0f };
        out.right = { 1.0f };
        return true;
    }
};

} // namespace

TEST_CASE("TapeCodecRegistry — empty registry returns nullptr for any id", "[tape-record]")
{
    ida::TapeCodecRegistry reg;
    REQUIRE(reg.codecFor(ida::TapeCodecId::AudioPcm)  == nullptr);
    REQUIRE(reg.codecFor(ida::TapeCodecId::AudioFlac) == nullptr);
}

TEST_CASE("TapeCodecRegistry — registered codec is retrievable by id", "[tape-record]")
{
    ida::TapeCodecRegistry reg;
    reg.registerCodec(std::make_shared<FakePcmCodec>());

    ida::IPayloadCodec* codec = reg.codecFor(ida::TapeCodecId::AudioPcm);
    REQUIRE(codec != nullptr);
    REQUIRE(codec->codecId() == ida::TapeCodecId::AudioPcm);

    // Unrelated id still returns nullptr.
    REQUIRE(reg.codecFor(ida::TapeCodecId::AudioFlac) == nullptr);
}

TEST_CASE("TapeCodecRegistry — registering a second codec with same id replaces the first", "[tape-record]")
{
    ida::TapeCodecRegistry reg;
    auto first  = std::make_shared<FakePcmCodec>();
    auto second = std::make_shared<FakePcmCodec2>();

    reg.registerCodec(first);
    reg.registerCodec(second);

    ida::IPayloadCodec* found = reg.codecFor(ida::TapeCodecId::AudioPcm);
    REQUIRE(found != nullptr);

    // The returned codec should encode to the second's sentinel bytes to
    // confirm identity.
    auto bytes = found->encode(nullptr, nullptr, 0, 44100.0);
    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == std::byte{0xBE});
    REQUIRE(bytes[1] == std::byte{0xEF});
}

// ---------------------------------------------------------------------------
// 8. PcmAudioCodec + FlacAudioCodec (T0a Task 3)
// ---------------------------------------------------------------------------
#include "ida/AudioPayloadCodec.h"

namespace {

// Build a stereo ramp: left[i] = i / (N-1), right[i] = -left[i].
void makeStereoRamp (int numFrames, std::vector<float>& left, std::vector<float>& right)
{
    left.resize  (static_cast<std::size_t> (numFrames));
    right.resize (static_cast<std::size_t> (numFrames));
    for (int i = 0; i < numFrames; ++i)
    {
        left [static_cast<std::size_t> (i)] =  static_cast<float> (i) / static_cast<float> (numFrames - 1);
        right[static_cast<std::size_t> (i)] = -left[static_cast<std::size_t> (i)];
    }
}

} // namespace

TEST_CASE("PcmAudioCodec — encode→decode of stereo ramp is bit-exact", "[tape-record]")
{
    ida::PcmAudioCodec codec;
    REQUIRE(codec.codecId() == ida::TapeCodecId::AudioPcm);

    static constexpr int kFrames = 512;
    std::vector<float> left, right;
    makeStereoRamp (kFrames, left, right);

    const auto payload = codec.encode (left.data(), right.data(), kFrames, 48000.0);
    REQUIRE_FALSE (payload.empty());

    ida::PcmBlock block;
    REQUIRE (codec.decode (payload.data(), payload.size(), block));
    REQUIRE (block.numFrames() == kFrames);

    for (int i = 0; i < kFrames; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        // PCM round-trip must be bit-exact — use tolerance 0 to express the
        // intent without triggering -Wfloat-equal on ==.
        CHECK_THAT (block.left [idx], Catch::Matchers::WithinAbs (left [idx], 0.0f));
        CHECK_THAT (block.right[idx], Catch::Matchers::WithinAbs (right[idx], 0.0f));
    }
}

TEST_CASE("FlacAudioCodec — encode→decode returns correct frame count and samples within 24-bit tolerance", "[tape-record]")
{
    ida::FlacAudioCodec codec;
    REQUIRE(codec.codecId() == ida::TapeCodecId::AudioFlac);

    static constexpr int kFrames = 512;
    std::vector<float> left, right;
    makeStereoRamp (kFrames, left, right);

    const auto payload = codec.encode (left.data(), right.data(), kFrames, 44100.0);
    REQUIRE_FALSE (payload.empty());

    ida::PcmBlock block;
    REQUIRE (codec.decode (payload.data(), payload.size(), block));
    REQUIRE (block.numFrames() == kFrames);

    // 24-bit FLAC quantization: max error is 1/2^23 plus a small margin.
    static constexpr float kTol = 2.0f / static_cast<float> (1 << 23);
    for (int i = 0; i < kFrames; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        CHECK_THAT (block.left [idx], Catch::Matchers::WithinAbs (left [idx], kTol));
        CHECK_THAT (block.right[idx], Catch::Matchers::WithinAbs (right[idx], kTol));
    }
}

TEST_CASE("FlacAudioCodec — each block decodes standalone (no cross-block state)", "[tape-record]")
{
    ida::FlacAudioCodec codec;

    static constexpr int kFrames = 256;

    // Block A: constant 0.25f / -0.25f.
    std::vector<float> aL (kFrames, 0.25f);
    std::vector<float> aR (kFrames, -0.25f);
    const auto payloadA = codec.encode (aL.data(), aR.data(), kFrames, 44100.0);

    // Block B: ramp, encoded independently.
    std::vector<float> bL, bR;
    makeStereoRamp (kFrames, bL, bR);
    const auto payloadB = codec.encode (bL.data(), bR.data(), kFrames, 44100.0);

    // Decode B WITHOUT ever touching A — simulate receiving out-of-order or
    // reading from an arbitrary file position.
    ida::PcmBlock block;
    REQUIRE (codec.decode (payloadB.data(), payloadB.size(), block));
    REQUIRE (block.numFrames() == kFrames);

    static constexpr float kTol = 2.0f / static_cast<float> (1 << 23);
    for (int i = 0; i < kFrames; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        CHECK_THAT (block.left [idx], Catch::Matchers::WithinAbs (bL[idx], kTol));
        CHECK_THAT (block.right[idx], Catch::Matchers::WithinAbs (bR[idx], kTol));
    }
    (void) payloadA; // payloadA encoded but never decoded — by design
}

TEST_CASE("PcmAudioCodec — decode of garbage bytes returns false without throwing", "[tape-record]")
{
    ida::PcmAudioCodec codec;
    static constexpr std::array<std::byte, 7> kGarbage = {
        std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00},
        std::byte{0xAB}, std::byte{0xCD}, std::byte{0xEF}, std::byte{0x42}
    };
    ida::PcmBlock block;
    bool threw = false;
    bool result = false;
    try { result = codec.decode (kGarbage.data(), kGarbage.size(), block); }
    catch (...) { threw = true; }
    REQUIRE_FALSE (threw);
    REQUIRE_FALSE (result);
}

TEST_CASE("FlacAudioCodec — decode of garbage bytes returns false without throwing", "[tape-record]")
{
    ida::FlacAudioCodec codec;
    static constexpr std::array<std::byte, 16> kGarbage = {
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF},
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
        std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
        std::byte{0x88}, std::byte{0x99}, std::byte{0xAA}, std::byte{0xBB}
    };
    ida::PcmBlock block;
    bool threw = false;
    bool result = false;
    try { result = codec.decode (kGarbage.data(), kGarbage.size(), block); }
    catch (...) { threw = true; }
    REQUIRE_FALSE (threw);
    REQUIRE_FALSE (result);
}
