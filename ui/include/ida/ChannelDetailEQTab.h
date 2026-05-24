#pragma once

#include "ida/EqCurveView.h"
#include "ida/InternalFxConfigs.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace ida::ui
{

class ChannelDetailEQTabListener
{
public:
    virtual ~ChannelDetailEQTabListener() = default;

    /// One of the EQ controls changed — the tab posts the entire
    /// `EqConfig` (not a per-control delta) so the host pane can route
    /// straight into `setInternalEqConfigAt` without state reconstruction.
    virtual void eqTabConfigChanged (const EqConfig& /*cfg*/) {}

    /// Operator tapped the "+ Add EQ" button in the empty-state surface.
    /// The host pane responds by appending an EQ slot to the channel's
    /// `EffectChain` via the standard setEffectChain bracket pattern.
    virtual void eqTabRequestSlotAdd() {}
};

/// IDA-native EQ parameter editor — slice EC + EC-Polish.
///
/// Layout mirrors OTTO's `EQPanel` byte-for-byte at the operator-facing
/// level: ENABLE toggle (top), curve view (dominant), band-selector row
/// of 5 buttons (HP / Low / Mid / High / LP) below the curve, and a
/// contextual control row for the SELECTED band only (HP / LP: slope
/// buttons 6 / 12 / 18 / 24; Low / Mid / High: freq / gain / Q knobs).
/// IDA does not embed OTTO's `EQPanel.cpp` (which depends on
/// PresetManager + SpectrumDisplay + binding-adapter base classes
/// that IDA hasn't grown yet) — we replicate the visual idiom against
/// IDA's existing EqAdapter so adapters propagate cleanly through the
/// host's typed setter.
///
/// Three display states:
///   1. No channel bound        — "Select a channel" hint.
///   2. Channel bound, no slot  — big centered "+ Add EQ" button that
///                                fires `eqTabRequestSlotAdd`.
///   3. Channel bound + slot    — full editor with the OTTO layout.
class ChannelDetailEQTab : public juce::Component,
                           public EqCurveView::Listener
{
public:
    struct ChannelState
    {
        EqConfig config;
        bool     hasEqSlot { false };
    };

    ChannelDetailEQTab();
    ~ChannelDetailEQTab() override;

    void setChannelState   (const ChannelState& state);
    void clearChannelState();

    /// True iff an EQ slot is wired on the currently-bound channel (i.e.
    /// the tab is showing the band editor, not the "+ Add EQ" empty state).
    /// Used by the host pane to gate full-screen layout — empty-state
    /// shouldn't full-screen because the operator would be stranded.
    bool hasEqSlot() const noexcept { return hasEqSlot_; }

    void addListener    (ChannelDetailEQTabListener* l);
    void removeListener (ChannelDetailEQTabListener* l);

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    /// One vertical band column. Some bands have only a frequency
    /// knob + slope toggle (HP, LP); shelves have gain + freq + Q.
    struct BandColumn
    {
        std::unique_ptr<juce::Label>          title;
        std::unique_ptr<juce::Slider>         gain;          ///< nullptr on HP/LP
        std::unique_ptr<juce::Slider>         freq;
        std::unique_ptr<juce::Slider>         q;             ///< nullptr on HP/LP
        std::unique_ptr<juce::TextButton>     slopeToggle;   ///< only on HP/LP, "12 dB" / "24 dB"
        std::unique_ptr<juce::Label>          readoutFreq;
        std::unique_ptr<juce::Label>          readoutGain;   ///< nullptr on HP/LP
        std::unique_ptr<juce::Label>          readoutQ;      ///< nullptr on HP/LP
    };

    void buildColumns();
    void pushConfigToControls (const EqConfig& cfg);
    void publishCurrentConfig();
    void updateReadouts();

    enum BandIndex : int { kHP = 0, kLow, kMid, kHigh, kLP, kBandCount };

    std::array<BandColumn, kBandCount>             bands_;
    std::unique_ptr<juce::ToggleButton>            enableToggle_;
    std::unique_ptr<juce::TextButton>              addSlotButton_;
    std::unique_ptr<EqCurveView>                   curveView_;

    /// Band-selector row buttons (HP / Low / Mid / High / LP). Radio-grouped
    /// so exactly one is "on" at a time — mirrors OTTO's `drawBandSelector`.
    std::array<std::unique_ptr<juce::TextButton>, kBandCount> bandButtons_;

    /// HP / LP slope-button row (6 / 12 / 18 / 24 dB-per-oct). Visible only
    /// when the selected band is HP or LP. The fifth element is a Bypass
    /// toggle for the selected band (mirrors OTTO's slope row).
    std::array<std::unique_ptr<juce::TextButton>, 4> slopeButtons_;
    std::unique_ptr<juce::TextButton>               bypassBandButton_;

    /// Currently selected band. Drives which contextual controls are visible
    /// in the row below the band selector. Defaults to HP (matches OTTO).
    int                                            selectedBand_ { kHP };
    void setSelectedBand (int band);
    void layoutContextualRow (juce::Rectangle<int> row);

    // EqCurveView::Listener — the curve view drives operator gestures on the
    // graphical surface; this callback unifies them with knob-driven edits.
    void eqCurveConfigChanged (const EqConfig& cfg) override;

    EqConfig    cachedConfig_ {};
    bool        hasChannelBound_ { false };
    bool        hasEqSlot_       { false };
    bool        suppressNotify_  { false }; ///< true while pushConfigToControls runs

    juce::ListenerList<ChannelDetailEQTabListener> listeners_;

    static constexpr int kEnableToggleHeight = 24;
    static constexpr int kAddButtonHeight    = 48;
    static constexpr int kBandSelectorHeight = 36;   // matches OTTO band-button row
    static constexpr int kSlopeButtonHeight  = 32;
    static constexpr int kBandTitleHeight    = 18;
    static constexpr int kReadoutHeight      = 14;
    static constexpr int kKnobMinSize        = 36;
    static constexpr int kBandGap            = 6;
    static constexpr int kRowGap             = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelDetailEQTab)
};

} // namespace ida::ui
