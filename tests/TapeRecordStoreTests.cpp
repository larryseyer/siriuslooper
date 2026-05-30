// Tests for the T0a tape record byte-layer primitives. These pin the
// on-disk format contract: correct little-endian encoding, standard
// CRC-32 (IEEE 802.3), file-header magic / version, and full
// encode→decode round-trips for every header field and the payload.
// Task 4 tests additionally cover TapeRecordWriter (worker-framed append,
// flush, RT-safe entry, per-tape sequencing, exact Rational lmcTs).
#include "ida/TapeRecord.h"
#include "ida/Rational.h"
#include "ida/AudioPayloadCodec.h"
#include "ida/IPayloadCodec.h"
#include "ida/TapeRecordReader.h"
#include "ida/TapeRecordWriter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
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

namespace {

// Build a stereo ramp: left[i] = i / (N-1), right[i] = -left[i].
// Requires numFrames >= 2 (divides by numFrames-1; UB / NaN at numFrames == 1).
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

// ---------------------------------------------------------------------------
// 9. TapeRecordWriter — worker-framed append, flush, RT-safe entry (T0a Task 4)
// ---------------------------------------------------------------------------

namespace {

// Helper: read the entire contents of a juce::File into a std::vector<std::byte>.
std::vector<std::byte> readFileBytes (const juce::File& f)
{
    juce::FileInputStream fis (f);
    if (! fis.openedOk()) return {};
    const auto size = static_cast<std::size_t> (fis.getTotalLength());
    std::vector<std::byte> buf (size);
    fis.read (buf.data(), static_cast<int> (size));
    return buf;
}

// Parse all records from raw file bytes starting at offset `pos` (after the
// 12-byte file header). Each record layout: [u32 bodyLen][body][u32 crc].
// Returns number of records successfully parsed into `records`.
struct ParsedRecord
{
    ida::TapeRecordHeader header;
    std::size_t           payloadLen { 0 };
};

bool parseAllRecords (const std::vector<std::byte>& raw,
                      std::vector<ParsedRecord>&    records)
{
    if (raw.size() < ida::kTapeFileHeaderBytes) return false;

    std::size_t pos = ida::kTapeFileHeaderBytes; // skip file header
    while (pos + 4 <= raw.size())
    {
        const std::uint32_t bodyLen = ida::readLE32 (raw.data() + pos);
        pos += 4;
        if (pos + bodyLen + 4 > raw.size()) return false; // truncated

        const std::byte*    body      = raw.data() + pos;
        const std::uint32_t storedCrc = ida::readLE32 (raw.data() + pos + bodyLen);
        const std::uint32_t calcCrc   = ida::crc32 (body, bodyLen);
        if (storedCrc != calcCrc) return false; // CRC mismatch

        ParsedRecord pr;
        const std::byte* payloadOut = nullptr;
        std::size_t      payloadLen = 0;
        if (! ida::decodeRecordBody (body, bodyLen, pr.header, payloadOut, payloadLen))
            return false;
        pr.payloadLen = payloadLen;
        records.push_back (pr);

        pos += bodyLen + 4; // advance past body + crc
    }
    return pos == raw.size(); // must consume exactly the whole file
}

} // namespace

TEST_CASE("TapeRecordWriter — N blocks produce N records with valid CRC and ascending seq",
          "[tape-record]")
{
    // Write to a temp directory so nothing persists across runs.
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-single");
    dir.deleteRecursively();
    dir.createDirectory();

    static constexpr double kSr        = 48000.0;
    static constexpr int    kBlockSize  = 64;
    static constexpr int    kNumBlocks  = 8;
    static constexpr int    kFlushMs    = 5;
    static constexpr std::size_t kQueueCap = 64;

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    // Inner scope: writer dtor joins the worker, guaranteeing flush + close.
    {
        ida::TapeRecordWriter writer (dir, kSr, kQueueCap, ida::TapeCodecId::AudioPcm, kFlushMs);

        // Generate constant-value stereo blocks (value doesn't matter for format test).
        std::vector<float> left  (kBlockSize, 0.1f);
        std::vector<float> right (kBlockSize, -0.1f);

        for (int i = 0; i < kNumBlocks; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlockSize);

        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    } // dtor joins worker; file is complete at this point

    // Parse the raw bytes.
    const auto raw = readFileBytes (tapeFile);
    REQUIRE (raw.size() >= ida::kTapeFileHeaderBytes);

    // File header: first 12 bytes must have valid magic and version == 1.
    std::uint16_t ver = 0;
    REQUIRE (ida::readFileHeader (raw.data(), raw.size(), ver));
    REQUIRE (ver == 1);

    // Parse records.
    std::vector<ParsedRecord> records;
    REQUIRE (parseAllRecords (raw, records));
    REQUIRE (static_cast<int> (records.size()) == kNumBlocks);

    // Each record must have seq 0..N-1, valid CRC was verified by parseAllRecords.
    for (int i = 0; i < kNumBlocks; ++i)
    {
        REQUIRE (records[static_cast<std::size_t> (i)].header.seq
                 == static_cast<std::uint64_t> (i));
    }
}

TEST_CASE("TapeRecordWriter — two tapes write to two distinct .idatape files",
          "[tape-record]")
{
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-two");
    dir.deleteRecursively();
    dir.createDirectory();

    static constexpr double kSr       = 44100.0;
    static constexpr int    kBlock    = 128;
    static constexpr int    kFlushMs  = 5;

    const ida::TapeId tapeA { 1 };
    const ida::TapeId tapeB { 2 };
    juce::File fileA, fileB;

    // Inner scope: writer dtor joins the worker, guaranteeing flush + close.
    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, kFlushMs);

        std::vector<float> lA (kBlock, 0.2f), rA (kBlock, -0.2f);
        std::vector<float> lB (kBlock, 0.3f), rB (kBlock, -0.3f);

        writer.deliverTapeBlock (tapeA, lA.data(), rA.data(), kBlock);
        writer.deliverTapeBlock (tapeB, lB.data(), rB.data(), kBlock);

        writer.closeTape (tapeA);
        writer.closeTape (tapeB);

        fileA = writer.tapeFile (tapeA);
        fileB = writer.tapeFile (tapeB);
    } // dtor joins worker; both files are complete at this point

    // Files must exist and be distinct paths.
    REQUIRE (fileA.existsAsFile());
    REQUIRE (fileB.existsAsFile());
    REQUIRE (fileA.getFullPathName() != fileB.getFullPathName());

    // Both must have valid file headers.
    {
        const auto rawA = readFileBytes (fileA);
        std::uint16_t ver = 0;
        REQUIRE (ida::readFileHeader (rawA.data(), rawA.size(), ver));
        REQUIRE (ver == 1);
    }
    {
        const auto rawB = readFileBytes (fileB);
        std::uint16_t ver = 0;
        REQUIRE (ida::readFileHeader (rawB.data(), rawB.size(), ver));
        REQUIRE (ver == 1);
    }
}

TEST_CASE("TapeRecordWriter — per-record lmcTs increases by blockFrames/sampleRate exactly",
          "[tape-record]")
{
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-ts");
    dir.deleteRecursively();
    dir.createDirectory();

    static constexpr double kSr       = 48000.0;
    static constexpr int    kBlock    = 512;
    static constexpr int    kN        = 5;
    static constexpr int    kFlushMs  = 5;

    const ida::TapeId tape { 7 };
    juce::File tapeFile;

    // Inner scope: writer dtor joins the worker, guaranteeing flush + close.
    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, kFlushMs);

        std::vector<float> left (kBlock, 0.0f), right (kBlock, 0.0f);

        for (int i = 0; i < kN; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);

        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    } // dtor joins worker; file is complete at this point

    const auto raw = readFileBytes (tapeFile);
    std::vector<ParsedRecord> records;
    REQUIRE (parseAllRecords (raw, records));
    REQUIRE (static_cast<int> (records.size()) == kN);

    // lmcTs for record i == Rational(i * kBlock, round(kSr)).
    // The numerator must be i * kBlock and denominator round(kSr), after normalization
    // the GCD may reduce them — so check via cross-multiplication for exact equality.
    const auto kSrInt = static_cast<std::int64_t> (std::llround (kSr));
    for (int i = 0; i < kN; ++i)
    {
        const ida::Rational expected { static_cast<std::int64_t> (i) * kBlock, kSrInt };
        const ida::Rational& actual  = records[static_cast<std::size_t> (i)].header.lmcTs;
        REQUIRE (actual == expected);
    }
}

TEST_CASE("TapeRecordWriter — oversized block is dropped and bumps droppedBlockCount",
          "[tape-record]")
{
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-drop");
    dir.deleteRecursively();
    dir.createDirectory();

    ida::TapeRecordWriter writer (dir, 48000.0, 64, ida::TapeCodecId::AudioPcm, 5);

    REQUIRE (writer.droppedBlockCount() == 0u);

    const ida::TapeId tape { 1 };
    // numSamples one more than the hard cap — must be dropped, not truncated.
    const int oversized = ida::kTapeRecordWriterMaxFramesPerMessage + 1;
    std::vector<float> left  (static_cast<std::size_t> (oversized), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (oversized), 0.0f);
    writer.deliverTapeBlock (tape, left.data(), right.data(), oversized);

    REQUIRE (writer.droppedBlockCount() == 1u);
}

TEST_CASE("TapeRecordWriter — flushIntervalMs is accessible after construction",
          "[tape-record]")
{
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-flush");
    dir.deleteRecursively();
    dir.createDirectory();

    static constexpr int kFlushMs = 17;
    ida::TapeRecordWriter writer (dir, 48000.0, 32, ida::TapeCodecId::AudioPcm, kFlushMs);

    REQUIRE (writer.flushIntervalMs() == kFlushMs);
}

// ---------------------------------------------------------------------------
// 10. deliverTapeBlock is allocation-free (m5 / spec test d)
// ---------------------------------------------------------------------------
// The operator-new override and alloc counters live in IdaMasterSpectrumTests.cpp.
extern thread_local std::atomic<size_t> g_allocCount;
extern thread_local bool g_counting;

TEST_CASE("TapeRecordWriter::deliverTapeBlock is allocation-free", "[tape-record][rt-safety]")
{
    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-trw-alloc");
    dir.deleteRecursively();
    dir.createDirectory();

    static constexpr int kBlock   = 64;
    static constexpr int kFlushMs = 5;

    ida::TapeRecordWriter writer (dir, 48000.0, 256, ida::TapeCodecId::AudioPcm, kFlushMs);

    const ida::TapeId tape { 1 };
    std::vector<float> left  (kBlock, 0.1f);
    std::vector<float> right (kBlock, -0.1f);

    // Prime: one block outside the measured region so any lazy setup (queue
    // pre-allocation, first openTapeFor on the worker) is complete before arming
    // the counter. The dtor-join guarantees the worker has processed this block.
    {
        ida::TapeRecordWriter primer (dir, 48000.0, 256, ida::TapeCodecId::AudioPcm, kFlushMs);
        primer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
    } // worker joins here

    // Measured region: arm counter, call deliverTapeBlock, disarm.
    g_allocCount.store (0, std::memory_order_relaxed);
    g_counting = true;
    writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
    g_counting = false;

    REQUIRE (g_allocCount.load (std::memory_order_relaxed) == 0u);
}

// ---------------------------------------------------------------------------
// 11. TapeRecordReader — scan, index, random-access decode (T0a Task 5)
// ---------------------------------------------------------------------------

namespace {

// Helper: build a registry with both PcmAudioCodec and FlacAudioCodec.
ida::TapeCodecRegistry makeFullRegistry()
{
    ida::TapeCodecRegistry reg;
    reg.registerCodec (std::make_shared<ida::PcmAudioCodec>());
    reg.registerCodec (std::make_shared<ida::FlacAudioCodec>());
    return reg;
}

} // namespace

TEST_CASE("TapeRecordReader — open(recover=true) indexes N PCM records with ascending seqs and offsets",
          "[tape-record]")
{
    static constexpr int    kN         = 6;
    static constexpr int    kBlock     = 128;
    static constexpr double kSr        = 48000.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-index");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, 5);
        std::vector<float> left (kBlock, 0.1f), right (kBlock, -0.1f);
        for (int i = 0; i < kN; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    } // dtor joins; file is complete

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (report.truncated == false);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    const auto& idx = reader->index();
    REQUIRE (static_cast<int> (idx.size()) == kN);

    // seqs must be 0..N-1 ascending
    for (int i = 0; i < kN; ++i)
        REQUIRE (idx[static_cast<std::size_t> (i)].seq == static_cast<std::uint64_t> (i));

    // file offsets must be strictly increasing
    for (int i = 1; i < kN; ++i)
        REQUIRE (idx[static_cast<std::size_t> (i)].fileOffset
                 > idx[static_cast<std::size_t> (i - 1)].fileOffset);
}

TEST_CASE("TapeRecordReader — readAudioRecord(K) decodes position K without decoding predecessors",
          "[tape-record]")
{
    static constexpr int    kN     = 5;
    static constexpr int    kBlock = 64;
    static constexpr double kSr    = 44100.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-random");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    // Each block has a distinct constant value so we can identify which record
    // we decoded (record i has left[j] == (i+1)*0.1f).
    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, 5);
        for (int i = 0; i < kN; ++i)
        {
            const float val = static_cast<float> (i + 1) * 0.1f;
            std::vector<float> left (kBlock, val), right (kBlock, -val);
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
        }
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    }

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    // Read position K=3 (out-of-order: skip 0,1,2).
    static constexpr std::uint64_t kK = 3;
    ida::PcmBlock block;
    ida::TapeRecordHeader hdr;
    REQUIRE (reader->readAudioRecord (kK, block, hdr));

    // hOut.seq must be K — confirms we decoded the right record.
    REQUIRE (hdr.seq == kK);

    // PCM value for record 3 is (3+1)*0.1 == 0.4f — bit-exact.
    REQUIRE (block.numFrames() == kBlock);
    const float expectedVal = static_cast<float> (kK + 1) * 0.1f;
    for (int i = 0; i < kBlock; ++i)
    {
        CHECK_THAT (block.left [static_cast<std::size_t> (i)],
                    Catch::Matchers::WithinAbs (expectedVal, 0.0f));
        CHECK_THAT (block.right[static_cast<std::size_t> (i)],
                    Catch::Matchers::WithinAbs (-expectedVal, 0.0f));
    }
}

TEST_CASE("TapeRecordReader — FLAC encoded records round-trip within 24-bit tolerance",
          "[tape-record]")
{
    static constexpr int    kN     = 3;
    static constexpr int    kBlock = 256;
    static constexpr double kSr    = 48000.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-flac");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    std::vector<std::vector<float>> origLeft (kN), origRight (kN);
    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioFlac, 5);
        for (int i = 0; i < kN; ++i)
        {
            origLeft [static_cast<std::size_t> (i)].resize (kBlock);
            origRight[static_cast<std::size_t> (i)].resize (kBlock);
            for (int j = 0; j < kBlock; ++j)
            {
                const float v = static_cast<float> (j) / static_cast<float> (kBlock - 1);
                origLeft [static_cast<std::size_t> (i)][static_cast<std::size_t> (j)] =  v;
                origRight[static_cast<std::size_t> (i)][static_cast<std::size_t> (j)] = -v;
            }
            writer.deliverTapeBlock (tape,
                origLeft [static_cast<std::size_t> (i)].data(),
                origRight[static_cast<std::size_t> (i)].data(),
                kBlock);
        }
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    }

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    static constexpr float kTol = 2.0f / static_cast<float> (1 << 23);

    for (int i = 0; i < kN; ++i)
    {
        ida::PcmBlock block;
        ida::TapeRecordHeader hdr;
        REQUIRE (reader->readAudioRecord (static_cast<std::uint64_t> (i), block, hdr));
        REQUIRE (hdr.seq == static_cast<std::uint64_t> (i));
        REQUIRE (block.numFrames() == kBlock);

        for (int j = 0; j < kBlock; ++j)
        {
            const auto jj = static_cast<std::size_t> (j);
            CHECK_THAT (block.left [jj],
                        Catch::Matchers::WithinAbs (origLeft [static_cast<std::size_t> (i)][jj], kTol));
            CHECK_THAT (block.right[jj],
                        Catch::Matchers::WithinAbs (origRight[static_cast<std::size_t> (i)][jj], kTol));
        }
    }
}

TEST_CASE("TapeRecordReader — readAudioRecord returns false for position >= recordCount",
          "[tape-record]")
{
    static constexpr int    kN     = 4;
    static constexpr int    kBlock = 64;
    static constexpr double kSr    = 48000.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-oob");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, 5);
        std::vector<float> left (kBlock, 0.5f), right (kBlock, -0.5f);
        for (int i = 0; i < kN; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    }

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    ida::PcmBlock block;
    ida::TapeRecordHeader hdr;

    // Exactly at count — out of bounds.
    REQUIRE_FALSE (reader->readAudioRecord (static_cast<std::uint64_t> (kN), block, hdr));
    // Well past count.
    REQUIRE_FALSE (reader->readAudioRecord (999u, block, hdr));
}
