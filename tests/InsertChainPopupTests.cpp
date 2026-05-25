// Tests for ida::InsertChainPopup — P7 T5 slice 4. The popup is a JUCE
// Component but its slot picker / bypass / reorder / close gestures all funnel
// through public test seams (`simulatePickFx`, `simulateBypassToggle`,
// `simulateReorder`, `simulateClose`) so Catch2 can drive the callback
// contract without painting pixels. Pixel rendering (geometry, fonts, accent
// colours, drag-handle gesture) is operator-eyes-on at slice 5.
//
// Each TEST_CASE constructs the popup inside a `juce::ScopedJuceInitialiser_GUI`
// (the same shape `MainComponentPluginEditorTests.cpp` uses to keep the
// harness headless). The popup is then driven through its public surface and
// the registered std::function spies are asserted.

#include "ida/InsertChainPopup.h"

#include "ida/EffectChain.h"
#include "ida/InternalFxId.h"
#include "ida/PluginDescriptor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using ida::EffectChain;
using ida::EffectChainEntry;
using ida::EffectChainSlotKind;
using ida::InsertChainPopup;
using ida::InternalFxId;

namespace
{

// Captured callback invocation for the slot-changed signal.
struct SlotChangedEvent
{
    std::size_t                 slot;
    std::optional<InternalFxId> id;
};

struct BypassEvent
{
    std::size_t slot;
    bool        bypassed;
};

struct ReorderEvent
{
    std::size_t fromSlot;
    std::size_t toSlot;
};

} // namespace

TEST_CASE ("InsertChainPopup setInitialChain seeds slot state and simulatePickFx fires onSlotChanged",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kRvb));
    popup.setInitialChain (chain);

    REQUIRE (popup.slotStates()[0].id.has_value());
    REQUIRE (*popup.slotStates()[0].id == InternalFxId::kRvb);
    REQUIRE_FALSE (popup.slotStates()[0].bypassed);

    std::vector<SlotChangedEvent> events;
    popup.setOnSlotChanged ([&] (std::size_t s, std::optional<InternalFxId> id)
                            { events.push_back ({ s, id }); });

    popup.simulatePickFx (0, InternalFxId::kDly);

    REQUIRE (events.size() == 1);
    CHECK (events[0].slot == 0);
    REQUIRE (events[0].id.has_value());
    CHECK (*events[0].id == InternalFxId::kDly);
    REQUIRE (popup.slotStates()[0].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kDly);
}

TEST_CASE ("InsertChainPopup picker rejects EQ and CMP (operator rule 2026-05-24 — strip-tab only)",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    // Seed a known starting state so we can prove the reject leaves it untouched.
    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kRvb));
    popup.setInitialChain (chain);

    std::vector<SlotChangedEvent> events;
    popup.setOnSlotChanged ([&] (std::size_t s, std::optional<InternalFxId> id)
                            { events.push_back ({ s, id }); });

    // EQ pick — rejected. No callback, no state change.
    popup.simulatePickFx (0, InternalFxId::kEq);
    CHECK (events.empty());
    REQUIRE (popup.slotStates()[0].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kRvb);

    // CMP pick — also rejected.
    popup.simulatePickFx (0, InternalFxId::kCmp);
    CHECK (events.empty());
    REQUIRE (popup.slotStates()[0].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kRvb);

    // Positive control: DLY still works through the same seam.
    popup.simulatePickFx (0, InternalFxId::kDly);
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].id.has_value());
    CHECK (*events[0].id == InternalFxId::kDly);
    REQUIRE (popup.slotStates()[0].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kDly);
}

TEST_CASE ("InsertChainPopup simulatePickFx with nullopt fires onSlotChanged with nullopt (remove)",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq));
    popup.setInitialChain (chain);

    std::vector<SlotChangedEvent> events;
    popup.setOnSlotChanged ([&] (std::size_t s, std::optional<InternalFxId> id)
                            { events.push_back ({ s, id }); });

    popup.simulatePickFx (0, std::nullopt);

    REQUIRE (events.size() == 1);
    CHECK (events[0].slot == 0);
    CHECK_FALSE (events[0].id.has_value());
    CHECK_FALSE (popup.slotStates()[0].id.has_value());
}

TEST_CASE ("InsertChainPopup simulateBypassToggle fires onSlotBypassToggled with expected args",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly));
    popup.setInitialChain (chain);

    std::vector<BypassEvent> events;
    popup.setOnSlotBypassToggled ([&] (std::size_t s, bool b)
                                  { events.push_back ({ s, b }); });

    popup.simulateBypassToggle (0, true);
    popup.simulateBypassToggle (0, false);

    REQUIRE (events.size() == 2);
    CHECK (events[0].slot == 0);
    CHECK (events[0].bypassed);
    CHECK (events[1].slot == 0);
    CHECK_FALSE (events[1].bypassed);
    CHECK_FALSE (popup.slotStates()[0].bypassed);
}

TEST_CASE ("InsertChainPopup simulateReorder fires onSlotsReordered and swaps local state",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq));   // slot 0
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp));  // slot 1
    popup.setInitialChain (chain);

    std::vector<ReorderEvent> events;
    popup.setOnSlotsReordered ([&] (std::size_t from, std::size_t to)
                               { events.push_back ({ from, to }); });

    popup.simulateReorder (0, 1);

    REQUIRE (events.size() == 1);
    CHECK (events[0].fromSlot == 0);
    CHECK (events[0].toSlot   == 1);
    REQUIRE (popup.slotStates()[0].id.has_value());
    REQUIRE (popup.slotStates()[1].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kCmp);
    CHECK (*popup.slotStates()[1].id == InternalFxId::kEq);
}

TEST_CASE ("InsertChainPopup simulateReorder with equal slots is a no-op",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    std::vector<ReorderEvent> events;
    popup.setOnSlotsReordered ([&] (std::size_t f, std::size_t t)
                               { events.push_back ({ f, t }); });

    popup.simulateReorder (3, 3);

    CHECK (events.empty());
}

TEST_CASE ("InsertChainPopup simulateClose fires onClose exactly once",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    int closeCount = 0;
    popup.setOnClose ([&] { ++closeCount; });

    popup.simulateClose();

    CHECK (closeCount == 1);
}

TEST_CASE ("InsertChainPopup unregistered callback path is a no-op (no crash)",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    // None of these have been registered. Each call must update local state
    // and return without crashing.
    popup.simulatePickFx       (0, InternalFxId::kCmp);
    popup.simulateBypassToggle (0, true);
    popup.simulateReorder      (0, 1);
    popup.simulateClose();

    SUCCEED ("no crash on unregistered callbacks");
}

TEST_CASE ("InsertChainPopup setInitialChain ignores non-Internal slots",
           "[insert-chain-popup]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    InsertChainPopup popup;

    ida::PluginDescriptor descriptor;
    descriptor.uniqueId = "com.example.plugin";

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq));   // slot 0 — Internal
    chain = chain.withAppended (EffectChainEntry {});                                   // slot 1 — Empty (default)
    chain = chain.withAppended (EffectChainEntry::makePlugin (descriptor, "Stub", "")); // slot 2 — Plugin
    popup.setInitialChain (chain);

    REQUIRE (popup.slotStates()[0].id.has_value());
    CHECK (*popup.slotStates()[0].id == InternalFxId::kEq);
    CHECK_FALSE (popup.slotStates()[1].id.has_value());
    CHECK_FALSE (popup.slotStates()[2].id.has_value());
}
