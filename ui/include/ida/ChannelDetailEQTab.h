#pragma once

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

/// IDA-native EQ parameter editor — slice EC. One instance per
/// `ChannelDetail`; both InputMixerPane and OutputMixerPane host an
/// identical component per the parity invariant in
/// `project_two_mixers_totally_separate`. Visual idiom mirrors
/// `ChannelDetailSendsTab` (cards + central knob + dB readout) but the
/// payload here is a structured 5-band EQ panel instead of dynamic-N
/// FX-return sends.
///
/// Three display states:
///   1. No channel bound        — "Select a channel" hint.
///   2. Channel bound, no slot  — big centered "+ Add EQ" button that
///                                fires `eqTabRequestSlotAdd`.
///   3. Channel bound + slot    — full 5-band editor.
class ChannelDetailEQTab : public juce::Component
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

    EqConfig    cachedConfig_ {};
    bool        hasChannelBound_ { false };
    bool        hasEqSlot_       { false };
    bool        suppressNotify_  { false }; ///< true while pushConfigToControls runs

    juce::ListenerList<ChannelDetailEQTabListener> listeners_;

    static constexpr int kEnableToggleHeight = 24;
    static constexpr int kAddButtonHeight    = 48;
    static constexpr int kBandTitleHeight    = 18;
    static constexpr int kReadoutHeight      = 14;
    static constexpr int kSlopeToggleHeight  = 18;
    static constexpr int kKnobMinSize        = 36;
    static constexpr int kBandGap            = 6;
    static constexpr int kRowGap             = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelDetailEQTab)
};

} // namespace ida::ui
