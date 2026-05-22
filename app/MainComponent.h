#pragma once

#include "CapabilityTier.h"
#include "DemoSession.h"

#include "sirius/AudioCallback.h"
#include "sirius/AudioDeviceCalibration.h"
#include "sirius/CaptureSession.h"
#include "sirius/ChannelStrip.h"
#include "sirius/DirectLayer.h"
#include "sirius/EngineConfig.h"
#include "sirius/InputDescriptor.h"
#include "sirius/FlacTapeSink.h"
#include "sirius/InputMixer.h"
#include "sirius/LatencyBudget.h"
#include "sirius/Lmc.h"
#include "sirius/MonotonicClock.h"
#include "sirius/NotificationBus.h"
#include "sirius/OutputMixer.h"
#include "sirius/OverloadProtection.h"
#include "sirius/PerformanceView.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/PluginScanner.h"
#include "sirius/Promotion.h"
#include "sirius/RetroactiveRing.h"
#include "sirius/SessionSnapshot.h"
#include "sirius/TapeId.h"
#include "sirius/TapePool.h"
#include "sirius/UndoStack.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

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

    // Test-only accessors (M7 S7). Exposed because MainComponentPluginEditorTests
    // exercises the openPluginEditor / closePluginEditor lifecycle from a
    // headless harness; these are not part of the operator surface.
    juce::File   hostBinaryPathForTesting()         const { return hostBinaryPath(); }
    std::size_t  editorWindowCountForTesting()      const noexcept { return openEditorBusIds_.size(); }
    std::int64_t firstOpenBusIdForTesting()         const noexcept;
    std::int64_t childPidForTestingAtBusId (std::int64_t busId) const noexcept;
    void         openPluginEditorForTesting  (const PluginDescriptor& d) { openPluginEditor  (d); }
    void         closePluginEditorForTesting (std::int64_t busId)        { closePluginEditor (busId); }

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    // --- tape pool management (tape-UI T3) ---
    void addTape      (const juce::String& name);
    void addNextTape  ();
    void renameTape   (sirius::TapeId id, const juce::String& name);
    void removeTape   (sirius::TapeId id);
    void refreshTapesPane();  // body filled in tape-UI T5

private:
    void timerCallback() override;

    // --- per-tick view refresh ---
    void refreshPerformance();
    void refreshPreparation();
    void refreshTimeline();
    void refreshDiagnostics();
    void refreshInputMixer();
    /// Resolves each input strip's current tape destination + the pooled-tape
    /// choice list and pushes both into the pane's per-strip picker buttons.
    /// Message-thread only.
    void refreshInputDestinations();

    // --- input mixer strip wiring ---
    /// The Audio ChannelStrip behind input-mixer strip `index`, or nullptr if
    /// the index is out of range. Message-thread only.
    ChannelStrip<SignalType::Audio>* inputStripAt (int index) noexcept;
    /// Recomputes each strip's effective mute from the mute + solo button state
    /// (solo-in-place: any solo → non-soloed strips are silenced) and applies it
    /// to both the engine strip and the strip's visual mute indication.
    void recomputeInputStripMutes();
    /// (Re)registers the engine input channels and rebuilds the pane's strips
    /// from `inputPairs_`: a stereo pair → one stereo strip, a mono pair → two
    /// mono-source strips (RME split). Brackets the channel-map mutation with
    /// removeAudioCallback/addAudioCallback so the audio thread never reads a
    /// half-mutated registry. Message-thread only; resets mute/solo state.
    void rebuildInputStrips();
    /// Rebuilds the bus/FX-return strip row in InputMixerPane from the engine
    /// InputMixer's current bus list. Calls prepare() on each bus off the audio
    /// thread. Message-thread only. Called after rebuildInputStrips() so bus
    /// meters start alongside channel meters.
    void rebuildBusStrips();
    /// Flips the stereo/mono mode of the pair behind `stripIndex` (RME-style
    /// split/collapse) and rebuilds. Message-thread only.
    void toggleInputPairStereo (int stripIndex);

    // --- per-tape arm + focus (refined Mockup A, this session's UX) ---
    void toggleArm    (TapeId tape);
    void setFocused   (TapeId tape);
    std::vector<TapeId> armedTapesVec() const;

    // --- editing demo for undo/redo ---
    void onUndo();
    void onRedo();

    // --- shared-placement fork gesture (Task 8) ---
    // Deep-copies the wrapper's shared subtree with fresh ConstituentIds and
    // flips its role to "forked-placement". The selector then drops it from
    // the tie-bar group and the renderer draws the prime mark above the Pill.
    void forkPlacement (ConstituentId wrapperId);

    // --- arm / disarm gesture (white paper 14.6 — coarse, decisive) ---
    void onArmToggle();
    void onMarkIn();
    void onMarkOut();
    void refreshCaptureControls();

    // --- plugin scanning ---
    void scanFolder (const juce::File& folder);
    void chooseFolderAndScan();
   #if JUCE_MAC
    void scanGlobalPluginFolder();
    void scanUserPluginFolder();
    void openSyntheticTestPlugin();
   #endif

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

    // --- audio I/O (M1 Sessions 1-2) ---
    // The device manager owns OS-side device lifecycle; the callback is the
    // single audio-thread entry point. The LMC is the V7 §4 logical master
    // clock — the callback drives its sample-clock via advanceBySamples per
    // buffer (Session 2). The shared_ptr<MonotonicClock> backs the LMC's
    // existing wall-clock reader (nowSeconds()) and stays alive for the LMC
    // lifetime. Declaration order matters: monotonicClock_ → lmc_ →
    // audioCallback_ so destruction unwinds in the safe reverse order.
    std::shared_ptr<const MonotonicClock> monotonicClock_;
    std::unique_ptr<Lmc>                  lmc_;
    EngineConfig                          engineConfig_;
    juce::AudioDeviceManager              audioDeviceManager_;
    // M4 Session 3 — engine-side mixers and DirectLayer the audio callback
    // now drives. Declared before audioCallback_ so destruction unwinds the
    // callback first (it's destroyed before the mixers it holds non-owning
    // pointers into). The MainComponent destructor also explicitly removes
    // the callback from the device manager before any teardown begins, so
    // the audio thread can't see a half-destroyed mixer.
    // Tape-UI slice — single source of truth for which tapes exist; mirrored
    // into the input mixer's routing terminals at startup. Declared before
    // inputMixer_ so it outlives the mixer on destruction.
    sirius::TapePool                      tapePool_;
    std::unique_ptr<InputMixer>           inputMixer_;
    std::unique_ptr<OutputMixer>          outputMixer_;
    std::unique_ptr<DirectLayer>          directLayer_;
    // Tape slice 3 — declared before audioCallback_ so the sink outlives the
    // callback: the callback's last audio delivery (via InputMixer→ITapeSink)
    // must land before the sink's worker thread drains and finalises the files.
    // The destructor's explicit removeAudioCallback is the primary guard; this
    // ordering is a belt-and-suspenders invariant.
    std::unique_ptr<sirius::FlacTapeSink> flacTapeSink_;
    std::unique_ptr<AudioCallback>        audioCallback_;
    juce::String                          audioDeviceLastError_;

    // M1 Session 3 — engine pieces wired alongside the audio thread.
    // ASRCs are held by the callback (one per input/output channel) but
    // not invoked from the buffer body for M1; M2-M5 grow real routing
    // through them. Calibration sits at identity until M8 measures drift.
    // OverloadProtection is a non-RT consumer driven by the 30 Hz
    // timerCallback off `audioCallback_->lastCallbackElapsedSec()`.
    // RetroactiveRing is the engine-side consumer the (M3/M4) lock-free
    // tape-event queue will eventually drain into; T is provisionally
    // std::uint8_t until the real tape-event type lands.
    std::vector<std::unique_ptr<Asrc>>    asrcInputs_;
    std::vector<std::unique_ptr<Asrc>>    asrcOutputs_;
    AudioDeviceCalibration                calibration_ { AudioDeviceCalibration::identity() };
    OverloadProtection                    overloadProtection_;
    RetroactiveRing<std::uint8_t>         retroactiveRing_ { 1024 };

    // M6 Session 2 — engine→UI truthfulness channel (V5 §8.6, V7 §17.9).
    // Owned here so the bus outlives every collaborator that posts to it
    // (AudioCallback, InputMixer, TapeWriter). Declared after the engine
    // pieces it does NOT outlive (mixers/callback hold raw pointers to the
    // bus, but the bus has no back-pointer to them); the destructor sequence
    // remains correct because MainComponent's destructor first removes the
    // audio callback from the device manager, then unwinds members in
    // reverse declaration order. The drain buffer is reserved up-front so
    // `drain()` never reallocates on the message-thread timer tick (the bus
    // header documents that `drain` may throw bad_alloc if the caller
    // under-reserves — `kCategoryCount * kRingCapacity` is the worst-case
    // single-drain payload).
    std::unique_ptr<NotificationBus>      notificationBus_;
    std::vector<Notification>             notificationDrainBuffer_;

    // M6 Session 3 — rolling window of the most-recent notifications surfaced
    // in the Preparation tab. The drain on the 30 Hz timer appends new entries
    // and trims the front so the deque never grows past `kNotificationHistorySize`.
    // Deque (not vector) because append-back + pop-front are O(1) — vector
    // would shift on every trim. Sized for "operator sees the recent few
    // without scrolling; older history is reachable by scrolling the editor"
    // per the M6 S3 spec; M22 redesigns this surface entirely so the size is
    // not a long-term commitment.
    static constexpr std::size_t kNotificationHistorySize = 20;
    std::deque<Notification>              notificationHistory_;

    // --- top-level layout ---
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    // --- Performance tab ---
    PerformanceView performanceView_;

    // --- Preparation tab ---
    class PreparationPane;
    std::unique_ptr<PreparationPane> preparationPane_;

    // --- Settings tab ---
    class SettingsPane;
    std::unique_ptr<SettingsPane> settingsPane_;

    // --- Input Mixer tab (whitepaper Part VI — the capture console) ---
    // One stereo strip per stereo pair of active device inputs (default
    // stereo, RME-style; the mono/stereo split toggle is a later slice). Each
    // strip's fader/mute/solo drives the engine ChannelStrip behind its
    // ChannelId; meters read the strip's post-fader peaks on the 30 Hz timer.
    class InputMixerPane;
    std::unique_ptr<InputMixerPane> inputMixerPane_;

    // --- Tapes tab (tape-UI T5 — operator-facing tape-pool management:
    // list + create/rename/remove with a >=1 floor, plus the dropped-block
    // capture-overflow diagnostic). All mutation relays through the T3 pool
    // methods (addTape/renameTape/removeTape). ---
    class TapesPane;
    std::unique_ptr<TapesPane>      tapesPane_;
    /// One source pair per stereo pair of device inputs. `stereo` true → one
    /// stereo strip; false → two mono-source strips (RME split). `leftCh`/
    /// `rightCh` are device channel indices.
    struct InputPair { int leftCh; int rightCh; bool stereo; };
    std::vector<InputPair>          inputPairs_;
    // Flat per-strip state, parallel arrays rebuilt by rebuildInputStrips().
    std::vector<ChannelId>          inputStripChannelIds_;
    std::vector<int>                inputStripPair_;     // strip → index into inputPairs_
    std::vector<bool>               inputStripMuted_;
    std::vector<bool>               inputStripSoloed_;
    // Bus/FX-return strip IDs, parallel to InputMixerPane bus strips.
    // Kept in sync by rebuildBusStrips().
    std::vector<sirius::BusId>      busStripIds_;

    // --- Plugins tab ---
    class PluginListBox;
    class PluginsPane;
    std::unique_ptr<PluginsPane> pluginsPane_;
    PluginScanner pluginScanner_;
    std::unique_ptr<juce::FileChooser> pluginFolderChooser_;
    std::unique_ptr<juce::FileChooser> sessionFileChooser_;

    // --- M7 S7: out-of-process plug-in editor hosting -----------------------
    // Scratch busIds for editor-only plug-in loads. The OutputMixer is NOT
    // wired to know about these buses, so no audio routes through them. The
    // operator gets a GUI surface; engine audio output is unaffected. Real
    // per-bus chain integration is a later session.
    static constexpr std::int64_t kScratchBusIdBase = 1000;

    sirius::OutOfProcessEffectChainHost effectChainHost_;
    std::int64_t                        nextScratchBusId_ { kScratchBusIdBase };

    /// Bus IDs of currently-open editors (M7 S9). Each entry corresponds
    /// to one sirius_plugin_host child that has been asked to show its
    /// editor; the child owns the actual NSWindow. The engine just tracks
    /// which children currently have an editor visible so the PluginsPane
    /// can toggle its button label.
    std::vector<std::int64_t> openEditorBusIds_;

    /// Resolves Contents/MacOS/sirius_plugin_host alongside the running app
    /// binary. Returns an invalid juce::File outside a .app bundle (dev-loop
    /// runs from build/...); callers must check `existsAsFile()` before use.
    juce::File hostBinaryPath() const;

    /// Spawns a sirius_plugin_host child via configureBus on a fresh scratch
    /// busId, then sends requestEditorShow so the child opens its OWN
    /// top-level NSWindow (Reaper-style; M7 S9). Message-thread only.
    void openPluginEditor (const PluginDescriptor& descriptor);

    /// Builds the save/load slot lookup over openEditorBusIds_: entry index
    /// N → bus openEditorBusIds_[N], slot 0 (M7 S9 one-editor-per-bus model).
    /// Message-thread only — reads openEditorBusIds_ without locking.
    sirius::SlotLookup slotLookup() const;

    /// Tears down the slot at `busId`. The child window dies with the
    /// child process. Message-thread only.
    void closePluginEditor (std::int64_t busId);

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
