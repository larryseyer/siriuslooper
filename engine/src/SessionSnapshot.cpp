#include "sirius/SessionSnapshot.h"

#include "sirius/EffectChain.h"
#include "sirius/VersionPinningRecord.h"

#include <array>
#include <cstdio>
#include <utility>

namespace sirius
{
namespace
{

/// Walks the input tree depth-first. For each Constituent that carries
/// an EffectChain, rebuilds the chain with persistedSnapshot populated
/// on any VersionPinning entry that doesn't already have one.
/// Returns a new Constituent (shared_ptr<const Constituent>) reflecting
/// the populated tree; subtrees with no VersionPinning changes are
/// shared with the input.
std::shared_ptr<const Constituent> walkAndPopulate (
    std::shared_ptr<const Constituent> node)
{
    bool anyChange = false;
    Constituent rebuilt = *node;

    if (node->hasEffectChain())
    {
        const auto& chain = *node->effectChain();
        EffectChain newChain;
        bool chainChanged = false;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            EffectChainEntry entry = chain.at (i);
            if (entry.archivalMode == ArchivalMode::VersionPinning
                && ! entry.persistedSnapshot.has_value())
            {
                entry.persistedSnapshot = makeVersionPinningRecord (entry.descriptor, {});
                chainChanged = true;
            }
            newChain = newChain.withAppended (std::move (entry));
        }
        if (chainChanged)
        {
            rebuilt = rebuilt.withEffectChain (std::move (newChain));
            anyChange = true;
        }
    }

    for (std::size_t i = 0; i < node->children().size(); ++i)
    {
        const auto& original = node->children()[i];
        auto populated = walkAndPopulate (original);
        if (populated.get() != original.get())
        {
            rebuilt = rebuilt.withChildReplaced (i, populated);
            anyChange = true;
        }
    }

    if (! anyChange)
        return node;
    return std::make_shared<const Constituent> (std::move (rebuilt));
}

void walkAndVerify (const Constituent& node, INotificationSink& sink)
{
    if (node.hasEffectChain())
    {
        const auto& chain = *node.effectChain();
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto& entry = chain.at (i);
            if (! entry.persistedSnapshot.has_value())
                continue;

            const auto live = makeVersionPinningRecord (entry.descriptor, {});
            if (entry.persistedSnapshot->matches (live))
                continue;

            // Format a compact drift message. The fixed 128-byte
            // NotificationBus buffer truncates; keep the message well
            // under the limit by naming only the unique id + expected/
            // found versions (the two most-actionable fields for an
            // operator triaging the drift line item).
            std::array<char, 128> msg {};
            std::snprintf (msg.data(), msg.size(),
                           "plug-in version drift: %s expected=%s found=%s",
                           entry.descriptor.uniqueId.c_str(),
                           entry.persistedSnapshot->version.c_str(),
                           live.version.c_str());
            sink.post (NotificationLevel::Warning, Category::PluginEvent, msg.data());
        }
    }

    for (const auto& child : node.children())
        walkAndVerify (*child, sink);
}

} // namespace

std::shared_ptr<const Constituent> populateVersionPinningRecords (
    std::shared_ptr<const Constituent> root)
{
    return walkAndPopulate (std::move (root));
}

void verifyVersionPinningOnLoad (const Constituent& loadedRoot,
                                 INotificationSink& sink)
{
    walkAndVerify (loadedRoot, sink);
}

} // namespace sirius
