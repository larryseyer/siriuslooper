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
    enableToggle_ = std::make_unique<juce::ToggleButton> ("ENABLE");
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

    enableToggle_->setVisible (hasEqSlot_);
    addSlotButton_->setVisible (! hasEqSlot_);
    for (auto& col : bands_)
    {
        if (col.title)        col.title       ->setVisible (hasEqSlot_);
        if (col.freq)         col.freq        ->setVisible (hasEqSlot_);
        if (col.gain)         col.gain        ->setVisible (hasEqSlot_);
        if (col.q)            col.q           ->setVisible (hasEqSlot_);
        if (col.slopeToggle)  col.slopeToggle ->setVisible (hasEqSlot_);
        if (col.readoutFreq)  col.readoutFreq ->setVisible (hasEqSlot_);
        if (col.readoutGain)  col.readoutGain ->setVisible (hasEqSlot_);
        if (col.readoutQ)     col.readoutQ    ->setVisible (hasEqSlot_);
    }

    resized();
    repaint();
}

void ChannelDetailEQTab::clearChannelState()
{
    hasChannelBound_ = false;
    hasEqSlot_       = false;
    enableToggle_->setVisible (false);
    addSlotButton_->setVisible (false);
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
    suppressNotify_ = false;
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

    // 5 equal band columns.
    const int totalGap = kBandGap * (kBandCount - 1);
    const int colW = (bounds.getWidth() - totalGap) / kBandCount;
    for (int b = 0; b < kBandCount; ++b)
    {
        auto& col = bands_[static_cast<std::size_t> (b)];
        const int x = bounds.getX() + b * (colW + kBandGap);
        auto colBounds = juce::Rectangle<int> (x, bounds.getY(), colW, bounds.getHeight());

        col.title->setBounds (colBounds.removeFromTop (kBandTitleHeight));
        colBounds.removeFromTop (kRowGap);

        const bool isShelf = (b == kLow || b == kMid || b == kHigh);
        if (isShelf)
        {
            // Three knob rows: Gain, Freq, Q — each with a readout under it.
            const int rowH = colBounds.getHeight() / 3;
            for (int r = 0; r < 3; ++r)
            {
                auto rowBounds = colBounds.removeFromTop (rowH);
                auto readout = rowBounds.removeFromBottom (kReadoutHeight);
                const int knobSide = juce::jmax (kKnobMinSize,
                                                  juce::jmin (rowBounds.getWidth(), rowBounds.getHeight()));
                auto knob = juce::Rectangle<int> (0, 0, knobSide, knobSide)
                               .withCentre (rowBounds.getCentre());
                if (r == 0) { col.gain->setBounds (knob); col.readoutGain->setBounds (readout); }
                if (r == 1) { col.freq->setBounds (knob); col.readoutFreq->setBounds (readout); }
                if (r == 2) { col.q   ->setBounds (knob); col.readoutQ   ->setBounds (readout); }
            }
        }
        else
        {
            // HP/LP: freq knob + slope toggle.
            auto slopeArea = colBounds.removeFromBottom (kSlopeToggleHeight);
            col.slopeToggle->setBounds (slopeArea);
            auto readout = colBounds.removeFromBottom (kReadoutHeight);
            col.readoutFreq->setBounds (readout);
            const int knobSide = juce::jmax (kKnobMinSize,
                                              juce::jmin (colBounds.getWidth(), colBounds.getHeight()));
            auto knob = juce::Rectangle<int> (0, 0, knobSide, knobSide)
                           .withCentre (colBounds.getCentre());
            col.freq->setBounds (knob);
        }
    }
}

} // namespace ida::ui
