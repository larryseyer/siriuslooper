#pragma once

#include "CompactFaderStrip.h"
#include "ChannelDetailPanWidTab.h"
#include "ida/ChannelDetailCMPTab.h"
#include "ida/ChannelDetailEQTab.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace ida::ui
{

// =============================================================================
// FxReturnInfo + Sends tab listener
// =============================================================================

/// One FX-return entry displayed in the Sends tab. Index in the surrounding
/// `std::vector<FxReturnInfo>` is the `fxReturnIdx` the listener callbacks
/// reference. The host pane translates that index back to a `BusId` when
/// calling into the engine.
struct FxReturnInfo
{
    juce::String  name;
    juce::Colour  color;
};

class ChannelDetailSendsTabListener
{
public:
    virtual ~ChannelDetailSendsTabListener() = default;

    /// Operator moved the send knob for the (currently-bound channel, FX return)
    /// pair. `level` is linear 0..1.
    virtual void sendsTabSendChanged (int fxReturnIdx, float level)
    { juce::ignoreUnused (fxReturnIdx, level); }

    /// Operator toggled the per-channel pre/post-fader sends flag.
    virtual void sendsTabPreFaderToggled (bool preFader)
    { juce::ignoreUnused (preFader); }
};

// =============================================================================
// ChannelDetailSendsTab — IDA-native, dynamic N FX returns
// =============================================================================
//
// One card per FX-return entry, plus a single per-channel "PRE FADER" toggle
// at the top. The host pane refreshes the displayed state on every channel
// selection change (and whenever the FX-return roster mutates) by calling
// `setChannelState`. The tab is mixer-agnostic — both InputMixerPane and
// OutputMixerPane host their own instance via `ida::ui::ChannelDetail`.
//
// **Differs from OTTO's ChannelDetailSendsTab** in two load-bearing ways:
//   1. OTTO assumes a fixed `kNumFxReturns = mixer::kNumFxReturns` (4) with
//      reverb/delay preset categories baked in; IDA's FX returns are operator-
//      created at arbitrary count and have no preset model yet.
//   2. OTTO's tab depends on `otto::presets::PresetManager`; IDA has no
//      equivalent yet (preset surface is a future slice).
//
// Both differences make the OTTO component impossible to embed unmodified.
//
class ChannelDetailSendsTab : public juce::Component
{
public:
    /// What the tab displays for the currently-selected channel. The host pane
    /// recomputes this whenever the channel selection changes or the FX-return
    /// roster mutates.
    struct ChannelState
    {
        std::vector<FxReturnInfo> fxReturns;
        /// Parallel to fxReturns; linear 0..1 send level into each FX return.
        std::vector<float>        sendLevels;
        /// Per-channel pre-fader sends flag (E2 — single toggle, all sends share).
        bool                      preFaderSends { false };
    };

    ChannelDetailSendsTab();
    ~ChannelDetailSendsTab() override;

    /// Refresh the tab's display from `state`. Rebuilds the card row only when
    /// the FX-return count changes; otherwise just pushes new values into the
    /// existing controls (no allocation, no repaint thrash).
    void setChannelState (const ChannelState& state);

    /// Drop the row entirely (no channel selected). The tab paints an empty
    /// placeholder hint instead of stale cards.
    void clearChannelState();

    void addListener    (ChannelDetailSendsTabListener* l);
    void removeListener (ChannelDetailSendsTabListener* l);

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    /// One FX-return card: name label + rotary knob + dB readout.
    struct SendCard
    {
        std::unique_ptr<juce::Label>  nameLabel;
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::Label>  dbReadout;
    };

    void rebuildCards (std::size_t fxReturnCount);
    void updateDbReadout (std::size_t cardIdx);
    void notifySendChanged (std::size_t cardIdx);
    void notifyPreFaderToggled();

    // Layout constants chosen to match OTTO's ChannelDetailPanWidTab visual
    // idiom (slice EC-Polish): borderless rotary, knob centered in its
    // column, label directly below, dB readout below the label. Knob sizing
    // follows PanWid (kMinKnobSize=60 / kMaxKnobSize=500) so columns scale
    // smoothly from iPhone to desktop.
    static constexpr int kColumnGap        = 16;     // matches PanWid kKnobGap
    static constexpr int kNameLabelHeight  = 20;     // matches PanWid kLabelHeight
    static constexpr int kDbReadoutHeight  = 16;
    static constexpr int kPreToggleHeight  = 24;
    static constexpr int kMinKnobSize      = 60;     // matches PanWid kMinKnobSize
    static constexpr int kMaxKnobSize      = 500;    // matches PanWid kMaxKnobSize

    std::vector<SendCard>           cards_;
    std::vector<juce::Colour>       cardColors_;   // parallel to cards_; cached for paint()
    std::unique_ptr<juce::ToggleButton> preFaderToggle_;
    bool                            hasChannelBound_ { false };
    juce::ListenerList<ChannelDetailSendsTabListener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelDetailSendsTab)
};

// =============================================================================
// ChannelDetailPlaceholderTab — explicit "wiring lands later" card
// =============================================================================
//
// Used for the EQ and CMP tabs in slice U. Per `feedback_sirius_done_right_and_complete`
// we don't ship half-baked controls — instead this paints an explicit
// "wiring lands with insert-chain UI (P7)" message so the operator knows the
// tab is reserved, not broken.
//
class ChannelDetailPlaceholderTab : public juce::Component
{
public:
    explicit ChannelDetailPlaceholderTab (juce::String message);
    void paint (juce::Graphics& g) override;

private:
    juce::String message_;
};

// =============================================================================
// ChannelDetail — tabbed wrapper, OTTO visual reference
// =============================================================================

class ChannelDetailListener
{
public:
    virtual ~ChannelDetailListener() = default;
    virtual void channelDetailTabChanged (int tabIndex) { juce::ignoreUnused (tabIndex); }
};

class ChannelDetail : public juce::Component
{
public:
    enum Tab { PanWid = 0, Sends, EQ, CMP, NumTabs };

    static constexpr int kTabBarHeight = 32;
    static constexpr int kTabMinWidth  = 56;
    static constexpr int kPadding      = 4;

    ChannelDetail();
    ~ChannelDetail() override;

    /// Forwarded to the Pan/Width tab — keeps the existing pane wiring working.
    void setChannel       (int channelIndex, otto::ui::ChannelType type);
    void setChannelColor  (juce::Colour color);

    /// Active tab index. Defaults to PanWid on construction.
    void setActiveTab (Tab tab);
    Tab  getActiveTab() const noexcept { return activeTab_; }

    /// Set which tabs are visible in the tab bar. Tabs not in the mask are
    /// hidden from the bar (and their content suppressed). If the currently
    /// active tab becomes unavailable, falls through to the first available
    /// tab in PanWid → Sends → EQ → CMP order. Bus/FX strips use this to
    /// hide PanWid + Sends (stereo bus: no pan; no bus-side send model yet).
    struct TabMask
    {
        bool panWid { true };
        bool sends  { true };
        bool eq     { true };
        bool cmp    { true };

        bool contains (Tab t) const noexcept
        {
            switch (t)
            {
                case Tab::PanWid:  return panWid;
                case Tab::Sends:   return sends;
                case Tab::EQ:      return eq;
                case Tab::CMP:     return cmp;
                case Tab::NumTabs: break;
            }
            return false;
        }
    };
    void setTabsAvailable (TabMask mask);
    TabMask getTabsAvailable() const noexcept { return tabMask_; }

    /// Tab content accessors — host panes wire their own listeners directly
    /// onto the underlying tabs. Returned references stay valid for the lifetime
    /// of the ChannelDetail.
    otto::ui::ChannelDetailPanWidTab& panWidTab() noexcept { return *panWidTab_; }
    ChannelDetailSendsTab&            sendsTab()  noexcept { return *sendsTab_;  }
    ChannelDetailEQTab&               eqTab()     noexcept { return *eqTab_;     }
    ChannelDetailCMPTab&              cmpTab()    noexcept { return *cmpTab_;    }

    void addListener    (ChannelDetailListener* l);
    void removeListener (ChannelDetailListener* l);

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    class TabButton;

    void rebuildTabBar();
    void notifyTabChanged();

    Tab                          activeTab_ { Tab::PanWid };
    TabMask                      tabMask_   {};   // all visible by default
    juce::OwnedArray<TabButton>  tabButtons_;

    std::unique_ptr<otto::ui::ChannelDetailPanWidTab> panWidTab_;
    std::unique_ptr<ChannelDetailSendsTab>            sendsTab_;
    std::unique_ptr<ChannelDetailEQTab>               eqTab_;
    std::unique_ptr<ChannelDetailCMPTab>              cmpTab_;

    juce::ListenerList<ChannelDetailListener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelDetail)
};

} // namespace ida::ui
