#include "ida/ChannelDetail.h"

#include "OTTOColours.h"

#include <cmath>

namespace ida::ui
{

// =============================================================================
// Helpers
// =============================================================================

namespace
{

juce::String formatDb (float linear01)
{
    if (linear01 <= 0.0001f)
        return "-" + juce::String::charToString (0x221E) + " dB";   // -∞ dB
    const float db = 20.0f * std::log10 (linear01);
    return juce::String (db, 1) + " dB";
}

} // namespace

// =============================================================================
// ChannelDetailSendsTab
// =============================================================================

ChannelDetailSendsTab::ChannelDetailSendsTab()
{
    preFaderToggle_ = std::make_unique<juce::ToggleButton> ("PRE FADER");
    preFaderToggle_->setColour (juce::ToggleButton::textColourId, otto::Colours::textPrimary);
    preFaderToggle_->onClick = [this] { notifyPreFaderToggled(); };
    addAndMakeVisible (*preFaderToggle_);
}

ChannelDetailSendsTab::~ChannelDetailSendsTab() = default;

void ChannelDetailSendsTab::setChannelState (const ChannelState& state)
{
    hasChannelBound_ = true;

    if (cards_.size() != state.fxReturns.size())
        rebuildCards (state.fxReturns.size());

    cardColors_.assign (state.fxReturns.size(), juce::Colour {});
    cardNames_.assign (state.fxReturns.size(), juce::String {});

    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        cardNames_[i]  = state.fxReturns[i].name;
        cardColors_[i] = state.fxReturns[i].color;

        const float level = i < state.sendLevels.size() ? state.sendLevels[i] : 0.0f;
        cards_[i].knob->setValue (level, juce::dontSendNotification);
        cards_[i].knob->getProperties().set ("fillColor",
                                             state.fxReturns[i].color.toString());
        updateDbReadout (i);    // composes "FX N  -∞ dB" into nameLabel
    }

    preFaderToggle_->setToggleState (state.preFaderSends, juce::dontSendNotification);
    preFaderToggle_->setVisible (true);

    resized();
    repaint();
}

void ChannelDetailSendsTab::clearChannelState()
{
    hasChannelBound_ = false;
    cards_.clear();
    cardColors_.clear();
    preFaderToggle_->setVisible (false);
    repaint();
}

void ChannelDetailSendsTab::addListener    (ChannelDetailSendsTabListener* l) { listeners_.add (l); }
void ChannelDetailSendsTab::removeListener (ChannelDetailSendsTabListener* l) { listeners_.remove (l); }

void ChannelDetailSendsTab::paint (juce::Graphics& g)
{
    // Slice EC-Polish: no card outlines / fills (operator: "sends knobs
    // should look like pan/width with no border"). The whole tab is a flat
    // bg2 surface; FX-return color lives on the knob's indicator arc, fed
    // via the "fillColor" property OTTOLookAndFeel reads in drawRotarySlider.
    g.fillAll (otto::Colours::bg2);

    if (! hasChannelBound_)
    {
        g.setColour (otto::Colours::textDisabled);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select a channel to edit sends",
                    getLocalBounds().toFloat(), juce::Justification::centred);
        return;
    }

    if (cards_.empty())
    {
        g.setColour (otto::Colours::textDisabled);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("No FX returns on this mixer — add one from the blank-area menu",
                    getLocalBounds().toFloat(), juce::Justification::centred);
    }
}

void ChannelDetailSendsTab::resized()
{
    auto bounds = getLocalBounds();

    // PRE FADER toggle: pinned to the top-LEFT corner, fixed footprint
    // (~120 × 22). The toggle no longer reserves a horizontal row across
    // the tab — slice EC-Polish-fix moves it inline so the knob column
    // layout uses the full pane height, matching OTTO's ChannelDetailPanWidTab
    // knob sizing per pixel.
    if (preFaderToggle_)
        preFaderToggle_->setBounds (bounds.getX() + 4, bounds.getY() + 4,
                                    140, kPreToggleHeight);

    if (cards_.empty()) return;

    // PanWid-style sizing: knob fills a (knobSide × knobSide) box centered
    // in its column; "FX N  −∞ dB" combined label sits directly below.
    // No card chrome, no extra reserved rows — knob area is the entire
    // bounds minus a single label-line at the bottom (matches OTTO's
    // PanWidTab math: `availH = bounds.getHeight() - kLabelHeight`).
    const int n = static_cast<int> (cards_.size());
    const int availH = bounds.getHeight() - kNameLabelHeight;
    const int availW = (bounds.getWidth() - kColumnGap * (n - 1)) / juce::jmax (1, n);
    const int knobSide = juce::jlimit (kMinKnobSize, kMaxKnobSize,
                                       juce::jmin (availH, availW));

    // Center the group horizontally + vertically (PanWid idiom).
    const int totalW = knobSide * n + kColumnGap * (n - 1);
    const int startX = bounds.getX() + juce::jmax (0, (bounds.getWidth()  - totalW) / 2);
    const int centerY = bounds.getY()
                      + juce::jmax (0, (bounds.getHeight()
                                        - (knobSide + kNameLabelHeight)) / 2);

    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        const int colX = startX + static_cast<int> (i) * (knobSide + kColumnGap);
        auto knobBounds = juce::Rectangle<int> (colX, centerY, knobSide, knobSide);
        auto labelRow   = juce::Rectangle<int> (colX, centerY + knobSide,
                                                knobSide, kNameLabelHeight);
        cards_[i].knob     ->setBounds (knobBounds);
        cards_[i].nameLabel->setBounds (labelRow);
        // dB readout sits ON the label row (the label paints centered text,
        // we add the dB suffix to the label itself; the standalone readout
        // label disappears off-screen). updateDbReadout pushes the combined
        // "FX N  -∞ dB" string into nameLabel each tick.
        if (cards_[i].dbReadout) cards_[i].dbReadout->setBounds (0, 0, 0, 0);
    }
}

void ChannelDetailSendsTab::rebuildCards (std::size_t fxReturnCount)
{
    cards_.clear();
    cards_.reserve (fxReturnCount);

    for (std::size_t i = 0; i < fxReturnCount; ++i)
    {
        SendCard card;
        card.nameLabel = std::make_unique<juce::Label>();
        card.nameLabel->setJustificationType (juce::Justification::centred);
        card.nameLabel->setColour (juce::Label::textColourId, otto::Colours::textPrimary);
        card.nameLabel->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        card.nameLabel->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*card.nameLabel);

        card.knob = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                    juce::Slider::NoTextBox);
        card.knob->setRange (0.0, 1.0, 0.01);
        card.knob->setValue (0.0);
        const std::size_t cardIdx = i;
        card.knob->onValueChange = [this, cardIdx]
        {
            notifySendChanged (cardIdx);
            updateDbReadout   (cardIdx);
        };
        addAndMakeVisible (*card.knob);

        card.dbReadout = std::make_unique<juce::Label> ("", formatDb (0.0f));
        card.dbReadout->setJustificationType (juce::Justification::centred);
        card.dbReadout->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        card.dbReadout->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*card.dbReadout);

        cards_.push_back (std::move (card));
    }
}

void ChannelDetailSendsTab::updateDbReadout (std::size_t cardIdx)
{
    if (cardIdx >= cards_.size()) return;
    const float level = static_cast<float> (cards_[cardIdx].knob->getValue());
    // Combine FX-return name + dB readout into one label so the layout
    // matches OTTO's PanWidTab (knob + single label below). Saves the
    // vertical real estate a dedicated readout row would consume.
    const auto& name = cardIdx < cardNames_.size() ? cardNames_[cardIdx] : juce::String();
    cards_[cardIdx].nameLabel->setText (name + "  " + formatDb (level),
                                        juce::dontSendNotification);
}

void ChannelDetailSendsTab::notifySendChanged (std::size_t cardIdx)
{
    if (cardIdx >= cards_.size()) return;
    const int   idx   = static_cast<int> (cardIdx);
    const float level = static_cast<float> (cards_[cardIdx].knob->getValue());
    listeners_.call ([idx, level] (ChannelDetailSendsTabListener& l)
                     { l.sendsTabSendChanged (idx, level); });
}

void ChannelDetailSendsTab::notifyPreFaderToggled()
{
    const bool pre = preFaderToggle_->getToggleState();
    listeners_.call ([pre] (ChannelDetailSendsTabListener& l)
                     { l.sendsTabPreFaderToggled (pre); });
}

// =============================================================================
// ChannelDetailPlaceholderTab
// =============================================================================

ChannelDetailPlaceholderTab::ChannelDetailPlaceholderTab (juce::String message)
    : message_ (std::move (message))
{
}

void ChannelDetailPlaceholderTab::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);
    g.setColour (otto::Colours::textDisabled);
    g.setFont (juce::FontOptions (14.0f).withStyle ("Italic"));
    g.drawFittedText (message_, getLocalBounds().reduced (16),
                      juce::Justification::centred, /*maxLines*/ 3);
}

// =============================================================================
// ChannelDetail::TabButton
// =============================================================================

class ChannelDetail::TabButton final : public juce::TextButton
{
public:
    TabButton (const juce::String& label, ChannelDetail::Tab tab, ChannelDetail& owner)
        : juce::TextButton (label)
        , owner_ (owner)
        , tab_   (tab)
    {
        setClickingTogglesState (true);
        setRadioGroupId (1);
        onClick = [this] { if (getToggleState()) owner_.setActiveTab (tab_); };
    }

private:
    ChannelDetail&     owner_;
    ChannelDetail::Tab tab_;
};

// =============================================================================
// ChannelDetail
// =============================================================================

ChannelDetail::ChannelDetail()
{
    panWidTab_ = std::make_unique<otto::ui::ChannelDetailPanWidTab>();
    sendsTab_  = std::make_unique<ChannelDetailSendsTab>();
    eqTab_     = std::make_unique<ChannelDetailEQTab>();
    cmpTab_    = std::make_unique<ChannelDetailCMPTab>();

    addChildComponent (*panWidTab_);
    addChildComponent (*sendsTab_);
    addChildComponent (*eqTab_);
    addChildComponent (*cmpTab_);

    rebuildTabBar();
    setActiveTab (Tab::PanWid);
}

ChannelDetail::~ChannelDetail() = default;

void ChannelDetail::setChannel (int channelIndex, otto::ui::ChannelType type)
{
    panWidTab_->setChannel (channelIndex, type);
}

void ChannelDetail::setChannelColor (juce::Colour color)
{
    panWidTab_->setChannelColor (color);
}

void ChannelDetail::setActiveTab (Tab tab)
{
    // Fall through to the first available tab if `tab` was hidden via
    // setTabsAvailable. Caller can't observe the corrected value except by
    // re-reading getActiveTab() after the call returns.
    if (! tabMask_.contains (tab))
    {
        constexpr Tab order[] = { Tab::PanWid, Tab::Sends, Tab::EQ, Tab::CMP };
        for (Tab candidate : order)
        {
            if (tabMask_.contains (candidate))
            {
                tab = candidate;
                break;
            }
        }
    }

    activeTab_ = tab;

    panWidTab_->setVisible (tab == Tab::PanWid);
    sendsTab_ ->setVisible (tab == Tab::Sends);
    eqTab_    ->setVisible (tab == Tab::EQ);
    cmpTab_   ->setVisible (tab == Tab::CMP);

    for (int i = 0; i < tabButtons_.size(); ++i)
        tabButtons_[i]->setToggleState (i == static_cast<int> (tab),
                                        juce::dontSendNotification);

    notifyTabChanged();
    resized();
}

void ChannelDetail::setTabsAvailable (TabMask mask)
{
    tabMask_ = mask;

    // Hide buttons for unavailable tabs.
    for (int i = 0; i < tabButtons_.size(); ++i)
    {
        const auto tab = static_cast<Tab> (i);
        tabButtons_[i]->setVisible (tabMask_.contains (tab));
    }

    // If the active tab is now hidden, fall through via setActiveTab.
    setActiveTab (activeTab_);
}

void ChannelDetail::addListener    (ChannelDetailListener* l) { listeners_.add (l); }
void ChannelDetail::removeListener (ChannelDetailListener* l) { listeners_.remove (l); }

void ChannelDetail::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);
}

void ChannelDetail::resized()
{
    auto bounds = getLocalBounds().reduced (kPadding);

    // Tab bar across the top.
    auto tabRow = bounds.removeFromTop (kTabBarHeight);
    const int tabCount = tabButtons_.size();
    if (tabCount > 0)
    {
        const int tabW = juce::jmax (kTabMinWidth, tabRow.getWidth() / tabCount);
        for (int i = 0; i < tabCount; ++i)
            tabButtons_[i]->setBounds (tabRow.removeFromLeft (tabW));
    }

    bounds.removeFromTop (kPadding);

    // The active tab fills the remainder.
    panWidTab_->setBounds (bounds);
    sendsTab_ ->setBounds (bounds);
    eqTab_    ->setBounds (bounds);
    cmpTab_   ->setBounds (bounds);
}

void ChannelDetail::rebuildTabBar()
{
    tabButtons_.clear();
    const char* labels[Tab::NumTabs] = { "Pan/Wid", "Sends", "EQ", "CMP" };
    for (int i = 0; i < Tab::NumTabs; ++i)
    {
        auto* btn = tabButtons_.add (new TabButton (labels[i],
                                                    static_cast<Tab> (i),
                                                    *this));
        addAndMakeVisible (btn);
    }
}

void ChannelDetail::notifyTabChanged()
{
    const int idx = static_cast<int> (activeTab_);
    listeners_.call ([idx] (ChannelDetailListener& l) { l.channelDetailTabChanged (idx); });
}

} // namespace ida::ui
