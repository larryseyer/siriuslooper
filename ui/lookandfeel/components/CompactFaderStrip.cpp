#include "CompactFaderStrip.h"
#include "../OTTOColours.h"
#include <cmath>

namespace otto::ui {

namespace {

// Numeric pair label: pair 0 -> "1-2", pair 31 -> "63-64".
// Bug 116 precedent: emit numeric pairs from a dedicated formatter rather than
// indexing into a shared name table whose size diverges from output count.
// Bug 192 precedent: single format helper consumed by every channel strip
// footer; no parallel string table per consumer. Dash separator is the canonical
// OTTO routing-label form mandated by Sprint 30 BUG-04 / BUG-05 (reverts Sprint
// 29 BUG-10's '&' separator). The 'N-M' form matches the audio-pro convention
// used in Pro Tools' Output Routing pane and the OTTO spec docs' channel-pair
// tables (UI_SYSTEM_SPECIFICATIONS.md § 19, TECHNICAL_SPECIFICATIONS.md § 3.15).
juce::String formatOutputPairLabel(int pairIndex) {
    return juce::String(2 * pairIndex + 1) + "-" + juce::String(2 * pairIndex + 2);
}

}  // namespace

// =============================================================================
// MuteButton
// =============================================================================

class CompactFaderStrip::MuteButton : public juce::TextButton {
public:
    explicit MuteButton(CompactFaderStrip& owner) : owner_(owner) {
        setButtonText("M");
        setClickingTogglesState(true);
        onClick = [this] { owner_.notifyMuteChanged(); };
    }

    void setEffectivelyMuted(bool effectivelyMuted) {
        effectivelyMuted_ = effectivelyMuted;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        bool showMuted = getToggleState() || effectivelyMuted_;
        juce::Colour bgColour = showMuted ? Colours::muteActive : Colours::bg4;

        if (isButtonDown)
            bgColour = bgColour.darker(0.2f);
        else if (isMouseOver && !showMuted)
            bgColour = bgColour.brighter(0.1f);

        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(showMuted ? Colours::textPrimary : Colours::textSecondary);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("M", bounds, juce::Justification::centred);
    }

private:
    CompactFaderStrip& owner_;
    bool effectivelyMuted_ = false;
};

// =============================================================================
// SoloButton
// =============================================================================

class CompactFaderStrip::SoloButton : public juce::TextButton {
public:
    explicit SoloButton(CompactFaderStrip& owner) : owner_(owner) {
        setButtonText("S");
        setClickingTogglesState(true);
        onClick = [this] { owner_.notifySoloChanged(); };
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        juce::Colour bgColour = getToggleState() ? Colours::soloActive : Colours::bg4;

        if (isButtonDown)
            bgColour = bgColour.darker(0.2f);
        else if (isMouseOver && !getToggleState())
            bgColour = bgColour.brighter(0.1f);

        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(getToggleState() ? Colours::textInverse : Colours::textSecondary);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("S", bounds, juce::Justification::centred);
    }

private:
    CompactFaderStrip& owner_;
};

// =============================================================================
// CompactFaderStrip Implementation
// =============================================================================

CompactFaderStrip::CompactFaderStrip(int channelIndex, ChannelType type)
    : channelIndex_(channelIndex),
      channelType_(type),
      channelColor_(Colours::accent) {
    channelName_ = juce::String(channelIndex + 1);
    createControls();
}

CompactFaderStrip::~CompactFaderStrip() = default;

// =============================================================================
// Control Creation
// =============================================================================

void CompactFaderStrip::createControls() {
    muteButton_ = std::make_unique<MuteButton>(*this);
    addAndMakeVisible(*muteButton_);

    if (hasSoloButton()) {
        soloButton_ = std::make_unique<SoloButton>(*this);
        addAndMakeVisible(*soloButton_);
    }

    faderMeter_ = std::make_unique<FaderMeter>();
    faderMeter_->setChannelName(channelName_);
    faderMeter_->addListener(this);
    addAndMakeVisible(*faderMeter_);

    if (hasOutputCombo()) {
        outputCombo_ = std::make_unique<juce::ComboBox>();
        populateOutputCombo();
        // Boot-time defaults preserve legacy audible behaviour:
        //   FXReturn → pair 0 (sums into master, matches pre-Sprint-31 path)
        //   Bus      → bus index (preserves PerPlayer "bus c → pair c")
        //   Instrument → channelIndex_ % kNumOutputPairs (legacy default)
        // Sync-from-DSP overwrites with the live atomic value once the bridge
        // is wired.
        const int initialPair = (channelType_ == ChannelType::FXReturn)
                                    ? 0
                                    : (channelIndex_ % kNumOutputPairs);
        outputCombo_->setSelectedItemIndex(initialPair,
                                           juce::dontSendNotification);
        outputCombo_->onChange = [this] { notifyOutputAssignmentChanged(); };
        addAndMakeVisible(*outputCombo_);
    }
}

void CompactFaderStrip::populateOutputCombo() {
    if (outputCombo_ == nullptr) return;
    outputCombo_->clear(juce::dontSendNotification);
    // Combo indices 0..31 (IDs 1..32) map 1-to-1 onto output pairs 0..31. Labels
    // are numeric stereo pairs only (1-2 .. 63-64). No ellipsis, no truncation,
    // no name-table lookup at this site. All 32 entries are unconditionally
    // enumerated — no hard cap, no binding-state filter (BUG-04 diagnosis).
    for (int i = 0; i < kNumOutputPairs; ++i) {
        outputCombo_->addItem(formatOutputPairLabel(i), i + 1);
    }
}

// =============================================================================
// Channel Configuration
// =============================================================================

void CompactFaderStrip::setChannelIndex(int index) {
    channelIndex_ = index;
}

void CompactFaderStrip::setChannelType(ChannelType type) {
    if (channelType_ == type)
        return;

    channelType_ = type;

    // Recreate solo button based on type
    if (hasSoloButton() && !soloButton_) {
        soloButton_ = std::make_unique<SoloButton>(*this);
        addAndMakeVisible(*soloButton_);
    } else if (!hasSoloButton() && soloButton_) {
        removeChildComponent(soloButton_.get());
        soloButton_.reset();
    }

    // Output combo exists on Instrument, FXReturn, AND Bus strips. Master is
    // the only type with fixed downstream routing (always pair 0).
    if (hasOutputCombo() && !outputCombo_) {
        outputCombo_ = std::make_unique<juce::ComboBox>();
        populateOutputCombo();
        const int initialPair = (channelType_ == ChannelType::FXReturn)
                                    ? 0
                                    : (channelIndex_ % kNumOutputPairs);
        outputCombo_->setSelectedItemIndex(initialPair,
                                           juce::dontSendNotification);
        outputCombo_->onChange = [this] { notifyOutputAssignmentChanged(); };
        addAndMakeVisible(*outputCombo_);
    } else if (!hasOutputCombo() && outputCombo_) {
        removeChildComponent(outputCombo_.get());
        outputCombo_.reset();
    }

    resized();
}

void CompactFaderStrip::setChannelColor(juce::Colour color) {
    channelColor_ = color;
    repaint();
}

void CompactFaderStrip::setChannelName(const juce::String& name) {
    channelName_ = name;
    if (faderMeter_)
        faderMeter_->setChannelName(name);
    repaint();
}

// =============================================================================
// Parameter Accessors
// =============================================================================

void CompactFaderStrip::setGain(float gainLinear) {
    if (faderMeter_) {
        float dB = (gainLinear <= 0.0f) ? -60.0f : 20.0f * std::log10(gainLinear);
        faderMeter_->setVolume(dB);
    }
}

float CompactFaderStrip::getGain() const {
    if (!faderMeter_) return 1.0f;
    float dB = faderMeter_->getVolume();
    if (dB <= -60.0f) return 0.0f;
    return std::pow(10.0f, dB / 20.0f);
}

void CompactFaderStrip::setMuted(bool muted) {
    muted_ = muted;
    if (muteButton_)
        muteButton_->setToggleState(muted, juce::dontSendNotification);
}

void CompactFaderStrip::setSoloed(bool soloed) {
    soloed_ = soloed;
    if (soloButton_)
        soloButton_->setToggleState(soloed, juce::dontSendNotification);
}

// =============================================================================
// Display State
// =============================================================================

void CompactFaderStrip::setEffectivelyMuted(bool effectivelyMuted) {
    effectivelyMuted_ = effectivelyMuted;
    if (muteButton_)
        muteButton_->setEffectivelyMuted(effectivelyMuted);
}

void CompactFaderStrip::setOutputComboVisible(bool visible) {
    if (outputCombo_) {
        outputCombo_->setVisible(visible);
        resized();
    }
}

void CompactFaderStrip::setOutputAssignment(int outputPairIndex) {
    if (outputCombo_ == nullptr) return;
    // outputPairIndex is the external semantic (0..kNumOutputPairs-1) and now
    // maps 1-to-1 onto combo indices 0..31 (numeric pair labels 1-2 .. 63-64).
    const int clampedPair = juce::jlimit(0, kNumOutputPairs - 1, outputPairIndex);
    if (outputCombo_->getSelectedItemIndex() != clampedPair)
        outputCombo_->setSelectedItemIndex(clampedPair, juce::dontSendNotification);
}

int CompactFaderStrip::getOutputAssignment() const {
    if (outputCombo_ == nullptr) return channelIndex_ % kNumOutputPairs;
    const int idx = outputCombo_->getSelectedItemIndex();
    return idx >= 0 ? idx : channelIndex_ % kNumOutputPairs;
}

void CompactFaderStrip::setSelected(bool selected) {
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

// =============================================================================
// Level Meter
// =============================================================================

void CompactFaderStrip::setLevel(float leftDb, float rightDb) {
    if (faderMeter_)
        faderMeter_->setPeakLevels(leftDb, rightDb);
}

void CompactFaderStrip::setLUFSLevel(float lufs) {
    if (faderMeter_)
        faderMeter_->setLUFSIntegrated(lufs);
}

// =============================================================================
// Listener Management
// =============================================================================

void CompactFaderStrip::addListener(CompactFaderStripListener* listener) {
    listeners_.add(listener);
}

void CompactFaderStrip::removeListener(CompactFaderStripListener* listener) {
    listeners_.remove(listener);
}

// =============================================================================
// Fader Thumb Access
// =============================================================================

juce::Rectangle<int> CompactFaderStrip::getFaderThumbBounds() const {
    if (!faderMeter_)
        return {};
    return faderMeter_->getThumbBounds().toNearestInt();
}

// =============================================================================
// Paint
// =============================================================================

void CompactFaderStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Strip background
    g.setColour(Colours::bg3);
    g.fillRoundedRectangle(bounds, static_cast<float>(kCornerRadius));

    // Selection border
    if (selected_) {
        g.setColour(Colours::accent);
        g.drawRoundedRectangle(bounds.reduced(1.0f),
                               static_cast<float>(kCornerRadius),
                               static_cast<float>(kSelectionBorderWidth));
    }

    // Name header background with channel color
    auto nameArea = bounds.removeFromTop(static_cast<float>(kNameHeight));
    g.setColour(channelColor_.withAlpha(0.15f));
    // Rounded top corners only
    juce::Path headerPath;
    float cr = static_cast<float>(kCornerRadius);
    headerPath.addRoundedRectangle(nameArea.getX(), nameArea.getY(),
                                    nameArea.getWidth(), nameArea.getHeight(),
                                    cr, cr, true, true, false, false);
    g.fillPath(headerPath);

    // Channel name text
    g.setColour(channelColor_);
    g.setFont(juce::Font(12.0f).boldened());
    g.drawText(channelName_, nameArea, juce::Justification::centred, true);
}

// =============================================================================
// Layout
// =============================================================================

void CompactFaderStrip::resized() {
    layoutControls();
}

void CompactFaderStrip::layoutControls() {
    auto bounds = getLocalBounds();

    // Skip name header area (painted directly)
    bounds.removeFromTop(kNameHeight);
    bounds.removeFromTop(kGap);

    // Apply horizontal padding
    bounds = bounds.reduced(kPadding, 0);

    // Output-routing ComboBox at bottom (Instrument strips only; hidden when
    // channel detail is visible to reclaim vertical space for the fader).
    if (outputCombo_ && outputCombo_->isVisible()) {
        bounds.removeFromBottom(kGap);
        auto comboArea = bounds.removeFromBottom(kOutputComboHeight);
        outputCombo_->setBounds(comboArea);
        bounds.removeFromBottom(kGap);
    }

    // Mute/Solo buttons
    auto muteSoloRow = bounds.removeFromTop(kMuteSoloHeight);
    if (hasSoloButton() && soloButton_) {
        int halfWidth = (muteSoloRow.getWidth() - kGap) / 2;
        muteButton_->setBounds(muteSoloRow.removeFromLeft(halfWidth));
        muteSoloRow.removeFromLeft(kGap);
        soloButton_->setBounds(muteSoloRow);
    } else {
        muteButton_->setBounds(muteSoloRow);
    }

    bounds.removeFromTop(kGap);

    // FaderMeter gets remaining space
    faderMeter_->setBounds(bounds);
}

// =============================================================================
// Mouse Handling
// =============================================================================

void CompactFaderStrip::mouseDown(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    notifyChannelSelected();
}

// =============================================================================
// Internal Helpers
// =============================================================================

int CompactFaderStrip::getStripWidth() const {
    return (channelType_ == ChannelType::Master) ? kMasterStripWidth : kStripWidth;
}

bool CompactFaderStrip::hasSoloButton() const {
    // Master has no solo (it's the final output). All other types do.
    return channelType_ != ChannelType::Master;
}

bool CompactFaderStrip::hasOutputCombo() const {
    // Sprint 31 BUG-04 follow-up: Instrument, FXReturn, AND Bus strips all
    // expose the 32-pair output-routing ComboBox — users get explicit control
    // over which stereo pair each strip contributes to. Bus defaults to its
    // own index (Drums→pair 0, Percs→pair 1, Shkrs→pair 2, Hands→pair 3),
    // preserving the legacy PerPlayer routing. FXReturn defaults to pair 0
    // (matching the pre-Sprint-31 sumGlobalMixerToMasterBus path).
    //
    // Master is the lone exception — it ALWAYS lands on pair 0 (no combo).
    // The three configurable strip types share an identical bottom-row
    // composition so they render at pixel-identical heights.
    return channelType_ != ChannelType::Master;
}

// =============================================================================
// Notification Methods
// =============================================================================

void CompactFaderStrip::notifyMuteChanged() {
    muted_ = muteButton_ ? muteButton_->getToggleState() : false;
    listeners_.call([this](CompactFaderStripListener& l) {
        l.stripMuteChanged(channelIndex_, channelType_, muted_);
    });
}

void CompactFaderStrip::notifySoloChanged() {
    soloed_ = soloButton_ ? soloButton_->getToggleState() : false;
    listeners_.call([this](CompactFaderStripListener& l) {
        l.stripSoloChanged(channelIndex_, channelType_, soloed_);
    });
}

void CompactFaderStrip::faderMeterVolumeChanged(float newDb) {
    float gain = (newDb <= -60.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
    listeners_.call([this, gain](CompactFaderStripListener& l) {
        l.stripGainChanged(channelIndex_, channelType_, gain);
    });
}

void CompactFaderStrip::notifyGainChanged() {
    float gain = getGain();
    listeners_.call([this, gain](CompactFaderStripListener& l) {
        l.stripGainChanged(channelIndex_, channelType_, gain);
    });
}

void CompactFaderStrip::notifyChannelSelected() {
    listeners_.call([this](CompactFaderStripListener& l) {
        l.stripChannelSelected(channelIndex_, channelType_);
    });
}

void CompactFaderStrip::notifyOutputAssignmentChanged() {
    if (outputCombo_ == nullptr) return;
    const int idx = outputCombo_->getSelectedItemIndex();
    if (idx < 0) return;
    listeners_.call([this, idx](CompactFaderStripListener& l) {
        l.stripOutputAssignmentChanged(channelIndex_, channelType_, idx);
    });
}

}  // namespace otto::ui
