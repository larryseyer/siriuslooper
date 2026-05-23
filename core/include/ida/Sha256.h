#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace ida
{

/// SHA-256 of the given byte span, returned as a 64-character lowercase
/// hex string. Pure C++; intentionally not a `juce::SHA256` wrapper
/// because `core/` is JUCE-free (see core/CMakeLists.txt). Used by
/// `VersionPinningRecord` to produce a stable state-blob fingerprint
/// that round-trips through the session format and survives
/// machine-to-machine reopen.
///
/// RFC 6234 conformant. Empty input returns the well-defined
/// `e3b0c4...b855` digest. Allocation-free apart from the returned
/// `std::string`. Not thread-safe per call site, but reentrant — each
/// invocation owns its own stack-resident hashing state.
std::string sha256Hex (std::span<const std::byte> data);

} // namespace ida
