#pragma once

/**
 * @file ChannelDetailPanWidTab.h
 * @brief Pan/Width tab for Channel Detail - PAN and WIDTH knobs
 *
 * Shows two large rotary knobs for pan position and stereo width.
 * Pan: -1.0 (L) to 1.0 (R), Width: 0.0 (mono) to 2.0 (wide).
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "../OTTOLookAndFeel.h"
#include "CompactFaderStrip.h"

namespace otto::ui {

// =============================================================================
// ChannelDetailPanWidTabListener
// =============================================================================

class ChannelDetailPanWidTabListener {
public:
    virtual ~ChannelDetailPanWidTabListener() = default;

    virtual void panWidTabPanChanged(int channelIndex, ChannelType type, float pan) {
        juce::ignoreUnused(channelIndex, type, pan);
    }
    virtual void panWidTabWidthChanged(int channelIndex, ChannelType type, float width) {
        juce::ignoreUnused(channelIndex, type, width);
    }
};

// =============================================================================
// ChannelDetailPanWidTab
// =============================================================================

class ChannelDetailPanWidTab : public juce::Component {
public:
    static constexpr int kLabelHeight = 20;
    static constexpr int kMinKnobSize = 60;
    static constexpr int kMaxKnobSize = 500;
    static constexpr int kKnobGap = 16;

    ChannelDetailPanWidTab();
    ~ChannelDetailPanWidTab() override;

    void setChannel(int channelIndex, ChannelType type);
    void setChannelColor(juce::Colour color);

    void setPan(float pan);
    void setWidth(float width);
    [[nodiscard]] float getPan() const;
    [[nodiscard]] float getWidth() const;

    void addListener(ChannelDetailPanWidTabListener* listener);
    void removeListener(ChannelDetailPanWidTabListener* listener);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void notifyPanChanged();
    void notifyWidthChanged();

    int channelIndex_ = 0;
    ChannelType channelType_ = ChannelType::Instrument;
    juce::Colour channelColor_;

    std::unique_ptr<juce::Slider> panKnob_;
    std::unique_ptr<juce::Slider> widthKnob_;
    std::unique_ptr<juce::Label> panLabel_;
    std::unique_ptr<juce::Label> widthLabel_;

    juce::ListenerList<ChannelDetailPanWidTabListener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelDetailPanWidTab)
};

}  // namespace otto::ui
