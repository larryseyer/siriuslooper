#pragma once

#include "CapabilityTier.h"
#include "DemoSession.h"

#include "ida/AudioCallback.h"
#include "ida/AudioDeviceCalibration.h"
#include "ida/CaptureSession.h"
#include "ida/ChannelDetail.h"
#include "ida/ChannelStrip.h"
#include "ida/EngineConfig.h"
#include "ida/InputDescriptor.h"
#include "ida/FileInputRegistry.h"
#include "ida/FlacTapeSink.h"
#include "ida/TapeColoringSink.h"
#include "ida/InputMixer.h"
#include "ida/LatencyBudget.h"
#include "ida/Lmc.h"
#include "ida/MonotonicClock.h"
#include "ida/NotificationBus.h"
#include "ida/OutputMixer.h"
#include "ida/OverloadProtection.h"
#include "ida/PerformanceView.h"
#include "ida/OutOfProcessEffectChainHost.h"
#include "ida/PluginScanner.h"
#include "ida/Promotion.h"
#include "ida/RetroactiveRing.h"
#include "ida/SessionSnapshot.h"
#include "ida/TapeId.h"
#include "ida/TapePool.h"
#include "ida/UndoStack.h"

#include "FileInputPlayerWindow.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ida
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
    void renameTape   (ida::TapeId id, const juce::String& name);
    void removeTape   (ida::TapeId id);
    void refreshTapesPane();  // body filled in tape-UI T5

private:
    void timerCallback() override;

    // --- per-tick view refresh ---
    void refreshPerformance();
    void refreshPreparation();
    void refreshTimeline();
    void refreshDiagnostics();
    /// Composes the four post-capture / post-edit refreshes (performance →
    /// preparation → captureControls → diagnostics) into one call so undo /
    /// redo / fork / similar gestures need a single line, not four. Pure
    /// composition; no behaviour beyond the four constituent refreshes.
    void refreshAll();
    void refreshInputMixer();
    /// Pushes the OutputMixer master bus's post-fader peak + short-term LUFS
    /// into the OutputMixerPane's master strip + every aux bus row. Called
    /// from the 30 Hz timer. No-op when the pane is absent. Message-thread
    /// only.
    void refreshOutputMixer();
    /// Walks `outputMixer_->busCount()` (skipping master at BusId{0}),
    /// publishes the resulting aux-bus list to the OutputMixerPane and
    /// rebuilds `outputBusStripIds_` in parallel. Brackets `Bus::prepare`
    /// in a remove/addAudioCallback so the LufsMeter reallocation can never
    /// race the audio thread. Message-thread only.
    void rebuildOutputBusStrips();
    /// Resolves each aux bus's current main-out destination + builds its
    /// per-bus picker choice list (every OTHER aux bus + master + "Direct
    /// out"). Pushed into the pane via setBusDestinations. Phrase strips
    /// (slice 5b) get the same treatment via setPhraseDestinations — phrase
    /// channels have no cycle filter (they're leaves in the routing graph).
    /// Message-thread only.
    void refreshOutputDestinations();
    /// Walks the current pill list (PillState DFS order via selectTimelineView),
    /// adds/removes OutputMixer channels to match, and pushes the resulting
    /// PhraseStripInfo array to the pane. Skips the pane rebuild when the
    /// pill set + order are unchanged (avoids nuking fader state on every
    /// refresh tick). Engine mutations are bracketed by audio-callback pause.
    /// Message-thread only; called from refreshPreparation + refreshPerformance.
    void refreshOutputMixerPhraseChannels();

    /// V9 §6.3.1 / §7.2: walks every input channel; for those whose
    /// MON is On, pushes a MonStripInfo to the OutputMixerPane's MON
    /// band (LEFTMOST). The auto-created OutputMixer channel itself is
    /// minted by `InputMixer::setChannelMonitorMode` — this method only
    /// drives the GUI's visibility of those channels. Called from the
    /// MON-toggle callback, the session-load rebind site, and any input-
    /// channel add/remove site. Message-thread only.
    void refreshOutputMixerMonChannels();
    /// Opens the InsertChainPopup anchored to aux bus `busIdx`'s INS button.
    /// Same detach/setEffectChain/re-attach shape as the input variants —
    /// see openInsertChainPopupForMasterBus.
    void openInsertChainPopupForOutputBus (int busIdx);
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

    /// Slice U: snapshot the InputMixer's FX-return roster + the named input
    /// strip's current send levels + pre-fader-sends flag, ready to push into
    /// the Sends tab via `InputMixerPane::showDetailFor`. The returned
    /// `fxReturns` and `sendLevels` vectors are parallel (same order as
    /// `InputMixer::busIdAt` over FxReturn-kind entries). Out-of-range
    /// `stripIdx` returns empty + preFader=false. Message-thread only.
    struct ChannelSendsView
    {
        std::vector<ida::ui::FxReturnInfo> fxReturns;
        std::vector<float>                  sendLevels;
        bool                                preFader { false };
    };
    [[nodiscard]] ChannelSendsView collectInputSendsView  (int stripIdx) const;
    [[nodiscard]] ChannelSendsView collectOutputSendsView (int phraseIdx) const;
    [[nodiscard]] ChannelSendsView collectMonSendsView    (int monIdx)    const;

    /// Slice EC: snapshot the channel strip's current EQ slot index +
    /// live `EqConfig` (or default + `hasSlot=false` if no EQ slot is
    /// present in the chain). The slot index is the first chain entry
    /// with `kind==Internal && internalId==kEq`; the config is read
    /// through `effectChainHost_.internalEqConfigAt`. Symmetric helpers
    /// exist for CMP and for the OutputMixer (phrase-strip lookup via
    /// the phrase-channel map).
    struct ChannelFxProbe
    {
        ida::EqConfig  config {};
        std::size_t    slotIdx { 0 };
        bool           hasSlot { false };
    };
    struct ChannelCmpProbe
    {
        ida::CmpConfig config {};
        std::size_t    slotIdx { 0 };
        bool           hasSlot { false };
    };
    [[nodiscard]] ChannelFxProbe  collectInputEqView   (int stripIdx) const;
    [[nodiscard]] ChannelCmpProbe collectInputCmpView  (int stripIdx) const;
    [[nodiscard]] ChannelFxProbe  collectOutputEqView  (int phraseIdx) const;
    [[nodiscard]] ChannelCmpProbe collectOutputCmpView (int phraseIdx) const;
    [[nodiscard]] ChannelFxProbe  collectMonEqView     (int monIdx)    const;
    [[nodiscard]] ChannelCmpProbe collectMonCmpView    (int monIdx)    const;
    /// Slice EC-Polish: bus/FX-return strips can now hold EQ + CMP. The
    /// probes are keyed by the bus-row index (parallel to busStripIds_ on
    /// the input pane, outputBusStripIds_ on the output pane). The output
    /// mixer's master bus (BusId{0}) is its own pair of probes.
    [[nodiscard]] ChannelFxProbe  collectInputBusEqView    (int busIdx) const;
    [[nodiscard]] ChannelCmpProbe collectInputBusCmpView   (int busIdx) const;
    [[nodiscard]] ChannelFxProbe  collectOutputBusEqView   (int busIdx) const;
    [[nodiscard]] ChannelCmpProbe collectOutputBusCmpView  (int busIdx) const;
    [[nodiscard]] ChannelFxProbe  collectOutputMasterEqView()  const;
    [[nodiscard]] ChannelCmpProbe collectOutputMasterCmpView() const;

    // --- P7 T5 slice 5: per-strip insert chain popup ---
    /// Opens the InsertChainPopup anchored to input strip `stripIdx`'s INS
    /// button. The popup mirrors the strip's current EffectChain (Internal
    /// slots only); each popup callback recomputes the chain and pushes it
    /// back via the strip's setEffectChain inside a detach/re-attach bracket.
    /// No-op if the index is out of range or no audio device is present.
    void openInsertChainPopupForChannel (int stripIdx);
    /// Bus-side mirror of openInsertChainPopupForChannel. Same shape; the
    /// strip lookup uses inputMixer_->busForId(busStripIds_[busIdx]).
    void openInsertChainPopupForBus     (int busIdx);
    /// Output-side mirror — opens the popup anchored to the OutputMixerPane's
    /// master INS button. Drives outputMixer_->busForId(BusId{0})->setEffectChain
    /// through the same detach/re-attach bracket as the input variants.
    void openInsertChainPopupForMasterBus();

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
    // Engine-side mixers the audio callback drives. Declared before
    // audioCallback_ so destruction unwinds the callback first (it's
    // destroyed before the mixers it holds non-owning pointers into).
    // The MainComponent destructor also explicitly removes the callback
    // from the device manager before any teardown begins, so the audio
    // thread can't see a half-destroyed mixer.
    // Tape-UI slice — single source of truth for which tapes exist; mirrored
    // into the input mixer's routing terminals at startup. Declared before
    // inputMixer_ so it outlives the mixer on destruction.
    ida::TapePool                      tapePool_;
    std::unique_ptr<InputMixer>           inputMixer_;
    // File-input slice Task 11 — UI-side registry that owns FileInputSource
    // instances behind each registered file input. Declared on the message-
    // thread side because the player-window lifetime and the Add-file-input
    // gesture both run on the message thread. Constructed at 48 kHz (the
    // FileInputSource resampler handles file-SR mismatches; device-SR
    // mismatch is a known v1 limitation — see continue.md §1).
    ida::FileInputRegistry                fileInputRegistry_ { 48000.0 };
    std::unique_ptr<juce::FileChooser>    fileInputChooser_;
    std::unordered_map<std::int64_t, std::unique_ptr<ida::FileInputPlayerWindow>>
                                          filePlayerWindows_;
    /// Opens (or brings to front) the player window for the given file-input
    /// `id`. Message-thread only. The window holds a reference to
    /// `fileInputRegistry_` so the registry must outlive every entry in
    /// `filePlayerWindows_` (true today: registry is declared above this map).
    void openFilePlayerWindow (ida::InputId id);
    std::unique_ptr<OutputMixer>          outputMixer_;
    // Tape slice 3 — declared before audioCallback_ so the sink outlives the
    // callback: the callback's last audio delivery (via InputMixer→ITapeSink)
    // must land before the sink's worker thread drains and finalises the files.
    // The destructor's explicit removeAudioCallback is the primary guard; this
    // ordering is a belt-and-suspenders invariant.
    std::unique_ptr<ida::FlacTapeSink>     flacTapeSink_;
    // TAPECOLOR Slice 2 — per-tape routing decorator. Sits between the
    // InputMixer's ITapeSink boundary and flacTapeSink_, applying TAPECOLOR
    // to tapes whose mode is BeforeWrite (default OFF everywhere). Declared
    // after flacTapeSink_ so the inner sink it references outlives it.
    std::unique_ptr<ida::TapeColoringSink> tapeColoringSink_;
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

    // --- Output Mixer tab (whitepaper §5.2/§6.6/§7.1 — the mixdown console) ---
    // Slice 1: a single Master bus strip wired to OutputMixer::busForId(BusId{0}).
    // Fader → Bus::setGain, mute → Bus::setMute, INS → InsertChainPopup. Meters
    // read post-fader peak + short-term LUFS on the 30 Hz refresh. Channels and
    // aux buses land in subsequent slices.
    class OutputMixerPane;
    std::unique_ptr<OutputMixerPane> outputMixerPane_;

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
    std::vector<int>                inputStripPair_;     // strip → index into inputPairs_ (-1 for file-input strips)
    /// Strip → source InputId, parallel to inputStripChannelIds_. For hardware
    /// strips this is InputId(leftCh); for file-input strips it is the
    /// registry-allocated InputId (>= 100000). Used by the "Show player…"
    /// strip-context-menu callback to find which descriptor each strip belongs
    /// to. Maintained by rebuildInputStrips().
    std::vector<ida::InputId>       inputStripInputIds_;
    std::vector<bool>               inputStripMuted_;
    std::vector<bool>               inputStripSoloed_;
    // Bus/FX-return strip IDs, parallel to InputMixerPane bus strips.
    // Kept in sync by rebuildBusStrips().
    std::vector<ida::BusId>      busStripIds_;
    /// Output Mixer aux-bus strip IDs, parallel to OutputMixerPane::busStrips_.
    /// Index N here = BusId at OutputMixerPane row N. Master (BusId{0}) is
    /// NOT in this vector — it's tracked separately by the pane's master_.
    /// Kept in sync by rebuildOutputBusStrips().
    std::vector<ida::BusId>      outputBusStripIds_;

    // --- slice 5b: phrase-channel binding ----------------------------------
    /// Pane-row order for the LEFT-band phrase strips, parallel to
    /// OutputMixerPane::phraseStrips_. Each entry is the ConstituentId of the
    /// phrase rendered at that row. Kept in sync by
    /// refreshOutputMixerPhraseChannels(). Empty on startup (no phrases yet).
    std::vector<ida::ConstituentId> phraseStripConstituentIds_;
    /// ConstituentId → OutputChannelId binding for phrase-channel engine
    /// strips. Keyed by ConstituentId.value() because ConstituentId is not
    /// hashable. Survives pill ordering changes (id is stable across CoW
    /// edits); the (ConstituentId, OutputChannelId) pair is not persisted in
    /// 5b — that lands in 5c. Current destination is INFERRED at refresh
    /// time from sendMatrix + hardwareOutPair (no separate state map needed
    /// because the picker is radio-style: a single send at unity, all
    /// others at 0; hardware-output choice zeroes every send).
    std::unordered_map<std::int64_t, ida::OutputChannelId>
                                 phraseChannelByConstituent_;

    /// V9 MON strip binding. Indexed by mon-strip row, holds the *input*
    /// ChannelId whose MON-on minted that strip's auto-created OutputMixer
    /// channel. The OutputChannelId itself is resolved at call time via
    /// `InputMixer::channelMonitorOutputChannel(chId)` — no separate map
    /// because the InputMixer already owns the mapping. Empty when no
    /// MON-on inputs exist (the default). Kept in sync by
    /// refreshOutputMixerMonChannels().
    std::vector<ida::ChannelId>  monStripInputChannelIds_;

    /// V9 §7.2 (2026-05-25) — for each MON-band row whose SOURCE is a bus
    /// (not an input channel), the BusId of the source. Parallel to
    /// `monStripInputChannelIds_`: row i is bus-sourced iff
    /// `monStripInputBusIds_[i] != ida::BusId{0}` (BusId 0 is master on
    /// OutputMixer; on InputMixer there is no BusId 0, so it works as a
    /// sentinel). Used by refresh-destination + refresh-meter paths to
    /// resolve each row to its OutputChannelId via
    /// `InputMixer::busMonitorOutputChannel` instead of
    /// `InputMixer::channelMonitorOutputChannel`.
    std::vector<ida::BusId>      monStripInputBusIds_;

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

    ida::OutOfProcessEffectChainHost effectChainHost_;
    std::int64_t                        nextScratchBusId_ { kScratchBusIdBase };

    /// Bus IDs of currently-open editors (M7 S9). Each entry corresponds
    /// to one ida_plugin_host child that has been asked to show its
    /// editor; the child owns the actual NSWindow. The engine just tracks
    /// which children currently have an editor visible so the PluginsPane
    /// can toggle its button label.
    std::vector<std::int64_t> openEditorBusIds_;

    /// Resolves Contents/MacOS/ida_plugin_host alongside the running app
    /// binary. Returns an invalid juce::File outside a .app bundle (dev-loop
    /// runs from build/...); callers must check `existsAsFile()` before use.
    juce::File hostBinaryPath() const;

    /// Spawns a ida_plugin_host child via configureBus on a fresh scratch
    /// busId, then sends requestEditorShow so the child opens its OWN
    /// top-level NSWindow (Reaper-style; M7 S9). Message-thread only.
    void openPluginEditor (const PluginDescriptor& descriptor);

    /// Builds the save/load slot lookup over openEditorBusIds_: entry index
    /// N → bus openEditorBusIds_[N], slot 0 (M7 S9 one-editor-per-bus model).
    /// Message-thread only — reads openEditorBusIds_ without locking.
    ida::SlotLookup slotLookup() const;

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

} // namespace ida
