#include "ida/CalibrationStore.h"
#include "ida/AudioDeviceCalibration.h"
#include "ida/INotificationSink.h"
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ida;

namespace
{
// Mirrors the recording-sink pattern from ConstituentStateTests.cpp.
struct RecordingSink : INotificationSink
{
    struct Entry { NotificationLevel level; Category category; std::string message; };
    std::vector<Entry> entries;

    bool post (NotificationLevel level, Category category,
               const char* message) noexcept override
    {
        entries.push_back ({ level, category, message ? message : "" });
        return true;
    }
};
} // namespace

TEST_CASE ("a non-identity calibration round-trips through serialize/parse", "[calibration-recovery]")
{
    const CalibrationDocument doc {
        AudioDeviceCalibration (Rational (999987, 1000000), Rational (1, 48000)),
        /*recalibrationPending*/ false
    };

    const auto result = parseAndValidateCalibration (serializeCalibration (doc));

    REQUIRE (result.status == CalibrationLoadStatus::Ok);
    CHECK (result.document.calibration.rateFactor()    == Rational (999987, 1000000));
    CHECK (result.document.calibration.offsetSeconds() == Rational (1, 48000));
    CHECK_FALSE (result.document.recalibrationPending);
}

TEST_CASE ("the recalibration-pending marker round-trips", "[calibration-recovery]")
{
    const CalibrationDocument doc { AudioDeviceCalibration::identity(), /*pending*/ true };

    const auto result = parseAndValidateCalibration (serializeCalibration (doc));

    REQUIRE (result.status == CalibrationLoadStatus::Ok);
    CHECK (result.document.recalibrationPending);
    CHECK (result.document.calibration.rateFactor()    == Rational (1));
    CHECK (result.document.calibration.offsetSeconds() == Rational (0));
}

TEST_CASE ("a tampered payload fails the checksum and reports Corrupt", "[calibration-recovery]")
{
    std::string text = serializeCalibration (
        { AudioDeviceCalibration (Rational (2), Rational (0)), false });

    // Mutate the rate factor in the payload without recomputing the sha256 line.
    const auto pos = text.find ("rateFactor=2/1");
    REQUIRE (pos != std::string::npos);
    text.replace (pos, std::string ("rateFactor=2/1").size(), "rateFactor=3/1");

    const auto result = parseAndValidateCalibration (text);

    REQUIRE (result.status == CalibrationLoadStatus::Corrupt);
    // Non-Ok always recovers to identity + pending.
    CHECK (result.document.calibration.rateFactor() == Rational (1));
    CHECK (result.document.recalibrationPending);
}

TEST_CASE ("a tampered checksum reports Corrupt", "[calibration-recovery]")
{
    std::string text = serializeCalibration (
        { AudioDeviceCalibration (Rational (1), Rational (0)), false });

    const auto pos = text.find ("sha256=");
    REQUIRE (pos != std::string::npos);
    // Flip the first hex character of the digest.
    const auto hexAt = pos + std::string ("sha256=").size();
    text[hexAt] = (text[hexAt] == '0') ? '1' : '0';

    CHECK (parseAndValidateCalibration (text).status == CalibrationLoadStatus::Corrupt);
}

TEST_CASE ("malformed sidecar contents report Corrupt", "[calibration-recovery]")
{
    // Missing the offset line.
    CHECK (parseAndValidateCalibration (
        "rateFactor=1/1\npending=0\nsha256=deadbeef\n").status
        == CalibrationLoadStatus::Corrupt);

    // Non-numeric rational.
    CHECK (parseAndValidateCalibration (
        "rateFactor=abc/1\noffset=0/1\npending=0\nsha256=deadbeef\n").status
        == CalibrationLoadStatus::Corrupt);

    // Zero denominator.
    CHECK (parseAndValidateCalibration (
        "rateFactor=1/0\noffset=0/1\npending=0\nsha256=deadbeef\n").status
        == CalibrationLoadStatus::Corrupt);

    // Non-positive rate factor (the AudioDeviceCalibration invariant).
    CHECK (parseAndValidateCalibration (
        "rateFactor=0/1\noffset=0/1\npending=0\nsha256=deadbeef\n").status
        == CalibrationLoadStatus::Corrupt);
}

TEST_CASE ("empty or whitespace-only sidecar reports Missing", "[calibration-recovery]")
{
    for (const std::string& empty : { std::string (""), std::string ("   \n  \t\n") })
    {
        const auto result = parseAndValidateCalibration (empty);
        CHECK (result.status == CalibrationLoadStatus::Missing);
        CHECK (result.document.calibration.rateFactor() == Rational (1));
        CHECK (result.document.recalibrationPending);
    }
}

TEST_CASE ("recovery notification posts a Warning only on a non-Ok status", "[calibration-recovery]")
{
    {
        RecordingSink sink;
        postCalibrationRecoveryNotification (CalibrationLoadStatus::Corrupt, sink);
        REQUIRE (sink.entries.size() == 1);
        CHECK (sink.entries[0].level == NotificationLevel::Warning);
        CHECK (sink.entries[0].category == Category::StateRepair);
        CHECK_FALSE (sink.entries[0].message.empty());
    }
    {
        RecordingSink sink;
        postCalibrationRecoveryNotification (CalibrationLoadStatus::Missing, sink);
        REQUIRE (sink.entries.size() == 1);
        CHECK (sink.entries[0].level == NotificationLevel::Warning);
        CHECK (sink.entries[0].category == Category::StateRepair);
    }
    {
        RecordingSink sink;
        postCalibrationRecoveryNotification (CalibrationLoadStatus::Ok, sink);
        CHECK (sink.entries.empty());
    }
}
