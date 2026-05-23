#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sirius
{

/// The four built-in FX shipped by IDA (white paper §6.6 + the contract in
/// `docs/design/ida-internal-fx.md`). Each id resolves at T3 to one of OTTO's
/// header-only Player FX via a Sirius-side adapter. The underlying type is
/// `uint8_t` with a reserved range up to 16 so a future built-in (e.g. saturator,
/// transient shaper) can land without growing the discriminant.
///
/// These values are wire-stable — they ride inside session JSON as strings
/// (`internalFxIdToString` is the serialization, `internalFxIdFromString` the
/// deserialization). Renaming a value breaks every saved session that holds an
/// Internal slot with that id.
enum class InternalFxId : std::uint8_t
{
    kEq  = 0,
    kCmp = 1,
    kRvb = 2,
    kDly = 3,
    // reserved up to 15 for future built-ins
};

/// Wire-stable string form. Used by SessionFormat and by any other code that
/// needs to serialize / log an Internal id. The strings are the public ones
/// the operator sees on insert pickers (UI lookup is allowed to render a
/// friendlier label, but the canonical id stays).
inline const char* internalFxIdToString (InternalFxId id) noexcept
{
    switch (id)
    {
        case InternalFxId::kEq:  return "EQ";
        case InternalFxId::kCmp: return "CMP";
        case InternalFxId::kRvb: return "RVB";
        case InternalFxId::kDly: return "DLY";
    }
    return ""; // unreachable; noexcept prevents throw
}

inline InternalFxId internalFxIdFromString (std::string_view s)
{
    if (s == "EQ")  return InternalFxId::kEq;
    if (s == "CMP") return InternalFxId::kCmp;
    if (s == "RVB") return InternalFxId::kRvb;
    if (s == "DLY") return InternalFxId::kDly;
    throw std::invalid_argument ("ida::internalFxIdFromString: unknown id \"" + std::string (s) + "\"");
}

} // namespace sirius
