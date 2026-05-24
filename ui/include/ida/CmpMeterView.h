#pragma once

#include "ida/InternalFxConfigs.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ida::ui
{

/// Leaf compressor visualiser for `ChannelDetailCMPTab`. Two graphical
/// surfaces side by side:
///
///   1. **Transfer curve**   — input vs. output dB plot showing the
///      threshold knee + ratio slope (mirrors OTTO's `CompressorPanel`
///      idiom). Hovering the threshold lets the operator drag it via
///      the curve itself; ratio responds to vertical drag on the slope.
///   2. **Gain-reduction meter** — vertical bar reading 0..-24 dB.
///      Stays at 0 until the adapter surfaces live GR (future viz
///      upgrade, see `continue.md` EC7 queue). Drawn always so the
///      operator sees the chamber is there.
///
/// Listener fires the full `CmpConfig` on any control change so the
/// host pane can route into `setInternalCmpConfigAt` without
/// per-control plumbing.
class CmpMeterView : public juce::Component
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void cmpViewConfigChanged (const CmpConfig& cfg) = 0;
    };

    CmpMeterView();
    ~CmpMeterView() override;

    void setConfig (const CmpConfig& cfg);
    const CmpConfig& getConfig() const noexcept { return cfg_; }

    void setAccentColour (juce::Colour c);

    /// Push the live gain-reduction reading in dB (negative number, e.g.
    /// -3.2 = 3.2 dB of attenuation). Defaults to 0 (no reduction).
    /// Wired to the adapter's live GR once that surface lands.
    void setGainReductionDb (float dB);

    void addListener    (Listener* l) { listeners_.add (l); }
    void removeListener (Listener* l) { listeners_.remove (l); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

private:
    enum DragKind { kDragNone, kDragThreshold, kDragRatio };

    float dbToY    (float dB) const noexcept;
    float yToDb    (float y) const noexcept;
    float dbToX    (float dB) const noexcept;
    float xToDb    (float x) const noexcept;

    /// y_out = compressor transfer fn at x_in (dB). Mirrors OTTO's
    /// CompressorPanel transfer-curve formula.
    float transferDbOut (float dBin) const noexcept;

    void publishConfig();

    void drawTransfer (juce::Graphics& g) const;
    void drawGrMeter  (juce::Graphics& g) const;

    CmpConfig                 cfg_ {};
    juce::Colour              accent_ { juce::Colour (0xFFD9534F) };
    float                     liveGrDb_ { 0.0f };

    juce::Rectangle<int>      transferBounds_;
    juce::Rectangle<int>      grBounds_;

    DragKind                  drag_ { kDragNone };
    float                     dragStartDb_ { 0.0f };

    juce::ListenerList<Listener> listeners_;

    static constexpr float kDbMin = -60.0f;
    static constexpr float kDbMax = 0.0f;
    static constexpr float kGrMaxDb = -24.0f;   // meter range
    static constexpr int   kGrWidth = 28;       // px column for GR meter
    static constexpr int   kPad     = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CmpMeterView)
};

} // namespace ida::ui
