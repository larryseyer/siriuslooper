#include "ida/InsertChainPopup.h"

#include "OTTOLookAndFeel.h"
#include "Sizing.h"

#include <algorithm>

namespace ida
{

namespace
{

// ComboBox item IDs. JUCE forbids item id 0 (used for "no selection"), so
// the picker starts at 1. The mapping is bidirectional and explicit — we do
// NOT lean on the int value of `InternalFxId` because the display order
// (Empty → EQ → CMP → DLY → RVB, signal-chain convention) differs from the
// enum's storage order (kEq=0 / kCmp=1 / kRvb=2 / kDly=3).
constexpr int kComboIdEmpty = 1;
constexpr int kComboIdEq    = 2;
constexpr int kComboIdCmp   = 3;
constexpr int kComboIdDly   = 4;
constexpr int kComboIdRvb   = 5;

int comboIdFor (std::optional<InternalFxId> id) noexcept
{
    if (! id.has_value())
        return kComboIdEmpty;
    switch (*id)
    {
        case InternalFxId::kEq:  return kComboIdEq;
        case InternalFxId::kCmp: return kComboIdCmp;
        case InternalFxId::kDly: return kComboIdDly;
        case InternalFxId::kRvb: return kComboIdRvb;
    }
    return kComboIdEmpty;
}

std::optional<InternalFxId> fxIdForCombo (int comboId) noexcept
{
    switch (comboId)
    {
        case kComboIdEq:  return InternalFxId::kEq;
        case kComboIdCmp: return InternalFxId::kCmp;
        case kComboIdDly: return InternalFxId::kDly;
        case kComboIdRvb: return InternalFxId::kRvb;
        case kComboIdEmpty:
        default:          return std::nullopt;
    }
}

} // namespace

// =============================================================================
// InsertChainPopup::SlotRow — one row in the popup, one per slot index.
// =============================================================================
class InsertChainPopup::SlotRow final : public juce::Component
{
public:
    SlotRow (InsertChainPopup& owner, std::size_t slotIndex)
        : owner_ (owner), slot_ (slotIndex)
    {
        picker_.addItem ("Empty", kComboIdEmpty);
        picker_.addItem ("EQ",    kComboIdEq);
        picker_.addItem ("CMP",   kComboIdCmp);
        picker_.addItem ("DLY",   kComboIdDly);
        picker_.addItem ("RVB",   kComboIdRvb);
        picker_.setSelectedId (kComboIdEmpty, juce::dontSendNotification);
        picker_.onChange = [this]
        {
            const auto chosen = fxIdForCombo (picker_.getSelectedId());
            owner_.handlePickerChange (slot_, chosen);
        };
        addAndMakeVisible (picker_);

        bypass_.setClickingTogglesState (true);
        bypass_.setTooltip ("Bypass this slot");
        bypass_.onClick = [this]
        {
            owner_.handleBypassClick (slot_, bypass_.getToggleState());
        };
        addAndMakeVisible (bypass_);

        remove_.setButtonText (juce::String::fromUTF8 ("\xC3\x97")); // ×
        remove_.setTooltip ("Remove this slot");
        remove_.onClick = [this]
        {
            owner_.handlePickerChange (slot_, std::nullopt);
        };
        addAndMakeVisible (remove_);
    }

    void syncFromState (const SlotState& s)
    {
        picker_.setSelectedId (comboIdFor (s.id), juce::dontSendNotification);
        bypass_.setToggleState (s.bypassed, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        // Left-edge drag handle gripper. Geometry from OTTO menu tokens so the
        // popup reads as part of the OTTO family. The drag gesture itself is
        // operator-eyes-on (slice 5); the simulateReorder seam covers the
        // callback contract for slice 4.
        const auto colours = otto::OTTOLookAndFeel::getMenuColors();
        const auto gripWidth = static_cast<int> (otto::Sizing::kMenuHorizontalPadding);
        auto bounds = getLocalBounds();
        auto grip = bounds.removeFromLeft (gripWidth).reduced (4, 8);
        g.setColour (colours.separator);
        for (int y = grip.getY(); y < grip.getBottom(); y += 3)
            g.fillRect (grip.getX(), y, grip.getWidth(), 1);
    }

    void resized() override
    {
        const auto rowH = static_cast<int> (otto::Sizing::kMenuRowHeight);
        const auto pad  = static_cast<int> (otto::Sizing::kMenuHorizontalPadding);

        auto bounds = getLocalBounds();
        bounds.removeFromLeft (pad);          // drag handle gutter (painted in paint())
        auto removeArea = bounds.removeFromRight (rowH);
        auto bypassArea = bounds.removeFromRight (rowH);
        bounds.removeFromRight (4);
        bypassArea.removeFromRight (4);

        picker_.setBounds (bounds.reduced (0, 2));
        bypass_.setBounds (bypassArea.reduced (4));
        remove_.setBounds (removeArea.reduced (4));
    }

private:
    InsertChainPopup&  owner_;
    std::size_t        slot_;
    juce::ComboBox     picker_;
    juce::ToggleButton bypass_;
    juce::TextButton   remove_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotRow)
};

// =============================================================================
// InsertChainPopup
// =============================================================================

InsertChainPopup::InsertChainPopup()
{
    rebuildRows();
}

InsertChainPopup::~InsertChainPopup() = default;

void InsertChainPopup::setInitialChain (const EffectChain& chain)
{
    slots_.fill (SlotState {});
    const auto& entries = chain.entries();
    const auto  count   = std::min (entries.size(), EffectChain::kMaxSlots);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& e = entries[i];
        if (e.kind == EffectChainSlotKind::Internal)
            slots_[i] = SlotState { e.internalId, e.bypassed };
        // Plugin / Empty kinds intentionally read as Empty in this popup —
        // T5 only handles internal-FX slots; Plugin support waits on
        // `project_plugin_scanner_broken`.
    }
    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
        refreshRow (i);
}

void InsertChainPopup::setOnSlotChanged       (SlotChangedFn fn)    { onSlotChanged_   = std::move (fn); }
void InsertChainPopup::setOnSlotBypassToggled (SlotBypassFn fn)     { onSlotBypass_    = std::move (fn); }
void InsertChainPopup::setOnSlotsReordered    (SlotsReorderedFn fn) { onSlotsReordered_= std::move (fn); }
void InsertChainPopup::setOnClose             (CloseFn fn)          { onClose_         = std::move (fn); }

void InsertChainPopup::paint (juce::Graphics& g)
{
    g.fillAll (otto::OTTOLookAndFeel::getMenuColors().background);
}

void InsertChainPopup::resized()
{
    const auto rowH = static_cast<int> (otto::Sizing::kMenuRowHeight);
    auto bounds = getLocalBounds();
    for (auto& row : rows_)
        row->setBounds (bounds.removeFromTop (rowH));
}

// -------- test seams --------

void InsertChainPopup::simulatePickFx (std::size_t slot, std::optional<InternalFxId> id)
{
    if (slot >= EffectChain::kMaxSlots)
        return;
    handlePickerChange (slot, id);
}

void InsertChainPopup::simulateBypassToggle (std::size_t slot, bool bypassed)
{
    if (slot >= EffectChain::kMaxSlots)
        return;
    handleBypassClick (slot, bypassed);
}

void InsertChainPopup::simulateReorder (std::size_t fromSlot, std::size_t toSlot)
{
    if (fromSlot >= EffectChain::kMaxSlots || toSlot >= EffectChain::kMaxSlots)
        return;
    if (fromSlot == toSlot)
        return;
    std::swap (slots_[fromSlot], slots_[toSlot]);
    refreshRow (fromSlot);
    refreshRow (toSlot);
    if (onSlotsReordered_)
        onSlotsReordered_ (fromSlot, toSlot);
}

void InsertChainPopup::simulateClose()
{
    if (onClose_)
        onClose_();
}

// -------- private --------

void InsertChainPopup::handlePickerChange (std::size_t slot, std::optional<InternalFxId> id)
{
    if (slot >= EffectChain::kMaxSlots)
        return;
    slots_[slot] = SlotState { id, false }; // picking a fresh fx resets bypass (mirrors engine contract)
    refreshRow (slot);
    if (onSlotChanged_)
        onSlotChanged_ (slot, id);
}

void InsertChainPopup::handleBypassClick (std::size_t slot, bool bypassed)
{
    if (slot >= EffectChain::kMaxSlots)
        return;
    slots_[slot].bypassed = bypassed;
    refreshRow (slot);
    if (onSlotBypass_)
        onSlotBypass_ (slot, bypassed);
}

void InsertChainPopup::rebuildRows()
{
    rows_.clear();
    rows_.reserve (EffectChain::kMaxSlots);
    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
    {
        auto row = std::make_unique<SlotRow> (*this, i);
        addAndMakeVisible (*row);
        row->syncFromState (slots_[i]);
        rows_.push_back (std::move (row));
    }
}

void InsertChainPopup::refreshRow (std::size_t slot)
{
    if (slot >= rows_.size())
        return;
    rows_[slot]->syncFromState (slots_[slot]);
}

} // namespace ida
