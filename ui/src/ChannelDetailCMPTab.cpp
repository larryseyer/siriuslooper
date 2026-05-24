#include "ida/ChannelDetailCMPTab.h"

#include "OTTOColours.h"

namespace ida::ui
{

namespace
{

juce::String formatDbValue (float db, int dp = 1)
{
    return juce::String (db, dp) + " dB";
}

juce::String formatRatio (float ratio)
{
    return juce::String (ratio, 1) + ":1";
}

juce::String formatMs (float ms)
{
    if (ms >= 100.0f) return juce::String (static_cast<int> (ms)) + " ms";
    return juce::String (ms, 1) + " ms";
}

juce::String formatMix (float mix)
{
    return juce::String (static_cast<int> (mix * 100.0f + 0.5f)) + "%";
}

} // namespace

ChannelDetailCMPTab::ChannelDetailCMPTab()
{
    enableToggle_ = std::make_unique<juce::ToggleButton> ("ENABLE");
    enableToggle_->setColour (juce::ToggleButton::textColourId, otto::Colours::textPrimary);
    enableToggle_->onClick = [this]
    {
        if (suppressNotify_) return;
        publishCurrentConfig();
    };
    addAndMakeVisible (*enableToggle_);

    sidechainHpfToggle_ = std::make_unique<juce::ToggleButton> ("SIDECHAIN HPF (100 Hz)");
    sidechainHpfToggle_->setColour (juce::ToggleButton::textColourId, otto::Colours::textSecondary);
    sidechainHpfToggle_->onClick = [this]
    {
        if (suppressNotify_) return;
        publishCurrentConfig();
    };
    addChildComponent (*sidechainHpfToggle_);

    addSlotButton_ = std::make_unique<juce::TextButton> ("+ Add CMP to this strip");
    addSlotButton_->setColour (juce::TextButton::buttonColourId, otto::Colours::bg3);
    addSlotButton_->setColour (juce::TextButton::textColourOffId, otto::Colours::textPrimary);
    addSlotButton_->onClick = [this]
    {
        listeners_.call ([] (ChannelDetailCMPTabListener& l) { l.cmpTabRequestSlotAdd(); });
    };
    addChildComponent (*addSlotButton_);

    // Slice EC-Polish: transfer-curve + gain-reduction meter view.
    meterView_ = std::make_unique<CmpMeterView>();
    meterView_->addListener (this);
    addChildComponent (*meterView_);

    buildKnobs();
}

ChannelDetailCMPTab::~ChannelDetailCMPTab() = default;

void ChannelDetailCMPTab::buildKnobs()
{
    static const char* kTitles[kKnobCount] = { "THRESH", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX" };

    for (int k = 0; k < kKnobCount; ++k)
    {
        auto& row = knobs_[static_cast<std::size_t> (k)];

        row.title = std::make_unique<juce::Label> ("", kTitles[k]);
        row.title->setJustificationType (juce::Justification::centred);
        row.title->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        row.title->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        row.title->setInterceptsMouseClicks (false, false);
        addChildComponent (*row.title);

        row.knob = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                    juce::Slider::NoTextBox);
        row.knob->onValueChange = [this] { if (! suppressNotify_) publishCurrentConfig(); };
        addChildComponent (*row.knob);

        row.readout = std::make_unique<juce::Label>();
        row.readout->setJustificationType (juce::Justification::centred);
        row.readout->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        row.readout->setFont (juce::FontOptions (10.0f));
        row.readout->setInterceptsMouseClicks (false, false);
        addChildComponent (*row.readout);
    }

    // Ranges per PlayerEffects.h:168-174.
    knobs_[kThreshold].knob->setRange (-60.0,    0.0, 0.1);
    knobs_[kRatio]    .knob->setRange (1.0,     20.0, 0.1);
    knobs_[kAttack]   .knob->setRange (0.1,    100.0, 0.1);
    knobs_[kRelease]  .knob->setRange (10.0, 1000.0, 1.0);
    knobs_[kMakeup]   .knob->setRange (0.0,    24.0, 0.1);
    knobs_[kMix]      .knob->setRange (0.0,     1.0, 0.01);
}

void ChannelDetailCMPTab::setChannelState (const ChannelState& state)
{
    hasChannelBound_ = true;
    hasCmpSlot_      = state.hasCmpSlot;
    cachedConfig_    = state.config;

    if (hasCmpSlot_)
        pushConfigToControls (cachedConfig_);

    enableToggle_->setVisible       (hasCmpSlot_);
    sidechainHpfToggle_->setVisible (hasCmpSlot_);
    addSlotButton_->setVisible      (! hasCmpSlot_);
    if (meterView_) meterView_->setVisible (hasCmpSlot_);
    for (auto& row : knobs_)
    {
        if (row.title)   row.title  ->setVisible (hasCmpSlot_);
        if (row.knob)    row.knob   ->setVisible (hasCmpSlot_);
        if (row.readout) row.readout->setVisible (hasCmpSlot_);
    }

    resized();
    repaint();
}

void ChannelDetailCMPTab::clearChannelState()
{
    hasChannelBound_ = false;
    hasCmpSlot_      = false;
    enableToggle_->setVisible (false);
    sidechainHpfToggle_->setVisible (false);
    addSlotButton_->setVisible (false);
    if (meterView_) meterView_->setVisible (false);
    for (auto& row : knobs_)
    {
        if (row.title)   row.title  ->setVisible (false);
        if (row.knob)    row.knob   ->setVisible (false);
        if (row.readout) row.readout->setVisible (false);
    }
    repaint();
}

void ChannelDetailCMPTab::pushConfigToControls (const CmpConfig& cfg)
{
    suppressNotify_ = true;

    enableToggle_->setToggleState       (cfg.enabled,      juce::dontSendNotification);
    sidechainHpfToggle_->setToggleState (cfg.sidechainHpf, juce::dontSendNotification);

    knobs_[kThreshold].knob->setValue (cfg.threshold, juce::dontSendNotification);
    knobs_[kRatio]    .knob->setValue (cfg.ratio,     juce::dontSendNotification);
    knobs_[kAttack]   .knob->setValue (cfg.attackMs,  juce::dontSendNotification);
    knobs_[kRelease]  .knob->setValue (cfg.releaseMs, juce::dontSendNotification);
    knobs_[kMakeup]   .knob->setValue (cfg.makeupDb,  juce::dontSendNotification);
    knobs_[kMix]      .knob->setValue (cfg.mix,       juce::dontSendNotification);

    updateReadouts();
    if (meterView_) meterView_->setConfig (cfg);
    suppressNotify_ = false;
}

void ChannelDetailCMPTab::cmpViewConfigChanged (const CmpConfig& cfg)
{
    // Meter-view drag pushed threshold or ratio. Sync to cached state +
    // knob row, then republish to the host pane. suppressNotify_ blocks
    // the knob's own onValueChange to prevent loops.
    cachedConfig_ = cfg;

    suppressNotify_ = true;
    knobs_[kThreshold].knob->setValue (cfg.threshold, juce::dontSendNotification);
    knobs_[kRatio]    .knob->setValue (cfg.ratio,     juce::dontSendNotification);
    updateReadouts();
    suppressNotify_ = false;

    listeners_.call ([&cfg] (ChannelDetailCMPTabListener& l)
                     { l.cmpTabConfigChanged (cfg); });
}

void ChannelDetailCMPTab::publishCurrentConfig()
{
    cachedConfig_.enabled      = enableToggle_->getToggleState();
    cachedConfig_.sidechainHpf = sidechainHpfToggle_->getToggleState();
    cachedConfig_.threshold    = static_cast<float> (knobs_[kThreshold].knob->getValue());
    cachedConfig_.ratio        = static_cast<float> (knobs_[kRatio].knob->getValue());
    cachedConfig_.attackMs     = static_cast<float> (knobs_[kAttack].knob->getValue());
    cachedConfig_.releaseMs    = static_cast<float> (knobs_[kRelease].knob->getValue());
    cachedConfig_.makeupDb     = static_cast<float> (knobs_[kMakeup].knob->getValue());
    cachedConfig_.mix          = static_cast<float> (knobs_[kMix].knob->getValue());

    updateReadouts();
    if (meterView_) meterView_->setConfig (cachedConfig_);

    const CmpConfig snapshot = cachedConfig_;
    listeners_.call ([&snapshot] (ChannelDetailCMPTabListener& l)
                     { l.cmpTabConfigChanged (snapshot); });
}

void ChannelDetailCMPTab::updateReadouts()
{
    knobs_[kThreshold].readout->setText (formatDbValue (cachedConfig_.threshold),    juce::dontSendNotification);
    knobs_[kRatio]    .readout->setText (formatRatio   (cachedConfig_.ratio),        juce::dontSendNotification);
    knobs_[kAttack]   .readout->setText (formatMs      (cachedConfig_.attackMs),     juce::dontSendNotification);
    knobs_[kRelease]  .readout->setText (formatMs      (cachedConfig_.releaseMs),    juce::dontSendNotification);
    knobs_[kMakeup]   .readout->setText (formatDbValue (cachedConfig_.makeupDb),     juce::dontSendNotification);
    knobs_[kMix]      .readout->setText (formatMix     (cachedConfig_.mix),          juce::dontSendNotification);
}

void ChannelDetailCMPTab::addListener    (ChannelDetailCMPTabListener* l) { listeners_.add (l); }
void ChannelDetailCMPTab::removeListener (ChannelDetailCMPTabListener* l) { listeners_.remove (l); }

void ChannelDetailCMPTab::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);

    if (! hasChannelBound_)
    {
        g.setColour (otto::Colours::textDisabled);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select a channel to edit dynamics",
                    getLocalBounds().toFloat(), juce::Justification::centred);
    }
}

void ChannelDetailCMPTab::resized()
{
    auto bounds = getLocalBounds().reduced (4);

    if (! hasChannelBound_) return;

    if (! hasCmpSlot_)
    {
        auto buttonBounds = bounds.withSizeKeepingCentre (260, kAddButtonHeight);
        addSlotButton_->setBounds (buttonBounds);
        return;
    }

    // Top row: ENABLE on the left, SIDECHAIN HPF on the right.
    auto topRow = bounds.removeFromTop (kEnableToggleHeight);
    enableToggle_->setBounds       (topRow.removeFromLeft  (90));
    sidechainHpfToggle_->setBounds (topRow.removeFromRight (200));
    bounds.removeFromTop (kRowGap);

    // Meter view dominates when there's room (full-screen tab mode); in
    // the small detail-band mode the knob row gets the height instead.
    const bool showMeter = (meterView_ != nullptr) && (bounds.getHeight() > 240);
    if (meterView_) meterView_->setVisible (showMeter);
    if (showMeter)
    {
        const int knobRowH = juce::jlimit (130, 200, bounds.getHeight() * 35 / 100);
        const int meterH   = bounds.getHeight() - knobRowH - kRowGap;
        meterView_->setBounds (bounds.removeFromTop (meterH));
        bounds.removeFromTop (kRowGap);
    }

    // 6 equal-width knob columns.
    const int totalGap = kColGap * (kKnobCount - 1);
    const int colW = (bounds.getWidth() - totalGap) / kKnobCount;
    for (int k = 0; k < kKnobCount; ++k)
    {
        auto& row = knobs_[static_cast<std::size_t> (k)];
        const int x = bounds.getX() + k * (colW + kColGap);
        auto colBounds = juce::Rectangle<int> (x, bounds.getY(), colW, bounds.getHeight());

        row.title  ->setBounds (colBounds.removeFromTop    (kKnobTitleHeight));
        row.readout->setBounds (colBounds.removeFromBottom (kReadoutHeight));
        const int knobSide = juce::jmax (kKnobMinSize,
                                          juce::jmin (colBounds.getWidth(), colBounds.getHeight()));
        auto knob = juce::Rectangle<int> (0, 0, knobSide, knobSide)
                       .withCentre (colBounds.getCentre());
        row.knob->setBounds (knob);
    }
}

} // namespace ida::ui
