// Tests for the T0a tape record byte-layer primitives. These pin the
// on-disk format contract: correct little-endian encoding, standard
// CRC-32 (IEEE 802.3), file-header magic / version, and full
// encode→decode round-trips for every header field and the payload.
// Task 4 tests additionally cover TapeRecordWriter (worker-framed append,
// flush, RT-safe entry, per-tape sequencing, exact Rational lmcTs).
// Task 7 tests cover concurrent read-during-write via reader refresh.
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
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
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

// T3: Counting codec — wraps PcmAudioCodec, increments decodeCalls on each
// decode(), delegates actual decoding so bit-exact checks still pass.
class CountingPcmCodec final : public ida::IPayloadCodec {
public:
    mutable int decodeCalls { 0 };

    ida::TapeCodecId codecId() const noexcept override
    {
        return ida::TapeCodecId::AudioPcm;
    }

    std::vector<std::byte> encode (const float* left, const float* right,
                                   int numFrames, double sampleRate) const override
    {
        return inner_.encode (left, right, numFrames, sampleRate);
    }

    bool decode (const std::byte* data, std::size_t size,
                 ida::PcmBlock& out) const override
    {
        ++decodeCalls;
        return inner_.decode (data, size, out);
    }

private:
    ida::PcmAudioCodec inner_;
};

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

// ---------------------------------------------------------------------------
// 12. TapeRecordReader — open() failure paths (T1)
// ---------------------------------------------------------------------------

TEST_CASE("TapeRecordReader::open returns nullptr for a nonexistent file",
          "[tape-record]")
{
    const juce::File missing = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-noexist.idatape");
    missing.deleteFile();

    ida::TapeCodecRegistry reg;
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (missing, reg, report, /*recover=*/false);
    REQUIRE (reader == nullptr);
}

TEST_CASE("TapeRecordReader::open returns nullptr when file header magic is wrong",
          "[tape-record]")
{
    const juce::File f = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-badmagic.idatape");
    f.deleteFile();

    // Write 12 bytes of garbage — definitely not IDATAPE\0 + version.
    {
        juce::FileOutputStream fos (f);
        REQUIRE (fos.openedOk());
        const std::array<std::byte, 12> garbage = {
            std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF},
            std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
            std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77}
        };
        fos.write (garbage.data(), garbage.size());
    }

    ida::TapeCodecRegistry reg;
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (f, reg, report, /*recover=*/false);
    REQUIRE (reader == nullptr);
}

// ---------------------------------------------------------------------------
// 13. TapeRecordReader — unregistered-codec path (T2)
// ---------------------------------------------------------------------------

TEST_CASE("TapeRecordReader::readAudioRecord returns false when codec is unregistered",
          "[tape-record]")
{
    static constexpr int    kN     = 3;
    static constexpr int    kBlock = 64;
    static constexpr double kSr    = 48000.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-nocodec");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    {
        ida::TapeRecordWriter writer (dir, kSr, 64, ida::TapeCodecId::AudioPcm, 5);
        std::vector<float> left (kBlock, 0.2f), right (kBlock, -0.2f);
        for (int i = 0; i < kN; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    }

    // Registry with NO codecs registered.
    ida::TapeCodecRegistry emptyReg;
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, emptyReg, report, /*recover=*/true);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    ida::PcmBlock block;
    ida::TapeRecordHeader hdr;

    bool threw = false;
    bool result = true;
    try { result = reader->readAudioRecord (0, block, hdr); }
    catch (...) { threw = true; }

    REQUIRE_FALSE (threw);
    REQUIRE_FALSE (result);
}

// ---------------------------------------------------------------------------
// 14. TapeRecordReader — random access proof via counting codec (T3)
// ---------------------------------------------------------------------------

TEST_CASE("TapeRecordReader::readAudioRecord(K) invokes decode exactly once (no predecessor decodes)",
          "[tape-record]")
{
    static constexpr int    kN     = 5;
    static constexpr int    kBlock = 64;
    static constexpr double kSr    = 44100.0;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-reader-counting");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    // Each block has a distinct constant value (record i → (i+1)*0.1f).
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

    // Build a registry wired to the counting codec.
    auto counting = std::make_shared<CountingPcmCodec>();
    ida::TapeCodecRegistry reg;
    reg.registerCodec (counting);

    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));

    // Reset counter after open() — open() only scans, should not call decode.
    counting->decodeCalls = 0;

    // Read position K=3 out-of-order (skips records 0, 1, 2).
    static constexpr std::uint64_t kK = 3;
    ida::PcmBlock block;
    ida::TapeRecordHeader hdr;
    REQUIRE (reader->readAudioRecord (kK, block, hdr));

    // Exactly one decode call — predecessors were NOT decoded.
    REQUIRE (counting->decodeCalls == 1);

    // Correct record decoded.
    REQUIRE (hdr.seq == kK);
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

// ---------------------------------------------------------------------------
// 15. TapeRecordReader — crash recovery (T0a Task 6)
// ---------------------------------------------------------------------------

namespace {

// Write N valid records into a temp file via TapeRecordWriter and return the
// file path. Also returns the start offset of each record in `recordOffsets`.
juce::File buildNRecordFile (const juce::File&           dir,
                              int                          n,
                              std::vector<std::uint64_t>& recordOffsets)
{
    static constexpr int    kBlock   = 64;
    static constexpr double kSr      = 48000.0;
    static constexpr int    kFlushMs = 5;

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    {
        ida::TapeRecordWriter writer (dir, kSr, 256, ida::TapeCodecId::AudioPcm, kFlushMs);
        std::vector<float> left (kBlock, 0.1f), right (kBlock, -0.1f);
        for (int i = 0; i < n; ++i)
            writer.deliverTapeBlock (tape, left.data(), right.data(), kBlock);
        writer.closeTape (tape);
        tapeFile = writer.tapeFile (tape);
    }

    // Parse the file to collect record start offsets.
    recordOffsets.clear();
    juce::FileInputStream fis (tapeFile);
    REQUIRE (fis.openedOk());
    const auto fileLen = static_cast<std::uint64_t> (fis.getTotalLength());

    std::uint64_t pos = static_cast<std::uint64_t> (ida::kTapeFileHeaderBytes);
    while (pos + 4 <= fileLen)
    {
        recordOffsets.push_back (pos);
        std::byte lenBuf[4];
        fis.setPosition (static_cast<juce::int64> (pos));
        fis.read (lenBuf, 4);
        const std::uint32_t bodyLen = ida::readLE32 (lenBuf);
        pos += 4 + static_cast<std::uint64_t> (bodyLen) + 4;
    }

    return tapeFile;
}

// Append a structurally partial record: a 4-byte bodyLen prefix claiming
// `claimedBodyLen` bytes of body, followed by only `actualBodyBytes` bytes.
void appendPartialRecord (const juce::File& f,
                          std::uint32_t     claimedBodyLen,
                          std::uint32_t     actualBodyBytes)
{
    juce::FileOutputStream fos (f);
    REQUIRE (fos.openedOk());
    fos.setPosition (fos.getFile().getSize());

    std::byte lenBuf[4];
    ida::writeLE32 (lenBuf, claimedBodyLen);
    fos.write (lenBuf, 4);

    std::vector<std::byte> garbage (actualBodyBytes, std::byte{0xAB});
    if (actualBodyBytes > 0)
        fos.write (garbage.data(), static_cast<std::size_t> (actualBodyBytes));
}

// Flip a single byte inside the body of the record at `recordOffset`
// (which points at the 4-byte bodyLen prefix).
// `byteInBody` is the 0-based index into the body (after the 4-byte prefix).
void corruptRecordBodyByte (const juce::File& f,
                             std::uint64_t     recordOffset,
                             std::uint32_t     byteInBody)
{
    const std::uint64_t byteOffset = recordOffset + 4 + byteInBody;

    // Read the byte in its own scope so the FileInputStream is fully closed
    // before the write handle opens (two competing handles on the same file
    // will fail on Windows).
    std::byte b{};
    {
        juce::FileInputStream fis (f);
        REQUIRE (fis.openedOk());
        fis.setPosition (static_cast<juce::int64> (byteOffset));
        fis.read (&b, 1);
    } // fis destroyed here — safe to open write handle below

    FILE* fp = std::fopen (f.getFullPathName().toRawUTF8(), "r+b");
    REQUIRE (fp != nullptr);
    std::fseek (fp, static_cast<long> (byteOffset), SEEK_SET);
    const std::byte flipped = ~b;
    std::fwrite (&flipped, 1, 1, fp);
    std::fclose (fp);
}

} // namespace

// (a) Partial trailing record — recover=true truncates and reports correctly.
TEST_CASE("TapeRecordReader recover=true: partial trailing record is truncated",
          "[tape-record]")
{
    static constexpr int kN = 4;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-partial");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);
    REQUIRE (static_cast<int> (recordOffsets.size()) == kN);

    // The partial record starts right after the last complete record.
    const std::uint64_t partialStart = static_cast<std::uint64_t> (tapeFile.getSize());

    // Append a partial record: claim 1000 body bytes but write only 10.
    appendPartialRecord (tapeFile, 1000, 10);
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) > partialStart);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));
    REQUIRE (report.truncated == true);
    REQUIRE (! report.reason.empty());
    REQUIRE (report.recordsKept == static_cast<std::uint64_t> (kN));
    REQUIRE (report.truncatedAtOffset == partialStart);

    // On-disk file must be physically truncated to partialStart.
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == partialStart);
}

// (b) CRC mismatch in the last record — recover=true truncates from that record.
TEST_CASE("TapeRecordReader recover=true: last record CRC mismatch truncates that record",
          "[tape-record]")
{
    static constexpr int kN = 5;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-crc-last");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);
    REQUIRE (static_cast<int> (recordOffsets.size()) == kN);

    const std::uint64_t lastRecordStart = recordOffsets.back();
    corruptRecordBodyByte (tapeFile, lastRecordStart, 5);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN - 1));
    REQUIRE (report.truncated == true);
    REQUIRE (! report.reason.empty());
    REQUIRE (report.recordsKept == static_cast<std::uint64_t> (kN - 1));
    REQUIRE (report.truncatedAtOffset == lastRecordStart);

    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == lastRecordStart);
}

// (c) CRC mismatch in a middle record — everything from that record onward is dropped.
TEST_CASE("TapeRecordReader recover=true: middle record CRC mismatch truncates from that record",
          "[tape-record]")
{
    static constexpr int kN = 6;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-crc-middle");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);
    REQUIRE (static_cast<int> (recordOffsets.size()) == kN);

    // Corrupt record at index 2 (0-based) — records 2..5 should be lost.
    static constexpr int kCorruptIdx = 2;
    const std::uint64_t corruptStart = recordOffsets[static_cast<std::size_t> (kCorruptIdx)];
    corruptRecordBodyByte (tapeFile, corruptStart, 0);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kCorruptIdx));
    REQUIRE (report.truncated == true);
    REQUIRE (! report.reason.empty());
    REQUIRE (report.recordsKept == static_cast<std::uint64_t> (kCorruptIdx));
    REQUIRE (report.truncatedAtOffset == corruptStart);

    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == corruptStart);
}

// (d) Clean file — recover=true reports no truncation and leaves file unchanged.
TEST_CASE("TapeRecordReader recover=true: clean file reports no truncation",
          "[tape-record]")
{
    static constexpr int kN = 5;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-clean");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);

    const auto originalSize = static_cast<std::uint64_t> (tapeFile.getSize());

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));
    REQUIRE (report.truncated == false);
    REQUIRE (report.reason.empty());
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == originalSize);
}

// (e) recover=false on a file with a partial trailing record: no mutation, no error.
TEST_CASE("TapeRecordReader recover=false: partial trailing record leaves file unchanged",
          "[tape-record]")
{
    static constexpr int kN = 3;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-false-partial");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);

    const std::uint64_t sizeBeforePartial = static_cast<std::uint64_t> (tapeFile.getSize());

    // Append a partial record: claim 500 bytes but write only 8.
    appendPartialRecord (tapeFile, 500, 8);

    const std::uint64_t sizeWithPartial = static_cast<std::uint64_t> (tapeFile.getSize());
    REQUIRE (sizeWithPartial > sizeBeforePartial);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/false);

    REQUIRE (reader != nullptr);
    // Only the N complete records are indexed; the partial tail is not an error.
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));
    // recover=false must not set truncated=true (no mutation claim).
    REQUIRE (report.truncated == false);
    // File must be byte-for-byte unchanged.
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == sizeWithPartial);
}

// (f) recover=false with a CRC-corrupt last record: file unchanged, recordCount==N-1.
TEST_CASE("TapeRecordReader recover=false: CRC-corrupt last record leaves file unchanged",
          "[tape-record]")
{
    static constexpr int kN = 4;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-false-crc");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);
    REQUIRE (static_cast<int> (recordOffsets.size()) == kN);

    const std::uint64_t originalSize = static_cast<std::uint64_t> (tapeFile.getSize());

    // Corrupt the last record's body — CRC will mismatch.
    const std::uint64_t lastRecordStart = recordOffsets.back();
    corruptRecordBodyByte (tapeFile, lastRecordStart, 5);

    // File size must not change from the corruption (only one byte was flipped).
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == originalSize);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/false);

    REQUIRE (reader != nullptr);
    // Stops at the corrupt record — N-1 complete records indexed.
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN - 1));
    // recover=false must never claim truncation.
    REQUIRE (report.truncated == false);
    // File must be byte-for-byte unchanged.
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == originalSize);
}

// (g) recover=true with a CRC-valid but structurally malformed body
//     (bodyLen < kRecordHeaderBytes): truncated==true, reason=="malformed record body",
//     recordsKept==N, file truncated to the malformed record's offset.
TEST_CASE("TapeRecordReader recover=true: CRC-valid malformed body is truncated honestly",
          "[tape-record]")
{
    static constexpr int kN = 3;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-recover-malformed");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);

    // Record the truncation point: where the malformed record will start.
    const std::uint64_t malformedStart = static_cast<std::uint64_t> (tapeFile.getSize());

    // Craft a record with bodyLen = 10 (< kRecordHeaderBytes == 44).
    // The CRC must match the 10-byte body so the CRC check passes and
    // decodeRecordBody is reached (where it will return false for bodyLen < 44).
    static constexpr std::uint32_t kBodyLen = 10;
    std::array<std::byte, kBodyLen> body{};
    for (std::size_t i = 0; i < kBodyLen; ++i)
        body[i] = std::byte{ static_cast<unsigned char> (i + 1) };

    const std::uint32_t goodCrc = ida::crc32 (body.data(), kBodyLen);

    {
        juce::FileOutputStream fos (tapeFile);
        REQUIRE (fos.openedOk());
        fos.setPosition (static_cast<juce::int64> (malformedStart));

        std::byte lenBuf[4];
        ida::writeLE32 (lenBuf, kBodyLen);
        fos.write (lenBuf, 4);
        fos.write (body.data(), kBodyLen);

        std::byte crcBuf[4];
        ida::writeLE32 (crcBuf, goodCrc);
        fos.write (crcBuf, 4);
    }

    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) > malformedStart);

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/true);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));
    REQUIRE (report.truncated == true);
    REQUIRE (report.reason == "malformed record body");
    REQUIRE (report.recordsKept == static_cast<std::uint64_t> (kN));
    REQUIRE (report.truncatedAtOffset == malformedStart);
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == malformedStart);
}

// ---------------------------------------------------------------------------
// 16. TapeRecordReader — concurrent read-during-write (T0a Task 7)
// ---------------------------------------------------------------------------

namespace {

// Poll until `reader.recordCount()` reaches `atLeast` or `timeoutMs` elapses.
// Returns the count seen at the polling deadline.
// IMPORTANT: asserts non-decreasing count on EVERY refresh iteration.
std::uint64_t pollForCount (ida::TapeRecordReader& reader,
                             std::uint64_t          atLeast,
                             int                    timeoutMs)
{
    using Clock    = std::chrono::steady_clock;
    using Ms       = std::chrono::milliseconds;
    const auto deadline = Clock::now() + Ms (timeoutMs);

    std::uint64_t last = reader.recordCount();
    while (last < atLeast && Clock::now() < deadline)
    {
        std::this_thread::sleep_for (Ms (10));
        ida::TapeTruncationReport tmp;
        reader.refresh (tmp);
        const std::uint64_t next = reader.recordCount();
        // Index must never shrink between refreshes.
        REQUIRE (next >= last);
        last = next;
    }
    return last;
}

// Verify that every entry in `reader.index()` [0, count) has a valid CRC
// by re-reading from disk. Returns true if all pass.
bool allVisibleRecordsHaveValidCrc (const ida::TapeRecordReader& reader,
                                    const juce::File&             file)
{
    const auto& idx = reader.index();
    juce::FileInputStream fis (file);
    if (! fis.openedOk())
        return false;

    for (const auto& entry : idx)
    {
        // Read bodyLen prefix, body, and CRC directly from disk.
        const std::uint64_t bodyOffset = entry.fileOffset + 4;

        std::vector<std::byte> body (entry.bodyLen);
        fis.setPosition (static_cast<juce::int64> (bodyOffset));
        const int bodyRead = fis.read (body.data(), static_cast<int> (entry.bodyLen));
        if (static_cast<std::uint32_t> (bodyRead) != entry.bodyLen)
            return false;

        // CRC sits immediately after the body; the stream cursor is already
        // positioned there after the body read above (positional).
        std::byte crcBuf[4];
        if (fis.read (crcBuf, 4) != 4)
            return false;

        if (ida::crc32 (body.data(), entry.bodyLen) != ida::readLE32 (crcBuf))
            return false;
    }
    return true;
}

} // namespace

// (a) Live writer: open(recover=false) + refresh() see monotonically growing,
//     always-valid-CRC record set; final refresh after writer closes sees all.
TEST_CASE("TapeRecordReader — live writer: refresh is monotonic and all visible CRCs valid",
          "[tape-record]")
{
    static constexpr int    kBlockSize  = 64;
    static constexpr int    kFirstBatch = 8;
    static constexpr int    kMoreBatch  = 8;
    static constexpr int    kTotalBlocks = kFirstBatch + kMoreBatch;
    static constexpr double kSr         = 48000.0;
    static constexpr int    kFlushMs    = 5;

    // Generous timeout: 2 s covers any realistic CI slowdown.
    static constexpr int kPollTimeoutMs = 2000;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-concurrent-rw");
    dir.deleteRecursively();
    dir.createDirectory();

    const ida::TapeId tape { 1 };
    juce::File tapeFile;

    // Registry with PCM codec (same as the writer).
    auto reg = makeFullRegistry();

    {
        ida::TapeRecordWriter writer (dir, kSr, 256, ida::TapeCodecId::AudioPcm, kFlushMs);
        tapeFile = writer.tapeFile (tape);

        // --- First batch ---
        {
            std::vector<float> left (kBlockSize, 0.1f), right (kBlockSize, -0.1f);
            for (int i = 0; i < kFirstBatch; ++i)
                writer.deliverTapeBlock (tape, left.data(), right.data(), kBlockSize);
        }

        // Wait for the writer's worker thread to create the file (lazy creation),
        // then open a reader. A bounded poll avoids a fixed sleep that can race
        // under CI load.
        {
            using Clock = std::chrono::steady_clock;
            using Ms    = std::chrono::milliseconds;
            const auto deadline = Clock::now() + Ms (500);
            while (! tapeFile.existsAsFile() && Clock::now() < deadline)
                std::this_thread::sleep_for (Ms (5));
        }

        ida::TapeTruncationReport report;
        auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/false);

        // The file header must be valid (writer created it on first open).
        REQUIRE (reader != nullptr);
        REQUIRE (report.truncated == false);

        // recordCount at open is the earliest observed count — [0, kFirstBatch].
        // All subsequent counts must be >= this value (monotonic chain anchored here).
        const std::uint64_t countAfterFirstOpen = reader->recordCount();
        REQUIRE (countAfterFirstOpen <= static_cast<std::uint64_t> (kFirstBatch));

        // Every visible record must have a valid CRC — never expose a partial.
        REQUIRE (allVisibleRecordsHaveValidCrc (*reader, tapeFile));

        // Poll until all first-batch records are visible (or timeout).
        // pollForCount enforces non-decreasing count on every refresh iteration.
        const std::uint64_t countAfterFirstBatch =
            pollForCount (*reader,
                          static_cast<std::uint64_t> (kFirstBatch),
                          kPollTimeoutMs);
        REQUIRE (countAfterFirstBatch == static_cast<std::uint64_t> (kFirstBatch));
        // Monotonic from open: first-batch count must not precede first-open count.
        REQUIRE (countAfterFirstBatch >= countAfterFirstOpen);
        REQUIRE (allVisibleRecordsHaveValidCrc (*reader, tapeFile));

        // --- Second batch ---
        {
            std::vector<float> left (kBlockSize, 0.2f), right (kBlockSize, -0.2f);
            for (int i = 0; i < kMoreBatch; ++i)
                writer.deliverTapeBlock (tape, left.data(), right.data(), kBlockSize);
        }

        // Poll until we see all second-batch records too.
        // pollForCount enforces non-decreasing count on every refresh iteration.
        const std::uint64_t countAfterSecondBatch =
            pollForCount (*reader,
                          static_cast<std::uint64_t> (kTotalBlocks),
                          kPollTimeoutMs);

        // Full monotonic chain: open ≤ firstBatch ≤ secondBatch ≤ total.
        REQUIRE (countAfterSecondBatch >= countAfterFirstOpen);
        REQUIRE (countAfterSecondBatch >= countAfterFirstBatch);
        REQUIRE (countAfterSecondBatch <= static_cast<std::uint64_t> (kTotalBlocks));

        // Every visible record still has a valid CRC.
        REQUIRE (allVisibleRecordsHaveValidCrc (*reader, tapeFile));

        writer.closeTape (tape);
    } // writer dtor joins the worker: all records flushed and file closed

    // After the writer is fully gone, one last refresh must see ALL records.
    ida::TapeTruncationReport finalReport;
    auto finalReader = ida::TapeRecordReader::open (tapeFile, reg, finalReport, /*recover=*/false);
    REQUIRE (finalReader != nullptr);
    REQUIRE (finalReport.truncated == false);
    REQUIRE (finalReader->recordCount() == static_cast<std::uint64_t> (kTotalBlocks));
    REQUIRE (allVisibleRecordsHaveValidCrc (*finalReader, tapeFile));
}

// (b) Deterministic partial tail: open(recover=false) on a file whose trailing
//     bytes are a dangling bodyLen prefix (no body/CRC yet) returns only the
//     complete prefix, truncated==false, and does NOT change the file size.
TEST_CASE("TapeRecordReader recover=false: dangling bodyLen prefix is invisible and file unchanged",
          "[tape-record]")
{
    static constexpr int kN = 4;

    const juce::File dir = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("ida-test-partial-tail-open");
    dir.deleteRecursively();
    dir.createDirectory();

    std::vector<std::uint64_t> recordOffsets;
    const juce::File tapeFile = buildNRecordFile (dir, kN, recordOffsets);
    REQUIRE (static_cast<int> (recordOffsets.size()) == kN);

    // Append only a 4-byte bodyLen prefix (claims 5000 bytes of body, none present).
    // This simulates a writer that has written the length field but not yet flushed body+CRC.
    static constexpr std::uint32_t kDanglingBodyLen = 5000;
    {
        juce::FileOutputStream fos (tapeFile);
        REQUIRE (fos.openedOk());
        fos.setPosition (fos.getFile().getSize());
        std::byte lenBuf[4];
        ida::writeLE32 (lenBuf, kDanglingBodyLen);
        fos.write (lenBuf, 4);
    }

    const std::uint64_t sizeWithDanglingPrefix =
        static_cast<std::uint64_t> (tapeFile.getSize());

    auto reg = makeFullRegistry();
    ida::TapeTruncationReport report;
    auto reader = ida::TapeRecordReader::open (tapeFile, reg, report, /*recover=*/false);

    REQUIRE (reader != nullptr);
    // Only the N complete records are visible — the dangling prefix is not.
    REQUIRE (reader->recordCount() == static_cast<std::uint64_t> (kN));
    // recover=false must never claim truncation.
    REQUIRE (report.truncated == false);
    // File must be byte-for-byte unchanged — no writes occurred.
    REQUIRE (static_cast<std::uint64_t> (tapeFile.getSize()) == sizeWithDanglingPrefix);

    // Exact size check: clean-end == last record's (fileOffset + 4 bodyLen prefix
    // + bodyLen body + 4 CRC); we appended exactly 4 dangling bytes after that.
    {
        const auto& lastEntry = reader->index().back();
        const std::uint64_t cleanEndSize =
            lastEntry.fileOffset
            + 4u                             // bodyLen prefix
            + static_cast<std::uint64_t> (lastEntry.bodyLen)
            + 4u;                            // CRC suffix
        REQUIRE (sizeWithDanglingPrefix == cleanEndSize + 4u);
    }

    // All visible records must have valid CRCs.
    REQUIRE (allVisibleRecordsHaveValidCrc (*reader, tapeFile));
}
