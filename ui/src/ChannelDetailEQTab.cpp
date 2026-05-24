#include "ida/ChannelDetailEQTab.h"

#include "OTTOColours.h"

namespace ida::ui
{

namespace
{

juce::String formatHz (float hz)
{
    if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + "k";
    return juce::String (static_cast<int> (hz)) + " Hz";
}

juce::String formatDbGain (float db)
{
    if (db > 0.0f) return "+" + juce::String (db, 1) + " dB";
    return juce::String (db, 1) + " dB";
}

juce::String formatQ (float q)
{
    return "Q " + juce::String (q, 2);
}

} // namespace

ChannelDetailEQTab::ChannelDetailEQTab()
{
    enableToggle_ = std::make_unique<juce::ToggleButton> ("EQ ENABLED");
    enableToggle_->setColour (juce::ToggleButton::textColourId, otto::Colours::textPrimary);
    enableToggle_->onClick = [this]
    {
        if (suppressNotify_) return;
        cachedConfig_.enabled = enableToggle_->getToggleState();
        publishCurrentConfig();
    };
    addAndMakeVisible (*enableToggle_);

    addSlotButton_ = std::make_unique<juce::TextButton> ("+ Add EQ to this strip");
    addSlotButton_->setColour (juce::TextButton::buttonColourId, otto::Colours::bg3);
    addSlotButton_->setColour (juce::TextButton::textColourOffId, otto::Colours::textPrimary);
    addSlotButton_->onClick = [this]
    {
        listeners_.call ([] (ChannelDetailEQTabListener& l) { l.eqTabRequestSlotAdd(); });
    };
    addChildComponent (*addSlotButton_);

    // Slice EC-Polish: graphical curve display — dominant element of the
    // tab once a slot is wired. Same response math as OTTO's EQPanel.
    curveView_ = std::make_unique<EqCurveView>();
    curveView_->addListener (this);
    addChildComponent (*curveView_);

    // Slice EC-Polish-fix: band selector row + contextual controls (OTTO
    // idiom). Exactly one band selector is "on" at a time — drives which
    // contextual controls appear in the row below.
    static const char* kBandLabels[kBandCount] = { "HP", "Low", "Mid", "High", "LP" };
    for (int b = 0; b < kBandCount; ++b)
    {
        auto btn = std::make_unique<juce::TextButton> (kBandLabels[b]);
        btn->setClickingTogglesState (true);
        btn->setRadioGroupId (1);
        btn->setColour (juce::TextButton::buttonColourId,   otto::Colours::bg3);
        btn->setColour (juce::TextButton::buttonOnColourId, otto::Colours::accent);
        btn->setColour (juce::TextButton::textColourOffId,  otto::Colours::textSecondary);
        btn->setColour (juce::TextButton::textColourOnId,   otto::Colours::textPrimary);
        const int idx = b;
        btn->onClick = [this, idx]
        {
            if (suppressNotify_) return;
            if (bandButtons_[static_cast<std::size_t> (idx)]->getToggleState())
                setSelectedBand (idx);
        };
        addChildComponent (*btn);
        bandButtons_[static_cast<std::size_t> (b)] = std::move (btn);
    }

    // HP/LP slope buttons — 4 in a row (6/12/18/24 dB-per-oct), radio-grouped.
    // Visible only when HP or LP is the selected band.
    static const char* kSlopeLabels[4] = { "6", "12", "18", "24" };
    static const std::uint8_t kSlopeValues[4] = { 6, 12, 18, 24 };
    for (int s = 0; s < 4; ++s)
    {
        auto btn = std::make_unique<juce::TextButton> (kSlopeLabels[s]);
        btn->setClickingTogglesState (true);
        btn->setRadioGroupId (2);
        btn->setColour (juce::TextButton::buttonColourId,   otto::Colours::bg3);
        btn->setColour (juce::TextButton::buttonOnColourId, otto::Colours::accent);
        btn->setColour (juce::TextButton::textColourOffId,  otto::Colours::textSecondary);
        btn->setColour (juce::TextButton::textColourOnId,   otto::Colours::textPrimary);
        const auto val = kSlopeValues[s];
        btn->onClick = [this, val]
        {
            if (suppressNotify_) return;
            if (selectedBand_ == kHP)      cachedConfig_.hpSlopeDbPerOct = val;
            else if (selectedBand_ == kLP) cachedConfig_.lpSlopeDbPerOct = val;
            publishCurrentConfig();
        };
        addChildComponent (*btn);
        slopeButtons_[static_cast<std::size_t> (s)] = std::move (btn);
    }

    bypassBandButton_ = std::make_unique<juce::TextButton> ("Bypass");
    bypassBandButton_->setClickingTogglesState (true);
    bypassBandButton_->setColour (juce::TextButton::buttonColourId,   otto::Colours::bg3);
    bypassBandButton_->setColour (juce::TextButton::buttonOnColourId, otto::Colours::accent);
    bypassBandButton_->setColour (juce::TextButton::textColourOffId,  otto::Colours::textSecondary);
    bypassBandButton_->setColour (juce::TextButton::textColourOnId,   otto::Colours::textPrimary);
    // Bypass is a UI affordance for the selected band only. The engine
    // doesn't model per-band bypass yet (PlayerEffectsConfig has only a
    // single eqEnabled flag); for HP/LP, "Bypass" parks the cutoff at
    // 20 Hz / 20 kHz (the engine's bypass-equivalent value). For shelves
    // it parks gain at 0. A real per-band flag is a follow-up.
    bypassBandButton_->onClick = [this]
    {
        if (suppressNotify_) return;
        const bool bypassed = bypassBandButton_->getToggleState();
        switch (selectedBand_)
        {
            case kHP:   cachedConfig_.hpFreq  = bypassed ? 20.0f    : cachedConfig_.hpFreq;
                        if (bypassed) cachedConfig_.hpFreq = 20.0f; break;
            case kLP:   if (bypassed) cachedConfig_.lpFreq = 20000.0f; break;
            case kLow:  if (bypassed) cachedConfig_.lowGain  = 0.0f; break;
            case kMid:  if (bypassed) cachedConfig_.midGain  = 0.0f; break;
            case kHigh: if (bypassed) cachedConfig_.highGain = 0.0f; break;
            default: break;
        }
        publishCurrentConfig();
    };
    addChildComponent (*bypassBandButton_);

    buildColumns();
}

ChannelDetailEQTab::~ChannelDetailEQTab() = default;

void ChannelDetailEQTab::buildColumns()
{
    static const char* kBandNames[kBandCount] = { "HP", "LOW", "MID", "HIGH", "LP" };

    for (int b = 0; b < kBandCount; ++b)
    {
        auto& col = bands_[static_cast<std::size_t> (b)];

        col.title = std::make_unique<juce::Label> ("", kBandNames[b]);
        col.title->setJustificationType (juce::Justification::centred);
        col.title->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        col.title->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        col.title->setInterceptsMouseClicks (false, false);
        addChildComponent (*col.title);

        col.freq = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                    juce::Slider::NoTextBox);
        col.freq->onValueChange = [this] { if (! suppressNotify_) publishCurrentConfig(); };
        addChildComponent (*col.freq);

        col.readoutFreq = std::make_unique<juce::Label>();
        col.readoutFreq->setJustificationType (juce::Justification::centred);
        col.readoutFreq->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        col.readoutFreq->setFont (juce::FontOptions (10.0f));
        col.readoutFreq->setInterceptsMouseClicks (false, false);
        addChildComponent (*col.readoutFreq);

        const bool isShelf = (b == kLow || b == kMid || b == kHigh);
        if (isShelf)
        {
            col.gain = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                        juce::Slider::NoTextBox);
            col.gain->setRange (-12.0, 12.0, 0.1);
            col.gain->onValueChange = [this] { if (! suppressNotify_) publishCurrentConfig(); };
            addChildComponent (*col.gain);

            col.q = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                     juce::Slider::NoTextBox);
            col.q->setRange (0.1, 10.0, 0.01);
            col.q->onValueChange = [this] { if (! suppressNotify_) publishCurrentConfig(); };
            addChildComponent (*col.q);

            col.readoutGain = std::make_unique<juce::Label>();
            col.readoutGain->setJustificationType (juce::Justification::centred);
            col.readoutGain->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
            col.readoutGain->setFont (juce::FontOptions (10.0f));
            col.readoutGain->setInterceptsMouseClicks (false, false);
            addChildComponent (*col.readoutGain);

            col.readoutQ = std::make_unique<juce::Label>();
            col.readoutQ->setJustificationType (juce::Justification::centred);
            col.readoutQ->setColour (juce::Label::textColourId, otto::Colours::textSecondary);
            col.readoutQ->setFont (juce::FontOptions (10.0f));
            col.readoutQ->setInterceptsMouseClicks (false, false);
            addChildComponent (*col.readoutQ);

            // Per-band frequency ranges (PlayerEffects.h:144-157).
            if (b == kLow)  col.freq->setRange (40.0,    500.0, 1.0);
            if (b == kMid)  col.freq->setRange (200.0,  8000.0, 1.0);
            if (b == kHigh) col.freq->setRange (2000.0,16000.0, 1.0);
        }
        else
        {
            // HP / LP — Freq + slope toggle only.
            if (b == kHP) col.freq->setRange (20.0,   500.0, 1.0);
            else          col.freq->setRange (2000.0, 20000.0, 1.0);

            col.slopeToggle = std::make_unique<juce::TextButton> ("12 dB");
            col.slopeToggle->setClickingTogglesState (true);
            col.slopeToggle->setColour (juce::TextButton::buttonColourId,    otto::Colours::bg3);
            col.slopeToggle->setColour (juce::TextButton::buttonOnColourId,  otto::Colours::accent);
            col.slopeToggle->setColour (juce::TextButton::textColourOffId,   otto::Colours::textSecondary);
            col.slopeToggle->setColour (juce::TextButton::textColourOnId,    otto::Colours::textPrimary);
            col.slopeToggle->onClick = [this, b]
            {
                if (suppressNotify_) return;
                const bool is24 = bands_[static_cast<std::size_t> (b)].slopeToggle->getToggleState();
                bands_[static_cast<std::size_t> (b)].slopeToggle->setButtonText (is24 ? "24 dB" : "12 dB");
                publishCurrentConfig();
            };
            addChildComponent (*col.slopeToggle);
        }
    }
}

void ChannelDetailEQTab::setChannelState (const ChannelState& state)
{
    hasChannelBound_ = true;
    hasEqSlot_       = state.hasEqSlot;
    cachedConfig_    = state.config;

    if (hasEqSlot_)
    {
        pushConfigToControls (cachedConfig_);
    }

    enableToggle_->setVisible  (hasEqSlot_);
    addSlotButton_->setVisible (! hasEqSlot_);
    if (curveView_) curveView_->setVisible (hasEqSlot_);

    // Band selector + contextual controls — slice EC-Polish-fix (OTTO layout).
    // The contextual visibility is driven by `selectedBand_`; setSelectedBand
    // handles per-band show/hide. We just toggle the SHARED visibility (all
    // hidden when no slot, all eligible to show when slot exists).
    for (auto& btn : bandButtons_)
        if (btn) btn->setVisible (hasEqSlot_);
    for (auto& col : bands_)
    {
        // Existing band columns retain their internal sub-widgets but the
        // OTTO-style contextual row uses freq/gain/Q only for the selected
        // band. Hide everything here; setSelectedBand re-shows the right ones.
        if (col.title)        col.title       ->setVisible (false);
        if (col.freq)         col.freq        ->setVisible (false);
        if (col.gain)         col.gain        ->setVisible (false);
        if (col.q)            col.q           ->setVisible (false);
        if (col.slopeToggle)  col.slopeToggle ->setVisible (false);   // legacy 12/24 toggle
        if (col.readoutFreq)  col.readoutFreq ->setVisible (false);
        if (col.readoutGain)  col.readoutGain ->setVisible (false);
        if (col.readoutQ)     col.readoutQ    ->setVisible (false);
    }
    for (auto& btn : slopeButtons_)
        if (btn) btn->setVisible (false);  // setSelectedBand re-shows for HP/LP
    if (bypassBandButton_) bypassBandButton_->setVisible (false);

    if (hasEqSlot_)
        setSelectedBand (selectedBand_);   // re-show contextual controls

    resized();
    repaint();
}

void ChannelDetailEQTab::clearChannelState()
{
    hasChannelBound_ = false;
    hasEqSlot_       = false;
    enableToggle_->setVisible (false);
    addSlotButton_->setVisible (false);
    if (curveView_) curveView_->setVisible (false);
    for (auto& col : bands_)
    {
        if (col.title)        col.title       ->setVisible (false);
        if (col.freq)         col.freq        ->setVisible (false);
        if (col.gain)         col.gain        ->setVisible (false);
        if (col.q)            col.q           ->setVisible (false);
        if (col.slopeToggle)  col.slopeToggle ->setVisible (false);
        if (col.readoutFreq)  col.readoutFreq ->setVisible (false);
        if (col.readoutGain)  col.readoutGain ->setVisible (false);
        if (col.readoutQ)     col.readoutQ    ->setVisible (false);
    }
    for (auto& btn : bandButtons_)  if (btn) btn->setVisible (false);
    for (auto& btn : slopeButtons_) if (btn) btn->setVisible (false);
    if (bypassBandButton_)              bypassBandButton_->setVisible (false);
    repaint();
}

void ChannelDetailEQTab::setSelectedBand (int band)
{
    if (band < 0 || band >= kBandCount) return;
    selectedBand_ = band;

    // Update curve view so the selected node renders larger / outlined
    // (mirrors OTTO's drawCurveForeground's isSelected branch).
    if (curveView_) curveView_->setSelectedBand (band);

    // Band-selector buttons: ensure exactly the selected one is "on".
    suppressNotify_ = true;
    for (int b = 0; b < kBandCount; ++b)
        if (bandButtons_[static_cast<std::size_t> (b)])
            bandButtons_[static_cast<std::size_t> (b)]->setToggleState (
                b == band, juce::dontSendNotification);
    suppressNotify_ = false;

    // Contextual row visibility: HP / LP show slope buttons + bypass;
    // Low / Mid / High show freq + gain + Q knobs + bypass.
    const bool isHpLp = (band == kHP || band == kLP);
    for (auto& btn : slopeButtons_)
        if (btn) btn->setVisible (hasEqSlot_ && isHpLp);
    if (bypassBandButton_) bypassBandButton_->setVisible (hasEqSlot_);

    for (int b = 0; b < kBandCount; ++b)
    {
        auto& col = bands_[static_cast<std::size_t> (b)];
        const bool isThisBand = (b == band) && hasEqSlot_;
        const bool isShelfBand = (b == kLow || b == kMid || b == kHigh);
        // Only show shelf controls when a shelf band is selected; HP / LP
        // controls go through the slope buttons + drag-on-curve instead.
        const bool showShelfKnobs = isThisBand && isShelfBand;
        if (col.freq)        col.freq       ->setVisible (showShelfKnobs);
        if (col.gain)        col.gain       ->setVisible (showShelfKnobs);
        if (col.q)           col.q          ->setVisible (showShelfKnobs);
        if (col.readoutFreq) col.readoutFreq->setVisible (showShelfKnobs);
        if (col.readoutGain) col.readoutGain->setVisible (showShelfKnobs);
        if (col.readoutQ)    col.readoutQ   ->setVisible (showShelfKnobs);
        if (col.title)       col.title      ->setVisible (false);  // band label now lives in selector button
        if (col.slopeToggle) col.slopeToggle->setVisible (false);  // legacy widget hidden
    }

    // Sync the slope buttons to the cached config for HP / LP.
    if (isHpLp)
    {
        const auto slope = (band == kHP) ? cachedConfig_.hpSlopeDbPerOct
                                         : cachedConfig_.lpSlopeDbPerOct;
        const std::uint8_t kSlopeValues[4] = { 6, 12, 18, 24 };
        suppressNotify_ = true;
        for (int s = 0; s < 4; ++s)
            if (slopeButtons_[static_cast<std::size_t> (s)])
                slopeButtons_[static_cast<std::size_t> (s)]->setToggleState (
                    slope == kSlopeValues[s], juce::dontSendNotification);
        suppressNotify_ = false;
    }

    resized();
    repaint();
}

void ChannelDetailEQTab::pushConfigToControls (const EqConfig& cfg)
{
    suppressNotify_ = true;

    enableToggle_->setToggleState (cfg.enabled, juce::dontSendNotification);

    bands_[kHP].freq->setValue        (cfg.hpFreq,   juce::dontSendNotification);
    bands_[kLow].freq->setValue       (cfg.lowFreq,  juce::dontSendNotification);
    bands_[kLow].gain->setValue       (cfg.lowGain,  juce::dontSendNotification);
    bands_[kLow].q->setValue          (cfg.lowQ,     juce::dontSendNotification);
    bands_[kMid].freq->setValue       (cfg.midFreq,  juce::dontSendNotification);
    bands_[kMid].gain->setValue       (cfg.midGain,  juce::dontSendNotification);
    bands_[kMid].q->setValue          (cfg.midQ,     juce::dontSendNotification);
    bands_[kHigh].freq->setValue      (cfg.highFreq, juce::dontSendNotification);
    bands_[kHigh].gain->setValue      (cfg.highGain, juce::dontSendNotification);
    bands_[kHigh].q->setValue         (cfg.highQ,    juce::dontSendNotification);
    bands_[kLP].freq->setValue        (cfg.lpFreq,   juce::dontSendNotification);

    const bool hp24 = (cfg.hpSlopeDbPerOct == 24);
    const bool lp24 = (cfg.lpSlopeDbPerOct == 24);
    bands_[kHP].slopeToggle->setToggleState (hp24, juce::dontSendNotification);
    bands_[kHP].slopeToggle->setButtonText  (hp24 ? "24 dB" : "12 dB");
    bands_[kLP].slopeToggle->setToggleState (lp24, juce::dontSendNotification);
    bands_[kLP].slopeToggle->setButtonText  (lp24 ? "24 dB" : "12 dB");

    updateReadouts();
    if (curveView_) curveView_->setConfig (cfg);
    suppressNotify_ = false;
}

void ChannelDetailEQTab::eqCurveConfigChanged (const EqConfig& cfg)
{
    // The curve view fired — update cached state, push the same numbers into
    // the knob row (so readouts + knob positions stay in sync with the
    // graphical drag), and republish to the host pane. suppressNotify_
    // gates the knob's own onValueChange to avoid a feedback loop.
    cachedConfig_ = cfg;

    suppressNotify_ = true;
    enableToggle_->setToggleState (cfg.enabled, juce::dontSendNotification);

    bands_[kHP].freq->setValue   (cfg.hpFreq,   juce::dontSendNotification);
    bands_[kLow].freq->setValue  (cfg.lowFreq,  juce::dontSendNotification);
    bands_[kLow].gain->setValue  (cfg.lowGain,  juce::dontSendNotification);
    bands_[kMid].freq->setValue  (cfg.midFreq,  juce::dontSendNotification);
    bands_[kMid].gain->setValue  (cfg.midGain,  juce::dontSendNotification);
    bands_[kHigh].freq->setValue (cfg.highFreq, juce::dontSendNotification);
    bands_[kHigh].gain->setValue (cfg.highGain, juce::dontSendNotification);
    bands_[kLP].freq->setValue   (cfg.lpFreq,   juce::dontSendNotification);
    updateReadouts();
    suppressNotify_ = false;

    listeners_.call ([&cfg] (ChannelDetailEQTabListener& l)
                     { l.eqTabConfigChanged (cfg); });
}

void ChannelDetailEQTab::publishCurrentConfig()
{
    cachedConfig_.enabled         = enableToggle_->getToggleState();
    cachedConfig_.hpFreq          = static_cast<float> (bands_[kHP].freq->getValue());
    cachedConfig_.hpSlopeDbPerOct = static_cast<std::uint8_t> (bands_[kHP].slopeToggle->getToggleState() ? 24 : 12);
    cachedConfig_.lowGain         = static_cast<float> (bands_[kLow].gain->getValue());
    cachedConfig_.lowFreq         = static_cast<float> (bands_[kLow].freq->getValue());
    cachedConfig_.lowQ            = static_cast<float> (bands_[kLow].q->getValue());
    cachedConfig_.midGain         = static_cast<float> (bands_[kMid].gain->getValue());
    cachedConfig_.midFreq         = static_cast<float> (bands_[kMid].freq->getValue());
    cachedConfig_.midQ            = static_cast<float> (bands_[kMid].q->getValue());
    cachedConfig_.highGain        = static_cast<float> (bands_[kHigh].gain->getValue());
    cachedConfig_.highFreq        = static_cast<float> (bands_[kHigh].freq->getValue());
    cachedConfig_.highQ           = static_cast<float> (bands_[kHigh].q->getValue());
    cachedConfig_.lpFreq          = static_cast<float> (bands_[kLP].freq->getValue());
    cachedConfig_.lpSlopeDbPerOct = static_cast<std::uint8_t> (bands_[kLP].slopeToggle->getToggleState() ? 24 : 12);

    updateReadouts();
    if (curveView_) curveView_->setConfig (cachedConfig_);

    const EqConfig snapshot = cachedConfig_;
    listeners_.call ([&snapshot] (ChannelDetailEQTabListener& l)
                     { l.eqTabConfigChanged (snapshot); });
}

void ChannelDetailEQTab::updateReadouts()
{
    bands_[kHP].readoutFreq->setText   (formatHz (cachedConfig_.hpFreq),   juce::dontSendNotification);
    bands_[kLow].readoutFreq->setText  (formatHz (cachedConfig_.lowFreq),  juce::dontSendNotification);
    bands_[kMid].readoutFreq->setText  (formatHz (cachedConfig_.midFreq),  juce::dontSendNotification);
    bands_[kHigh].readoutFreq->setText (formatHz (cachedConfig_.highFreq), juce::dontSendNotification);
    bands_[kLP].readoutFreq->setText   (formatHz (cachedConfig_.lpFreq),   juce::dontSendNotification);

    bands_[kLow].readoutGain->setText  (formatDbGain (cachedConfig_.lowGain),  juce::dontSendNotification);
    bands_[kMid].readoutGain->setText  (formatDbGain (cachedConfig_.midGain),  juce::dontSendNotification);
    bands_[kHigh].readoutGain->setText (formatDbGain (cachedConfig_.highGain), juce::dontSendNotification);

    bands_[kLow].readoutQ->setText  (formatQ (cachedConfig_.lowQ),  juce::dontSendNotification);
    bands_[kMid].readoutQ->setText  (formatQ (cachedConfig_.midQ),  juce::dontSendNotification);
    bands_[kHigh].readoutQ->setText (formatQ (cachedConfig_.highQ), juce::dontSendNotification);
}

void ChannelDetailEQTab::addListener    (ChannelDetailEQTabListener* l) { listeners_.add (l); }
void ChannelDetailEQTab::removeListener (ChannelDetailEQTabListener* l) { listeners_.remove (l); }

void ChannelDetailEQTab::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);

    if (! hasChannelBound_)
    {
        g.setColour (otto::Colours::textDisabled);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select a channel to edit EQ",
                    getLocalBounds().toFloat(), juce::Justification::centred);
    }
}

void ChannelDetailEQTab::resized()
{
    auto bounds = getLocalBounds().reduced (4);

    if (! hasChannelBound_) return;

    if (! hasEqSlot_)
    {
        // Center the "+ Add EQ" button.
        auto buttonBounds = bounds.withSizeKeepingCentre (260, kAddButtonHeight);
        addSlotButton_->setBounds (buttonBounds);
        return;
    }

    // Top row — enable toggle.
    auto topRow = bounds.removeFromTop (kEnableToggleHeight);
    enableToggle_->setBounds (topRow.removeFromLeft (90));
    bounds.removeFromTop (kRowGap);

    // OTTO layout (slice EC-Polish-fix):
    //   row 1: curve view (dominant — takes remaining vertical room minus
    //          the two button rows + a knob row when a shelf band is selected)
    //   row 2: band selector  (5 buttons, full width, ~36 px tall)
    //   row 3: contextual controls — slope buttons + Bypass when HP/LP is
    //          selected; freq + gain + Q knobs + Bypass when a shelf is
    //          selected
    const bool isShelfSelected = (selectedBand_ == kLow
                               || selectedBand_ == kMid
                               || selectedBand_ == kHigh);
    const int contextualH = isShelfSelected
                                ? juce::jmax (90, bounds.getHeight() / 3)
                                : kSlopeButtonHeight;

    // Bottom-up: contextual row at the bottom.
    auto contextualRow = bounds.removeFromBottom (contextualH);
    bounds.removeFromBottom (kRowGap);

    // Band selector row above contextual.
    auto selectorRow = bounds.removeFromBottom (kBandSelectorHeight);
    bounds.removeFromBottom (kRowGap);

    // Curve view fills what's left.
    if (curveView_)
        curveView_->setBounds (bounds);

    // Lay out the band selector buttons across the row.
    const int totalGap = kBandGap * (kBandCount - 1);
    const int colW = (selectorRow.getWidth() - totalGap) / kBandCount;
    for (int b = 0; b < kBandCount; ++b)
    {
        if (! bandButtons_[static_cast<std::size_t> (b)]) continue;
        const int x = selectorRow.getX() + b * (colW + kBandGap);
        bandButtons_[static_cast<std::size_t> (b)]->setBounds (
            x, selectorRow.getY(), colW, selectorRow.getHeight());
    }

    layoutContextualRow (contextualRow);
}

void ChannelDetailEQTab::layoutContextualRow (juce::Rectangle<int> row)
{
    const bool isHpLp = (selectedBand_ == kHP || selectedBand_ == kLP);
    if (isHpLp)
    {
        // OTTO HP/LP layout: 4 slope buttons (6/12/18/24) on the left,
        // Bypass button on the right with a small gap separating them.
        const int bypassW = 100;
        const int slopeRowW = row.getWidth() - bypassW - kBandGap * 2;
        const int slopeBtnW = (slopeRowW - kBandGap * 3) / 4;
        int x = row.getX();
        const int y = row.getY() + (row.getHeight() - kSlopeButtonHeight) / 2;
        for (int s = 0; s < 4; ++s)
        {
            if (slopeButtons_[static_cast<std::size_t> (s)])
                slopeButtons_[static_cast<std::size_t> (s)]->setBounds (
                    x, y, slopeBtnW, kSlopeButtonHeight);
            x += slopeBtnW + kBandGap;
        }
        if (bypassBandButton_)
            bypassBandButton_->setBounds (row.getRight() - bypassW, y,
                                          bypassW, kSlopeButtonHeight);
    }
    else
    {
        // Shelf band selected — show 3 knobs (freq/gain/Q) + Bypass on right.
        const int bypassW = 100;
        const int knobsRowW = row.getWidth() - bypassW - kBandGap * 2;
        const int knobColW = (knobsRowW - kBandGap * 2) / 3;
        auto& col = bands_[static_cast<std::size_t> (selectedBand_)];

        // Each column gets a label-on-top + knob + readout-on-bottom stack.
        auto layoutKnob = [&] (int colX, juce::Slider* knob,
                               juce::Label* readout)
        {
            if (knob == nullptr) return;
            auto colRect = juce::Rectangle<int> (colX, row.getY(),
                                                  knobColW, row.getHeight());
            auto readoutRow = colRect.removeFromBottom (kReadoutHeight);
            const int knobSide = juce::jmax (kKnobMinSize,
                                              juce::jmin (colRect.getWidth(),
                                                          colRect.getHeight()));
            auto knobRect = juce::Rectangle<int> (0, 0, knobSide, knobSide)
                                .withCentre (colRect.getCentre());
            knob->setBounds (knobRect);
            if (readout) readout->setBounds (readoutRow);
        };

        int x = row.getX();
        layoutKnob (x, col.gain.get(), col.readoutGain.get());
        x += knobColW + kBandGap;
        layoutKnob (x, col.freq.get(), col.readoutFreq.get());
        x += knobColW + kBandGap;
        layoutKnob (x, col.q.get(),    col.readoutQ.get());

        const int y = row.getY() + (row.getHeight() - kSlopeButtonHeight) / 2;
        if (bypassBandButton_)
            bypassBandButton_->setBounds (row.getRight() - bypassW, y,
                                          bypassW, kSlopeButtonHeight);
    }
}

} // namespace ida::ui
