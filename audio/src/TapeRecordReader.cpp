#include "ida/TapeRecordReader.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace ida {

namespace {

// Read exactly `n` bytes from `fis` starting at byte position `offset`.
// Returns false if the file is too short or the read is incomplete.
bool readBytesAt (juce::FileInputStream& fis,
                  std::uint64_t          offset,
                  void*                  dst,
                  std::uint64_t          n)
{
    if (! fis.setPosition (static_cast<juce::int64> (offset)))
        return false;
    const int read = fis.read (dst, static_cast<int> (n));
    return static_cast<std::uint64_t> (read) == n;
}

} // namespace

std::unique_ptr<TapeRecordReader> TapeRecordReader::open (
    const juce::File&     file,
    TapeCodecRegistry&    registry,
    TapeTruncationReport& reportOut,
    bool                  recover)
{
    juce::FileInputStream fis (file);
    if (! fis.openedOk())
        return nullptr;

    // Validate the 12-byte file header.
    std::vector<std::byte> headerBuf (kTapeFileHeaderBytes);
    if (fis.read (headerBuf.data(), static_cast<int> (kTapeFileHeaderBytes))
            != static_cast<int> (kTapeFileHeaderBytes))
        return nullptr;

    std::uint16_t version = 0;
    if (! readFileHeader (headerBuf.data(), kTapeFileHeaderBytes, version))
        return nullptr;

    auto reader = std::unique_ptr<TapeRecordReader> (new TapeRecordReader());
    reader->file_      = file;
    reader->registry_  = &registry;
    reader->scannedTo_ = static_cast<std::uint64_t> (kTapeFileHeaderBytes);

    reader->scanFrom (static_cast<std::uint64_t> (kTapeFileHeaderBytes),
                      recover, reportOut);

    return reader;
}

void TapeRecordReader::scanFrom (std::uint64_t         startOffset,
                                  bool                  recover,
                                  TapeTruncationReport& reportOut)
{
    (void) recover; // T6 will use this to truncate bad trailing records

    juce::FileInputStream fis (file_);
    if (! fis.openedOk())
        return;

    const auto fileLen = static_cast<std::uint64_t> (fis.getTotalLength());

    std::uint64_t offset = startOffset;

    while (offset + 4 <= fileLen)
    {
        const std::uint64_t recordStart = offset;

        // Read the 4-byte bodyLen prefix.
        std::byte lenBuf[4];
        if (! readBytesAt (fis, offset, lenBuf, 4))
            break;
        const std::uint32_t bodyLen = readLE32 (lenBuf);
        offset += 4;

        // Check that body + trailing CRC fit in the file.
        if (offset + static_cast<std::uint64_t> (bodyLen) + 4 > fileLen)
            break; // partial trailing record — stop (T6 handles truncation)

        // Read body.
        std::vector<std::byte> body (bodyLen);
        if (! readBytesAt (fis, offset, body.data(), bodyLen))
            break;

        // Read trailing CRC.
        std::byte crcBuf[4];
        if (! readBytesAt (fis, offset + bodyLen, crcBuf, 4))
            break;
        const std::uint32_t storedCrc = readLE32 (crcBuf);

        // Verify CRC.
        if (crc32 (body.data(), bodyLen) != storedCrc)
            break; // corrupted record — stop

        // Decode the header fields.
        TapeRecordHeader    hdr;
        const std::byte*    payloadPtr = nullptr;
        std::size_t         payloadLen = 0;
        if (! decodeRecordBody (body.data(), bodyLen, hdr, payloadPtr, payloadLen))
            break;

        RecordIndexEntry entry;
        entry.seq        = hdr.seq;
        entry.fileOffset = recordStart;
        entry.bodyLen    = bodyLen;
        entry.type       = hdr.type;
        entry.codec      = hdr.codec;
        entry.lmcTs      = hdr.lmcTs;
        index_.push_back (entry);

        offset += static_cast<std::uint64_t> (bodyLen) + 4;
    }

    scannedTo_ = offset;
    reportOut.truncated = false;
    reportOut.recordsKept = static_cast<std::uint64_t> (index_.size());
    reportOut.truncatedAtOffset = 0;
    reportOut.reason = "";
}

std::uint64_t TapeRecordReader::recordCount() const noexcept
{
    return static_cast<std::uint64_t> (index_.size());
}

const std::vector<RecordIndexEntry>& TapeRecordReader::index() const noexcept
{
    return index_;
}

bool TapeRecordReader::readAudioRecord (std::uint64_t     position,
                                        PcmBlock&         out,
                                        TapeRecordHeader& hOut) const
{
    if (position >= index_.size())
        return false;

    const RecordIndexEntry& entry = index_[static_cast<std::size_t> (position)];

    IPayloadCodec* codec = registry_->codecFor (entry.codec);
    if (codec == nullptr)
        return false;

    juce::FileInputStream fis (file_);
    if (! fis.openedOk())
        return false;

    // Seek to the record's bodyLen prefix and skip it, then read the body.
    const std::uint64_t bodyOffset = entry.fileOffset + 4;
    std::vector<std::byte> body (entry.bodyLen);
    if (! readBytesAt (fis, bodyOffset, body.data(), entry.bodyLen))
        return false;

    TapeRecordHeader    hdr;
    const std::byte*    payloadPtr = nullptr;
    std::size_t         payloadLen = 0;
    if (! decodeRecordBody (body.data(), entry.bodyLen, hdr, payloadPtr, payloadLen))
        return false;

    if (! codec->decode (payloadPtr, payloadLen, out))
        return false;

    hOut = hdr;
    return true;
}

void TapeRecordReader::refresh (TapeTruncationReport& reportOut)
{
    // Re-scan from the last successfully scanned byte offset, appending any
    // newly-written complete records to the index.
    scanFrom (scannedTo_, /*recover=*/false, reportOut);
}

} // namespace ida
