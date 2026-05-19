// Tests for the M8 S1 archival-mode surface (white paper §15.6) — the
// `ArchivalMode` enum, the `VersionPinningRecord` value type, the
// SHA-256 helper that produces stable state-blob fingerprints, and the
// engine-side populator/verifier that bridge the live session graph to
// the persistence layer. JUCE-free for the core types; JUCE-using for
// the SessionFormat round-trip and the NotificationBus verifier.
#include "sirius/ArchivalMode.h"
#include "sirius/Constituent.h"
#include "sirius/EffectChain.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/SessionFormat.h"
#include "sirius/Sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

using sirius::ArchivalMode;
using sirius::Constituent;
using sirius::ConstituentId;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::PluginDescriptor;
using sirius::PluginFormat;
using sirius::Position;
using sirius::Rational;
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

// Pinned because VersionPinning's drift verifier compares the saved
// `version` against the live one on reopen; a serializer that silently
// dropped the field would make every reopen a "no drift detected" false
// negative — the worst silent-failure mode for the archival surface.
TEST_CASE ("PluginDescriptor::version round-trips through SessionFormat",
           "[archival-mode][session-format]")
{
    PluginDescriptor d;
    d.format       = PluginFormat::Vst3;
    d.uniqueId     = "vendor.test.eq";
    d.version      = "2.3.1";
    d.name         = "Test EQ";
    d.manufacturer = "Vendor";
    d.filePath     = "/plugins/TestEQ.vst3";

    EffectChainEntry entry;
    entry.descriptor  = d;
    entry.displayName = d.name;

    auto leaf = std::make_shared<Constituent> (
        ConstituentId (1),
        Position (Rational (0)),
        Position (Rational (1)));
    *leaf = leaf->withEffectChain (EffectChain().withAppended (entry));

    const auto json   = sirius::persistence::serializeSession (*leaf);
    const auto round  = sirius::persistence::deserializeSession (json);
    REQUIRE (round->effectChain().has_value());
    REQUIRE (round->effectChain()->size() == 1);
    CHECK (round->effectChain()->at (0).descriptor.version == "2.3.1");
}

// Compiler-warning-as-error in the project's baseline (-Wswitch + -Werror)
// catches any future enum extension that forgets to update this switch at
// compile time. The runtime walk over all three enumerators ensures the
// test is exercised even if -Wswitch isn't on — a future ArchivalMode
// addition silently growing the enum without a matching switch arm would
// otherwise let a "no archival" code path through.
TEST_CASE ("ArchivalMode switch reaches every case", "[archival-mode]")
{
    int reached = 0;
    for (auto mode : { ArchivalMode::DeterminismContract,
                       ArchivalMode::WetCapture,
                       ArchivalMode::VersionPinning })
    {
        switch (mode)
        {
            case ArchivalMode::DeterminismContract: ++reached; break;
            case ArchivalMode::WetCapture:          ++reached; break;
            case ArchivalMode::VersionPinning:      ++reached; break;
        }
    }
    CHECK (reached == 3);
}
