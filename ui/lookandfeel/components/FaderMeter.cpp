#include "FaderMeter.h"
#include "OTTOBinaryData.h"

namespace otto::ui {

namespace {

// Top-row meter-strip cool-neutral radial overlay. `bounds` is the meter's
// own local rect (already filled with Colours::meterBackground); `stripLocal`
// is the full meter-strip rect (union of mainMeter_ + spectrumDisplay_)
// translated into THIS meter's local coordinates. Brightness peaks at the
// strip center and fades to transparent at the strip edges, so the master
// meter and the spectrum display read as ONE continuous gradient on iPad.
// Identical helper exists in SpectrumDisplay.cpp — single visual contract.
void paintMeterStripGradient(juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             float cornerRadius,
                             juce::Rectangle<float> stripLocal) {
    if (stripLocal.isEmpty()) return;

    const auto center = stripLocal.getCentre();
    const float halfSpan = stripLocal.getWidth() * 0.5f;

    juce::ColourGradient grad(
        juce::Colour(70, 95, 120).withAlpha(0.32f), center.x, center.y,
        juce::Colours::transparentBlack, center.x + halfSpan, center.y,
        /*isRadial=*/true);
    grad.addColour(0.45, juce::Colour(40, 55, 75).withAlpha(0.18f));

    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addRoundedRectangle(bounds, cornerRadius);
    g.reduceClipRegion(clip);
    g.setGradientFill(grad);
    g.fillRect(bounds);
}

} // namespace

// =============================================================================
// Construction / Destruction
// =============================================================================

FaderMeter::FaderMeter() {
    faderKnobImage_ = juce::ImageFileFormat::loadFrom(
        OTTOBinaryData::Fader_Knob_png,
        OTTOBinaryData::Fader_Knob_pngSize);
    startTimerHz(60);
    lastUpdateTime_ = juce::Time::currentTimeMillis();
}

FaderMeter::~FaderMeter() {
    stopTimer();
}

// =============================================================================
// Public API
// =============================================================================

void FaderMeter::setChannelName(const juce::String& name) {
    channelName_ = name;
    repaint();
}

void FaderMeter::setVolume(float dB) {
    isUpdatingFromExternal_ = true;
    volumeDb_ = juce::jlimit(kMinDb, kMaxDb, dB);
    isUpdatingFromExternal_ = false;
    repaint();
}

void FaderMeter::setPeakLevels(float leftDb, float rightDb) {
    peakLeftDb_ = juce::jlimit(kMinDb, kMaxDb, leftDb);
    peakRightDb_ = juce::jlimit(kMinDb, kMaxDb, rightDb);

    if (peakLeftDb_ > peakHoldLeft_) {
        peakHoldLeft_ = peakLeftDb_;
        peakHoldTimeLeft_ = juce::Time::currentTimeMillis();
    }
    if (peakRightDb_ > peakHoldRight_) {
        peakHoldRight_ = peakRightDb_;
        peakHoldTimeRight_ = juce::Time::currentTimeMillis();
    }

    if (peakLeftDb_ >= kClipThreshold) clipLeft_ = true;
    if (peakRightDb_ >= kClipThreshold) clipRight_ = true;
}

void FaderMeter::setLUFS(float lufs) {
    lufsTarget_ = juce::jlimit(kLUFSMin, kLUFSMax, lufs);
}

void FaderMeter::resetClip() {
    clipLeft_ = false;
    clipRight_ = false;
    peakHoldLeft_ = kMinDb;
    peakHoldRight_ = kMinDb;
}

void FaderMeter::setFaderEnabled(bool enabled) {
    faderEnabled_ = enabled;
    repaint();
}

void FaderMeter::setBarWidthMode(BarWidthMode mode) {
    if (barWidthMode_ == mode) return;
    barWidthMode_ = mode;
    repaint();
}

void FaderMeter::setOrientation(Orientation orientation) {
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    repaint();
}

void FaderMeter::addListener(FaderMeterListener* listener) {
    listeners_.add(listener);
}

void FaderMeter::removeListener(FaderMeterListener* listener) {
    listeners_.remove(listener);
}

// =============================================================================
// Timer - 60Hz meter ballistics
// =============================================================================

void FaderMeter::timerCallback() {
    auto currentTime = juce::Time::currentTimeMillis();
    auto deltaMs = static_cast<float>(currentTime - lastUpdateTime_);
    lastUpdateTime_ = currentTime;

    bool needsRepaint = false;
    float decayAmount = (deltaMs / kDecayRate) * (kMaxDb - kMinDb);

    // Peak decay - left
    if (displayPeakLeft_ > peakLeftDb_) {
        displayPeakLeft_ = juce::jmax(peakLeftDb_, displayPeakLeft_ - decayAmount);
        needsRepaint = true;
    } else if (displayPeakLeft_ < peakLeftDb_) {
        displayPeakLeft_ = peakLeftDb_;
        needsRepaint = true;
    }

    // Peak decay - right
    if (displayPeakRight_ > peakRightDb_) {
        displayPeakRight_ = juce::jmax(peakRightDb_, displayPeakRight_ - decayAmount);
        needsRepaint = true;
    } else if (displayPeakRight_ < peakRightDb_) {
        displayPeakRight_ = peakRightDb_;
        needsRepaint = true;
    }

    // LUFS decay (slower)
    float lufsDecay = (deltaMs / 500.0f) * (kLUFSMax - kLUFSMin);
    if (displayLufs_ > lufsTarget_) {
        displayLufs_ = juce::jmax(lufsTarget_, displayLufs_ - lufsDecay);
        needsRepaint = true;
    } else if (displayLufs_ < lufsTarget_) {
        displayLufs_ = lufsTarget_;
        needsRepaint = true;
    }

    // Peak hold decay
    if (currentTime - peakHoldTimeLeft_ > static_cast<juce::int64>(kPeakHoldMs)) {
        float peakDecay = (deltaMs / kPeakFadeMs) * (kMaxDb - kMinDb);
        if (peakHoldLeft_ > displayPeakLeft_) {
            peakHoldLeft_ = juce::jmax(displayPeakLeft_, peakHoldLeft_ - peakDecay);
            needsRepaint = true;
        }
    }
    if (currentTime - peakHoldTimeRight_ > static_cast<juce::int64>(kPeakHoldMs)) {
        float peakDecay = (deltaMs / kPeakFadeMs) * (kMaxDb - kMinDb);
        if (peakHoldRight_ > displayPeakRight_) {
            peakHoldRight_ = juce::jmax(displayPeakRight_, peakHoldRight_ - peakDecay);
            needsRepaint = true;
        }
    }

    if (needsRepaint) repaint();
}

// =============================================================================
// Conversion Helpers (Sprint 29 BUG-16: STATIC so SpectrumDisplay can match
// the meter's amplitude-to-Y scale without instantiating a FaderMeter)
// =============================================================================

float FaderMeter::dbToNormalized(float dB) {
    return (dB - kMinDb) / (kMaxDb - kMinDb);
}

float FaderMeter::normalizedToDb(float normalized) {
    return kMinDb + normalized * (kMaxDb - kMinDb);
}

float FaderMeter::dbToSkewed(float dB) {
    float normalized = dbToNormalized(dB);
    // 0dB maps to 0.777 physical height: skewFactor = log(0.777) / log(60/66)
    float normalizedUnity = (kDefaultDb - kMinDb) / (kMaxDb - kMinDb);
    float skewFactor = std::log(kUnityNormalized) / std::log(normalizedUnity);
    return std::pow(juce::jlimit(0.0f, 1.0f, normalized), skewFactor);
}

float FaderMeter::skewedToDb(float skewed) {
    float normalizedUnity = (kDefaultDb - kMinDb) / (kMaxDb - kMinDb);
    float skewFactor = std::log(kUnityNormalized) / std::log(normalizedUnity);
    float normalized = std::pow(juce::jlimit(0.0f, 1.0f, skewed), 1.0f / skewFactor);
    return normalizedToDb(normalized);
}

float FaderMeter::dbToY(float dB, float top, float height) {
    float skewed = dbToSkewed(dB);
    return top + (1.0f - skewed) * height;
}

float FaderMeter::yToDb(float y, float top, float height) {
    float skewed = 1.0f - (y - top) / height;
    return skewedToDb(skewed);
}

// static
juce::Colour FaderMeter::getColorForPeakLevel(float normalized) {
    float dbValue = kMinDb + normalized * (kMaxDb - kMinDb);
    if (dbValue >= 0.0f) return Colours::meterClip;
    if (dbValue >= -3.0f) return Colours::meterHigh;
    if (dbValue >= -12.0f) return Colours::meterMid;
    return Colours::meterLow;
}

// static
juce::Colour FaderMeter::getColorForLUFSLevel(float normalized) {
    float lufsValue = kLUFSMin + normalized * (kLUFSMax - kLUFSMin);
    if (lufsValue >= -6.0f) return Colours::meterClip;
    if (lufsValue >= -14.0f) return Colours::meterHigh;
    if (lufsValue >= -24.0f) return Colours::meterMid;
    return Colours::meterLow;
}

// =============================================================================
// Layout Helpers
// =============================================================================

juce::Rectangle<float> FaderMeter::getMeterArea() const {
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop(kReadoutHeight);
    bounds.removeFromBottom(kNameHeight + kLUFSReadoutHeight);
    return bounds;
}

juce::Rectangle<float> FaderMeter::getThumbBounds() const {
    auto meterArea = getMeterArea();
    float y = dbToY(volumeDb_, meterArea.getY(), meterArea.getHeight());
    float thumbX = meterArea.getCentreX() - kThumbWidth / 2.0f;
    return { thumbX, y - kThumbHeight / 2.0f, kThumbWidth, kThumbHeight };
}

bool FaderMeter::isInThumbArea(const juce::MouseEvent& e) const {
    auto thumb = getThumbBounds();
    auto hitArea = thumb.expanded(0.0f, 10.0f);
    return hitArea.contains(e.position);
}

// =============================================================================
// Mouse Interaction
// =============================================================================

void FaderMeter::mouseDown(const juce::MouseEvent& e) {
    if (!faderEnabled_) {
        // Meter-only mode (TransportBar master meter, BUG-02). Forward bare
        // left-click / tap to the parent-supplied callback so the click can
        // route through an existing opener helper without duplicating dispatch
        // (Bug 100). Right-click / ctrl-click is reserved for future
        // context-menu affordances.
        if (!e.mods.isPopupMenu() && onMeterAreaClicked) {
            onMeterAreaClicked();
        }
        return;
    }

    auto meterArea = getMeterArea();

    // Click on clip indicator area (top of meter) resets clip
    if (e.position.y < meterArea.getY() && hasClipIndicator()) {
        resetClip();
        repaint();
        return;
    }

    // Only interact within meter area
    if (!meterArea.contains(e.position)) return;

    isDragging_ = true;
    dragStartDb_ = volumeDb_;

    // Snap fader to click position
    float newDb = yToDb(e.position.y, meterArea.getY(), meterArea.getHeight());
    volumeDb_ = juce::jlimit(kMinDb, kMaxDb, newDb);
    repaint();

    if (!isUpdatingFromExternal_) {
        float vol = volumeDb_;
        listeners_.call([vol](FaderMeterListener& l) {
            l.faderMeterVolumeChanged(vol);
        });
    }
}

void FaderMeter::mouseDrag(const juce::MouseEvent& e) {
    if (!faderEnabled_ || !isDragging_) return;

    auto meterArea = getMeterArea();
    float newDb = yToDb(e.position.y, meterArea.getY(), meterArea.getHeight());
    volumeDb_ = juce::jlimit(kMinDb, kMaxDb, newDb);
    repaint();

    if (!isUpdatingFromExternal_) {
        float vol = volumeDb_;
        listeners_.call([vol](FaderMeterListener& l) {
            l.faderMeterVolumeChanged(vol);
        });
    }
}

void FaderMeter::mouseDoubleClick(const juce::MouseEvent&) {
    if (!faderEnabled_) return;
    volumeDb_ = kDefaultDb;
    repaint();

    if (!isUpdatingFromExternal_) {
        float vol = volumeDb_;
        listeners_.call([vol](FaderMeterListener& l) {
            l.faderMeterVolumeChanged(vol);
        });
    }
}

// =============================================================================
// Paint - Main
// =============================================================================

void FaderMeter::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // BUG-08: horizontal orientation is the meter-only top-row form. Suppress
    // readouts / channel name (no vertical room) and the fader overlay
    // (unsupported in horizontal). The bars are painted by the dedicated
    // horizontal background using the same striped gradient + peak indicator
    // primitives so it stays pixel-identical to the vertical FaderMeter modulo
    // orientation.
    if (orientation_ == Orientation::Horizontal) {
        paintHorizontal(g, bounds);
        return;
    }

    // Top: peak readouts
    auto readoutBounds = bounds.removeFromTop(kReadoutHeight);
    paintPeakReadouts(g, readoutBounds);

    // Bottom: channel name, then LUFS readout above it
    auto nameBounds = bounds.removeFromBottom(kNameHeight);
    auto lufsReadoutBounds = bounds.removeFromBottom(kLUFSReadoutHeight);
    paintChannelName(g, nameBounds);
    paintLUFSReadout(g, lufsReadoutBounds, displayLufs_);

    // Middle: meter bars + fader overlay
    paintMeterBackground(g, bounds);
    if (faderEnabled_)
        paintFaderOverlay(g, bounds);
}

void FaderMeter::resized() {
    // No child components - all custom painted
}

// =============================================================================
// Paint - Meter Background (bars, scales)
// =============================================================================

void FaderMeter::paintMeterBackground(juce::Graphics& g, juce::Rectangle<float> meterArea) {
    // Background
    g.setColour(Colours::meterBackground);
    g.fillRoundedRectangle(meterArea, 2.0f);

    // Resolve bar widths based on layout mode.
    float lufsBarWidth = kLUFSBarWidth;
    float peakBarWidth = kPeakBarWidth;
    float leftGutter = 0.0f;

    if (barWidthMode_ == BarWidthMode::FillAvailable) {
        // Preserve the 5:10:10:5 peak/LUFS bar ratio, grow to fill available room.
        const float available =
            meterArea.getWidth() - kScaleLabelWidth - kLUFSScaleLabelWidth - 3.0f * kBarGap;
        if (available > 0.0f) {
            constexpr float kRatioSum = 5.0f + 10.0f + 10.0f + 5.0f;  // 30
            lufsBarWidth = available * (5.0f / kRatioSum);
            peakBarWidth = available * (10.0f / kRatioSum);
        }
    } else {
        const float totalBarsWidth = kLUFSBarWidth + kBarGap + kPeakBarWidth + kBarGap
                                   + kPeakBarWidth + kBarGap + kLUFSBarWidth;
        const float totalWidth = kScaleLabelWidth + totalBarsWidth + kLUFSScaleLabelWidth;
        leftGutter = (meterArea.getWidth() - totalWidth) / 2.0f;
    }

    float startX = meterArea.getX() + leftGutter;
    const float barHeight = meterArea.getHeight();
    const float barY = meterArea.getY();

    // dB scale on left
    auto scaleBounds = juce::Rectangle<float>(startX, barY, kScaleLabelWidth, barHeight);
    paintScaleLabels(g, scaleBounds);
    startX += kScaleLabelWidth;

    // LUFS-L (narrow outer)
    auto lufsLBounds = juce::Rectangle<float>(startX, barY, lufsBarWidth, barHeight);
    paintLUFSBar(g, lufsLBounds, displayLufs_);
    startX += lufsBarWidth + kBarGap;

    // PEAK-L (wide inner)
    auto peakLBounds = juce::Rectangle<float>(startX, barY, peakBarWidth, barHeight);
    paintPeakBar(g, peakLBounds, displayPeakLeft_, peakHoldLeft_, clipLeft_);
    startX += peakBarWidth + kBarGap;

    // PEAK-R (wide inner)
    auto peakRBounds = juce::Rectangle<float>(startX, barY, peakBarWidth, barHeight);
    paintPeakBar(g, peakRBounds, displayPeakRight_, peakHoldRight_, clipRight_);
    startX += peakBarWidth + kBarGap;

    // LUFS-R (narrow outer)
    auto lufsRBounds = juce::Rectangle<float>(startX, barY, lufsBarWidth, barHeight);
    paintLUFSBar(g, lufsRBounds, displayLufs_);
    startX += lufsBarWidth;

    // LUFS scale labels on right
    auto lufsScaleBounds = juce::Rectangle<float>(startX, barY, kLUFSScaleLabelWidth, barHeight);
    paintLUFSScaleLabels(g, lufsScaleBounds);
}

// =============================================================================
// Paint - Striped Bar
// =============================================================================

// static
void FaderMeter::paintStripedBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  float levelNormalized, bool isPeakBar) {
    if (levelNormalized <= 0.0f) return;

    float fillHeight = bounds.getHeight() * levelNormalized;
    auto fillBounds = bounds.withHeight(fillHeight);
    fillBounds.setY(bounds.getBottom() - fillHeight);

    constexpr float stripeHeight = 2.0f;
    constexpr float gapHeight = 1.0f;
    constexpr float totalStripeHeight = stripeHeight + gapHeight;

    float y = fillBounds.getBottom() - stripeHeight;
    while (y >= fillBounds.getY()) {
        float normalizedY = (bounds.getBottom() - y) / bounds.getHeight();
        juce::Colour color = isPeakBar ? getColorForPeakLevel(normalizedY)
                                       : getColorForLUFSLevel(normalizedY);
        g.setColour(color);
        float stripeTop = juce::jmax(y, fillBounds.getY());
        float stripeBottom = juce::jmin(y + stripeHeight, fillBounds.getBottom());
        if (stripeBottom > stripeTop)
            g.fillRect(fillBounds.getX(), stripeTop, fillBounds.getWidth(), stripeBottom - stripeTop);
        y -= totalStripeHeight;
    }
}

// =============================================================================
// Paint - Peak Bar
// =============================================================================

// static
void FaderMeter::paintPeakBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                               float displayLevel, float peakHold, bool clipActive) {
    // Dark background for this bar
    g.setColour(Colours::meterBackground.brighter(0.05f));
    g.fillRoundedRectangle(bounds, 1.0f);

    // Clip indicator at top (4px)
    auto clipZone = bounds.removeFromTop(4.0f);
    if (clipActive) {
        g.setColour(Colours::meterClip);
        g.fillRoundedRectangle(clipZone, 1.0f);
    }

    // Level bar with stripes (static dbToNormalized is inline below)
    const float normalized = juce::jlimit(0.0f, 1.0f,
                                          (displayLevel - kMinDb) / (kMaxDb - kMinDb));
    paintStripedBar(g, bounds, normalized, true);

    // Peak hold indicator (white line)
    const float peakNorm = juce::jlimit(0.0f, 1.0f,
                                        (peakHold - kMinDb) / (kMaxDb - kMinDb));
    if (peakNorm > 0.0f) {
        float peakY = bounds.getBottom() - (bounds.getHeight() * peakNorm);
        if (peakY >= bounds.getY()) {
            g.setColour(juce::Colours::white);
            g.fillRect(bounds.getX(), peakY, bounds.getWidth(), 2.0f);
        }
    }
}

// =============================================================================
// Paint - LUFS Bar
// =============================================================================

// static
void FaderMeter::paintLUFSBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                                float levelLufs) {
    g.setColour(Colours::meterBackground.brighter(0.05f));
    g.fillRoundedRectangle(bounds, 1.0f);

    float normalized = (levelLufs - kLUFSMin) / (kLUFSMax - kLUFSMin);
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    paintStripedBar(g, bounds, normalized, false);
}

// =============================================================================
// Paint - Fader Overlay
// =============================================================================

void FaderMeter::paintFaderOverlay(juce::Graphics& g, juce::Rectangle<float> meterArea) {
    float centerX = meterArea.getCentreX();

    // Semi-transparent fader track line
    g.setColour(juce::Colour(0x40ffffff));
    g.fillRect(centerX - kTrackWidth / 2.0f, meterArea.getY(),
               kTrackWidth, meterArea.getHeight());

    // Fader thumb position
    float thumbY = dbToY(volumeDb_, meterArea.getY(), meterArea.getHeight());
    float thumbX = centerX - kThumbWidth / 2.0f;

    auto thumbRect = juce::Rectangle<float>(thumbX, thumbY - kThumbHeight / 2.0f,
                                             kThumbWidth, kThumbHeight);

    if (faderKnobImage_.isValid()) {
        g.setOpacity(1.0f);
        g.drawImage(faderKnobImage_, thumbRect,
                    juce::RectanglePlacement::centred);
    } else {
        // Fallback: programmatic gradient
        juce::ColourGradient capGrad(
            Colours::sliderCapColor.brighter(0.3f), thumbRect.getX(), thumbRect.getY(),
            Colours::sliderCapColor.darker(0.2f), thumbRect.getX(), thumbRect.getBottom(),
            false);
        g.setGradientFill(capGrad);
        g.fillRoundedRectangle(thumbRect, 3.0f);
        g.setColour(Colours::sliderCapBorder);
        g.drawRoundedRectangle(thumbRect, 3.0f, 1.0f);
    }
}

// =============================================================================
// Paint - Scale Labels
// =============================================================================

void FaderMeter::paintScaleLabels(juce::Graphics& g, juce::Rectangle<float> bounds) {
    paintMixerScaleLabels(g, bounds);
}

// static
void FaderMeter::paintMixerScaleLabels(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(7.0f));
    g.setColour(Colours::textSecondary);

    const float dbMarks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    const float normalizedUnity = (kDefaultDb - kMinDb) / (kMaxDb - kMinDb);
    const float skewFactor = std::log(kUnityNormalized) / std::log(normalizedUnity);

    for (float db : dbMarks) {
        const float normalized = (db - kMinDb) / (kMaxDb - kMinDb);
        const float skewed = std::pow(juce::jlimit(0.0f, 1.0f, normalized), skewFactor);
        const float y = bounds.getY() + (1.0f - skewed) * bounds.getHeight();

        juce::String label;
        if (db > 0.0f) label = "+" + juce::String(static_cast<int>(db));
        else label = juce::String(static_cast<int>(db));

        g.drawText(label,
                   juce::Rectangle<float>(bounds.getX(), y - 5.0f, bounds.getWidth(), 10.0f),
                   juce::Justification::centredRight);
    }
}

// =============================================================================
// Paint - Readouts
// =============================================================================

void FaderMeter::paintPeakReadouts(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    g.setColour(juce::Colour(0xff00ffff));  // Cyan

    juce::String leftStr = juce::String(displayPeakLeft_, 1);
    juce::String rightStr = juce::String(displayPeakRight_, 1);
    g.drawText(leftStr + " " + rightStr, bounds, juce::Justification::centred);
}

void FaderMeter::paintLUFSScaleLabels(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(7.0f));
    g.setColour(Colours::textSecondary);

    const float lufsMarks[] = { 0.0f, -14.0f, -24.0f, -40.0f, -60.0f };
    float range = kLUFSMax - kLUFSMin;

    for (float lufs : lufsMarks) {
        float normalized = (lufs - kLUFSMin) / range;
        float y = bounds.getBottom() - normalized * bounds.getHeight();

        juce::String label = juce::String(static_cast<int>(lufs));
        g.drawText(label,
                   juce::Rectangle<float>(bounds.getX(), y - 5.0f, bounds.getWidth(), 10.0f),
                   juce::Justification::centredLeft);
    }
}

// static
void FaderMeter::paintLUFSReadout(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  float displayLufs) {
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    g.setColour(juce::Colours::white);

    juce::String lufsStr;
    if (displayLufs <= kLUFSMin + 1.0f)
        lufsStr = "L-S  -inf";
    else
        lufsStr = "L-S  " + juce::String(displayLufs, 1);

    g.drawText(lufsStr, bounds, juce::Justification::centred);
}

void FaderMeter::paintChannelName(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.setColour(Colours::textPrimary);
    g.drawText(channelName_, bounds, juce::Justification::centred);
}

// =============================================================================
// Paint - Horizontal (BUG-08)
// =============================================================================
//
// Horizontal layout (top-to-bottom inside the meter area):
//   [ dB scale tick strip ]                          ← kScaleLabelWidth tall
//   [ LUFS-L bar                                  ]  ← kLUFSBarWidth tall
//   [ gap                                         ]
//   [ PEAK-L bar                                  ]  ← kPeakBarWidth tall
//   [ gap                                         ]
//   [ PEAK-R bar                                  ]  ← kPeakBarWidth tall
//   [ gap                                         ]
//   [ LUFS-R bar                                  ]  ← kLUFSBarWidth tall
//   [ LUFS scale tick strip ]                        ← kLUFSScaleLabelWidth tall
//
// Bars run left-to-right (level grows toward the right). Peak indicator is a
// vertical white line at the peak-hold position. Clip zone is at the RIGHT
// edge of each bar (mirror of the vertical "top of bar = clip" convention).
// Same striped gradient + zone colours as vertical so the two orientations
// match pixel-for-pixel modulo rotation (BUG-08 AC #3).

void FaderMeter::paintHorizontal(juce::Graphics& g, juce::Rectangle<float> bounds) {
    paintMeterBackgroundHorizontal(g, bounds);
}

void FaderMeter::paintMeterBackgroundHorizontal(juce::Graphics& g,
                                                juce::Rectangle<float> meterArea) {
    // Top-row strip mode (TransportBar has set a strip context) SKIPS the grey
    // Colours::meterBackground fill — the visible grey rounded rectangle it
    // produces was the wrong look (the mockup had only a cool-neutral radial
    // on the dark TransportBar backdrop). The gradient is painted directly,
    // rounded-clipped, on top of TransportBar's backgroundPrimary fill. Every
    // other usage of FaderMeter (Mixer, Master FX, Player Card alias) leaves
    // stripContext_ empty and renders the original solid background card.
    if (!stripContext_.isEmpty()) {
        const auto myOriginInParent = getBoundsInParent().getPosition();
        const auto stripLocal = stripContext_.toFloat()
            .translated(static_cast<float>(-myOriginInParent.x),
                        static_cast<float>(-myOriginInParent.y));
        paintMeterStripGradient(g, meterArea, 2.0f, stripLocal);
    } else {
        // Background card
        g.setColour(Colours::meterBackground);
        g.fillRoundedRectangle(meterArea, 2.0f);
    }

    // Resolve bar heights based on layout mode (mirrors vertical FillAvailable).
    float lufsBarHeight = kLUFSBarWidth;
    float peakBarHeight = kPeakBarWidth;
    float topGutter = 0.0f;

    if (barWidthMode_ == BarWidthMode::FillAvailable) {
        // Preserve the 5:10:10:5 LUFS/peak/peak/LUFS ratio, grow to fill the
        // available vertical room between the two scale strips.
        const float available =
            meterArea.getHeight() - kScaleLabelWidth - kLUFSScaleLabelWidth - 3.0f * kBarGap;
        if (available > 0.0f) {
            constexpr float kRatioSum = 5.0f + 10.0f + 10.0f + 5.0f;  // 30
            lufsBarHeight = available * (5.0f / kRatioSum);
            peakBarHeight = available * (10.0f / kRatioSum);
        }
    } else {
        const float totalBarsHeight = kLUFSBarWidth + kBarGap + kPeakBarWidth + kBarGap
                                    + kPeakBarWidth + kBarGap + kLUFSBarWidth;
        const float totalHeight = kScaleLabelWidth + totalBarsHeight + kLUFSScaleLabelWidth;
        topGutter = (meterArea.getHeight() - totalHeight) / 2.0f;
    }

    float startY = meterArea.getY() + topGutter;
    const float barWidth = meterArea.getWidth();
    const float barX = meterArea.getX();

    // dB scale on top (mirror of left-side scale in vertical mode)
    auto scaleBounds = juce::Rectangle<float>(barX, startY, barWidth, kScaleLabelWidth);
    paintMixerScaleLabelsHorizontal(g, scaleBounds);
    startY += kScaleLabelWidth;

    // LUFS-L (narrow outer)
    auto lufsLBounds = juce::Rectangle<float>(barX, startY, barWidth, lufsBarHeight);
    paintLUFSBarHorizontal(g, lufsLBounds, displayLufs_);
    startY += lufsBarHeight + kBarGap;

    // PEAK-L (wide inner)
    auto peakLBounds = juce::Rectangle<float>(barX, startY, barWidth, peakBarHeight);
    paintPeakBarHorizontal(g, peakLBounds, displayPeakLeft_, peakHoldLeft_, clipLeft_);
    startY += peakBarHeight + kBarGap;

    // PEAK-R (wide inner)
    auto peakRBounds = juce::Rectangle<float>(barX, startY, barWidth, peakBarHeight);
    paintPeakBarHorizontal(g, peakRBounds, displayPeakRight_, peakHoldRight_, clipRight_);
    startY += peakBarHeight + kBarGap;

    // LUFS-R (narrow outer)
    auto lufsRBounds = juce::Rectangle<float>(barX, startY, barWidth, lufsBarHeight);
    paintLUFSBarHorizontal(g, lufsRBounds, displayLufs_);
    startY += lufsBarHeight;

    // LUFS scale labels on bottom (mirror of right-side scale in vertical mode)
    auto lufsScaleBounds = juce::Rectangle<float>(barX, startY, barWidth, kLUFSScaleLabelWidth);
    paintLUFSScaleLabelsHorizontal(g, lufsScaleBounds);
}

// static
void FaderMeter::paintStripedBarHorizontal(juce::Graphics& g,
                                           juce::Rectangle<float> bounds,
                                           float levelNormalized,
                                           bool isPeakBar) {
    if (levelNormalized <= 0.0f) return;

    float fillWidth = bounds.getWidth() * levelNormalized;
    auto fillBounds = bounds.withWidth(fillWidth);

    constexpr float stripeWidth = 2.0f;
    constexpr float gapWidth = 1.0f;
    constexpr float totalStripeWidth = stripeWidth + gapWidth;

    // Stripes run left-to-right; colour zone is keyed to the stripe's
    // x-position (not y as in vertical) so the gradient matches vertical
    // when the meter is mentally rotated 90° clockwise.
    float x = fillBounds.getX();
    while (x < fillBounds.getRight()) {
        float normalizedX = (x - bounds.getX()) / bounds.getWidth();
        juce::Colour color = isPeakBar ? getColorForPeakLevel(normalizedX)
                                       : getColorForLUFSLevel(normalizedX);
        g.setColour(color);
        float stripeLeft = juce::jmax(x, fillBounds.getX());
        float stripeRight = juce::jmin(x + stripeWidth, fillBounds.getRight());
        if (stripeRight > stripeLeft)
            g.fillRect(stripeLeft, fillBounds.getY(),
                       stripeRight - stripeLeft, fillBounds.getHeight());
        x += totalStripeWidth;
    }
}

// static
void FaderMeter::paintPeakBarHorizontal(juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        float displayLevel, float peakHold,
                                        bool clipActive) {
    // Dark background for this bar
    g.setColour(Colours::meterBackground.brighter(0.05f));
    g.fillRoundedRectangle(bounds, 1.0f);

    // Clip indicator on the RIGHT edge (mirror of "top of bar in vertical")
    auto clipZone = bounds.removeFromRight(4.0f);
    if (clipActive) {
        g.setColour(Colours::meterClip);
        g.fillRoundedRectangle(clipZone, 1.0f);
    }

    // Level bar with stripes (same colour zones as vertical)
    const float normalized = juce::jlimit(0.0f, 1.0f,
                                          (displayLevel - kMinDb) / (kMaxDb - kMinDb));
    paintStripedBarHorizontal(g, bounds, normalized, true);

    // Peak hold indicator (vertical white line, mirror of horizontal line in vertical)
    const float peakNorm = juce::jlimit(0.0f, 1.0f,
                                        (peakHold - kMinDb) / (kMaxDb - kMinDb));
    if (peakNorm > 0.0f) {
        float peakX = bounds.getX() + (bounds.getWidth() * peakNorm);
        if (peakX <= bounds.getRight()) {
            g.setColour(juce::Colours::white);
            g.fillRect(peakX, bounds.getY(), 2.0f, bounds.getHeight());
        }
    }
}

// static
void FaderMeter::paintLUFSBarHorizontal(juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        float levelLufs) {
    g.setColour(Colours::meterBackground.brighter(0.05f));
    g.fillRoundedRectangle(bounds, 1.0f);

    float normalized = (levelLufs - kLUFSMin) / (kLUFSMax - kLUFSMin);
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    paintStripedBarHorizontal(g, bounds, normalized, false);
}

// static
void FaderMeter::paintMixerScaleLabelsHorizontal(juce::Graphics& g,
                                                 juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(7.0f));
    g.setColour(Colours::textSecondary);

    const float dbMarks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    const float normalizedUnity = (kDefaultDb - kMinDb) / (kMaxDb - kMinDb);
    const float skewFactor = std::log(kUnityNormalized) / std::log(normalizedUnity);

    constexpr float kLabelCellWidth = 28.0f;

    for (float db : dbMarks) {
        const float normalized = (db - kMinDb) / (kMaxDb - kMinDb);
        const float skewed = std::pow(juce::jlimit(0.0f, 1.0f, normalized), skewFactor);
        // Horizontal: skewed = 1.0 → right edge (loud), 0.0 → left edge (silence).
        // Mirrors vertical's "skewed = 1.0 → top (loud), 0.0 → bottom (silence)".
        const float x = bounds.getX() + skewed * bounds.getWidth();

        // Tick mark at the bottom edge of the strip
        g.drawVerticalLine(static_cast<int>(x),
                           bounds.getBottom() - 3.0f,
                           bounds.getBottom());

        juce::String label;
        if (db > 0.0f) label = "+" + juce::String(static_cast<int>(db));
        else label = juce::String(static_cast<int>(db));

        g.drawText(label,
                   juce::Rectangle<float>(x - kLabelCellWidth * 0.5f, bounds.getY(),
                                          kLabelCellWidth, bounds.getHeight() - 3.0f),
                   juce::Justification::centred);
    }
}

// static
void FaderMeter::paintLUFSScaleLabelsHorizontal(juce::Graphics& g,
                                                juce::Rectangle<float> bounds) {
    g.setFont(juce::FontOptions(7.0f));
    g.setColour(Colours::textSecondary);

    const float lufsMarks[] = { 0.0f, -14.0f, -24.0f, -40.0f, -60.0f };
    float range = kLUFSMax - kLUFSMin;

    constexpr float kLabelCellWidth = 28.0f;

    for (float lufs : lufsMarks) {
        float normalized = (lufs - kLUFSMin) / range;
        // Horizontal: normalized = 1.0 → right edge (loudest), 0.0 → left
        // edge (-60 LUFS / silence). Mirrors vertical's bottom→top growth.
        float x = bounds.getX() + normalized * bounds.getWidth();

        // Tick mark at the top edge of the strip (mirror of vertical's
        // right-side tick line)
        g.drawVerticalLine(static_cast<int>(x),
                           bounds.getY(),
                           bounds.getY() + 3.0f);

        juce::String label = juce::String(static_cast<int>(lufs));
        g.drawText(label,
                   juce::Rectangle<float>(x - kLabelCellWidth * 0.5f,
                                          bounds.getY() + 3.0f,
                                          kLabelCellWidth,
                                          bounds.getHeight() - 3.0f),
                   juce::Justification::centred);
    }
}

}  // namespace otto::ui
