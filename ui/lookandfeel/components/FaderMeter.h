#pragma once

/**
 * @file FaderMeter.h
 * @brief Unified fader+meter component with fader overlaid on stereo meter bars
 *
 * Layout (left to right):
 * [dB scale] [LUFS-L] [PEAK-L] [fader track + thumb] [PEAK-R] [LUFS-R]
 *
 * Meter renders first, fader thumb and track overlaid on top.
 * Reusable in: Spectrum page (Master), Channel Detail Fader tab, Master strip.
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "../OTTOColours.h"

namespace otto::ui {

class FaderMeterListener {
public:
    virtual ~FaderMeterListener() = default;
    virtual void faderMeterVolumeChanged(float newDb) = 0;
};

class FaderMeter : public juce::Component,
                   private juce::Timer {
public:
    // Meter bar layout mode.
    // - Fixed: bars use kPeakBarWidth / kLUFSBarWidth verbatim; natural content
    //   width is ~76 px (22 dB scale + 36 bar cluster + 18 LUFS scale). Used by
    //   Mixer CompactFaderStrip, MasterFXPanel.
    // - FillAvailable: bars scale proportionally so the whole content fills the
    //   component width with no visible empty gutter. Used by
    //   FullScreenSpectrumPanel's 96-px master column.
    enum class BarWidthMode { Fixed, FillAvailable };

    // Orientation of the meter chrome. Vertical is the canonical layout
    // (peak readouts on top, name + LUFS readout on bottom, bars run vertically);
    // Horizontal rotates the bar cluster 90° and is the meter-only top-row form
    // (TransportBar master meter, BUG-08). In Horizontal mode the readouts and
    // channel name are suppressed (no vertical room); the fader overlay is also
    // unsupported in Horizontal so callers must `setFaderEnabled(false)` before
    // switching orientation.
    enum class Orientation { Vertical, Horizontal };

    FaderMeter();
    ~FaderMeter() override;

    void setBarWidthMode(BarWidthMode mode);
    BarWidthMode getBarWidthMode() const { return barWidthMode_; }

    void setOrientation(Orientation orientation);
    Orientation getOrientation() const { return orientation_; }

    // Top-row meter-strip gradient context. When set to a non-empty rect (in
    // PARENT coordinates — i.e., TransportBar's local space), the horizontal
    // master-meter background paints a cool-neutral radial gradient overlay
    // computed from this rect's center, so the master meter and the spectrum
    // display read as ONE continuous gradient strip on iPad. Empty (default)
    // = no gradient overlay (every other usage of FaderMeter — Mixer fader
    // strip, Master FX panel, Player Card alias — keeps the prior solid
    // meterBackground fill unchanged).
    void setStripContext(juce::Rectangle<int> stripInParentCoords) {
        stripContext_ = stripInParentCoords;
        repaint();
    }

    // Channel identity
    void setChannelName(const juce::String& name);
    const juce::String& getChannelName() const { return channelName_; }

    // Fader control (-60 to +6 dB)
    void setVolume(float dB);
    float getVolume() const { return volumeDb_; }

    // Stereo peak meter levels
    void setPeakLevels(float leftDb, float rightDb);

    // LUFS short-term loudness (3 s window)
    void setLUFS(float lufs);

    // Clip control
    void resetClip();
    bool hasClipIndicator() const { return clipLeft_ || clipRight_; }

    // Fader enable/disable (false = meter-only mode for standalone meter usage)
    void setFaderEnabled(bool enabled);
    bool isFaderEnabled() const { return faderEnabled_; }

    // Listener
    void addListener(FaderMeterListener* listener);
    void removeListener(FaderMeterListener* listener);

    // Bare-click callback (juce::Button::onClick-style). Fires from mouseDown
    // ONLY when the fader is disabled (meter-only mode — currently exclusive to
    // the TransportBar master meter, BUG-02). Lets a parent route the click
    // through any existing opener helper (e.g. SpectrumDisplay::Listener) WITHOUT
    // adding a parallel dispatch path. Right-click / context-menu gestures
    // (e.mods.isPopupMenu()) are filtered out so future right-click affordances
    // can coexist with this callback. No-op when faderEnabled_ is true.
    std::function<void()> onMeterAreaClicked;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Geometry
    juce::Rectangle<float> getThumbBounds() const;

    // Width of the dB scale-label column used by paintMixerScaleLabels.
    // Exposed so other panels can reserve matching space beside their sliders.
    static constexpr float kMixerScaleLabelWidth = 22.0f;

    // Draw the mixer-fader-style dB tick scale (6 / 0 / -6 / -12 / -24 / -48)
    // into the given bounds. Used by FaderMeter itself and by parameter-slider
    // panels that want their tick column to match the mixer fader look.
    static void paintMixerScaleLabels(juce::Graphics& g,
                                      juce::Rectangle<float> bounds);

    // =========================================================================
    // Shared LUFS-paint helpers (UIPOL-04)
    // =========================================================================
    // The short-term-LUFS bar + readout and their colour zones are shared with
    // LevelMeter's horizontal master-meter overlay so both converge on a single
    // implementation (no duplicate paint code per Bug 33). The helpers are pure
    // functions of (graphics, bounds, value); they read no instance state.

    // LUFS display range (-60 to 0 dB short-term). Exposed so callers using
    // these helpers can write a meaningful "silence" floor.
    static constexpr float kLUFSMin = -60.0f;
    static constexpr float kLUFSMax = 0.0f;

    // Draw a thin LUFS level bar with green→yellow→red zones matching FaderMeter.
    static void paintLUFSBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                             float levelLufs);

    // Draw the short-term LUFS numeric readout (e.g., "L-S  -14.3" or "L-S  -inf").
    static void paintLUFSReadout(juce::Graphics& g, juce::Rectangle<float> bounds,
                                 float displayLufs);

    // =========================================================================
    // Shared peak/LUFS bar primitives (UNIFIED-30)
    // =========================================================================
    // Exposed PUBLIC so the PlayerCard dual-format meter (alias of the Mixer
    // Player Channel meter per UNIFIED_ROUTING_REDESIGN § 14 + § 15) can render
    // pixel-identical bars without instantiating a FaderMeter (which carries
    // chrome — scale labels, channel name, peak readouts, fader thumb — that
    // does not fit the player-card meter slot). The helpers are pure functions
    // of (graphics, bounds, value); they read no instance state.
    //
    // Striped fill: 2 px stripe + 1 px gap, painted bottom-to-top up to the
    // normalized fill height. `isPeakBar` selects the colour ramp via
    // `getColorForPeakLevel`; otherwise `getColorForLUFSLevel` is used.
    static void paintStripedBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                                float levelNormalized, bool isPeakBar);

    // Draw a wide peak-meter bar with striped fill, top-edge clip-zone, and a
    // 2 px white peak-hold line at the parametric peak position. Mirrors the
    // canonical chrome used by FaderMeter's stereo peak bars (Sprint 29).
    static void paintPeakBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                             float displayLevel, float peakHold, bool clipActive);

    // Map a normalized [0,1] peak position to the canonical peak-meter colour
    // ramp (green low → yellow mid → red top zone). Single source of truth so
    // every alias of the player-bus meter renders the same colour stops.
    static juce::Colour getColorForPeakLevel(float normalized);

    // Map a normalized [0,1] LUFS position to the canonical LUFS colour ramp
    // (typically narrower mid-dynamic green → yellow band than the peak ramp,
    // matching the audio-loudness convention of penalising clipping less
    // aggressively than peak meters).
    static juce::Colour getColorForLUFSLevel(float normalized);

    // =========================================================================
    // Shared dB-scale calibration (Sprint 29 BUG-16)
    // =========================================================================
    //
    // dB range, unity reference, and the skewed (logarithmic) amplitude-to-Y
    // transform are exposed PUBLIC so SpectrumDisplay (and any future amplitude
    // visualization) can match the meter's bar position pixel-for-pixel for the
    // same dB value. Single source of truth eliminates the prior calibration
    // drift where the spectrum used a [-60, 0] linear scale while the meter
    // used a [-60, +6] skewed scale — same audio rendered at different Y on
    // the two surfaces (Bug 205). Industry-standard mixer-fader convention
    // (FabFilter / UAD / Waves / Logic / Pro Tools): -60 to +6 dB window with
    // 0 dB at 77.7% of bar height from bottom, power-law skew giving more
    // visual range to higher-dB values.
    static constexpr float kMinDb           = -60.0f;
    static constexpr float kMaxDb           =   6.0f;
    static constexpr float kDefaultDb       =   0.0f;
    static constexpr float kUnityNormalized =   0.777f;  // 0 dB = 77.7% from bottom

    // Convert dB to Y position within a vertical bar of given height. JUCE
    // pixel convention: Y increases downward, so the returned value goes from
    // `top` (at +6 dB) to `top + height` (at -60 dB). Static so amplitude
    // visualizations can call it without a FaderMeter instance.
    static float dbToY(float dB, float top, float height);

    // Inverse of dbToY: convert a Y pixel position back to dB.
    static float yToDb(float y, float top, float height);

    // dB → skewed [0, 1] (0 = silence floor, 1 = ceiling, 0.777 = 0 dBFS).
    // Underlies dbToY; exposed for callers that want the normalized form
    // (e.g., a horizontal bar fill from left-to-right where the same skew
    // applies but Y-flip is unwanted).
    static float dbToSkewed(float dB);
    static float skewedToDb(float skewed);

    // Linear normalization within the [kMinDb, kMaxDb] window (no skew).
    // Used by colour-zone helpers that want the raw 0..1 dB position.
    static float dbToNormalized(float dB);
    static float normalizedToDb(float normalized);

private:
    // =========================================================================
    // Constants
    // =========================================================================
    // (kMinDb / kMaxDb / kDefaultDb / kUnityNormalized moved to public above
    // for shared spectrum calibration, Sprint 29 BUG-16.)
    static constexpr float kClipThreshold = 0.0f;

    // (kLUFSMin / kLUFSMax moved to public above for shared LUFS helpers.)

    // Bar sizing
    static constexpr float kPeakBarWidth = 10.0f;
    static constexpr float kLUFSBarWidth = 5.0f;
    static constexpr float kBarGap = 2.0f;
    static constexpr float kScaleLabelWidth = 22.0f;

    // Fader
    static constexpr float kThumbWidth = 140.0f;
    static constexpr float kThumbHeight = 56.0f;
    static constexpr float kTrackWidth = 4.0f;

    // Layout regions
    static constexpr float kReadoutHeight = 14.0f;
    static constexpr float kLUFSReadoutHeight = 14.0f;
    static constexpr float kNameHeight = 16.0f;
    static constexpr float kLUFSScaleLabelWidth = 18.0f;

    // Ballistics
    static constexpr float kDecayRate = 1800.0f;
    static constexpr float kPeakHoldMs = 2000.0f;
    static constexpr float kPeakFadeMs = 200.0f;

    // =========================================================================
    // Timer
    // =========================================================================

    void timerCallback() override;

    // =========================================================================
    // Paint Helpers
    // =========================================================================

    void paintMeterBackground(juce::Graphics& g, juce::Rectangle<float> meterArea);
    // (paintStripedBar / paintPeakBar promoted to public static above for
    //  PlayerCard dual-format meter alias — UNIFIED-30.)
    void paintFaderOverlay(juce::Graphics& g, juce::Rectangle<float> meterArea);
    void paintScaleLabels(juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintLUFSScaleLabels(juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintPeakReadouts(juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintChannelName(juce::Graphics& g, juce::Rectangle<float> bounds);

    // Horizontal-mode painters (BUG-08). Mirror their vertical siblings — same
    // gradient stops, same peak indicator, same dB scale ticks, same stereo
    // channel split — but the bars run left→right and stack top→bottom (LUFS-L
    // / PEAK-L / PEAK-R / LUFS-R from top to bottom). Only `paintHorizontal`
    // is non-static; the bar primitives are static so they can be invoked from
    // any future caller that wants pixel-identical horizontal bars.
    void paintHorizontal(juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintMeterBackgroundHorizontal(juce::Graphics& g,
                                        juce::Rectangle<float> meterArea);
    static void paintStripedBarHorizontal(juce::Graphics& g,
                                          juce::Rectangle<float> bounds,
                                          float levelNormalized,
                                          bool isPeakBar);
    static void paintPeakBarHorizontal(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       float displayLevel, float peakHold,
                                       bool clipActive);
    static void paintLUFSBarHorizontal(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       float levelLufs);
    static void paintMixerScaleLabelsHorizontal(juce::Graphics& g,
                                                juce::Rectangle<float> bounds);
    static void paintLUFSScaleLabelsHorizontal(juce::Graphics& g,
                                               juce::Rectangle<float> bounds);

    // =========================================================================
    // Conversion
    // =========================================================================
    // (dbToY / yToDb / dbToSkewed / skewedToDb / dbToNormalized / normalizedToDb
    //  moved to public static API above for shared spectrum calibration,
    //  Sprint 29 BUG-16.)
    // (getColorForPeakLevel / getColorForLUFSLevel promoted to public static
    //  above for PlayerCard dual-format meter alias — UNIFIED-30.)

    // =========================================================================
    // Hit testing
    // =========================================================================

    juce::Rectangle<float> getMeterArea() const;
    bool isInThumbArea(const juce::MouseEvent& e) const;

    // =========================================================================
    // State
    // =========================================================================

    juce::String channelName_{"Channel"};
    float volumeDb_ = kDefaultDb;
    bool isDragging_ = false;
    float dragStartDb_ = 0.0f;

    // Target levels (from audio thread)
    float peakLeftDb_ = kMinDb;
    float peakRightDb_ = kMinDb;
    float lufsTarget_ = kLUFSMin;

    // Display levels (smoothed)
    float displayPeakLeft_ = kMinDb;
    float displayPeakRight_ = kMinDb;
    float displayLufs_ = kLUFSMin;

    // Peak hold
    float peakHoldLeft_ = kMinDb;
    float peakHoldRight_ = kMinDb;
    juce::int64 peakHoldTimeLeft_ = 0;
    juce::int64 peakHoldTimeRight_ = 0;

    // Clip indicators
    bool clipLeft_ = false;
    bool clipRight_ = false;

    juce::int64 lastUpdateTime_ = 0;

    // Fader
    bool faderEnabled_ = true;
    juce::Image faderKnobImage_;

    // Bar layout mode (see BarWidthMode above).
    BarWidthMode barWidthMode_ = BarWidthMode::Fixed;

    // Orientation (see Orientation above).
    Orientation orientation_ = Orientation::Vertical;

    // Master meter-strip gradient context (see setStripContext). Empty = no overlay.
    juce::Rectangle<int> stripContext_;

    juce::ListenerList<FaderMeterListener> listeners_;
    bool isUpdatingFromExternal_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaderMeter)
};

}  // namespace otto::ui
