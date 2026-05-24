#pragma once

#include "ida/CmpMeterView.h"
#include "ida/InternalFxConfigs.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace ida::ui
{

class ChannelDetailCMPTabListener
{
public:
    virtual ~ChannelDetailCMPTabListener() = default;

    virtual void cmpTabConfigChanged (const CmpConfig& /*cfg*/) {}
    virtual void cmpTabRequestSlotAdd() {}
};

/// IDA-native compressor parameter editor — slice EC. Mirrors
/// `ChannelDetailEQTab` in structure (enable + "no slot" empty state +
/// editor when bound). Six rotary knobs (threshold, ratio, attack,
/// release, makeup, mix) + two toggles (enable, sidechain-HPF).
class ChannelDetailCMPTab : public juce::Component,
                            public CmpMeterView::Listener
{
public:
    struct ChannelState
    {
        CmpConfig config;
        bool      hasCmpSlot { false };
    };

    ChannelDetailCMPTab();
    ~ChannelDetailCMPTab() override;

    void setChannelState   (const ChannelState& state);
    void clearChannelState();

    /// True iff a CMP slot is wired on the currently-bound channel.
    bool hasCmpSlot() const noexcept { return hasCmpSlot_; }

    void addListener    (ChannelDetailCMPTabListener* l);
    void removeListener (ChannelDetailCMPTabListener* l);

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    enum KnobIndex : int { kThreshold = 0, kRatio, kAttack, kRelease, kMakeup, kMix, kKnobCount };

    struct KnobRow
    {
        std::unique_ptr<juce::Label>  title;
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::Label>  readout;
    };

    void buildKnobs();
    void pushConfigToControls (const CmpConfig& cfg);
    void publishCurrentConfig();
    void updateReadouts();

    std::array<KnobRow, kKnobCount>                knobs_;
    std::unique_ptr<juce::ToggleButton>            enableToggle_;
    std::unique_ptr<juce::ToggleButton>            sidechainHpfToggle_;
    std::unique_ptr<juce::TextButton>              addSlotButton_;
    std::unique_ptr<CmpMeterView>                  meterView_;

    void cmpViewConfigChanged (const CmpConfig& cfg) override;

    CmpConfig   cachedConfig_ {};
    bool        hasChannelBound_ { false };
    bool        hasCmpSlot_      { false };
    bool        suppressNotify_  { false };

    juce::ListenerList<ChannelDetailCMPTabListener> listeners_;

    static constexpr int kEnableToggleHeight = 24;
    static constexpr int kSidechainHeight    = 24;
    static constexpr int kAddButtonHeight    = 48;
    static constexpr int kKnobTitleHeight    = 16;
    static constexpr int kReadoutHeight      = 14;
    static constexpr int kKnobMinSize        = 36;
    static constexpr int kSidechainToggleW   = 160;
    static constexpr int kSidechainToggleH   = 32;
    static constexpr int kColGap             = 6;
    static constexpr int kRowGap             = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelDetailCMPTab)
};

} // namespace ida::ui
