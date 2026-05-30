#pragma once

#include "ida/IPayloadCodec.h"
#include "ida/TapeRecord.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ida {

struct RecordIndexEntry {
    std::uint64_t  seq;
    std::uint64_t  fileOffset;   // byte offset of the u32 bodyLen prefix
    std::uint32_t  bodyLen;
    TapeRecordType type;
    TapeCodecId    codec;
    Rational       lmcTs;
};

struct TapeTruncationReport {
    bool          truncated            { false };
    std::uint64_t recordsKept          { 0 };
    std::uint64_t truncatedAtOffset    { 0 };
    std::string   reason;              // "" when not truncated
};

class TapeRecordReader {
public:
    // recover=true  — exclusive crash-recovery open: truncates a bad/partial
    //                  trailing record on disk.
    // recover=false — read-only live open: stops at the last complete record,
    //                  never writes (concurrent read).
    static std::unique_ptr<TapeRecordReader> open (const juce::File& file,
                                                   TapeCodecRegistry& registry,
                                                   TapeTruncationReport& reportOut,
                                                   bool recover = true);

    std::uint64_t recordCount() const noexcept;
    const std::vector<RecordIndexEntry>& index() const noexcept;

    // Random access by 0-based record position: decode payload via the
    // record's registered codec. Does NOT decode predecessor records.
    bool readAudioRecord (std::uint64_t position,
                          PcmBlock& out,
                          TapeRecordHeader& hOut) const;

    // Live re-scan: pick up records appended since the last open/refresh.
    // Safe for recover=false readers polling a live file.
    void refresh (TapeTruncationReport& reportOut);

private:
    TapeRecordReader() = default;   // I3: only open() constructs via private ctor

    juce::File          file_;
    TapeCodecRegistry*  registry_  { nullptr };
    std::vector<RecordIndexEntry> index_;
    std::uint64_t       scannedTo_ { 0 };   // byte offset scanned up to

    // Scan records from startOffset, appending valid entries to index_.
    // On a clean file both recover=true and recover=false behave identically.
    // When recover=true and a bad/partial record is found, truncates the file.
    void scanFrom (std::uint64_t startOffset,
                   bool          recover,
                   TapeTruncationReport& reportOut);

    // Attempt to read and index one record at `offset` from `fis`.
    // Advances `offset` past the record on success.
    // On a bad/partial record, sets `truncOffsetOut` and `reasonOut`, resets
    // `offset` to `recordStart`, and returns false.
    // Returns false on clean EOF (reasonOut remains empty) or I/O error.
    bool tryReadRecord (juce::FileInputStream& fis,
                        std::uint64_t          fileLen,
                        std::uint64_t&         offset,
                        std::uint64_t&         truncOffsetOut,
                        std::string&           reasonOut);
};

} // namespace ida
