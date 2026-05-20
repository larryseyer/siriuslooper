#pragma once

/**
 * @file CompactFaderStrip.h
 * @brief Unified compact mixer strip replacing MixerStripUpper + MixerStripLower
 *
 * Single component containing (top to bottom):
 * - Color-coded channel name header
 * - Mute (M) and Solo (S) buttons
 * - Vertical fader with stereo meter
 * - Output-routing ComboBox (Instrument strips only: 32 stereo pairs)
 *
 * Supports all channel types: Instrument, FXReturn, Bus, Master.
 * Only Instrument strips expose the routing ComboBox — FX returns, buses, and
 * master use fixed downstream routing.
 * Selected channel is highlighted with an accent border.
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "../OTTOLookAndFeel.h"
#include "FaderMeter.h"

namespace otto::ui {

// =============================================================================
// ChannelType
// =============================================================================

enum class ChannelType { Instrument, FXReturn, Bus, Master };

// =============================================================================
// CompactFaderStripListener
// =============================================================================

class CompactFaderStripListener {
public:
    virtual ~CompactFaderStripListener() = default;

    virtual void stripMuteChanged(int channelIndex, ChannelType type, bool muted) {
        juce::ignoreUnused(channelIndex, type, muted);
    }
    virtual void stripSoloChanged(int channelIndex, ChannelType type, bool soloed) {
        juce::ignoreUnused(channelIndex, type, soloed);
    }
    virtual void stripGainChanged(int channelIndex, ChannelType type, float gain) {
        juce::ignoreUnused(channelIndex, type, gain);
    }
    virtual void stripChannelSelected(int channelIndex, ChannelType type) {
        juce::ignoreUnused(channelIndex, type);
    }
    virtual void stripOutputAssignmentChanged(int channelIndex, ChannelType type,
                                              int outputPairIndex) {
        juce::ignoreUnused(channelIndex, type, outputPairIndex);
    }
};

// =============================================================================
// CompactFaderStrip
// =============================================================================

class CompactFaderStrip : public juce::Component,
                          public FaderMeterListener {
public:
    // =========================================================================
    // Layout Constants
    // =========================================================================

    static constexpr int kStripWidth = static_cast<int>(otto::Sizing::kStripWidth);
    static constexpr int kMasterStripWidth = 96;
    // Reduced non-fader overhead so the FaderMeter occupies ~60-70% of strip
    // height at typical landscape mixer heights (>= ~280 px).
    static constexpr int kNameHeight = 22;
    static constexpr int kMuteSoloHeight = 36;
    static constexpr int kOutputComboHeight = 26;
    static constexpr int kFaderMinHeight = 80;
    static constexpr int kMeterWidth = 20;
    static constexpr int kPadding = static_cast<int>(otto::Sizing::kStripPadding);
    static constexpr int kGap = static_cast<int>(otto::Sizing::kSpacingSmall);
    static constexpr int kCornerRadius = static_cast<int>(otto::Sizing::kStripCornerRadius);
    static constexpr int kSelectionBorderWidth = 2;

    // 32 stereo output pairs — matches otto::mixer::GlobalMixer::kNumOutputPairs
    // and the Architecture constraint in CLAUDE.md (kNumSfzOutputs = 32).
    static constexpr int kNumOutputPairs = 32;

    // Sum of all fixed-height layout components when the output combo is
    // present. Bug 195 precedent: parent containers must size to this floor
    // so the FaderMeter does not get clipped below the viewport bottom.
    // Composition: name (22) + gap (4) + M/S (36) + gap (4) + faderMin (80)
    //            + gap (4) + combo (26) + gap (4) = 180.
    static constexpr int kPreferredHeight = kNameHeight + kGap
                                          + kMuteSoloHeight + kGap
                                          + kFaderMinHeight + kGap
                                          + kOutputComboHeight + kGap;
    [[nodiscard]] static constexpr int getPreferredHeight() { return kPreferredHeight; }

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    explicit CompactFaderStrip(int channelIndex = 0,
                               ChannelType type = ChannelType::Instrument);
    ~CompactFaderStrip() override;

    // =========================================================================
    // Channel Configuration
    // =========================================================================

    void setChannelIndex(int index);
    [[nodiscard]] int getChannelIndex() const noexcept { return channelIndex_; }

    void setChannelType(ChannelType type);
    [[nodiscard]] ChannelType getChannelType() const noexcept { return channelType_; }

    void setChannelColor(juce::Colour color);
    [[nodiscard]] juce::Colour getChannelColor() const { return channelColor_; }

    void setChannelName(const juce::String& name);
    [[nodiscard]] const juce::String& getChannelName() const noexcept { return channelName_; }

    // =========================================================================
    // Parameter Accessors (for UI sync from DSP)
    // =========================================================================

    void setGain(float gainLinear);
    [[nodiscard]] float getGain() const;

    void setMuted(bool muted);
    [[nodiscard]] bool isMuted() const noexcept { return muted_; }

    void setSoloed(bool soloed);
    [[nodiscard]] bool isSoloed() const noexcept { return soloed_; }

    // =========================================================================
    // Display State
    // =========================================================================

    void setEffectivelyMuted(bool effectivelyMuted);
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const noexcept { return selected_; }
    void setOutputComboVisible(bool visible);

    // =========================================================================
    // Output-Routing ComboBox (Instrument channels only)
    // =========================================================================

    void setOutputAssignment(int outputPairIndex);
    [[nodiscard]] int getOutputAssignment() const;

    // =========================================================================
    // Level Meter
    // =========================================================================

    void setLevel(float leftDb, float rightDb);
    void setLUFSLevel(float lufs);

    // =========================================================================
    // Listener Management
    // =========================================================================

    void addListener(CompactFaderStripListener* listener);
    void removeListener(CompactFaderStripListener* listener);

    // =========================================================================
    // Fader Thumb Access (for touch gesture hit-testing)
    // =========================================================================

    [[nodiscard]] juce::Rectangle<int> getFaderThumbBounds() const;

    // =========================================================================
    // FaderMeterListener
    // =========================================================================

    void faderMeterVolumeChanged(float newDb) override;

    // =========================================================================
    // Component Overrides
    // =========================================================================

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    // =========================================================================
    // Forward Declarations
    // =========================================================================

    class MuteButton;
    class SoloButton;

    // =========================================================================
    // Internal Helpers
    // =========================================================================

    void createControls();
    void layoutControls();
    void populateOutputCombo();
    [[nodiscard]] int getStripWidth() const;
    [[nodiscard]] bool hasSoloButton() const;
    [[nodiscard]] bool hasOutputCombo() const;

    void notifyMuteChanged();
    void notifySoloChanged();
    void notifyGainChanged();
    void notifyChannelSelected();
    void notifyOutputAssignmentChanged();

    // =========================================================================
    // State
    // =========================================================================

    int channelIndex_ = 0;
    ChannelType channelType_ = ChannelType::Instrument;
    juce::Colour channelColor_;
    juce::String channelName_;
    bool muted_ = false;
    bool soloed_ = false;
    bool effectivelyMuted_ = false;
    bool selected_ = false;

    // =========================================================================
    // Controls
    // =========================================================================

    std::unique_ptr<MuteButton> muteButton_;
    std::unique_ptr<SoloButton> soloButton_;
    std::unique_ptr<FaderMeter> faderMeter_;
    // Sirius substitutes juce::ComboBox for OTTO's TouchComboBox (which pulls in
    // the iOS touch-menu presenter stack). Sirius is desktop-first; the routing
    // combo's API surface used here (addItem/clear/selectedItemIndex/onChange)
    // is identical on juce::ComboBox, so the substitution is local to this type.
    std::unique_ptr<juce::ComboBox> outputCombo_;

    // Listeners
    juce::ListenerList<CompactFaderStripListener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactFaderStrip)
};

}  // namespace otto::ui
