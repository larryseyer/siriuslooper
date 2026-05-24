#include "ida/EqCurveView.h"

#include "OTTOColours.h"

#include <algorithm>
#include <cmath>

namespace ida::ui
{

namespace
{

constexpr float kDbGridStepDb  = 6.0f;          ///< grid lines at -12, -6, 0, +6, +12
constexpr float kDbGridSpanDb  = 12.0f;         ///< grid covers ±12 dB (within ±18 view)

constexpr float kFreqLabels[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                   1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };

constexpr float kHpSlope = 24.0f;               ///< matches IDA EqConfig default
constexpr float kLpSlope = 24.0f;

juce::String formatHz (float hz)
{
    if (hz >= 1000.0f)
    {
        auto k = hz / 1000.0f;
        if (k >= 10.0f) return juce::String (juce::roundToInt (k)) + "k";
        return juce::String (k, 1) + "k";
    }
    return juce::String (juce::roundToInt (hz));
}

} // namespace

// =============================================================================
// Construction
// =============================================================================

EqCurveView::EqCurveView()
{
    setOpaque (true);
    setInterceptsMouseClicks (true, false);
}

EqCurveView::~EqCurveView() = default;

// =============================================================================
// State
// =============================================================================

void EqCurveView::setConfig (const EqConfig& cfg)
{
    cfg_ = cfg;
    repaint();
}

void EqCurveView::setAccentColour (juce::Colour c)
{
    accent_ = c;
    repaint();
}

float EqCurveView::bandFreq (int band) const noexcept
{
    switch (band)
    {
        case kHP:   return cfg_.hpFreq;
        case kLow:  return cfg_.lowFreq;
        case kMid:  return cfg_.midFreq;
        case kHigh: return cfg_.highFreq;
        case kLP:   return cfg_.lpFreq;
    }
    return 1000.0f;
}

float EqCurveView::bandGain (int band) const noexcept
{
    switch (band)
    {
        case kLow:  return cfg_.lowGain;
        case kMid:  return cfg_.midGain;
        case kHigh: return cfg_.highGain;
        default:    return 0.0f;   // HP / LP have no gain
    }
}

void EqCurveView::setBandFreq (int band, float hz)
{
    switch (band)
    {
        case kHP:   cfg_.hpFreq   = juce::jlimit (20.0f,   500.0f,  hz); break;
        case kLow:  cfg_.lowFreq  = juce::jlimit (40.0f,   500.0f,  hz); break;
        case kMid:  cfg_.midFreq  = juce::jlimit (100.0f,  10000.0f, hz); break;
        case kHigh: cfg_.highFreq = juce::jlimit (1000.0f, 16000.0f, hz); break;
        case kLP:   cfg_.lpFreq   = juce::jlimit (2000.0f, 20000.0f, hz); break;
        default: break;
    }
}

void EqCurveView::setBandGain (int band, float dB)
{
    const float clamped = juce::jlimit (-12.0f, 12.0f, dB);
    switch (band)
    {
        case kLow:  cfg_.lowGain  = clamped; break;
        case kMid:  cfg_.midGain  = clamped; break;
        case kHigh: cfg_.highGain = clamped; break;
        default: break;  // HP / LP gain locked at 0
    }
}

void EqCurveView::resetBand (int band)
{
    const EqConfig defaults {};
    switch (band)
    {
        case kHP:
            cfg_.hpFreq = defaults.hpFreq;
            break;
        case kLow:
            cfg_.lowFreq = defaults.lowFreq;
            cfg_.lowGain = defaults.lowGain;
            cfg_.lowQ    = defaults.lowQ;
            break;
        case kMid:
            cfg_.midFreq = defaults.midFreq;
            cfg_.midGain = defaults.midGain;
            cfg_.midQ    = defaults.midQ;
            break;
        case kHigh:
            cfg_.highFreq = defaults.highFreq;
            cfg_.highGain = defaults.highGain;
            cfg_.highQ    = defaults.highQ;
            break;
        case kLP:
            cfg_.lpFreq = defaults.lpFreq;
            break;
        default: break;
    }
}

// =============================================================================
// Response math (mirrors OTTO EQPanel::calculateEQResponse)
// =============================================================================

float EqCurveView::responseAt (float freqHz) const noexcept
{
    if (! cfg_.enabled) return 0.0f;

    float total = 0.0f;

    // HP: -slope dB/oct below cutoff (mirrors OTTO).
    if (freqHz < cfg_.hpFreq && cfg_.hpFreq > kFreqMin)
    {
        const float octavesBelow = std::log2 (cfg_.hpFreq / freqHz);
        total += -kHpSlope * octavesBelow;
    }

    // LP: -slope dB/oct above cutoff.
    if (freqHz > cfg_.lpFreq && cfg_.lpFreq < kFreqMax)
    {
        const float octavesAbove = std::log2 (freqHz / cfg_.lpFreq);
        total += -kLpSlope * octavesAbove;
    }

    // Bell curve for low shelf / mid / high shelf.
    auto addBell = [&] (float gainDb, float freq, float q)
    {
        if (std::abs (gainDb) < 0.01f) return;
        const float logRatio = std::log2 (freqHz / freq);
        const float width    = 1.0f / std::max (0.1f, q);
        total += gainDb * std::exp (-0.5f * (logRatio * logRatio) / (width * width));
    };
    addBell (cfg_.lowGain,  cfg_.lowFreq,  cfg_.lowQ);
    addBell (cfg_.midGain,  cfg_.midFreq,  cfg_.midQ);
    addBell (cfg_.highGain, cfg_.highFreq, cfg_.highQ);

    return juce::jlimit (-kDbRange, kDbRange, total);
}

// =============================================================================
// Coordinate mapping
// =============================================================================

float EqCurveView::frequencyToX (float hz) const noexcept
{
    const float lo = std::log10 (kFreqMin);
    const float hi = std::log10 (kFreqMax);
    const float v  = std::log10 (juce::jlimit (kFreqMin, kFreqMax, hz));
    const float n  = (v - lo) / (hi - lo);
    return curveBounds_.getX() + n * curveBounds_.getWidth();
}

float EqCurveView::xToFrequency (float x) const noexcept
{
    if (curveBounds_.getWidth() <= 0) return 1000.0f;
    const float n = juce::jlimit (0.0f, 1.0f,
                                  (x - curveBounds_.getX()) / (float) curveBounds_.getWidth());
    const float lo = std::log10 (kFreqMin);
    const float hi = std::log10 (kFreqMax);
    return std::pow (10.0f, lo + n * (hi - lo));
}

float EqCurveView::gainToY (float dB) const noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, (dB + kDbRange) / (2.0f * kDbRange));
    return curveBounds_.getBottom() - n * curveBounds_.getHeight();
}

float EqCurveView::yToGain (float y) const noexcept
{
    if (curveBounds_.getHeight() <= 0) return 0.0f;
    const float n = juce::jlimit (0.0f, 1.0f,
                                  (curveBounds_.getBottom() - y) / (float) curveBounds_.getHeight());
    return n * 2.0f * kDbRange - kDbRange;
}

// =============================================================================
// Hit testing + per-band styling
// =============================================================================

int EqCurveView::findNodeAt (juce::Point<float> p) const noexcept
{
    int best = -1;
    float bestDist = kHitRadiusPx;
    for (int b = 0; b < kBandCount; ++b)
    {
        const auto pos = juce::Point<float> (frequencyToX (bandFreq (b)),
                                             gainToY (bandGain (b)));
        const float d = p.getDistanceFrom (pos);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

juce::Colour EqCurveView::colourForBand (int band) const noexcept
{
    // Mirrors OTTO's per-band tinting: each band gets a distinct hue so the
    // five nodes remain distinguishable when curves overlap. The bell-band
    // colours stay close to the accent so the operator's strip colour reads.
    switch (band)
    {
        case kHP:   return juce::Colour (0xFFB0BEC5);
        case kLow:  return juce::Colour (0xFFE57373);
        case kMid:  return accent_;
        case kHigh: return juce::Colour (0xFF81C784);
        case kLP:   return juce::Colour (0xFFB0BEC5);
    }
    return accent_;
}

juce::String EqCurveView::nameForBand (int band) const noexcept
{
    switch (band)
    {
        case kHP:   return "HP";
        case kLow:  return "LOW";
        case kMid:  return "MID";
        case kHigh: return "HIGH";
        case kLP:   return "LP";
    }
    return {};
}

// =============================================================================
// Mouse
// =============================================================================

void EqCurveView::mouseDown (const juce::MouseEvent& e)
{
    draggingBand_ = findNodeAt (e.position);
}

void EqCurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBand_ < 0) return;
    setBandFreq (draggingBand_, xToFrequency (e.position.x));
    setBandGain (draggingBand_, yToGain      (e.position.y));
    publishConfig();
    repaint();
}

void EqCurveView::mouseUp (const juce::MouseEvent&)
{
    draggingBand_ = -1;
}

void EqCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = findNodeAt (e.position);
    if (b < 0) return;
    resetBand (b);
    publishConfig();
    repaint();
}

void EqCurveView::publishConfig()
{
    const auto snapshot = cfg_;
    listeners_.call ([&snapshot] (Listener& l) { l.eqCurveConfigChanged (snapshot); });
}

// =============================================================================
// Layout + paint
// =============================================================================

void EqCurveView::resized()
{
    auto b = getLocalBounds().reduced (kPadX, 0);
    b.removeFromTop (kPadTop);
    b.removeFromBottom (kFreqLabelHeight);
    curveBounds_ = b;
}

void EqCurveView::paint (juce::Graphics& g)
{
    g.fillAll (otto::Colours::bg2);
    drawGrid   (g);
    drawCurve  (g);
    drawNodes  (g);
    drawLabels (g);
}

void EqCurveView::drawGrid (juce::Graphics& g) const
{
    // Horizontal dB lines (every 6 dB, ±12 dB visible).
    g.setColour (otto::Colours::bg4);
    for (float dB = -kDbGridSpanDb; dB <= kDbGridSpanDb + 0.1f; dB += kDbGridStepDb)
    {
        const float y = gainToY (dB);
        const float lineAlpha = (std::abs (dB) < 0.01f) ? 1.0f : 0.5f;
        g.setColour (otto::Colours::bg4.withAlpha (lineAlpha));
        g.drawHorizontalLine (juce::roundToInt (y),
                              (float) curveBounds_.getX(),
                              (float) curveBounds_.getRight());
    }
    // Vertical frequency lines at decade boundaries + halves.
    for (float hz : kFreqLabels)
    {
        const float x = frequencyToX (hz);
        g.drawVerticalLine (juce::roundToInt (x),
                            (float) curveBounds_.getY(),
                            (float) curveBounds_.getBottom());
    }
}

void EqCurveView::drawCurve (juce::Graphics& g) const
{
    // Slice EC-Polish-fix: matches OTTO + FabFilter Pro-Q — a plain
    // stroked curve over the grid, no fill underneath. The fill-under
    // idiom belongs to spectrum analyzers, not the EQ response curve
    // itself; mixing the two reads as "different from FabFilter".
    if (curveBounds_.getWidth() <= 0) return;

    juce::Path curve;
    const int   N    = curveBounds_.getWidth();
    const float xMin = (float) curveBounds_.getX();
    const float xMax = (float) curveBounds_.getRight();

    bool started = false;
    for (int i = 0; i <= N; ++i)
    {
        const float t   = (float) i / (float) std::max (1, N);
        const float x   = xMin + t * (xMax - xMin);
        const float hz  = xToFrequency (x);
        const float dB  = responseAt (hz);
        const float y   = gainToY (dB);
        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else            curve.lineTo (x, y);
    }

    g.setColour (accent_);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

void EqCurveView::drawNodes (juce::Graphics& g) const
{
    for (int b = 0; b < kBandCount; ++b)
    {
        const auto pos = juce::Point<float> (frequencyToX (bandFreq (b)),
                                             gainToY (bandGain (b)));
        const float r = (b == draggingBand_) ? 10.0f : 8.0f;
        g.setColour (colourForBand (b).withAlpha (0.9f));
        g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
        g.setColour (otto::Colours::textPrimary);
        g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.5f);
    }
}

void EqCurveView::drawLabels (juce::Graphics& g) const
{
    g.setColour (otto::Colours::textSecondary);
    g.setFont (juce::FontOptions (10.0f));

    // Frequency labels on a row beneath the grid.
    const int labelY = curveBounds_.getBottom() + 2;
    for (float hz : kFreqLabels)
    {
        const int x = juce::roundToInt (frequencyToX (hz));
        g.drawSingleLineText (formatHz (hz), x, labelY + kFreqLabelHeight - 5,
                              juce::Justification::centred);
    }

    // dB labels on the left margin at +12 / 0 / -12.
    auto drawDbLabel = [&] (float dB)
    {
        const int y = juce::roundToInt (gainToY (dB));
        const auto text = (dB > 0.0f ? juce::String ("+") : juce::String())
                        + juce::String (juce::roundToInt (dB));
        g.drawSingleLineText (text,
                              curveBounds_.getX() - 6, y + 4,
                              juce::Justification::right);
    };
    drawDbLabel (12.0f);
    drawDbLabel (0.0f);
    drawDbLabel (-12.0f);

    // Slice EC-Polish-fix: no per-band labels next to nodes — OTTO + FabFilter
    // both keep the nodes naked; their color + position carry the meaning.
    // The text-above-node was reading as a visual upward tilt.
}

} // namespace ida::ui
