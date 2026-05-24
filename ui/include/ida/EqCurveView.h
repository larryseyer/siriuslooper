#pragma once

#include "ida/InternalFxConfigs.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ida::ui
{

/// Leaf curve-display widget for `ChannelDetailEQTab`. Paints a 5-band
/// (HP / low shelf / mid peak / high shelf / LP) frequency response over
/// a logarithmic dB-vs-frequency grid, with one draggable node per band.
/// Mirrors OTTO's `EQPanel` visual idiom and uses the same response math
/// (see `external/OTTO/src/otto-plugin/ui/panels/EQPanel.cpp::calculateEQResponse`)
/// so a band edit looks identical here vs. the OTTO host. IDA-native: no
/// preset / spectrum / breakpoint dependencies — those slot in later
/// when the supporting subsystems land.
///
/// Mouse model:
///   * Left-drag a node     → freq (x) + gain (y) for shelves / mid peak,
///                            freq (x) only for HP / LP (gain is fixed at
///                            cutoff).
///   * Right-click / wheel  → Q (shelves / mid only; future slice).
///   * Double-click a node  → reset that band to its EqConfig default.
class EqCurveView : public juce::Component
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /// One control changed — the view publishes the full `EqConfig`
        /// rather than a per-control delta so the host pane can route
        /// straight into `effectChainHost_.setInternalEqConfigAt` without
        /// state reconstruction. Same contract as
        /// `ChannelDetailEQTabListener::eqTabConfigChanged`.
        virtual void eqCurveConfigChanged (const EqConfig& cfg) = 0;
    };

    EqCurveView();
    ~EqCurveView() override;

    /// Push a fresh config in (e.g. when the operator selects a new strip).
    /// Does NOT fire the listener.
    void setConfig (const EqConfig& cfg);
    const EqConfig& getConfig() const noexcept { return cfg_; }

    /// Tinge the curve. Defaults to the IDA accent colour; channel /
    /// phrase strips pass their own colour through here so the curve
    /// reads as theirs.
    void setAccentColour (juce::Colour c);

    /// The host tab's currently-selected band. The matching node renders
    /// larger / outlined so it reads as the active focus (mirrors OTTO's
    /// `drawCurveForeground::isSelected` branch). Defaults to HP.
    void setSelectedBand (int band);

    void addListener    (Listener* l) { listeners_.add (l); }
    void removeListener (Listener* l) { listeners_.remove (l); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    enum BandIndex : int { kHP = 0, kLow, kMid, kHigh, kLP, kBandCount };

    float bandFreq (int band) const noexcept;
    float bandGain (int band) const noexcept;        ///< 0 for HP/LP
    void  setBandFreq (int band, float hz);
    void  setBandGain (int band, float dB);
    void  resetBand   (int band);

    /// Total 5-band response in dB at `freqHz`, matching OTTO's
    /// `EQPanel::calculateEQResponse`.
    float responseAt (float freqHz) const noexcept;

    // Domain mapping (log-X frequency, linear-Y dB).
    float frequencyToX (float hz) const noexcept;
    float xToFrequency (float x) const noexcept;
    float gainToY (float dB) const noexcept;
    float yToGain (float y)  const noexcept;

    int   findNodeAt (juce::Point<float> p) const noexcept;
    juce::Colour colourForBand (int band) const noexcept;
    juce::String nameForBand   (int band) const noexcept;

    void publishConfig();

    // Drawing helpers.
    void drawGrid   (juce::Graphics& g) const;
    void drawCurve  (juce::Graphics& g) const;
    void drawNodes  (juce::Graphics& g) const;
    void drawLabels (juce::Graphics& g) const;

    EqConfig                 cfg_ {};
    juce::Colour             accent_ { juce::Colour (0xFFD9534F) }; // IDA accent default
    juce::Rectangle<int>     curveBounds_;

    int                      draggingBand_  { -1 };
    int                      selectedBand_  { kHP };

    juce::ListenerList<Listener> listeners_;

    static constexpr float kDbRange         = 18.0f;   // ±18 dB
    static constexpr float kFreqMin         = 20.0f;
    static constexpr float kFreqMax         = 20000.0f;
    static constexpr float kHitRadiusPx     = 12.0f;
    static constexpr int   kFreqLabelHeight = 18;
    static constexpr int   kPadX            = 28;
    static constexpr int   kPadTop          = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurveView)
};

} // namespace ida::ui
