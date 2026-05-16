#pragma once

#include "CapabilityTier.h"
#include "DemoSession.h"

#include "sirius/CaptureSession.h"
#include "sirius/InputDescriptor.h"
#include "sirius/LatencyBudget.h"
#include "sirius/PerformanceView.h"
#include "sirius/PluginScanner.h"
#include "sirius/Promotion.h"
#include "sirius/TapeId.h"
#include "sirius/UndoStack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <unordered_set>

namespace sirius
{

/// The standalone app's top-level window content: a TabbedComponent surfacing
/// every M3 → M8 component the operator needs to see, plus a common bottom
/// bar that drives a playhead slider and the sacred undo/redo gestures
/// (white paper Part 14.7) across every tab.
///
/// The four tabs map directly to the cognitive modes the white paper Part
/// XIV calls out:
///
///   * Performance — glanceable, eyes-free (14.5, 14.9)
///   * Preparation — fine, precise, allowed-to-look (14.6) plus a diagnostics
///     row honest about tier, latency budget, and undo state (13.3 rule 3)
///   * Plugins     — the M5 hosting surface; descriptors live here, the
///     parameter view will when a real plugin is loaded
///   * Video       — the M6 preview surface, empty until the FFmpeg pipeline
///     lands (operator-deferred)
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    // --- per-tick view refresh ---
    void refreshPerformance();
    void refreshPreparation();
    void refreshTimeline();
    void refreshDiagnostics();

    // --- per-tape arm + focus (refined Mockup A, this session's UX) ---
    void toggleArm    (TapeId tape);
    void setFocused   (TapeId tape);
    std::vector<TapeId> armedTapesVec() const;

    // --- editing demo for undo/redo ---
    void onUndo();
    void onRedo();

    // --- arm / disarm gesture (white paper 14.6 — coarse, decisive) ---
    void onArmToggle();
    void onMarkIn();
    void onMarkOut();
    void refreshCaptureControls();

    // --- plugin scanning ---
    void chooseFolderAndScan();

    // --- session save/load (round-trips the current undo-stack top) ---
    void chooseFileAndSave();
    void chooseFileAndLoad();
    void reloadDemo();

    // --- session state (drives every view) ---
    DemoSession  demo_;
    UndoStack    undoStack_;
    Rational     sessionLengthSeconds_;

    // --- runtime helpers ---
    LatencyBudget latencyBudget_;
    juce::int64   expectedTickMicros_ { 0 };
    CapabilityTier tier_            { CapabilityTier::Survival };
    TierPolicy     tierPolicy_      { policyFor (CapabilityTier::Survival) };

    // --- top-level layout ---
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    // --- Performance tab ---
    PerformanceView performanceView_;

    // --- Preparation tab ---
    class PreparationPane;
    std::unique_ptr<PreparationPane> preparationPane_;

    // --- Plugins tab ---
    class PluginsPane;
    std::unique_ptr<PluginsPane> pluginsPane_;
    PluginScanner pluginScanner_;
    std::unique_ptr<juce::FileChooser> pluginFolderChooser_;
    std::unique_ptr<juce::FileChooser> sessionFileChooser_;

    // --- Video tab ---
    class VideoPane;
    std::unique_ptr<VideoPane> videoPane_;

    // --- transient capture announcement (white paper 14.5 — glanceable) ---
    class CaptureBanner;
    std::unique_ptr<CaptureBanner> captureBanner_;
    void announceCapture (const CaptureRegion& region,
                          const promotion::PromotionResult& result);

    // --- bottom control bar ---
    juce::Slider     playhead_;
    juce::TextButton armButton_      { "Arm" };
    juce::TextButton markInButton_   { "Mark In" };
    juce::TextButton markOutButton_  { "Mark Out" };
    juce::TextButton undoButton_     { "Undo" };
    juce::TextButton redoButton_     { "Redo" };
    juce::Label      bottomInfo_;

    // --- capture state (white paper 14.5 / 14.6) ---
    CaptureSession captureSession_;
    // Message-thread only — promotion's allocateId callback is called from
    // onMarkOut on the JUCE message thread, never from the audio thread.
    std::int64_t   nextConstituentId_ { 0 };
    bool           pendingOverlay_ { false };
    /// Snapshot of pendingOverlay_ taken in onMarkOut just before promote()
    /// consumes the flag. announceCapture() reads this to distinguish the
    /// Overlay-downgraded-to-Shared case ("no section here yet") from a plain
    /// Shared capture, per spec §11 row 4.
    bool           lastRequestWasOverlay_ { false };

    /// 500 ms long-press detector on Mark In. The press starts a timer; if
    /// the user releases before the timer fires, the capture stays Shared
    /// (the default). If the timer fires, pendingOverlay_ is set true and
    /// the next Mark Out resolves to Overlay (see promote()).
    static constexpr int kOverlayLongPressMs = 500;
    std::unique_ptr<juce::Timer> longPressTimer_;

    // --- input topology + per-tape arm/focus (this session) ---
    std::vector<InputDescriptor>     inputs_;
    std::unordered_set<std::int64_t> armedTapeIds_;
    TapeId                           focusedTape_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace sirius
