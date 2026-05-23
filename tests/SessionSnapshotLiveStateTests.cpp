// Live-state tests for SessionSnapshot (M8 S2). Exercises the
// drop-guard regression (always-refresh), the repin notification, the
// descriptor-only fallback, and the new hash-prefix-first drift format.
// These configure a real OutOfProcessEffectChainHost against the
// synthetic CLAP and consult its live state — they require both
// IDA_HOST_BINARY_PATH and IDA_SYNTHETIC_CLAP_PATH.
#include "sirius/SessionSnapshot.h"

#include "sirius/Constituent.h"
#include "sirius/ConstituentId.h"
#include "sirius/EffectChain.h"
#include "sirius/INotificationSink.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/VersionPinningRecord.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef IDA_HOST_BINARY_PATH
    #error "IDA_HOST_BINARY_PATH required"
#endif
#ifndef IDA_SYNTHETIC_CLAP_PATH
    #error "IDA_SYNTHETIC_CLAP_PATH required"
#endif

namespace
{
    constexpr const char* kEmptyHash =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    struct RecordingSink : ida::INotificationSink
    {
        struct Entry { ida::NotificationLevel level;
                       ida::Category cat; std::string msg; };
        std::vector<Entry> entries;
        bool post (ida::NotificationLevel level,
                   ida::Category cat,
                   const char* msg) noexcept override
        {
            entries.push_back ({ level, cat, msg != nullptr ? std::string (msg) : std::string {} });
            return true;
        }
    };

    ida::EffectChainEntry makeClapEntry (const std::string& uniqueId,
                                            const std::string& version)
    {
        ida::EffectChainEntry e;
        e.descriptor.format   = ida::PluginFormat::Clap;
        e.descriptor.uniqueId = uniqueId;
        e.descriptor.version  = version;
        e.descriptor.name     = "Synthetic";
        e.descriptor.filePath = IDA_SYNTHETIC_CLAP_PATH;
        e.displayName         = "S";
        e.archivalMode        = ida::ArchivalMode::VersionPinning;
        return e;
    }

    // The real Constituent has no makeRoot() factory; mirror the
    // ArchivalModeTests helper pattern (leaf + withEffectChain).
    std::shared_ptr<const ida::Constituent>
        rootWithChain (const ida::EffectChain& chain)
    {
        auto leaf = std::make_shared<ida::Constituent> (
            ida::ConstituentId (1),
            ida::Position (ida::Rational (0)),
            ida::Position (ida::Rational (1)));
        *leaf = leaf->withEffectChain (chain);
        return leaf;
    }
}

TEST_CASE ("populator hashes real state bytes, not the empty-string hash",
           "[session-snapshot-live]")
{
    ida::OutOfProcessEffectChainHost host;
    ida::EffectChain chain;
    chain = chain.withAppended (
        makeClapEntry ("com.sirius.synthetic.identity", "1.0.0"));
    host.configureBus (10, chain,
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    auto root = rootWithChain (chain);

    RecordingSink sink;
    const auto lookup = [] (const ida::Constituent&, std::size_t i)
        -> std::optional<ida::SlotLocation>
    { return ida::SlotLocation { 10, i }; };

    auto populated = ida::populateVersionPinningRecords (
        root, host, lookup, sink);

    const auto& entry = populated->effectChain()->at (0);
    REQUIRE (entry.persistedSnapshot.has_value());
    // The empty-string SHA-256 is e3b0c4...b855 — our state hash must NOT
    // equal it because the synthetic plug-in emits real state bytes.
    CHECK (entry.persistedSnapshot->stateBlobSha256 != kEmptyHash);
}

TEST_CASE ("populator always refreshes (no !has_value() guard)",
           "[session-snapshot-live]")
{
    ida::OutOfProcessEffectChainHost host;
    ida::EffectChain chain;
    auto entry = makeClapEntry ("com.sirius.synthetic.identity", "1.0.0");
    // Pre-populate with a record that intentionally has a different
    // version — simulates a save-A followed by an upgrade.
    ida::VersionPinningRecord stale;
    stale.uniqueId        = "com.sirius.synthetic.identity";
    stale.version         = "0.9.0";
    stale.stateBlobSha256 = "deadbeef00000000000000000000000000000000000000000000000000000000";
    entry.persistedSnapshot = stale;
    chain = chain.withAppended (entry);

    host.configureBus (11,
                       ida::EffectChain{}.withAppended (
                           makeClapEntry ("com.sirius.synthetic.identity", "1.0.0")),
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    auto root = rootWithChain (chain);

    RecordingSink sink;
    const auto lookup = [] (const ida::Constituent&, std::size_t i)
        -> std::optional<ida::SlotLocation>
    { return ida::SlotLocation { 11, i }; };

    auto populated = ida::populateVersionPinningRecords (
        root, host, lookup, sink);

    const auto& post = populated->effectChain()->at (0);
    REQUIRE (post.persistedSnapshot.has_value());
    CHECK (post.persistedSnapshot->version == "1.0.0");

    // Exactly one PluginVersionRepinned notification.
    int repinCount = 0;
    for (const auto& n : sink.entries)
        if (n.msg.find ("repinned") != std::string::npos)
            ++repinCount;
    CHECK (repinCount == 1);
}

TEST_CASE ("populator falls back to descriptor-only when slot is not configured",
           "[session-snapshot-live]")
{
    ida::OutOfProcessEffectChainHost host; // no buses configured
    ida::EffectChain chain;
    chain = chain.withAppended (
        makeClapEntry ("com.sirius.synthetic.identity", "1.0.0"));

    auto root = rootWithChain (chain);

    RecordingSink sink;
    const auto lookup = [] (const ida::Constituent&, std::size_t)
        -> std::optional<ida::SlotLocation>
    { return std::nullopt; };

    auto populated = ida::populateVersionPinningRecords (
        root, host, lookup, sink);

    const auto& entry = populated->effectChain()->at (0);
    REQUIRE (entry.persistedSnapshot.has_value());
    CHECK (entry.persistedSnapshot->stateBlobSha256 == kEmptyHash);
}

TEST_CASE ("verifier with matching records posts zero notifications",
           "[session-snapshot-live]")
{
    ida::OutOfProcessEffectChainHost host;
    ida::EffectChain chain;
    chain = chain.withAppended (
        makeClapEntry ("com.sirius.synthetic.identity", "1.0.0"));
    host.configureBus (12, chain,
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    RecordingSink sink;
    const auto lookup = [] (const ida::Constituent&, std::size_t i)
        -> std::optional<ida::SlotLocation>
    { return ida::SlotLocation { 12, i }; };

    auto root = rootWithChain (chain);
    auto populated = ida::populateVersionPinningRecords (
        root, host, lookup, sink);
    sink.entries.clear();

    ida::verifyVersionPinningOnLoad (*populated, host, lookup, sink);
    CHECK (sink.entries.empty());
}

TEST_CASE ("verifier with mismatched version posts drift with hash prefixes",
           "[session-snapshot-live]")
{
    ida::OutOfProcessEffectChainHost host;
    ida::EffectChain chain;
    chain = chain.withAppended (
        makeClapEntry ("com.sirius.synthetic.identity", "1.0.0"));
    host.configureBus (13, chain,
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    RecordingSink sink;
    const auto lookup = [] (const ida::Constituent&, std::size_t i)
        -> std::optional<ida::SlotLocation>
    { return ida::SlotLocation { 13, i }; };

    // Build a tree whose snapshot has a stale version + hash.
    auto entry = makeClapEntry ("com.sirius.synthetic.identity", "1.0.0");
    ida::VersionPinningRecord stale;
    stale.uniqueId        = "com.sirius.synthetic.identity";
    stale.version         = "0.9.0";
    stale.stateBlobSha256 = "deadbeef00000000000000000000000000000000000000000000000000000000";
    entry.persistedSnapshot = stale;
    ida::EffectChain staleChain;
    staleChain = staleChain.withAppended (entry);

    auto root = rootWithChain (staleChain);

    ida::verifyVersionPinningOnLoad (*root, host, lookup, sink);

    int driftCount = 0;
    bool sawHashPrefix = false;
    for (const auto& n : sink.entries)
    {
        if (n.msg.find ("version drift") != std::string::npos)
        {
            ++driftCount;
            if (n.msg.find ("eh=deadbeef") != std::string::npos)
                sawHashPrefix = true;
        }
    }
    CHECK (driftCount == 1);
    CHECK (sawHashPrefix);
}
