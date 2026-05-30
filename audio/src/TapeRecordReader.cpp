#include "ida/TapeRecordReader.h"

#include <juce_core/juce_core.h>

#include <limits>
#include <string>
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
    jassert (n <= static_cast<std::uint64_t> (std::numeric_limits<int>::max()));
    const int read = fis.read (dst, static_cast<int> (n));
    return static_cast<std::uint64_t> (read) == n;
}

// Truncate `file` to `offset` bytes and fill `reportOut` honestly.
// Called only when recover==true after the input stream has been destroyed.
// reportOut.truncated is set to true ONLY when bytes are actually removed from disk.
void truncateAt (const juce::File&     file,
                 std::uint64_t         offset,
                 std::uint64_t         recordsKept,
                 const std::string&    reason,
                 TapeTruncationReport& reportOut)
{
    juce::FileOutputStream fos (file);
    if (! fos.openedOk())
    {
        reportOut.truncated         = false;
        reportOut.recordsKept       = recordsKept;
        reportOut.truncatedAtOffset = 0;
        reportOut.reason            = "truncation failed: could not open file for writing";
        return;
    }

    fos.setPosition (static_cast<juce::int64> (offset));
    const juce::Result result = fos.truncate();
    if (result.failed())
    {
        reportOut.truncated         = false;
        reportOut.recordsKept       = recordsKept;
        reportOut.truncatedAtOffset = 0;
        reportOut.reason            = "truncation failed: " + result.getErrorMessage().toStdString();
        return;
    }

    // Successful open + setPosition + truncate: bytes were actually removed.
    // juce::FileOutputStream::truncate() already flushes/syncs; no extra flush needed.
    reportOut.truncated         = true;
    reportOut.recordsKept       = recordsKept;
    reportOut.truncatedAtOffset = offset;
    reportOut.reason            = reason;
}

} // namespace

std::unique_ptr<TapeRecordReader> TapeRecordReader::open (
    const juce::File&     file,
    TapeCodecRegistry&    registry,
    TapeTruncationReport& reportOut,
    bool                  recover)
{
    // Validate the 12-byte file header in its own scope so the stream is
    // closed before scanFrom (which opens its own stream and may truncate).
    {
        juce::FileInputStream fis (file);
        if (! fis.openedOk())
            return nullptr;

        std::vector<std::byte> headerBuf (kTapeFileHeaderBytes);
        if (fis.read (headerBuf.data(), static_cast<int> (kTapeFileHeaderBytes))
                != static_cast<int> (kTapeFileHeaderBytes))
            return nullptr;

        std::uint16_t version = 0;
        if (! readFileHeader (headerBuf.data(), kTapeFileHeaderBytes, version))
            return nullptr;
    } // fis destroyed here — no open handle during scanFrom

    auto reader = std::unique_ptr<TapeRecordReader> (new TapeRecordReader());
    reader->file_      = file;
    reader->registry_  = &registry;
    reader->scannedTo_ = static_cast<std::uint64_t> (kTapeFileHeaderBytes);

    reader->scanFrom (static_cast<std::uint64_t> (kTapeFileHeaderBytes),
                      recover, reportOut);

    return reader;
}

// Scan one record starting at `offset` from `fis`. Returns true if a complete
// valid record was decoded and appended to `index_`. Returns false on three
// distinct failure modes:
//   1. Clean EOF (offset+4 > fileLen): reasonOut is empty, truncOffsetOut unchanged.
//   2. Partial/CRC-bad/malformed-body: reasonOut is non-empty, truncOffsetOut set
//      to recordStart. Caller should truncate when recover==true.
//   3. I/O short-read: reasonOut is empty, truncOffsetOut unchanged (treat as EOF).
bool TapeRecordReader::tryReadRecord (juce::FileInputStream& fis,
                                       std::uint64_t          fileLen,
                                       std::uint64_t&         offset,
                                       std::uint64_t&         truncOffsetOut,
                                       std::string&           reasonOut)
{
    if (offset + 4 > fileLen)
        return false; // clean EOF

    const std::uint64_t recordStart = offset;

    std::byte lenBuf[4];
    if (! readBytesAt (fis, offset, lenBuf, 4))
        return false;
    const std::uint32_t bodyLen = readLE32 (lenBuf);
    offset += 4;

    if (offset + static_cast<std::uint64_t> (bodyLen) + 4 > fileLen)
    {
        truncOffsetOut = recordStart;
        reasonOut      = "partial trailing record";
        offset         = recordStart;
        return false;
    }

    std::vector<std::byte> body (bodyLen);
    if (! readBytesAt (fis, offset, body.data(), bodyLen))
        return false;

    std::byte crcBuf[4];
    if (! readBytesAt (fis, offset + bodyLen, crcBuf, 4))
        return false;

    if (crc32 (body.data(), bodyLen) != readLE32 (crcBuf))
    {
        truncOffsetOut = recordStart;
        reasonOut      = "crc mismatch at record " + std::to_string (index_.size());
        offset         = recordStart;
        return false;
    }

    TapeRecordHeader hdr;
    const std::byte* payloadPtr = nullptr;
    std::size_t      payloadLen = 0;
    if (! decodeRecordBody (body.data(), bodyLen, hdr, payloadPtr, payloadLen))
    {
        // CRC matched but body is structurally invalid (bodyLen < kRecordHeaderBytes).
        // This is not clean EOF — set offset and reason so caller can truncate.
        truncOffsetOut = recordStart;
        reasonOut      = "malformed record body";
        offset         = recordStart;
        return false;
    }

    RecordIndexEntry entry;
    entry.seq        = hdr.seq;
    entry.fileOffset = recordStart;
    entry.bodyLen    = bodyLen;
    entry.type       = hdr.type;
    entry.codec      = hdr.codec;
    entry.lmcTs      = hdr.lmcTs;
    index_.push_back (entry);

    offset += static_cast<std::uint64_t> (bodyLen) + 4;
    return true;
}

void TapeRecordReader::scanFrom (std::uint64_t         startOffset,
                                  bool                  recover,
                                  TapeTruncationReport& reportOut)
{
    std::uint64_t truncOffset = 0;
    std::string   truncReason;
    bool          needsTrunc  = false;

    // The input stream is scoped so it is destroyed before any truncation.
    // (Two open handles on the same file fight on Windows.)
    {
        juce::FileInputStream fis (file_);
        if (! fis.openedOk())
            return;

        const juce::int64 rawLen = fis.getTotalLength();
        if (rawLen < 0)
            return;

        const auto    fileLen = static_cast<std::uint64_t> (rawLen);
        std::uint64_t offset  = startOffset;

        while (offset + 4 <= fileLen)
        {
            const std::uint64_t prevOffset = offset;
            if (! tryReadRecord (fis, fileLen, offset, truncOffset, truncReason))
            {
                if (recover && ! truncReason.empty())
                    needsTrunc = true;
                scannedTo_ = prevOffset; // last good position
                break;
            }
        }

        if (! needsTrunc)
            scannedTo_ = offset;
    } // fis destroyed here — safe to open FileOutputStream below

    if (needsTrunc)
    {
        truncateAt (file_, truncOffset,
                    static_cast<std::uint64_t> (index_.size()),
                    truncReason, reportOut);
        return;
    }

    reportOut.truncated         = false;
    reportOut.recordsKept       = static_cast<std::uint64_t> (index_.size());
    reportOut.truncatedAtOffset = 0;
    reportOut.reason            = "";
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
