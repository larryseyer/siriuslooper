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
#include "sirius/VersionPinningRecord.h"

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
using sirius::makeVersionPinningRecord;
using sirius::PluginDescriptor;
using sirius::PluginFormat;
using sirius::Position;
using sirius::Rational;
using sirius::sha256Hex;
using sirius::VersionPinningRecord;

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

// The runtime walk over the three-value initializer list is the
// load-bearing check: if a future ArchivalMode enumerator lands in the
// header without a matching `case` arm added below, the new value never
// enters the for-range, `reached` never grows past 3, and this test
// stays green for the wrong reason — making the initializer list (which
// the PR author must remember to extend) the real enforcement boundary.
// A `-Wswitch` warning may also fire on the missing case under some
// toolchains, but the project does not currently set `-Wswitch` or
// `-Werror`, so do not rely on it.
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

// Fixtures used by multiple later cases; kept at the bottom of the file
// because Tasks 5/6 of M8 S1 append more cases and want their fixtures
// adjacent. Don't relocate to the top.
namespace
{
    PluginDescriptor descriptorFixture()
    {
        PluginDescriptor d;
        d.format       = PluginFormat::Clap;
        d.uniqueId     = "com.sirius.synthetic.test";
        d.version      = "1.0.0";
        d.name         = "Synthetic Test Plug-in";
        d.manufacturer = "Sirius";
        d.filePath     = "/fixtures/SyntheticTestPlugin.clap";
        return d;
    }
}

// Anchors the empty-state-blob baseline. M8 S1's state blob is always
// the empty byte sequence (no CLAP state integration until M8 S2); a
// regression that hashed "the descriptor" instead of "the state blob"
// would silently flip every snapshot's fingerprint and produce phantom
// drift events on every reopen. Pin the canonical RFC 6234 empty-input
// digest so any such regression fails this test loudly.
TEST_CASE ("makeVersionPinningRecord with empty state has the canonical empty hash",
           "[archival-mode][version-pinning]")
{
    const auto record = makeVersionPinningRecord (descriptorFixture(), {});
    CHECK (record.uniqueId == "com.sirius.synthetic.test");
    CHECK (record.version  == "1.0.0");
    CHECK (record.stateBlobSha256
           == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK (record.oversamplingRate == 1u);
    CHECK (record.declaredInternalStateHash.empty());
}

// The placeholder oversamplingRate must NOT trigger drift events until
// the engine actually tracks oversampling per-slot. Spec
// "Risks and open decisions" calls this out as the S1-local decision:
// persist the field for future use, but exclude it from matches() so
// the placeholder value doesn't generate noise on every reopen.
TEST_CASE ("VersionPinningRecord::matches excludes oversamplingRate",
           "[archival-mode][version-pinning]")
{
    auto a = makeVersionPinningRecord (descriptorFixture(), {});
    auto b = a;
    b.oversamplingRate = 4u;
    CHECK (a.matches (b));
}

// The motivating drift scenario from the white paper §15.6: an operator
// saves on machine A with plug-in v1.0.0, reopens on machine B where
// the installed plug-in is v1.0.1. matches() must return false so the
// verifier raises PluginVersionDrift and the performer is warned.
TEST_CASE ("VersionPinningRecord::matches detects version drift",
           "[archival-mode][version-pinning]")
{
    auto a = makeVersionPinningRecord (descriptorFixture(), {});
    auto b = a;
    b.version = "1.0.1";
    CHECK_FALSE (a.matches (b));
}

// State-blob hash drift is the other half of the drift surface — same
// plug-in version but different internal state (e.g., the plug-in vendor
// changed its preset format mid-version-string). Pin that matches() is
// sensitive to the hash too, not just the version string.
TEST_CASE ("VersionPinningRecord::matches detects state-blob hash drift",
           "[archival-mode][version-pinning]")
{
    auto a = makeVersionPinningRecord (descriptorFixture(), {});
    auto b = a;
    b.stateBlobSha256 = std::string (64, '0');
    CHECK_FALSE (a.matches (b));
}

// declaredInternalStateHash is part of the matches() contract today
// even though M8 S1 leaves it empty in makeVersionPinningRecord(). When
// M8 S2 wires up the CLAP self-reported state hash (white paper §15.6),
// callers will start populating this field — pinning the drift check
// now prevents a future refactor from quietly dropping it out of the
// comparator, which would let a real plug-in-internal-state change slip
// past the verifier silently.
TEST_CASE ("VersionPinningRecord::matches detects declaredInternalStateHash drift",
           "[archival-mode][version-pinning]")
{
    auto a = makeVersionPinningRecord (descriptorFixture(), {});
    auto b = a;
    b.declaredInternalStateHash = "vendor-self-report-hash-1";
    CHECK_FALSE (a.matches (b));
}
