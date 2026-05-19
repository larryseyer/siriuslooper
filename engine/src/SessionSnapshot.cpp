#include "sirius/SessionSnapshot.h"

#include "sirius/EffectChain.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/VersionPinningRecord.h"

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

namespace sirius
{
namespace
{

/// Resolves the live VersionPinningRecord for one entry. Consults the
/// host via `lookup` for the slot's actual descriptor + state bytes.
/// Falls back to a descriptor-only record with an empty state blob when
/// the slot isn't configured (nested chain ahead of host wiring) OR when
/// the host can't produce a state blob (timeout / unsupported).
VersionPinningRecord computeLiveRecord (
    const EffectChainEntry&            entry,
    const OutOfProcessEffectChainHost& host,
    const SlotLookup&                  lookup,
    const Constituent&                 owning,
    std::size_t                        entryIndex)
{
    const auto fallback = [&] {
        return makeVersionPinningRecord (entry.descriptor, {});
    };

    const auto loc = lookup (owning, entryIndex);
    if (! loc.has_value())
        return fallback();

    const auto desc = host.descriptorForSlot (loc->busId, loc->slotIndex);
    if (! desc.has_value())
        return fallback();

    const auto blob = host.stateBlobForSlot (loc->busId, loc->slotIndex);
    if (! blob.has_value())
    {
        // The host already posted a Warning notification on timeout.
        // Fall back to a descriptor-only record so the save completes.
        return makeVersionPinningRecord (*desc, {});
    }
    return makeVersionPinningRecord (
        *desc, std::span<const std::byte> (blob->data(), blob->size()));
}

void postRepinNotification (INotificationSink& sink,
                            const std::string& uniqueId,
                            const std::string& oldVersion,
                            const std::string& newVersion)
{
    std::array<char, 128> msg {};
    std::snprintf (msg.data(), msg.size(),
                   "plug-in version repinned: %s old=%s new=%s",
                   uniqueId.c_str(), oldVersion.c_str(), newVersion.c_str());
    sink.post (NotificationLevel::Info, Category::PluginEvent, msg.data());
}

void postDriftNotification (INotificationSink&          sink,
                            const VersionPinningRecord& expected,
                            const VersionPinningRecord& found)
{
    // Reordered format (M8 S2): hashes first so the fixed 128-byte
    // NotificationBus truncation eats the uniqueId tail (least
    // diagnostic) rather than the hashes (most diagnostic). An 8-char
    // hash prefix fingerprints the state blob without bloating the
    // notification budget.
    const auto hashPrefix = [] (const std::string& full) {
        return full.size() >= 8 ? full.substr (0, 8) : full;
    };
    std::array<char, 128> msg {};
    std::snprintf (msg.data(), msg.size(),
                   "version drift eh=%s fh=%s ev=%s fv=%s id=%s",
                   hashPrefix (expected.stateBlobSha256).c_str(),
                   hashPrefix (found.stateBlobSha256).c_str(),
                   expected.version.c_str(),
                   found.version.c_str(),
                   expected.uniqueId.c_str());
    sink.post (NotificationLevel::Warning, Category::PluginEvent, msg.data());
}

/// Walks the input tree depth-first. For each Constituent that carries an
/// EffectChain, recomputes the live record on every VersionPinning entry
/// (always-refresh — no `!has_value()` guard; an upgrade between save-A
/// and save-B must re-pin to the new version). Posts a repin notification
/// when the prior snapshot differed. Returns a new Constituent reflecting
/// the populated tree; subtrees with no VersionPinning changes are shared
/// with the input.
std::shared_ptr<const Constituent> walkAndPopulate (
    std::shared_ptr<const Constituent> node,
    const OutOfProcessEffectChainHost& host,
    const SlotLookup&                  lookup,
    INotificationSink&                 sink)
{
    std::optional<Constituent> rebuilt;
    auto materialize = [&]() -> Constituent& {
        if (! rebuilt.has_value())
            rebuilt.emplace (*node);
        return *rebuilt;
    };

    if (node->hasEffectChain())
    {
        const auto& chain = *node->effectChain();
        EffectChain newChain;
        bool chainChanged = false;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            EffectChainEntry entry = chain.at (i);

            if (entry.archivalMode == ArchivalMode::VersionPinning)
            {
                const auto fresh = computeLiveRecord (entry, host, lookup, *node, i);

                if (entry.persistedSnapshot.has_value()
                    && ! entry.persistedSnapshot->matches (fresh))
                {
                    postRepinNotification (sink,
                                           entry.descriptor.uniqueId,
                                           entry.persistedSnapshot->version,
                                           fresh.version);
                }

                if (! entry.persistedSnapshot.has_value()
                    || ! entry.persistedSnapshot->matches (fresh))
                {
                    entry.persistedSnapshot = fresh;
                    chainChanged = true;
                }
            }

            newChain = newChain.withAppended (std::move (entry));
        }
        if (chainChanged)
        {
            auto& r = materialize();
            r = r.withEffectChain (std::move (newChain));
        }
    }

    for (std::size_t i = 0; i < node->children().size(); ++i)
    {
        const auto& original = node->children()[i];
        auto populated = walkAndPopulate (original, host, lookup, sink);
        if (populated.get() != original.get())
        {
            auto& r = materialize();
            r = r.withChildReplaced (i, populated);
        }
    }

    if (! rebuilt.has_value())
        return node;
    return std::make_shared<const Constituent> (std::move (*rebuilt));
}

void walkAndVerify (const Constituent&                 node,
                    const OutOfProcessEffectChainHost& host,
                    const SlotLookup&                  lookup,
                    INotificationSink&                 sink)
{
    if (node.hasEffectChain())
    {
        const auto& chain = *node.effectChain();
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto& entry = chain.at (i);
            if (! entry.persistedSnapshot.has_value())
                continue;

            const auto live = computeLiveRecord (entry, host, lookup, node, i);
            if (entry.persistedSnapshot->matches (live))
                continue;

            postDriftNotification (sink, *entry.persistedSnapshot, live);
        }
    }

    for (const auto& child : node.children())
        walkAndVerify (*child, host, lookup, sink);
}

} // namespace

std::shared_ptr<const Constituent> populateVersionPinningRecords (
    std::shared_ptr<const Constituent> root,
    const OutOfProcessEffectChainHost& host,
    SlotLookup                         lookup,
    INotificationSink&                 sink)
{
    return walkAndPopulate (std::move (root), host, lookup, sink);
}

void verifyVersionPinningOnLoad (
    const Constituent&                 loadedRoot,
    const OutOfProcessEffectChainHost& host,
    SlotLookup                         lookup,
    INotificationSink&                 sink)
{
    walkAndVerify (loadedRoot, host, lookup, sink);
}

} // namespace sirius
