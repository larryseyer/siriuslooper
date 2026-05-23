#include "ida/ConstituentValidator.h"

#include <array>
#include <cstdio>
#include <functional>
#include <optional>

namespace ida
{

bool alwaysResolves (const TapeReference&) noexcept { return true; }

ConstituentValidation::ConstituentValidation (
    std::unordered_map<ConstituentId, ConstituentState> states)
    : states_ (std::move (states))
{
}

ConstituentState ConstituentValidation::state (ConstituentId id) const noexcept
{
    const auto it = states_.find (id);
    return it == states_.end() ? ConstituentState::Valid : it->second;
}

bool ConstituentValidation::renderable (ConstituentId id) const noexcept
{
    return state (id) == ConstituentState::Valid;
}

namespace
{
    /// Classify one node. `parentLength` is the parent's span in parent-local
    /// whole notes; nullopt for the root (no parent => no containment check).
    /// Invalid dominates Broken: a node whose placement contradicts its parent
    /// cannot be trusted to reference anything.
    ConstituentState classify (const Constituent&             c,
                               const std::optional<Rational>& parentLength,
                               const TapeResolver&            resolver)
    {
        if (parentLength.has_value())
        {
            const Rational in  = c.conceptualIn().wholeNotes();
            const Rational out = c.conceptualOut().wholeNotes();
            if (in < Rational (0) || out > *parentLength)
                return ConstituentState::Invalid;
        }

        if (c.isLeaf() && c.tapeReference().has_value() && ! resolver (*c.tapeReference()))
            return ConstituentState::Broken;

        return ConstituentState::Valid;
    }

    void walk (const Constituent&             c,
               const std::optional<Rational>& parentLength,
               const TapeResolver&            resolver,
               std::unordered_map<ConstituentId, ConstituentState>& out)
    {
        const ConstituentState s = classify (c, parentLength, resolver);
        if (s != ConstituentState::Valid)
            out.emplace (c.id(), s);

        const Rational length = c.duration();
        for (const auto& child : c.children())
            walk (*child, length, resolver, out);
    }
}

ConstituentValidation validate (const Constituent& root, const TapeResolver& resolver)
{
    std::unordered_map<ConstituentId, ConstituentState> states;
    walk (root, std::nullopt, resolver, states);
    return ConstituentValidation (std::move (states));
}

void postConstituentStateNotifications (const Constituent&           root,
                                        const ConstituentValidation& validation,
                                        INotificationSink&           sink)
{
    std::function<void (const Constituent&)> visit =
        [&] (const Constituent& c)
        {
            const ConstituentState s = validation.state (c.id());
            if (s == ConstituentState::Broken || s == ConstituentState::Invalid)
            {
                std::array<char, 128> msg {};
                std::snprintf (msg.data(), msg.size(),
                               s == ConstituentState::Broken
                                   ? "constituent %lld broken: tape unresolved"
                                   : "constituent %lld invalid: bounds outside parent",
                               static_cast<long long> (c.id().value()));
                sink.post (NotificationLevel::Warning, Category::StateRepair, msg.data());
            }

            for (const auto& child : c.children())
                visit (*child);
        };

    visit (root);
}

} // namespace ida
