#pragma once

#include "ida/AudioDeviceCalibration.h"
#include "ida/INotificationSink.h"

#include <string>

namespace ida
{

/// Result of validating a persisted calibration sidecar (M8 S6).
///   Ok      : present and the checksum matches.
///   Missing : the sidecar is absent / empty.
///   Corrupt : present but malformed, out-of-range, or checksum-mismatched.
enum class CalibrationLoadStatus { Ok, Missing, Corrupt };

/// The persisted calibration plus a recovery marker. The calibration is the
/// per-device clock model (white paper Part 4.3); `recalibrationPending` is
/// store metadata — NOT a field on the immutable `AudioDeviceCalibration` value
/// — set true when the future loopback engine still owes a real measurement.
struct CalibrationDocument
{
    AudioDeviceCalibration calibration { AudioDeviceCalibration::identity() };
    bool recalibrationPending { false };
};

/// Serialize to the canonical sidecar text (a payload of rateFactor/offset/
/// pending lines, followed by a `sha256=` line over the payload bytes). Pure.
std::string serializeCalibration (const CalibrationDocument& doc);

/// Outcome of `parseAndValidateCalibration`. On any non-Ok status the document
/// is `{ identity, recalibrationPending = true }` so the caller can recover and
/// re-persist directly.
struct CalibrationParseResult
{
    CalibrationLoadStatus status;
    CalibrationDocument   document;
};

/// Parse and validate sidecar text. Empty/whitespace input -> Missing. Malformed
/// lines, an unparseable or non-positive rate factor, a zero denominator, or a
/// checksum mismatch -> Corrupt. Pure — no disk, no notification sink.
CalibrationParseResult parseAndValidateCalibration (const std::string& fileContents);

/// No-op on Ok. On Missing/Corrupt posts exactly one Warning / StateRepair
/// naming the cause and the recovery, within the 128-byte notification cap.
/// Kept out of the parse so the parse stays sink-free and unit-testable —
/// parallel to `postConstituentStateNotifications` (M8 S3).
void postCalibrationRecoveryNotification (CalibrationLoadStatus status,
                                          INotificationSink&    sink);

} // namespace ida
