#pragma once

#include "ida/EffectChain.h"
#include "ida/InternalFxId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ida
{

/// Per-strip popup that lets the operator add, remove, reorder, and bypass
/// the 8 internal-FX insert slots on a single node (channel / bus). Lives
/// in the IdaUi library so it can be reused by future Output Mixer / per-bus
/// pickers; rendered with juce primitives (juce::ComboBox / juce::ToggleButton
/// / juce::TextButton) rather than OTTO's TouchMenuPresenter, per IDA's
/// standalone convention + the `OTTO_OUTPUT_COMBO_AS_JUCE_COMBOBOX` opt-in
/// already wired into IdaLookAndFeel.
///
/// **Stateless w.r.t. the audio thread.** The popup never touches
/// `IEffectChainHost` itself; it emits four callbacks and the host (slice 5,
/// `MainComponent`) translates each into the detach-mutate-reattach sequence
/// around the engine APIs. The caller owns truth (the persisted EffectChain);
/// the popup mirrors the Internal-only subset of that chain for display.
///
/// **Visual rendering is operator-eyes-on** (slice 5). Slice 4 wires the
/// callbacks via the public `simulate*` test seams so Catch2 can drive the
/// callback contract without painting pixels.
class InsertChainPopup final : public juce::Component
{
public:
    using SlotChangedFn      = std::function<void (std::size_t slot, std::optional<InternalFxId> id)>;
    using SlotBypassFn       = std::function<void (std::size_t slot, bool bypassed)>;
    using SlotsReorderedFn   = std::function<void (std::size_t fromSlot, std::size_t toSlot)>;
    using CloseFn            = std::function<void ()>;

    InsertChainPopup();
    ~InsertChainPopup() override;

    /// Seeds the displayed state from an EffectChain. Non-Internal slots
    /// (Empty / Plugin) read as Empty in the popup — Plugin slots are out
    /// of T5 scope (gated on `project_plugin_scanner_broken`). Calling this
    /// rebuilds the row UI; safe to call from the message thread on every
    /// popup-open or chain-change.
    void setInitialChain (const EffectChain& chain);

    /// Read-only view of the popup's current local slot state. Used by tests
    /// and by callers that want to confirm what the popup is showing
    /// (e.g. after a `simulate*` round-trip).
    struct SlotState
    {
        std::optional<InternalFxId> id;
        bool                        bypassed { false };
    };
    const std::array<SlotState, EffectChain::kMaxSlots>& slotStates() const noexcept { return slots_; }

    // -------- callback wiring --------
    void setOnSlotChanged       (SlotChangedFn fn);
    void setOnSlotBypassToggled (SlotBypassFn fn);
    void setOnSlotsReordered    (SlotsReorderedFn fn);
    void setOnClose             (CloseFn fn);

    // -------- juce::Component --------
    void paint   (juce::Graphics& g) override;
    void resized () override;

    // -------- test seams (public on purpose) --------
    /// Pick a new FX for `slot` (`std::nullopt` = remove). Updates local
    /// state, refreshes the row's combo selection, and fires
    /// `onSlotChanged` with the same args. Out-of-range `slot` is a no-op.
    void simulatePickFx (std::size_t slot, std::optional<InternalFxId> id);

    /// Toggle bypass for `slot`. Updates local state, refreshes the row's
    /// toggle button, and fires `onSlotBypassToggled` with the same args.
    /// Out-of-range `slot` is a no-op.
    void simulateBypassToggle (std::size_t slot, bool bypassed);

    /// Reorder slot `fromSlot` to `toSlot`. Mirrors the engine's
    /// `moveInternalFxSlot` semantics (swap on both-occupied, move on
    /// one-occupied, no-op on both-empty / fromSlot == toSlot). Updates
    /// local state, refreshes the affected rows, and fires
    /// `onSlotsReordered` with the same args.
    void simulateReorder (std::size_t fromSlot, std::size_t toSlot);

    /// Fire `onClose` exactly once. Caller is expected to remove the popup
    /// from the parent component after this fires.
    void simulateClose ();

private:
    // One row per slot. Owns its child components (picker / bypass / remove
    // / drag-handle); the popup lays them out in `resized()`.
    class SlotRow;

    void rebuildRows ();
    void refreshRow  (std::size_t slot);

    // Called by SlotRow when a real JUCE event fires; also invoked by the
    // simulate* test seams. Single funnel so the local-state mirror + the
    // public callback fire from one place regardless of the source.
    void handlePickerChange (std::size_t slot, std::optional<InternalFxId> id);
    void handleBypassClick  (std::size_t slot, bool bypassed);

    std::array<SlotState, EffectChain::kMaxSlots> slots_ {};
    std::vector<std::unique_ptr<SlotRow>>         rows_;

    SlotChangedFn    onSlotChanged_;
    SlotBypassFn     onSlotBypass_;
    SlotsReorderedFn onSlotsReordered_;
    CloseFn          onClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertChainPopup)
};

} // namespace ida
