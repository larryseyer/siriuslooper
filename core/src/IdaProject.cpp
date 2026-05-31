#include "ida/IdaProject.h"

#include <cctype>

namespace ida
{

namespace
{
    constexpr const char* kFallbackName = "untitled";

    bool isSafeNameChar (unsigned char c) noexcept
    {
        // Lowercased ASCII letter or digit. Everything else is replaced.
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }
}

std::string IdaProject::sanitizeName (const std::string& displayName)
{
    std::string out;
    out.reserve (displayName.size());

    bool pendingUnderscore = false;
    for (const char rawCh : displayName)
    {
        const auto lowered = static_cast<unsigned char> (
            std::tolower (static_cast<unsigned char> (rawCh)));

        if (isSafeNameChar (lowered))
        {
            // Emit a single collapsed '_' for any preceding run of illegal chars,
            // but never as a leading character.
            if (pendingUnderscore && ! out.empty())
                out.push_back ('_');
            pendingUnderscore = false;
            out.push_back (static_cast<char> (lowered));
        }
        else
        {
            pendingUnderscore = true; // defer — collapses runs and trims trailing
        }
    }

    if (out.empty())
        return kFallbackName;
    return out;
}

IdaProject IdaProject::create (std::string displayName, std::string timestampDigits)
{
    auto folderId = timestampDigits + "-" + sanitizeName (displayName);
    return IdaProject (std::move (folderId), std::move (displayName), std::move (timestampDigits));
}

IdaProject::IdaProject (std::string folderId, std::string displayName, std::string createdTimestamp)
    : folderId_ (std::move (folderId)),
      displayName_ (std::move (displayName)),
      createdTimestamp_ (std::move (createdTimestamp))
{
}

void IdaProject::setDisplayName (std::string displayName)
{
    displayName_ = std::move (displayName); // folderId_ deliberately untouched (§2.2)
}

} // namespace ida
