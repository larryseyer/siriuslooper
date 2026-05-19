// Tests for the M8 S1 archival-mode surface (white paper §15.6) — the
// `ArchivalMode` enum, the `VersionPinningRecord` value type, the
// SHA-256 helper that produces stable state-blob fingerprints, and the
// engine-side populator/verifier that bridge the live session graph to
// the persistence layer. JUCE-free for the core types; JUCE-using for
// the SessionFormat round-trip and the NotificationBus verifier.
#include "sirius/Sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>

using sirius::sha256Hex;

TEST_CASE ("sha256Hex of zero bytes returns the canonical empty-input digest",
           "[sha256]")
{
    // RFC 6234 §A: SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.
    // Pinned here because VersionPinningRecord defaults to an empty state
    // blob in M8 S1 — any regression that "hashes the descriptor" instead
    // of the state blob would silently flip every snapshot's fingerprint
    // and produce spurious drift events on every reopen.
    const std::array<std::byte, 0> empty {};
    CHECK (sha256Hex (std::span<const std::byte> (empty.data(), empty.size()))
           == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE ("sha256Hex matches the canonical \"abc\" test vector",
           "[sha256]")
{
    // RFC 6234 §A: SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad.
    // Catches any byte-order / padding / message-length bug in the
    // implementation — the single-block, single-padding-byte case.
    const char* text = "abc";
    const auto* bytes = reinterpret_cast<const std::byte*> (text);
    CHECK (sha256Hex (std::span<const std::byte> (bytes, 3))
           == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
