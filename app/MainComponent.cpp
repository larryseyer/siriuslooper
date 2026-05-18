#include "MainComponent.h"

#include "sirius/PerformanceViewState.h"
#include "sirius/PreparationView.h"
#include "sirius/PreparationViewState.h"
#include "sirius/SessionFormat.h"
#include "sirius/TapeId.h"
#include "sirius/TimelineView.h"
#include "sirius/TimelineViewState.h"
#include "sirius/VideoPreview.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <exception>
#include <functional>
#include <optional>
#include <vector>

namespace sirius
{

namespace
{
    /// Small local Timer subclass — Sirius's vendored JUCE has no
    /// FunctionTimer. Holds a captured callable and invokes it on each
    /// timerCallback. The long-press detector uses startTimer(ms) once and
    /// stopTimer()s itself in the callback, so the one-shot semantics live
    /// in the caller, not here.
    class FunctionTimer : public juce::Timer
    {
    public:
        std::function<void()> onTimer;
        void timerCallback() override { if (onTimer) onTimer(); }
    };

    /// The playhead slider works in sixteenths of an LMC second so its value
    /// converts to an exact Rational — the engine still never sees a double.
    constexpr int ticksPerSecond = 16;

    /// Default stereo-channel count — matches initialiseWithDefaultDevices(2, 2)
    /// and the M4 default identity routes (input N → output N for N in [0, count)).
    /// If a Sirius device with a different default ever lands, change this in
    /// one place.
    constexpr int kDefaultStereoChannels = 2;

    Rational playheadValueToLmc (double sliderValue)
    {
        return Rational (static_cast<std::int64_t> (sliderValue), ticksPerSecond);
    }

    /// A demo capability tier. The real assessment (white paper 13.1) runs at
    /// startup against measured hardware; here we report a Comfortable result
    /// so the diagnostics row has something honest to show without lying
    /// about a Lavish machine we never measured.
    CapabilityTier demoTier()
    {
        HardwareProfile hw;
        hw.cpuCores                = 8;
        hw.hasVectorUnit           = true;
        hw.ramTotalBytes           = std::int64_t (16) * 1024 * 1024 * 1024;
        hw.ramAvailableBytes       = std::int64_t (10) * 1024 * 1024 * 1024;
        hw.storageWriteBytesPerSec = std::int64_t (1000) * 1024 * 1024;
        hw.audioBufferFrames       = 256;
        hw.onBattery               = false;
        hw.thermallyThrottled      = false;
        return selectTier (hw);
    }

    const char* tapeFormatName (TapeFormat f)
    {
        return f == TapeFormat::UncompressedPcm ? "PCM" : "FLAC";
    }
    const char* asrcName (AsrcQuality q)
    {
        switch (q) {
            case AsrcQuality::VeryHigh: return "VHQ";
            case AsrcQuality::High:     return "HQ";
            case AsrcQuality::Medium:   return "MQ";
        }
        return "?";
    }
    const char* effectName (EffectStrategy s)
    {
        switch (s) {
            case EffectStrategy::AllLive:           return "AllLive";
            case EffectStrategy::MixedLiveCached:   return "MixedLiveCached";
            case EffectStrategy::AggressiveCaching: return "AggressiveCaching";
        }
        return "?";
    }

    /// Deep-copy a Constituent subtree, minting fresh ConstituentIds for
    /// every node. The structure (boundaries, names, metadata, tape refs) is
    /// preserved; only ids change. Used by the fork gesture to break sharing.
    sirius::Constituent deepCopyWithFreshIds (
        const sirius::Constituent& src,
        const sirius::promotion::IdAllocator& allocate)
    {
        sirius::Constituent copy (allocate(), src.conceptualIn(), src.conceptualOut());
        if (! src.name().empty())  copy = copy.withName (src.name());
        if (src.phraseMetadata())  copy = copy.withPhraseMetadata (*src.phraseMetadata());
        if (src.tapeReference())   copy = copy.withTapeReference  (*src.tapeReference());
        if (src.localMeter())      copy = copy.withLocalMeter     (*src.localMeter());
        if (src.localTempoMap())   copy = copy.withLocalTempoMap  (*src.localTempoMap());
        if (src.hasEffectChain())  copy = copy.withEffectChain    (*src.effectChain());
        copy = copy.withAnchor (src.anchor());
        copy = copy.withRepetitionRules (src.repetitionRules());
        for (const auto& child : src.children())
            copy = copy.withChildAdded (
                std::make_shared<const sirius::Constituent> (
                    deepCopyWithFreshIds (*child, allocate)));
        return copy;
    }

    /// Locate the wrapper by id in `root` and return its index path from the
    /// root's children. Returns empty optional if not found (caller can no-op).
    std::optional<std::vector<std::size_t>> findWrapperPath (
        const sirius::Constituent& root, sirius::ConstituentId wrapperId)
    {
        std::optional<std::vector<std::size_t>> found;
        std::vector<std::size_t> path;
        std::function<void (const sirius::Constituent&)> walk;
        walk = [&] (const sirius::Constituent& c)
        {
            if (c.id() == wrapperId) { found = path; return; }
            for (std::size_t i = 0; i < c.children().size(); ++i)
            {
                path.push_back (i);
                walk (*c.children()[i]);
                path.pop_back();
                if (found) return;
            }
        };
        for (std::size_t i = 0; i < root.children().size(); ++i)
        {
            path.push_back (i);
            walk (*root.children()[i]);
            path.pop_back();
            if (found) return found;
        }
        return found;
    }
}

// =============================================================================
// PreparationPane — PreparationView on top, a diagnostics row at the bottom,
// and (since M1 Session 1) an audio-device section that exposes JUCE's stock
// device selector plus the explicit `Enable monitoring` toggle the audio
// callback gates input→output pass-through on. Monitoring is off at startup
// (a hot mic plus live monitoring is one slip away from feedback).
// =============================================================================
class MainComponent::PreparationPane final : public juce::Component
{
public:
    PreparationPane (juce::AudioDeviceManager& deviceManager,
                     AudioCallback&            audioCallback)
        : audioCallback_ (audioCallback),
          deviceSelector_ (deviceManager,
                           /*minInputChannels*/  0, /*maxInputChannels*/  2,
                           /*minOutputChannels*/ 0, /*maxOutputChannels*/ 2,
                           /*showMidiInputOptions*/  false,
                           /*showMidiOutputSelector*/ false,
                           /*showChannelsAsStereoPairs*/ true,
                           /*hideAdvancedOptionsWithButton*/ true)
    {
        saveButton_.setButtonText ("Save...");
        loadButton_.setButtonText ("Load...");
        reloadDemoButton_.setButtonText ("Reload demo");
        addAndMakeVisible (saveButton_);
        addAndMakeVisible (loadButton_);
        addAndMakeVisible (reloadDemoButton_);

        statusLabel_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        statusLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (statusLabel_);

        audioHeaderLabel_.setText ("Audio device", juce::dontSendNotification);
        audioHeaderLabel_.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (audioHeaderLabel_);

        addAndMakeVisible (deviceSelector_);

        monitoringToggle_.setButtonText ("Enable monitoring (input → output pass-through)");
        monitoringToggle_.setToggleState (audioCallback_.isMonitoringEnabled(),
                                          juce::dontSendNotification);
        monitoringToggle_.onClick = [this]
        {
            audioCallback_.setMonitoringEnabled (monitoringToggle_.getToggleState());
        };
        addAndMakeVisible (monitoringToggle_);

        addAndMakeVisible (preparationView_);
        addAndMakeVisible (timelineView_);
        diagnosticsLabel_.setJustificationType (juce::Justification::topLeft);
        diagnosticsLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (diagnosticsLabel_);
    }

    void setState (PreparationViewState s)    { preparationView_.setState (std::move (s)); }
    void setTimelineState (TimelineViewState s) { timelineView_.setState  (std::move (s)); }
    void setTimelinePlayhead (std::optional<Rational> t) { timelineView_.setPlayhead (t); }
    void setDiagnostics (const juce::String& text) { diagnosticsLabel_.setText (text, juce::dontSendNotification); }
    void setStatus (const juce::String& text)      { statusLabel_.setText (text, juce::dontSendNotification); }

    juce::TextButton& saveButton()       { return saveButton_; }
    juce::TextButton& loadButton()       { return loadButton_; }
    juce::TextButton& reloadDemoButton() { return reloadDemoButton_; }
    TimelineView&     timelineView()     { return timelineView_; }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        auto topRow = area.removeFromTop (28);
        saveButton_.setBounds       (topRow.removeFromLeft (84));
        topRow.removeFromLeft (4);
        loadButton_.setBounds       (topRow.removeFromLeft (84));
        topRow.removeFromLeft (4);
        reloadDemoButton_.setBounds (topRow.removeFromLeft (120));
        topRow.removeFromLeft (12);
        statusLabel_.setBounds      (topRow);
        area.removeFromTop (6);

        // Audio-device block: header, JUCE picker, monitoring toggle. The
        // picker collapses its advanced controls behind a button so the
        // default block stays compact; 220 leaves room for the basic rows.
        audioHeaderLabel_.setBounds (area.removeFromTop (22));
        deviceSelector_.setBounds   (area.removeFromTop (220));
        monitoringToggle_.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);

        diagnosticsLabel_.setBounds (area.removeFromBottom (84));
        area.removeFromBottom (6);

        // The timeline gets the dominant share of vertical space — it's the
        // surface the performer reaches for. The tree readout sits above it
        // as a structural reference. Minimum heights guard tiny windows.
        const int timelineMin = timelineView_.totalHeight() + 8;
        const int timelineH   = std::max (timelineMin,
                                          juce::jmin (area.getHeight() * 6 / 10,
                                                      area.getHeight() - 80));
        timelineView_.setBounds (area.removeFromBottom (timelineH));
        area.removeFromBottom (6);
        preparationView_.setBounds (area);
    }

private:
    AudioCallback&   audioCallback_;
    juce::TextButton saveButton_;
    juce::TextButton loadButton_;
    juce::TextButton reloadDemoButton_;
    juce::Label      statusLabel_;
    juce::Label      audioHeaderLabel_;
    juce::AudioDeviceSelectorComponent deviceSelector_;
    juce::ToggleButton                 monitoringToggle_;
    PreparationView  preparationView_;
    TimelineView     timelineView_;
    juce::Label      diagnosticsLabel_;
};

// =============================================================================
// CaptureBanner — transient on-screen confirmation for a Mark Out gesture.
// Painted on top of the tabbed content so the performer's eyes don't have to
// drop to the diagnostics row to know the loop landed (white paper 14.5 —
// "shape, color, position, motion," not text). Click-through so the gesture
// underneath remains uninterrupted.
// =============================================================================
class MainComponent::CaptureBanner final : public juce::Component
{
public:
    CaptureBanner()
    {
        // Banner intercepts clicks while visible so the operator can tap it to
        // undo a too-early Mark Out. Once the fade animator drives visibility
        // to false, JUCE stops routing clicks here and the underlying tabbed
        // content receives them again.
        setInterceptsMouseClicks (true, false);
        setVisible (false);
    }

    std::function<void()> onUndoRequested;

    void mouseDown (const juce::MouseEvent&) override
    {
        if (isVisible() && onUndoRequested)
            onUndoRequested();
    }

    void show (const juce::String& message)
    {
        // Reset any in-flight fade from a previous announcement — otherwise
        // a rapid second Mark Out would resume the prior fade curve instead
        // of starting fresh at full alpha.
        auto& animator = juce::Desktop::getInstance().getAnimator();
        animator.cancelAnimation (this, false);

        text_ = message;
        setAlpha (1.0f);
        setVisible (true);
        toFront (false);   // Stay above siblings even if a tab repaint reordered.
        repaint();
        animator.fadeOut (this, 1500);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xf21a1a1a));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (juce::Colour (0xffffd24a));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 10.0f, 2.0f);

        g.setColour (juce::Colour (0xffffd24a));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      16.0f, juce::Font::bold));
        g.drawText (text_, getLocalBounds(),
                    juce::Justification::centred, false);

        // Quieter right-aligned hint — same amber as the main label but with
        // reduced alpha and a smaller font so it reads as guidance, not a
        // prominent button.
        g.setColour (juce::Colour (0xffffd24a).withAlpha (0.65f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      12.0f, juce::Font::bold));
        g.drawText ("\xe2\x86\xb6 Undo",
                    getLocalBounds().reduced (12, 0),
                    juce::Justification::centredRight, false);
    }

private:
    juce::String text_;
};

// =============================================================================
// PluginsPane — registered formats, folder-scan button, descriptor list
// =============================================================================
class MainComponent::PluginsPane final : public juce::Component
{
public:
    PluginsPane()
    {
        headerLabel_.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        headerLabel_.setText ("Plugin hosting", juce::dontSendNotification);
        addAndMakeVisible (headerLabel_);

        formatsLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (formatsLabel_);

        scanButton_.setButtonText ("Scan a plugin folder...");
        addAndMakeVisible (scanButton_);

        scanStatusLabel_.setMinimumHorizontalScale (1.0f);
        scanStatusLabel_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (scanStatusLabel_);

        descriptorsList_.setMultiLine (true);
        descriptorsList_.setReadOnly (true);
        descriptorsList_.setScrollbarsShown (true);
        descriptorsList_.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0));
        addAndMakeVisible (descriptorsList_);
    }

    void setRegisteredFormats (const std::vector<std::string>& formats)
    {
        juce::String text = "Registered formats: ";
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            if (i) text << ", ";
            text << juce::String (formats[i]);
        }
        formatsLabel_.setText (text, juce::dontSendNotification);
    }

    void setScanStatus (const juce::String& text)
    {
        scanStatusLabel_.setText (text, juce::dontSendNotification);
    }

    void setDescriptors (const std::vector<PluginDescriptor>& descriptors,
                         const std::vector<std::string>&      failed)
    {
        juce::String text;
        for (const auto& d : descriptors)
        {
            text << juce::String (d.name) << "   (" << juce::String (d.manufacturer) << ")\n"
                 << "   " << juce::String (d.uniqueId) << "\n"
                 << "   " << juce::String (d.filePath) << "\n\n";
        }
        if (! failed.empty())
        {
            text << "\nFailed to load:\n";
            for (const auto& f : failed) text << "  " << juce::String (f) << "\n";
        }
        if (descriptors.empty() && failed.empty())
            text = "(no descriptors yet — pick a folder above to scan)";

        descriptorsList_.setText (text, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        headerLabel_.setBounds (area.removeFromTop (24));
        formatsLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        scanButton_.setBounds (area.removeFromTop (28).reduced (0, 2));
        scanStatusLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        descriptorsList_.setBounds (area);
    }

private:
    juce::Label      headerLabel_;
    juce::Label      formatsLabel_;
    juce::TextButton scanButton_;
    juce::Label      scanStatusLabel_;
    juce::TextEditor descriptorsList_;
    friend class MainComponent;
};

// =============================================================================
// VideoPane — preview surface plus a status line about the pending pipeline
// =============================================================================
class MainComponent::VideoPane final : public juce::Component
{
public:
    VideoPane()
    {
        addAndMakeVisible (preview_);
        statusLabel_.setJustificationType (juce::Justification::centred);
        statusLabel_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        statusLabel_.setText (
            "No video pipeline connected.  "
            "The M0 FFmpeg spike is pending; once landed, "
            "this surface displays the current frame at the playhead.",
            juce::dontSendNotification);
        addAndMakeVisible (statusLabel_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        statusLabel_.setBounds (area.removeFromBottom (40));
        preview_.setBounds (area);
    }

    VideoPreview& preview() noexcept { return preview_; }

private:
    VideoPreview preview_;
    juce::Label  statusLabel_;
};

// =============================================================================
// MainComponent
// =============================================================================
MainComponent::MainComponent()
    : demo_ (buildDemoSession()),
      undoStack_ (demo_.root),
      sessionLengthSeconds_ (demo_.lengthLmcSeconds),
      tier_ (demoTier()),
      tierPolicy_ (policyFor (tier_)),
      inputs_ (demo_.inputs),
      focusedTape_ (! demo_.inputs.empty() ? demo_.inputs.front().tapeId
                                           : TapeId (0))
{
    // Seed nextConstituentId_ to one past the maximum id present in the
    // initial demo session, so promotion's allocateId callback never collides
    // with an existing id.
    {
        std::function<void (const Constituent&)> walk = [&] (const Constituent& c)
        {
            if (c.id().value() >= nextConstituentId_)
                nextConstituentId_ = c.id().value() + 1;
            for (const auto& child : c.children())
                walk (*child);
        };
        walk (*undoStack_.current());
    }

    // Demo edit so undo/redo have something to traverse: a renamed session.
    {
        const auto renamed = std::make_shared<const Constituent> (
            demo_.root->withName ("renamed session"));
        undoStack_.push (renamed, "rename session");
    }

    // --- Audio I/O (M1 Sessions 1-2) ------------------------------------------
    // Initialise the device manager at OS-default sample rate / buffer size
    // (operator decision 2026-05-17: accept whatever the OS reports; the
    // EngineConfig defaults of 48 kHz / smallest-reliable only apply when the
    // OS doesn't dictate). 2 in / 2 out matches the existing demo input
    // topology — the input mixer milestone (M2) will widen this. Errors here
    // are surfaced via the diagnostics row rather than throwing: a user on a
    // machine without an audio device should still see the UI come up.
    //
    // Session 2: the LMC is constructed before the audio callback and handed
    // to it. The callback then advances the LMC's sample-clock once per
    // buffer (white paper §4.4 — hardware-counted sample-clock is the best
    // disciplined local time source once a device is running). The
    // SteadyMonotonicClock backs the LMC's wall-clock reader, which stays
    // available alongside the sample-clock reader until later milestones
    // reconcile the two via §4.3 calibration tables.
    monotonicClock_  = std::make_shared<SteadyMonotonicClock>();
    lmc_             = std::make_unique<Lmc> (monotonicClock_);

    // M4 Session 3 — the audio callback now drives engine-side mixers and
    // a DirectLayer. Mixers are constructed before the callback so the
    // raw pointers handed to setInputMixer/setOutputMixer/setDirectLayer
    // are live for the callback's entire lifetime. Default identity routes
    // 0→0 and 1→1 preserve M1 monitoring behaviour: when monitoring is
    // armed, input N is mixed into output N, matching the prior pass-
    // through semantics under the new DirectLayer plumbing. If the device
    // exposes more than 2 channels, extras simply route nowhere — the
    // operator wires additional routes through M4's manual-only API.
    inputMixer_      = std::make_unique<InputMixer>();
    outputMixer_     = std::make_unique<OutputMixer>();
    directLayer_     = std::make_unique<DirectLayer>();
    for (int ch = 0; ch < kDefaultStereoChannels; ++ch)
        directLayer_->addRawRoute (InputId (ch), OutputChannelId (ch));

    // M6 Session 2 — construct the truthfulness bus before the audio callback
    // so the setter below sees a live pointer, and pre-reserve the drain
    // buffer to its documented worst-case payload so `drain()` on the timer
    // tick never reallocates (the bus header documents that drain may throw
    // bad_alloc otherwise).
    notificationBus_ = std::make_unique<NotificationBus>();
    notificationDrainBuffer_.reserve (kCategoryCount * NotificationBus::kRingCapacity);
    inputMixer_->setNotificationBus (notificationBus_.get());

    audioCallback_   = std::make_unique<AudioCallback> (engineConfig_);
    audioCallback_->setLmc (lmc_.get());
    audioCallback_->setInputMixer  (inputMixer_.get());
    audioCallback_->setOutputMixer (outputMixer_.get());
    audioCallback_->setDirectLayer (directLayer_.get());
    audioCallback_->setNotificationBus (notificationBus_.get());
    // TapeWriter::setNotificationBus deferred — MainComponent doesn't
    // currently construct a TapeWriter (per M6 S2 audit). When TapeWriter
    // joins the owned app graph (M11 SAF wiring, likely), add a parallel
    // `tapeWriter_->setNotificationBus(notificationBus_.get())` here.

    // M1 Session 3 — engine pieces handed to the audio callback as
    // scaffolding. ASRCs sized 2/2 to match the device request below; the
    // input-mixer milestone (M2) widens this. 1.01 maxIoRatio gives 1% drift
    // headroom (real crystal drift is ppm — well under 0.001%). soxr_create
    // can throw if the platform's soxr is broken; absorb that here so the
    // app still comes up, with the error surfaced through the existing
    // audioDeviceLastError_ channel.
    try
    {
        for (int ch = 0; ch < kDefaultStereoChannels; ++ch)
            asrcInputs_.push_back (
                std::make_unique<Asrc> (1.01, engineConfig_.asrcQuality));
        for (int ch = 0; ch < kDefaultStereoChannels; ++ch)
            asrcOutputs_.push_back (
                std::make_unique<Asrc> (1.01, engineConfig_.asrcQuality));

        std::vector<Asrc*> inputPtrs, outputPtrs;
        inputPtrs.reserve (asrcInputs_.size());
        outputPtrs.reserve (asrcOutputs_.size());
        for (auto& a : asrcInputs_)  inputPtrs.push_back (a.get());
        for (auto& a : asrcOutputs_) outputPtrs.push_back (a.get());
        audioCallback_->setAsrcInputs  (std::move (inputPtrs));
        audioCallback_->setAsrcOutputs (std::move (outputPtrs));
    }
    catch (const std::exception& e)
    {
        asrcInputs_.clear();
        asrcOutputs_.clear();
        audioDeviceLastError_ = juce::String ("ASRC init failed: ") + e.what();
    }

    audioCallback_->setCalibration (&calibration_);

    const auto deviceInitError = audioDeviceManager_.initialiseWithDefaultDevices (
        /*numInputChannelsNeeded*/  kDefaultStereoChannels,
        /*numOutputChannelsNeeded*/ kDefaultStereoChannels);
    if (deviceInitError.isNotEmpty())
        audioDeviceLastError_ = deviceInitError;
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    // --- Performance tab ---
    tabs_.addTab ("Performance", juce::Colours::black, &performanceView_, false);

    // --- Preparation tab ---
    preparationPane_ = std::make_unique<PreparationPane> (audioDeviceManager_,
                                                          *audioCallback_);
    preparationPane_->saveButton().onClick       = [this] { chooseFileAndSave(); };
    preparationPane_->loadButton().onClick       = [this] { chooseFileAndLoad(); };
    preparationPane_->reloadDemoButton().onClick = [this] { reloadDemo(); };
    preparationPane_->timelineView().onArmClicked   = [this] (TapeId t) { toggleArm  (t); };
    preparationPane_->timelineView().onFocusClicked = [this] (TapeId t) { setFocused (t); };
    preparationPane_->timelineView().onPillContextMenuRequested =
        [this] (ConstituentId wrapperId)
    {
        const auto& root = *undoStack_.current();
        const auto path = findWrapperPath (root, wrapperId);
        if (! path) return;
        auto target = root.children()[(*path)[0]];
        for (std::size_t depth = 1; depth < path->size(); ++depth)
            target = target->children()[(*path)[depth]];

        if (! sirius::isPlacementWrapper (*target)) return;

        juce::PopupMenu menu;
        menu.addItem ("Vary this one", [this, wrapperId] { forkPlacement (wrapperId); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withMousePosition());
    };
    preparationPane_->setStatus ("");
    tabs_.addTab ("Preparation", juce::Colours::black, preparationPane_.get(), false);

    // --- Plugins tab ---
    pluginsPane_ = std::make_unique<PluginsPane>();
    pluginsPane_->scanButton_.onClick = [this] { chooseFolderAndScan(); };
    pluginsPane_->setRegisteredFormats (pluginScanner_.registeredFormatNames());
    pluginsPane_->setScanStatus ("");
    pluginsPane_->setDescriptors ({}, {});
    tabs_.addTab ("Plugins", juce::Colours::black, pluginsPane_.get(), false);

    // --- Video tab ---
    videoPane_ = std::make_unique<VideoPane>();
    tabs_.addTab ("Video", juce::Colours::black, videoPane_.get(), false);

    addAndMakeVisible (tabs_);

    // --- Bottom control bar ---
    const double maxTicks = sessionLengthSeconds_.toDouble() * ticksPerSecond;
    playhead_.setRange (0.0, maxTicks, 1.0);
    playhead_.setSliderStyle (juce::Slider::LinearHorizontal);
    playhead_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    playhead_.onValueChange = [this] { refreshPerformance(); refreshPreparation(); };
    addAndMakeVisible (playhead_);

    armButton_.onClick     = [this] { onArmToggle(); };
    markInButton_.onClick  = [this] { onMarkIn(); };
    markOutButton_.onClick = [this] { onMarkOut(); };
    addAndMakeVisible (armButton_);
    addAndMakeVisible (markInButton_);
    addAndMakeVisible (markOutButton_);

    // Long-press on Mark In: hold ≥ 500 ms to request Overlay. Mark In fires
    // at click (onClick); the long-press timer upgrades the pending mode if
    // the user keeps holding past the threshold, before they Mark Out.
    markInButton_.addMouseListener (this, false);

    refreshCaptureControls();

    undoButton_.onClick = [this] { onUndo(); };
    redoButton_.onClick = [this] { onRedo(); };
    addAndMakeVisible (undoButton_);
    addAndMakeVisible (redoButton_);

    bottomInfo_.setJustificationType (juce::Justification::centredRight);
    bottomInfo_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (bottomInfo_);

    // Capture banner sits on top of the tabbed content (z-order: last
    // addAndMakeVisible wins). addAndMakeVisible flips visibility to true,
    // so we explicitly hide it again — the banner only appears in response
    // to a successful Mark Out, never at app start.
    captureBanner_ = std::make_unique<CaptureBanner>();
    addAndMakeVisible (captureBanner_.get());
    captureBanner_->setVisible (false);
    captureBanner_->onUndoRequested = [this] { onUndo(); };

    setSize (1024, 720);

    refreshPerformance();
    refreshPreparation();
    refreshTimeline();
    refreshDiagnostics();

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    // Explicit teardown order: detach the callback from the device manager
    // *before* the callback is destroyed, so the audio thread cannot deliver
    // one last buffer into freed memory. Then close the device — the manager
    // would do this itself in its own destructor, but doing it explicitly
    // here lets the dev-loop log a clean shutdown rather than racing the
    // automatic teardown.
    if (audioCallback_)
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    audioDeviceManager_.closeAudioDevice();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto bottom = area.removeFromBottom (44);
    tabs_.setBounds (area);

    bottom = bottom.reduced (8, 4);
    armButton_.setBounds      (bottom.removeFromLeft (96));
    bottom.removeFromLeft (4);
    markInButton_.setBounds   (bottom.removeFromLeft (88));
    bottom.removeFromLeft (4);
    markOutButton_.setBounds  (bottom.removeFromLeft (88));
    bottom.removeFromLeft (12);
    undoButton_.setBounds     (bottom.removeFromLeft (72));
    bottom.removeFromLeft (4);
    redoButton_.setBounds     (bottom.removeFromLeft (72));
    bottom.removeFromLeft (8);
    bottomInfo_.setBounds (bottom.removeFromRight (220));
    playhead_.setBounds (bottom);

    // Banner: top-centred over the tabbed content. Sits below the tab bar so
    // it doesn't occlude tab switching, above the body so it remains the
    // visual top of stack within the active tab. Sized big enough that a
    // glance can't miss it — white paper 14.5 wants the gesture confirmed
    // through shape and position, not text legibility.
    const int bw = 480;
    const int bh = 52;
    captureBanner_->setBounds ((getWidth() - bw) / 2, 40, bw, bh);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != &markInButton_) return;

    pendingOverlay_ = false;  // every press starts Shared until proven otherwise

    auto t = std::make_unique<FunctionTimer>();
    auto* raw = t.get();
    raw->onTimer = [this, raw]
    {
        raw->stopTimer();
        pendingOverlay_ = true;
        // Visual feedback: tint the Mark In button to confirm the upgrade.
        // No banner here — the banner fires at Mark Out, with the resolved
        // mode reflected in the §11 template.
        markInButton_.setColour (juce::TextButton::buttonColourId,
                                 juce::Colours::orange.darker());
    };
    longPressTimer_ = std::move (t);
    longPressTimer_->startTimer (kOverlayLongPressMs);
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent != &markInButton_) return;
    if (longPressTimer_)
    {
        longPressTimer_->stopTimer();
        longPressTimer_.reset();
    }
    // Restore the button colour if it was tinted — removeColour drops the
    // override so the LookAndFeel default takes effect again.
    markInButton_.removeColour (juce::TextButton::buttonColourId);
}

void MainComponent::timerCallback()
{
    const auto nowMicros = juce::Time::getHighResolutionTicks();
    const auto microsInt = static_cast<juce::int64> (
        juce::Time::highResolutionTicksToSeconds (nowMicros) * 1.0e6);

    if (expectedTickMicros_ != 0)
    {
        // Jitter — how far the timer slipped past its expected fire time.
        // A loose proxy for UI-thread responsiveness; the proper measurement
        // is gesture-to-paint latency wired by the operator (todo.md M7).
        const double latencyMs = std::max (0.0,
            (microsInt - expectedTickMicros_) / 1000.0);
        latencyBudget_.record (latencyMs);
    }
    expectedTickMicros_ = microsInt + 33'333; // ~1/30 s

    // M1 Session 3 — feed OverloadProtection from the audio thread's
    // published per-buffer elapsed time. Division happens here, off the
    // audio thread, so the audio thread's contribution stays a single
    // atomic store. The reportLoad throw is unreachable because elapsed
    // and budget are both non-negative by construction.
    const double elapsed = audioCallback_->lastCallbackElapsedSec();
    const int    bufSize = audioCallback_->currentBufferSize();
    const double rate    = audioCallback_->currentSampleRate();
    if (bufSize > 0 && rate > 0.0)
    {
        const double budget = static_cast<double> (bufSize) / rate;
        overloadProtection_.reportLoad (elapsed / budget);
    }

    // M6 Session 2 — drain the engine→UI truthfulness channel on the same
    // 30 Hz cadence as the diagnostics refresh. Drained count is logged via
    // DBG for the session; the UI surface (a scrollable notification list in
    // the Preparation tab) is M6 Session 3's job. The buffer is pre-reserved
    // in the ctor so `drain()` does not reallocate here.
    if (notificationBus_ != nullptr)
    {
        notificationBus_->drain (notificationDrainBuffer_);
        if (! notificationDrainBuffer_.empty())
            DBG ("NotificationBus drained " << static_cast<int> (notificationDrainBuffer_.size())
                 << " notifications");
    }

    refreshDiagnostics();
}

void MainComponent::refreshPerformance()
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    performanceView_.setState (selectPerformanceView (*undoStack_.current(),
                                                      demo_.sessionToLmc, t));
}

void MainComponent::refreshPreparation()
{
    preparationPane_->setState (selectPreparationView (*undoStack_.current()));
    refreshTimeline();
    refreshDiagnostics();
}

void MainComponent::refreshTimeline()
{
    preparationPane_->setTimelineState (
        selectTimelineView (*undoStack_.current(),
                            demo_.sessionToLmc,
                            inputs_,
                            armedTapesVec(),
                            focusedTape_));
    preparationPane_->setTimelinePlayhead (
        playheadValueToLmc (playhead_.getValue()));
}

std::vector<TapeId> MainComponent::armedTapesVec() const
{
    std::vector<TapeId> v;
    v.reserve (armedTapeIds_.size());
    for (auto raw : armedTapeIds_)
        v.push_back (TapeId (raw));
    return v;
}

void MainComponent::toggleArm (TapeId tape)
{
    const auto raw = tape.value();
    auto it = armedTapeIds_.find (raw);
    if (it == armedTapeIds_.end())
    {
        // Arming a tape also implicitly focuses it: the performer's next
        // gesture (Mark In) will target the freshly-armed input. This is
        // the chord-arms-to-group story collapsed to its single-tape case.
        armedTapeIds_.insert (raw);
        focusedTape_ = tape;
        if (! captureSession_.isArmed())
            captureSession_.arm();
    }
    else
    {
        armedTapeIds_.erase (it);
        if (armedTapeIds_.empty() && captureSession_.isArmed())
            captureSession_.disarm();
    }

    refreshCaptureControls();
    refreshTimeline();
    refreshDiagnostics();
}

void MainComponent::setFocused (TapeId tape)
{
    focusedTape_ = tape;
    refreshTimeline();
    refreshDiagnostics();
}

void MainComponent::refreshDiagnostics()
{
    juce::String tierLine;
    tierLine << "Tier: " << juce::String (toString (tier_))
             << "  ("    << tapeFormatName (tierPolicy_.tapeFormat)
             << ", "     << asrcName       (tierPolicy_.asrcQuality)
             << ", "     << effectName     (tierPolicy_.effectStrategy)
             << ", ring " << juce::String (tierPolicy_.ringDepthSeconds) << "s)";

    juce::String latencyLine;
    latencyLine << "UI tick jitter: mean "
                << juce::String (latencyBudget_.meanMs(), 2) << " ms, worst "
                << juce::String (latencyBudget_.worstMs(), 2) << " ms, "
                << juce::String (latencyBudget_.fractionWithinBudget() * 100.0, 1)
                << "% within 30 ms";

    // Load: last audio-callback load fraction the OverloadProtection state
    // machine saw, plus current shed count. M1 Session 3 publishes the
    // metric; M11 (capability tiers) wires the shed flags back into the
    // video/UI/analyzer subsystems they gate.
    juce::String loadLine;
    loadLine << "Load: "
             << juce::String (overloadProtection_.lastReportedLoad() * 100.0, 1)
             << "% of budget (shed: "
             << juce::String (overloadProtection_.shedCount())
             << ")";

    juce::String undoLine;
    undoLine << "Undo: " << juce::String (undoStack_.currentIndex() + 1)
             << " / "    << juce::String (undoStack_.depth());
    if (undoStack_.canUndo())
        undoLine << " (next undo: " << juce::String (undoStack_.nextUndoLabel()) << ")";

    juce::String captureLine ("Capture: ");
    switch (captureSession_.state())
    {
        case CaptureState::Disarmed:    captureLine << "disarmed";                  break;
        case CaptureState::Armed:       captureLine << "armed, no in-point set";    break;
        case CaptureState::AwaitingOut:
            captureLine << "capturing — in at "
                        << juce::String (captureSession_.pendingIn()->toDouble(), 2)
                        << " s";
            break;
    }
    int loopCount = 0;
    std::optional<TapeId> lastTape;
    Rational lastIn  { 0 }, lastOut { 0 };
    std::function<void (const Constituent&)> count;
    count = [&] (const Constituent& c)
    {
        if (const auto& ref = c.tapeReference())
        {
            ++loopCount;
            lastTape = ref->tape;
            lastIn   = ref->tapeIn;
            lastOut  = ref->tapeOut;
        }
        for (const auto& child : c.children())
            count (*child);
    };
    count (*undoStack_.current());

    captureLine << "    Regions: " << juce::String (loopCount);
    if (lastTape.has_value())
    {
        const double in  = lastIn.toDouble();
        const double out = lastOut.toDouble();
        captureLine << "  (last: " << juce::String (in, 2)
                    << " s → "    << juce::String (out, 2)
                    << " s, "     << juce::String (out - in, 2) << " s long"
                    << " · tape #" << juce::String ((juce::int64) lastTape->value())
                    << ")";
    }

    preparationPane_->setDiagnostics (
        tierLine + "\n" + latencyLine + "\n" + loadLine + "\n"
        + undoLine + "\n" + captureLine);

    undoButton_.setEnabled (undoStack_.canUndo());
    redoButton_.setEnabled (undoStack_.canRedo());

    bottomInfo_.setText (
        juce::String (playheadValueToLmc (playhead_.getValue()).toDouble(), 2) + " s",
        juce::dontSendNotification);
}

void MainComponent::onArmToggle()
{
    // Bottom-bar Arm targets the focused tape — the row whose strip head
    // last received an Arm or Focus click. This keeps the one-handed
    // gesture (Arm → Mark In → Mark Out) working from the bottom bar while
    // letting per-row arm be the primary surface for selecting which input
    // the gesture acts on.
    toggleArm (focusedTape_);
}

void MainComponent::onMarkIn()
{
    // The playhead position is the LMC time source while the M2 audio
    // device wiring is still operator-deferred — once a real LMC clock
    // is running, this becomes Lmc::now() (or the equivalent) and the
    // playhead drops out of the capture path entirely.
    const Rational t = playheadValueToLmc (playhead_.getValue());
    // The focused tape — the row most recently armed or focus-clicked in
    // the timeline strip column. Per the refined Mockup A, multi-arm is
    // visual-only for now: only the focused tape's id stamps the region.
    // Group-capture across all armed tapes is M8 work and will need a
    // per-tape CaptureSession map; the data here is already future-shaped.
    captureSession_.markIn (t, focusedTape_);
    refreshCaptureControls();
    refreshDiagnostics();
}

void MainComponent::onMarkOut()
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    if (auto region = captureSession_.markOut (t))
    {
        const sirius::CaptureRestorePoint restorePoint {
            region->inLmcSeconds, region->tape };

        lastRequestWasOverlay_ = pendingOverlay_;
        auto result = promotion::promote (
            *undoStack_.current(),
            demo_.sessionToLmc,
            *region,
            region->inLmcSeconds,
            pendingOverlay_ ? promotion::AttachmentMode::Overlay
                            : promotion::AttachmentMode::Shared,
            [this] { return ConstituentId (nextConstituentId_++); });

        // Consume the pending-overlay flag — the next capture starts fresh.
        pendingOverlay_ = false;

        undoStack_.push (
            std::make_shared<const Constituent> (std::move (result.newRoot)),
            result.undoLabel,
            restorePoint);

        announceCapture (*region, result);
        refreshPerformance();
        refreshPreparation();
    }

    refreshCaptureControls();
    refreshDiagnostics();
}

void MainComponent::announceCapture (const CaptureRegion& region,
                                     const promotion::PromotionResult& result)
{
    // Spec §11 — four templates only. No tape numbers. No durations. No mode
    // indicators. The musician sees what landed, in their own vocabulary.
    juce::ignoreUnused (region);  // intentional: region details are plumbing

    juce::String msg;

    const bool wasOverlay = result.resolvedMode == promotion::AttachmentMode::Overlay;
    // Note: a "downgrade with no host AND no minted phrase" should not happen
    // in practice — the Shared path always mints when no host exists. The
    // downgrade case below covers the Overlay→Shared path landing in the mint
    // branch, where `mintedPhraseId` IS set; the banner still wants the
    // "no section here yet" phrasing per §11 row 4.

    if (wasOverlay)
    {
        // "Added to verse 2 only"  (placement ordinal from the data field)
        const juce::String hostName =
            result.hostPhraseName.value_or (std::string ("the phrase here"));
        const auto idx = result.overlayPlacementIndex.value_or (0u);
        msg << "Added to " << hostName << " " << static_cast<int> (idx) << " only";
    }
    else if (result.mintedPhraseId.has_value() && ! result.hostPhraseName.has_value())
    {
        // Two §11 rows produce this branch:
        //   - Shared + mint (no host found anywhere): "New phrase captured"
        //   - Overlay requested but downgraded AND fell through to mint:
        //     "Added — no section here yet"
        // We disambiguate by whether the operator's pending request was Overlay
        // (pendingOverlay_ was true at promote() time and got consumed there;
        // we reach the consumed-state here, so check the prior value via a
        // cached copy that onMarkOut sets before clearing).
        msg = lastRequestWasOverlay_
              ? juce::String ("Added — no section here yet")
              : juce::String ("New phrase captured");
    }
    else if (result.hostPhraseName.has_value())
    {
        // Shared, host found — the bread-and-butter case.
        msg << "Added to " << juce::String (*result.hostPhraseName);
        if (lastRequestWasOverlay_)
            msg << " — no section here yet";
    }
    else
    {
        // Defensive: no host, no mint — shouldn't reach here, but stay safe.
        msg = "Added";
    }

    lastRequestWasOverlay_ = false;
    captureBanner_->show (msg);
}

void MainComponent::refreshCaptureControls()
{
    // Glanceable (white paper 14.5): label, tint, and enabled state
    // communicate the capture state at a glance. Red means live, grey
    // means stood down. Mark In / Mark Out enable only when valid for the
    // current state — invalid gestures simply cannot be issued.
    const auto state = captureSession_.state();
    const bool armed       = state != CaptureState::Disarmed;
    const bool capturing   = state == CaptureState::AwaitingOut;

    armButton_.setButtonText (armed ? "Disarm" : "Arm");
    armButton_.setColour (juce::TextButton::buttonColourId,
                          armed ? juce::Colours::darkred
                                : juce::Colours::darkgrey);
    armButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    armButton_.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);

    // Mark In: available while armed (Armed and AwaitingOut both accept it
    // — the second tap replaces the pending in-point).
    markInButton_.setEnabled (armed);

    // Mark Out: only valid when a capture is in progress.
    markOutButton_.setEnabled (capturing);
    markOutButton_.setColour (juce::TextButton::buttonColourId,
                              capturing ? juce::Colours::darkgreen
                                        : juce::Colours::darkgrey);
    markOutButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    markOutButton_.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
}

void MainComponent::onUndo()
{
    if (undoStack_.canUndo())
    {
        // The entry we are about to leave (the current top) is the one whose
        // restore point applies — undoing a promotion entry restores the
        // CaptureSession state that existed before that promotion fired.
        const auto restoreOnLeave = undoStack_.currentEntryRestorePoint();

        undoStack_.undo();

        if (restoreOnLeave.has_value())
        {
            // Re-enter AwaitingOut with the original Mark In intact. The
            // operator can immediately Mark Out again at a different time, or
            // hit Disarm to abandon. Tape samples between the original Mark
            // In and any future Mark Out are still on the always-running tape.
            // If a new pending-In was set after the promotion, it is replaced — undo
            // authoritatively restores the pre-promotion in-point.
            captureSession_.arm();  // idempotent: only acts from Disarmed
            captureSession_.markIn (restoreOnLeave->pendingIn,
                                    restoreOnLeave->pendingTape);
        }

        refreshPerformance();
        refreshPreparation();
        refreshCaptureControls();
        refreshDiagnostics();
    }
}

void MainComponent::onRedo()
{
    if (undoStack_.canRedo())
    {
        undoStack_.redo();

        // Symmetric to onUndo: redo of a promotion entry replays the
        // post-promotion CaptureSession state, which is Armed-with-no-pending-In.
        // If the prior onUndo restored AwaitingOut, redo must clear it again or
        // the next Mark Out would close a phantom region and create a duplicate
        // Loop on top of the just-redone tree.
        //
        // Edge case: if the operator set a *new* Mark In after the undo (i.e.
        // pendingIn now diverges from the restore point), this cancel still
        // fires and silently discards the fresh in-point. The redone tree is
        // the authoritative state in that case — the operator's "I'm starting
        // a different capture" intent loses to the "go back to the promoted
        // state" intent. Worth noting; not currently surfaced to the operator.
        if (undoStack_.currentEntryRestorePoint().has_value())
            captureSession_.cancel();

        refreshPerformance();
        refreshPreparation();
        refreshCaptureControls();
        refreshDiagnostics();
    }
}

void MainComponent::forkPlacement (ConstituentId wrapperId)
{
    const auto& root = *undoStack_.current();
    const auto wrapperPath = findWrapperPath (root, wrapperId);
    if (! wrapperPath) return;

    // Path-splice copy-on-write: only the spine from root down to the wrapper
    // is rebuilt. At the wrapper we swap in a deep copy of its shared child
    // (with fresh ids) and flip the role string. Mirrors the splice shape
    // promote() uses on the capture path.
    std::function<sirius::Constituent (const sirius::Constituent&, std::size_t)>
        forkedSplice;
    forkedSplice = [&] (const sirius::Constituent& c, std::size_t depth)
                       -> sirius::Constituent
    {
        if (depth == wrapperPath->size())
        {
            if (! sirius::isPlacementWrapper (c)) return c;
            const auto& sharedPhrase = *c.children()[0];
            auto allocate = [this] { return ConstituentId (nextConstituentId_++); };
            const auto deepCopy = std::make_shared<const sirius::Constituent> (
                deepCopyWithFreshIds (sharedPhrase, allocate));

            sirius::PhraseMetadata forkedMeta = *c.phraseMetadata();
            forkedMeta.role = "forked-placement";
            return c.withPhraseMetadata (std::move (forkedMeta))
                    .withChildReplaced (0, deepCopy);
        }
        const std::size_t i = (*wrapperPath)[depth];
        auto childCopy = std::make_shared<const sirius::Constituent> (
            forkedSplice (*c.children()[i], depth + 1));
        return c.withChildReplaced (i, childCopy);
    };

    sirius::Constituent newRoot = forkedSplice (root, 0);

    undoStack_.push (
        std::make_shared<const sirius::Constituent> (std::move (newRoot)),
        "vary this placement");

    refreshPerformance();
    refreshPreparation();
    refreshCaptureControls();
    refreshDiagnostics();
}

void MainComponent::chooseFileAndSave()
{
    sessionFileChooser_ = std::make_unique<juce::FileChooser> (
        "Save Sirius session as...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("session.sirius.json"),
        "*.json");

    sessionFileChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            const auto target = fc.getResult();
            if (target == juce::File()) return;

            const auto json = persistence::serializeSession (*undoStack_.current());
            if (target.replaceWithText (json))
                preparationPane_->setStatus ("Saved to " + target.getFullPathName());
            else
                preparationPane_->setStatus ("Failed to write " + target.getFullPathName());
        });
}

void MainComponent::chooseFileAndLoad()
{
    sessionFileChooser_ = std::make_unique<juce::FileChooser> (
        "Load Sirius session...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.json");

    sessionFileChooser_->launchAsync (
        juce::FileBrowserComponent::openMode,
        [this] (const juce::FileChooser& fc)
        {
            const auto source = fc.getResult();
            if (source == juce::File()) return;

            const auto json = source.loadFileAsString();
            try
            {
                auto loaded = persistence::deserializeSession (json);
                // The white paper Part 14.7 rule: load is an edit; preserve
                // the existing undo history rather than wiping it. The
                // operator can undo back to whatever was on screen before.
                undoStack_.push (loaded, "load " + source.getFileName().toStdString());
                refreshPerformance();
                refreshPreparation();
                preparationPane_->setStatus ("Loaded " + source.getFileName());
            }
            catch (const std::exception& e)
            {
                // runtime_error covers JSON / format errors; logic_error covers
                // the shared-instance invariant raised by deserializeSession.
                preparationPane_->setStatus (
                    juce::String ("Load failed: ") + e.what());
            }
        });
}

void MainComponent::reloadDemo()
{
    // Push the demo as a fresh edit — undoable to whatever was loaded before.
    // Input topology refreshes from the same source so descriptors stay in
    // step with the loaded Constituent tree.
    const auto rebuilt = buildDemoSession();
    undoStack_.push (rebuilt.root, "reload demo");
    inputs_ = rebuilt.inputs;
    refreshPerformance();
    refreshPreparation();
    preparationPane_->setStatus ("Reloaded the built-in demo session");
}

void MainComponent::chooseFolderAndScan()
{
    pluginFolderChooser_ = std::make_unique<juce::FileChooser> (
        "Choose a folder to scan for plugins...",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    pluginFolderChooser_->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto chosen = fc.getResult();
        if (chosen == juce::File()) return;

        pluginsPane_->setScanStatus ("Scanning " + chosen.getFullPathName() + " ...");
        juce::FileSearchPath path;
        path.add (chosen);
        const auto result = pluginScanner_.scan (path);

        pluginsPane_->setScanStatus (
            "Scanned " + chosen.getFullPathName()
            + "  -  " + juce::String ((int) result.descriptors.size()) + " loaded, "
            + juce::String ((int) result.failedFiles.size()) + " failed");
        pluginsPane_->setDescriptors (result.descriptors, result.failedFiles);
    });
}

} // namespace sirius
