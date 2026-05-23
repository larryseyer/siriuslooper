#include "ida/CalibrationStore.h"

#include "ida/Sha256.h"

#include <charconv>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ida
{
namespace
{
    constexpr std::string_view kRateFactorPrefix = "rateFactor=";
    constexpr std::string_view kOffsetPrefix     = "offset=";
    constexpr std::string_view kPendingPrefix    = "pending=";
    constexpr std::string_view kSha256Prefix     = "sha256=";

    std::string hexOf (std::string_view payload)
    {
        return sha256Hex ({ reinterpret_cast<const std::byte*> (payload.data()),
                            payload.size() });
    }

    std::string rationalLine (std::string_view prefix, Rational r)
    {
        return std::string (prefix) + std::to_string (r.numerator())
             + "/" + std::to_string (r.denominator()) + "\n";
    }

    bool isBlank (std::string_view s)
    {
        for (char c : s)
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                return false;
        return true;
    }

    /// Parse an int64 consuming the entire view; nullopt on any junk.
    std::optional<std::int64_t> parseInt (std::string_view v)
    {
        std::int64_t out = 0;
        const char* begin = v.data();
        const char* end   = v.data() + v.size();
        const auto [ptr, ec] = std::from_chars (begin, end, out);
        if (ec != std::errc {} || ptr != end)
            return std::nullopt;
        return out;
    }

    /// Parse "num/den" into a Rational; nullopt on malformed input or a zero
    /// denominator (the Rational ctor would throw).
    std::optional<Rational> parseRational (std::string_view v)
    {
        const auto slash = v.find ('/');
        if (slash == std::string_view::npos)
            return std::nullopt;

        const auto num = parseInt (v.substr (0, slash));
        const auto den = parseInt (v.substr (slash + 1));
        if (! num || ! den || *den == 0)
            return std::nullopt;

        try
        {
            return Rational (*num, *den);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    CalibrationParseResult recovered (CalibrationLoadStatus status)
    {
        return { status, { AudioDeviceCalibration::identity(), /*pending*/ true } };
    }

    /// Strip a single trailing '\r' (tolerate CRLF sidecars).
    std::string_view stripCr (std::string_view line)
    {
        if (! line.empty() && line.back() == '\r')
            line.remove_suffix (1);
        return line;
    }
} // namespace

std::string serializeCalibration (const CalibrationDocument& doc)
{
    std::string payload;
    payload += rationalLine (kRateFactorPrefix, doc.calibration.rateFactor());
    payload += rationalLine (kOffsetPrefix,     doc.calibration.offsetSeconds());
    payload += std::string (kPendingPrefix) + (doc.recalibrationPending ? "1" : "0") + "\n";

    return payload + std::string (kSha256Prefix) + hexOf (payload) + "\n";
}

CalibrationParseResult parseAndValidateCalibration (const std::string& fileContents)
{
    if (isBlank (fileContents))
        return recovered (CalibrationLoadStatus::Missing);

    // Split into the (up to) four lines we expect, preserving exact text so the
    // reconstructed payload hashes identically to what serializeCalibration did.
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= fileContents.size())
    {
        const auto nl = fileContents.find ('\n', start);
        if (nl == std::string::npos)
        {
            if (start < fileContents.size())
                lines.push_back (std::string_view (fileContents).substr (start));
            break;
        }
        lines.push_back (std::string_view (fileContents).substr (start, nl - start));
        start = nl + 1;
    }

    if (lines.size() < 4)
        return recovered (CalibrationLoadStatus::Corrupt);

    const auto rateLine    = stripCr (lines[0]);
    const auto offsetLine  = stripCr (lines[1]);
    const auto pendingLine = stripCr (lines[2]);
    const auto shaLine     = stripCr (lines[3]);

    if (rateLine.substr (0, kRateFactorPrefix.size()) != kRateFactorPrefix
        || offsetLine.substr (0, kOffsetPrefix.size()) != kOffsetPrefix
        || pendingLine.substr (0, kPendingPrefix.size()) != kPendingPrefix
        || shaLine.substr (0, kSha256Prefix.size()) != kSha256Prefix)
        return recovered (CalibrationLoadStatus::Corrupt);

    const auto rate   = parseRational (rateLine.substr (kRateFactorPrefix.size()));
    const auto offset = parseRational (offsetLine.substr (kOffsetPrefix.size()));
    if (! rate || ! offset)
        return recovered (CalibrationLoadStatus::Corrupt);

    const auto pendingValue = pendingLine.substr (kPendingPrefix.size());
    if (pendingValue != "0" && pendingValue != "1")
        return recovered (CalibrationLoadStatus::Corrupt);

    // Verify the checksum over the exact payload bytes (the three lines, each
    // with its newline) before trusting the values.
    const std::string payload = std::string (rateLine) + "\n"
                              + std::string (offsetLine) + "\n"
                              + std::string (pendingLine) + "\n";
    if (shaLine.substr (kSha256Prefix.size()) != hexOf (payload))
        return recovered (CalibrationLoadStatus::Corrupt);

    try
    {
        CalibrationDocument doc;
        doc.calibration = AudioDeviceCalibration (*rate, *offset); // throws if rate <= 0
        doc.recalibrationPending = (pendingValue == "1");
        return { CalibrationLoadStatus::Ok, doc };
    }
    catch (const std::exception&)
    {
        return recovered (CalibrationLoadStatus::Corrupt);
    }
}

void postCalibrationRecoveryNotification (CalibrationLoadStatus status,
                                          INotificationSink&    sink)
{
    if (status == CalibrationLoadStatus::Ok)
        return;

    const char* message = (status == CalibrationLoadStatus::Corrupt)
        ? "calibration corrupt — reset to identity, recalibration pending"
        : "calibration missing — using identity, recalibration pending";

    sink.post (NotificationLevel::Warning, Category::StateRepair, message);
}

} // namespace ida
