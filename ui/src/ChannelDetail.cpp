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

    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        cards_[i].nameLabel->setText (state.fxReturns[i].name, juce::dontSendNotification);
        cardColors_[i] = state.fxReturns[i].color;

        const float level = i < state.sendLevels.size() ? state.sendLevels[i] : 0.0f;
        cards_[i].knob->setValue (level, juce::dontSendNotification);
        cards_[i].knob->getProperties().set ("fillColor",
                                             state.fxReturns[i].color.toString());
        updateDbReadout (i);
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
        return;
    }

    auto bounds = getLocalBounds();
    bounds.removeFromTop (kPreToggleHeight + kCardGap);

    const int totalGap = kCardGap * (static_cast<int> (cards_.size()) - 1);
    const int cardW = (bounds.getWidth() - totalGap) / static_cast<int> (cards_.size());

    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        const int x = bounds.getX() + static_cast<int> (i) * (cardW + kCardGap);
        auto card = juce::Rectangle<int> (x, bounds.getY(), cardW, bounds.getHeight()).toFloat();

        g.setColour (otto::Colours::bg3);
        g.fillRoundedRectangle (card, static_cast<float> (kCardCornerRadius));
        g.setColour (i < cardColors_.size() ? cardColors_[i] : otto::Colours::bg5);
        g.drawRoundedRectangle (card, static_cast<float> (kCardCornerRadius), 1.0f);
    }
}

void ChannelDetailSendsTab::resized()
{
    auto bounds = getLocalBounds();

    // Pre-fader toggle takes a strip across the top.
    preFaderToggle_->setBounds (bounds.removeFromTop (kPreToggleHeight));
    bounds.removeFromTop (kCardGap);

    if (cards_.empty()) return;

    const int totalGap = kCardGap * (static_cast<int> (cards_.size()) - 1);
    const int cardW = (bounds.getWidth() - totalGap) / static_cast<int> (cards_.size());

    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        const int x = bounds.getX() + static_cast<int> (i) * (cardW + kCardGap);
        auto card = juce::Rectangle<int> (x, bounds.getY(), cardW, bounds.getHeight())
                       .reduced (kCardPadding);

        auto nameRow = card.removeFromTop (kNameLabelHeight);
        card.removeFromTop (kCardGap / 2);
        auto dbRow   = card.removeFromBottom (kDbReadoutHeight);
        card.removeFromBottom (kCardGap / 2);

        const int knobSide = juce::jmax (kMinKnobSize,
                                         juce::jmin (card.getWidth(), card.getHeight()));
        auto knobBounds = juce::Rectangle<int> (0, 0, knobSide, knobSide)
                              .withCentre (card.getCentre());

        cards_[i].nameLabel->setBounds (nameRow);
        cards_[i].knob->setBounds (knobBounds);
        cards_[i].dbReadout->setBounds (dbRow);
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
    cards_[cardIdx].dbReadout->setText (formatDb (level), juce::dontSendNotification);
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
