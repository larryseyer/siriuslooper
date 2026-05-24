#include "ida/CmpMeterView.h"

#include "OTTOColours.h"

#include <algorithm>
#include <cmath>

namespace ida::ui
{

namespace
{

constexpr float kGridStepDb = 6.0f;    // grid lines at -6, -12, -18 ... -54

} // namespace

// =============================================================================
// Construction
// =============================================================================

CmpMeterView::CmpMeterView()
{
    setOpaque (true);
    setInterceptsMouseClicks (true, false);
}

CmpMeterView::~CmpMeterView() = default;

// =============================================================================
// State
// =============================================================================

void CmpMeterView::setConfig (const CmpConfig& cfg)
{
    cfg_ = cfg;
    repaint();
}

void CmpMeterView::setAccentColour (juce::Colour c)
{
    accent_ = c;
    repaint();
}

void CmpMeterView::setGainReductionDb (float dB)
{
    liveGrDb_ = std::min (0.0f, dB);
    repaint();
}

// =============================================================================
// Coordinate mapping (transfer plot — both axes are -60 .. 0 dB)
// =============================================================================

float CmpMeterView::dbToY (float dB) const noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, (dB - kDbMin) / (kDbMax - kDbMin));
    return transferBounds_.getBottom() - n * transferBounds_.getHeight();
}

float CmpMeterView::yToDb (float y) const noexcept
{
    if (transferBounds_.getHeight() <= 0) return 0.0f;
    const float n = juce::jlimit (0.0f, 1.0f,
                                  (transferBounds_.getBottom() - y) / (float) transferBounds_.getHeight());
    return kDbMin + n * (kDbMax - kDbMin);
}

float CmpMeterView::dbToX (float dB) const noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, (dB - kDbMin) / (kDbMax - kDbMin));
    return transferBounds_.getX() + n * transferBounds_.getWidth();
}

float CmpMeterView::xToDb (float x) const noexcept
{
    if (transferBounds_.getWidth() <= 0) return 0.0f;
    const float n = juce::jlimit (0.0f, 1.0f,
                                  (x - transferBounds_.getX()) / (float) transferBounds_.getWidth());
    return kDbMin + n * (kDbMax - kDbMin);
}

// =============================================================================
// Transfer-curve formula (mirrors a soft-knee downward compressor)
// =============================================================================

float CmpMeterView::transferDbOut (float dBin) const noexcept
{
    // Hard-knee downward compressor with makeup: below threshold passes
    // through 1:1; above threshold the excess is divided by the ratio.
    // Mix blends between dry and compressed (1.0 = fully compressed).
    const float t  = cfg_.threshold;
    const float r  = std::max (1.0f, cfg_.ratio);
    const float mu = cfg_.makeupDb;
    const float mx = juce::jlimit (0.0f, 1.0f, cfg_.mix);

    float compressed = dBin;
    if (dBin > t)
        compressed = t + (dBin - t) / r;
    compressed += mu;

    return dBin * (1.0f - mx) + compressed * mx;
}

// =============================================================================
// Mouse
// =============================================================================

void CmpMeterView::mouseDown (const juce::MouseEvent& e)
{
    if (! transferBounds_.contains (e.getPosition())) { drag_ = kDragNone; return; }

    // Picking heuristic: clicks near the unity line (above threshold) drag
    // the ratio; clicks near the threshold elbow (or below) drag threshold.
    const float clickDb = xToDb (e.position.x);
    const float thresholdY = dbToY (cfg_.threshold);
    if (std::abs (e.position.y - thresholdY) < 18.0f && clickDb < cfg_.threshold + 6.0f)
    {
        drag_ = kDragThreshold;
        dragStartDb_ = cfg_.threshold;
    }
    else
    {
        drag_ = kDragRatio;
        dragStartDb_ = cfg_.ratio;
    }
}

void CmpMeterView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag_ == kDragThreshold)
    {
        cfg_.threshold = juce::jlimit (-60.0f, 0.0f, xToDb (e.position.x));
        publishConfig();
        repaint();
    }
    else if (drag_ == kDragRatio)
    {
        // Vertical drag changes ratio (1:1 at the top of the box, 20:1 at the
        // bottom). Keeps the gesture intuitive — pulling the slope flatter
        // == higher ratio.
        const float dy = e.position.y - (float) e.mouseDownPosition.y;
        const float pixelsPerStep = 18.0f;
        const float newRatio = juce::jlimit (1.0f, 20.0f,
                                             dragStartDb_ + dy / pixelsPerStep);
        cfg_.ratio = newRatio;
        publishConfig();
        repaint();
    }
}

void CmpMeterView::mouseUp (const juce::MouseEvent&)
{
    drag_ = kDragNone;
}

void CmpMeterView::publishConfig()
{
    const auto snapshot = cfg_;
    listeners_.call ([&snapshot] (Listener& l) { l.cmpViewConfigChanged (snapshot); });
}

// =============================================================================
// Layout + paint
// =============================================================================

void CmpMeterView::resized()
{
    auto b = getLocalBounds().reduced (kPad);
    grBounds_       = b.removeFromRight (kGrWidth);
    b.removeFromRight (kPad);
    transferBounds_ = b;
}

void CmpMeterView::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);
    drawTransfer (g);
    drawGrMeter  (g);
}

void CmpMeterView::drawTransfer (juce::Graphics& g) const
{
    // Frame.
    g.setColour (otto::Colours::bg3);
    g.fillRect (transferBounds_);
    g.setColour (otto::Colours::bg4);
    g.drawRect (transferBounds_, 1);

    // Grid (6 dB steps on both axes).
    for (float dB = kDbMin + kGridStepDb; dB < kDbMax; dB += kGridStepDb)
    {
        const int y = juce::roundToInt (dbToY (dB));
        const int x = juce::roundToInt (dbToX (dB));
        g.setColour (otto::Colours::bg4);
        g.drawHorizontalLine (y, (float) transferBounds_.getX(),
                              (float) transferBounds_.getRight());
        g.drawVerticalLine   (x, (float) transferBounds_.getY(),
                              (float) transferBounds_.getBottom());
    }

    // Unity reference (input == output, no compression).
    g.setColour (otto::Colours::textDisabled);
    g.drawLine ((float) transferBounds_.getX(),     (float) transferBounds_.getBottom(),
                (float) transferBounds_.getRight(), (float) transferBounds_.getY(),
                1.0f);

    // Transfer curve under current config.
    juce::Path curve;
    const int N = transferBounds_.getWidth();
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / (float) std::max (1, N);
        const float dBin  = kDbMin + t * (kDbMax - kDbMin);
        const float dBout = transferDbOut (dBin);
        const float x = dbToX (dBin);
        const float y = dbToY (juce::jlimit (kDbMin, kDbMax, dBout));
        if (i == 0) curve.startNewSubPath (x, y);
        else        curve.lineTo (x, y);
    }
    g.setColour (accent_);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Threshold knee marker.
    const float tx = dbToX (cfg_.threshold);
    const float ty = dbToY (cfg_.threshold);
    g.setColour (accent_.withAlpha (0.85f));
    g.fillEllipse (tx - 6.0f, ty - 6.0f, 12.0f, 12.0f);
    g.setColour (otto::Colours::textPrimary);
    g.drawEllipse (tx - 6.0f, ty - 6.0f, 12.0f, 12.0f, 1.2f);

    // Threshold guide lines (faded crosshairs from the axes to the knee).
    g.setColour (accent_.withAlpha (0.25f));
    g.drawVerticalLine   (juce::roundToInt (tx), ty, (float) transferBounds_.getBottom());
    g.drawHorizontalLine (juce::roundToInt (ty), (float) transferBounds_.getX(), tx);

    // dB axis labels (input on bottom, output on left).
    g.setColour (otto::Colours::textSecondary);
    g.setFont (juce::FontOptions (10.0f));
    for (float dB = kDbMin; dB <= kDbMax + 0.1f; dB += 12.0f)
    {
        const int x = juce::roundToInt (dbToX (dB));
        const int y = juce::roundToInt (dbToY (dB));
        g.drawSingleLineText (juce::String (juce::roundToInt (dB)),
                              x, transferBounds_.getBottom() + 12,
                              juce::Justification::centred);
        g.drawSingleLineText (juce::String (juce::roundToInt (dB)),
                              transferBounds_.getX() - 6, y + 4,
                              juce::Justification::right);
    }
}

void CmpMeterView::drawGrMeter (juce::Graphics& g) const
{
    // Frame.
    g.setColour (otto::Colours::bg3);
    g.fillRect (grBounds_);
    g.setColour (otto::Colours::bg4);
    g.drawRect (grBounds_, 1);

    // Fill from top by liveGrDb_ (0..kGrMaxDb mapped to 0..1 of height).
    const float n = juce::jlimit (0.0f, 1.0f, liveGrDb_ / kGrMaxDb);
    const int   fillH = juce::roundToInt (n * (float) grBounds_.getHeight());
    auto fillRect = juce::Rectangle<int> (grBounds_.getX() + 2,
                                          grBounds_.getY() + 2,
                                          grBounds_.getWidth() - 4,
                                          fillH);
    g.setColour (accent_.withAlpha (0.75f));
    g.fillRect (fillRect);

    // Scale ticks.
    g.setColour (otto::Colours::textDisabled);
    for (float dB = 0.0f; dB >= kGrMaxDb - 0.1f; dB -= 6.0f)
    {
        const float t = juce::jlimit (0.0f, 1.0f, dB / kGrMaxDb);
        const int y = grBounds_.getY()
                    + juce::roundToInt (t * (float) grBounds_.getHeight());
        g.drawHorizontalLine (y, (float) grBounds_.getX(),
                              (float) grBounds_.getRight());
    }

    g.setColour (otto::Colours::textSecondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawSingleLineText ("GR", grBounds_.getCentreX(),
                          grBounds_.getY() - 2,
                          juce::Justification::centred);
}

} // namespace ida::ui
