#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ida
{

/// Maximum bytes available for an `OutOfProcessPluginInstance` instance id
/// before the macOS shm name budget is exceeded. The shm prefix is
/// `/ida.` (8 chars) and the longest suffix is `.e2h` / `.h2e` (4 chars);
/// macOS caps shm names at 31 chars total → 19 chars for the id itself.
/// We leave one char headroom for any future suffix growth → 18.
inline constexpr std::size_t kMaxPluginInstanceIdLength = 18;

/// Builds a stable, short instance id from an arbitrary input string,
/// hashing if (and only if) the input exceeds `kMaxPluginInstanceIdLength`.
/// Short inputs pass through verbatim so log lines, error messages, and
/// tests can read the id directly. Longer inputs are folded through
/// FNV-1a-64 and rendered as a 16-char zero-padded hex string — short
/// enough to fit the budget, wide enough that collisions are negligible
/// for any realistic plug-in slot population.
///
/// Lives in `core/` because the `OutOfProcessEffectChainHost` minting site
/// (`host/`) plus any future engine-side consumer should share one helper
/// rather than each open-coding the budget arithmetic.
inline std::string hashInstanceId (const std::string& input)
{
    if (input.size() <= kMaxPluginInstanceIdLength)
        return input;

    // FNV-1a 64-bit — well-distributed, allocation-free, no dependencies.
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime       = 0x100000001b3ULL;

    std::uint64_t hash = kFnvOffsetBasis;
    for (char c : input)
    {
        hash ^= static_cast<std::uint64_t> (static_cast<unsigned char> (c));
        hash *= kFnvPrime;
    }

    // Render as 16-char zero-padded lowercase hex. The static buffer +
    // explicit loop avoids `<iomanip>` and any locale-sensitive paths;
    // 16 chars sits exactly at the budget headroom we picked above.
    char buffer[17] {}; // 16 hex chars + NUL
    constexpr char kHex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i)
    {
        buffer[i] = kHex[hash & 0xF];
        hash >>= 4;
    }
    return std::string (buffer, 16);
}

} // namespace ida
