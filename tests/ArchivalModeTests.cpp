// Tests for the M8 S1 archival-mode surface (white paper §15.6) — the
// `ArchivalMode` enum, the `VersionPinningRecord` value type, the
// SHA-256 helper that produces stable state-blob fingerprints, and the
// engine-side populator/verifier that bridge the live session graph to
// the persistence layer. JUCE-free for the core types; JUCE-using for
// the SessionFormat round-trip and the NotificationBus verifier.
#include "sirius/ArchivalMode.h"
#include "sirius/Constituent.h"
#include "sirius/EffectChain.h"
#include "sirius/INotificationSink.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/SessionFormat.h"
#include "sirius/SessionSnapshot.h"
#include "sirius/Sha256.h"
#include "sirius/VersionPinningRecord.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using sirius::ArchivalMode;
using sirius::Category;
using sirius::Constituent;
using sirius::ConstituentId;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::makeVersionPinningRecord;
using sirius::NotificationLevel;
using sirius::PluginDescriptor;
using sirius::PluginFormat;
using sirius::populateVersionPinningRecords;
using sirius::Position;
using sirius::Rational;
using sirius::sha256Hex;
using sirius::verifyVersionPinningOnLoad;

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

    /// Recording sink — captures every post for later inspection by the
    /// verifier tests. Pattern matches the existing test helpers used in
    /// tests/OutOfProcessEffectChainHostSupervisorTests.cpp.
    struct RecordingSink : sirius::INotificationSink
    {
        struct Record { NotificationLevel level; Category category; std::string message; };
        std::vector<Record> records;

        bool post (NotificationLevel level, Category category, const char* message) noexcept override
        {
            records.push_back ({ level, category, message != nullptr ? std::string (message) : std::string {} });
            return true;
        }
    };

    std::shared_ptr<const Constituent> leafWithEntry (const EffectChainEntry& entry)
    {
        auto leaf = std::make_shared<Constituent> (
            ConstituentId (1),
            Position (Rational (0)),
            Position (Rational (1)));
        *leaf = leaf->withEffectChain (EffectChain().withAppended (entry));
        return leaf;
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

// Spec "Defaults for new sessions" — VersionPinning is the prescribed
// default per V7 plan line 563. This test pins that default so a
// refactor of the EffectChainEntry initializer cannot silently change
// the global disposition for every new chain slot in the codebase.
TEST_CASE ("ArchivalMode default for a new EffectChainEntry is VersionPinning",
           "[archival-mode]")
{
    EffectChainEntry e;
    CHECK (e.archivalMode == ArchivalMode::VersionPinning);
    CHECK_FALSE (e.persistedSnapshot.has_value());
}

// The save→serialize→deserialize→compare path pins that both new fields
// survive the JSON boundary intact. Without this, a serializer that
// silently dropped `persistedSnapshot` would make every reopen a
// "no drift detected" false negative — see the descriptor-version
// round-trip case for the parallel failure-mode argument.
TEST_CASE ("EffectChainEntry with persistedSnapshot round-trips through SessionFormat",
           "[archival-mode][session-format]")
{
    EffectChainEntry entry;
    entry.descriptor   = descriptorFixture();
    entry.displayName  = "Synthetic";
    entry.bypassed     = false;
    entry.archivalMode = ArchivalMode::VersionPinning;
    entry.persistedSnapshot = makeVersionPinningRecord (entry.descriptor, {});

    auto leaf = std::make_shared<Constituent> (
        ConstituentId (1),
        Position (Rational (0)),
        Position (Rational (1)));
    *leaf = leaf->withEffectChain (EffectChain().withAppended (entry));

    const auto json  = sirius::persistence::serializeSession (*leaf);
    const auto round = sirius::persistence::deserializeSession (json);
    REQUIRE (round->effectChain().has_value());
    REQUIRE (round->effectChain()->size() == 1);

    const auto& loadedEntry = round->effectChain()->at (0);
    CHECK (loadedEntry.archivalMode == ArchivalMode::VersionPinning);
    REQUIRE (loadedEntry.persistedSnapshot.has_value());
    CHECK (*loadedEntry.persistedSnapshot == *entry.persistedSnapshot);
}

// The DeterminismContract path doesn't populate a snapshot in M8 S1
// (snapshot is exclusive to VersionPinning entries). Pin that an
// unset optional round-trips back unset — i.e., that the serializer
// distinguishes "no snapshot" from "default-constructed snapshot".
TEST_CASE ("EffectChainEntry without persistedSnapshot round-trips with no snapshot",
           "[archival-mode][session-format]")
{
    EffectChainEntry entry;
    entry.descriptor   = descriptorFixture();
    entry.displayName  = "Synthetic";
    entry.archivalMode = ArchivalMode::DeterminismContract;
    // Deliberately no persistedSnapshot.

    auto leaf = std::make_shared<Constituent> (
        ConstituentId (1),
        Position (Rational (0)),
        Position (Rational (1)));
    *leaf = leaf->withEffectChain (EffectChain().withAppended (entry));

    const auto json  = sirius::persistence::serializeSession (*leaf);
    const auto round = sirius::persistence::deserializeSession (json);
    REQUIRE (round->effectChain()->size() == 1);
    CHECK (round->effectChain()->at (0).archivalMode == ArchivalMode::DeterminismContract);
    CHECK_FALSE (round->effectChain()->at (0).persistedSnapshot.has_value());
}

// The populator's whole job is to freeze the live identity of each
// VersionPinning slot at save time. If this test breaks, the saved
// JSON will lack a persistedSnapshot for the slot, and reopen-time
// drift detection becomes impossible — the verifier has nothing to
// compare against.
TEST_CASE ("populateVersionPinningRecords sets persistedSnapshot on VersionPinning entries",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor   = descriptorFixture();
    entry.archivalMode = ArchivalMode::VersionPinning;
    REQUIRE_FALSE (entry.persistedSnapshot.has_value());

    auto root = leafWithEntry (entry);
    auto populated = populateVersionPinningRecords (root);
    REQUIRE (populated->effectChain()->size() == 1);
    const auto& populatedEntry = populated->effectChain()->at (0);
    REQUIRE (populatedEntry.persistedSnapshot.has_value());
    CHECK (populatedEntry.persistedSnapshot->uniqueId == "com.sirius.synthetic.test");
    CHECK (populatedEntry.persistedSnapshot->version  == "1.0.0");
    CHECK (populatedEntry.persistedSnapshot->stateBlobSha256
           == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// DeterminismContract and WetCapture entries are out of scope for
// VersionPinning snapshotting — their identity is captured by other
// mechanisms (render-twice verification / wet-tape writes) that land
// in later M8 sessions. The populator must leave them alone.
TEST_CASE ("populateVersionPinningRecords leaves non-VersionPinning entries alone",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor   = descriptorFixture();
    entry.archivalMode = ArchivalMode::DeterminismContract;
    auto root = leafWithEntry (entry);
    auto populated = populateVersionPinningRecords (root);
    CHECK_FALSE (populated->effectChain()->at (0).persistedSnapshot.has_value());
}

// Mirror of the DeterminismContract case for the third enum arm. WetCapture
// entries are captured by a wet-tape writer (M8 S4-5), not by snapshotting —
// the populator must leave them alone too. Without this test, a bug that
// populated snapshots on WetCapture entries would go undetected until M8 S5
// observed phantom drift events.
TEST_CASE ("populateVersionPinningRecords leaves WetCapture entries alone",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor   = descriptorFixture();
    entry.archivalMode = ArchivalMode::WetCapture;
    auto root = leafWithEntry (entry);
    auto populated = populateVersionPinningRecords (root);
    CHECK_FALSE (populated->effectChain()->at (0).persistedSnapshot.has_value());
}

// Constituent::ChildPtr is shared_ptr<const Constituent> — the
// immutability contract is load-bearing across the entire structure
// layer. A populator that mutated the input would violate every
// copy-on-write invariant and break unrelated code (undo stack,
// session merge, etc.).
TEST_CASE ("populateVersionPinningRecords is copy-on-write — original tree unchanged",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor = descriptorFixture();
    auto root = leafWithEntry (entry);
    auto populated = populateVersionPinningRecords (root);
    CHECK_FALSE (root->effectChain()->at (0).persistedSnapshot.has_value());
    CHECK (populated->effectChain()->at (0).persistedSnapshot.has_value());
}

// The COW promise has two halves: (1) the input is not mutated (covered
// by the previous test), and (2) unchanged subtrees stay SHARED — same
// shared_ptr identity, no allocation. A naive deep-copy implementation
// would pass the mutate-check but waste memory by rebuilding every
// untouched sibling. This test pins the sharing invariant by giving a
// parent two children — one whose entry needs a snapshot and one whose
// entry is DeterminismContract (skipped by the populator) — and
// asserting the unchanged child's pointer survives the walk.
TEST_CASE ("populateVersionPinningRecords shares unchanged sibling subtrees",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry changingEntry;
    changingEntry.descriptor   = descriptorFixture();
    changingEntry.archivalMode = ArchivalMode::VersionPinning;

    EffectChainEntry stableEntry;
    stableEntry.descriptor   = descriptorFixture();
    stableEntry.archivalMode = ArchivalMode::DeterminismContract;

    auto changingChild = leafWithEntry (changingEntry);
    auto stableChild   = leafWithEntry (stableEntry);

    auto parent = std::make_shared<Constituent> (
        ConstituentId (100),
        Position (Rational (0)),
        Position (Rational (2)));
    *parent = parent->withChildAdded (changingChild)
                     .withChildAdded (stableChild);

    auto populated = populateVersionPinningRecords (parent);

    REQUIRE (populated->children().size() == 2);
    // The first child got a snapshot — its pointer must have changed
    // (rebuilt).
    CHECK (populated->children()[0].get() != changingChild.get());
    // The second child was untouched — its pointer MUST be the same
    // shared_ptr, proving the populator shared rather than deep-copied.
    CHECK (populated->children()[1].get() == stableChild.get());
}

// When the snapshot matches the live record (same machine reopen of
// an unchanged plug-in install), no drift notification fires. This
// is the dominant case at runtime — operators reopen sessions far
// more often than they upgrade plug-ins.
TEST_CASE ("verifyVersionPinningOnLoad emits nothing when persistedSnapshot matches live",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor        = descriptorFixture();
    entry.persistedSnapshot = makeVersionPinningRecord (entry.descriptor, {});

    auto root = leafWithEntry (entry);
    RecordingSink sink;
    verifyVersionPinningOnLoad (*root, sink);
    CHECK (sink.records.empty());
}

// The motivating reopen-drift scenario: an operator saves on machine
// A with plug-in v1.0.0, reopens on machine B where the installed
// plug-in is v0.9.0 (older). The verifier must post one Warning /
// PluginEvent naming the unique id + both versions so the performer
// sees what drifted in the notification-history pane.
TEST_CASE ("verifyVersionPinningOnLoad emits Warning/PluginEvent on version drift",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor        = descriptorFixture();
    entry.persistedSnapshot = makeVersionPinningRecord (entry.descriptor, {});
    entry.persistedSnapshot->version = "0.9.0"; // simulate snapshot from an older plug-in install

    auto root = leafWithEntry (entry);
    RecordingSink sink;
    verifyVersionPinningOnLoad (*root, sink);
    REQUIRE (sink.records.size() == 1);
    CHECK (sink.records[0].level    == NotificationLevel::Warning);
    CHECK (sink.records[0].category == Category::PluginEvent);
    CHECK (sink.records[0].message.find ("com.sirius.synthetic.test") != std::string::npos);
    CHECK (sink.records[0].message.find ("0.9.0") != std::string::npos);
    CHECK (sink.records[0].message.find ("1.0.0") != std::string::npos);
}

// Entries without a persistedSnapshot are either (a) DeterminismContract /
// WetCapture entries that the populator deliberately skipped, or (b) old
// pre-M8 session entries loaded with the default-empty optional. Neither
// is drift — the verifier must simply skip them, not emit phantom events.
TEST_CASE ("verifyVersionPinningOnLoad skips entries without a persistedSnapshot",
           "[archival-mode][session-snapshot]")
{
    EffectChainEntry entry;
    entry.descriptor = descriptorFixture();
    // No persistedSnapshot.
    auto root = leafWithEntry (entry);
    RecordingSink sink;
    verifyVersionPinningOnLoad (*root, sink);
    CHECK (sink.records.empty());
}
