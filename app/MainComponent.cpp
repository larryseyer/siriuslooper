#include "MainComponent.h"

#include "sirius/PerformanceViewState.h"
#include "sirius/PreparationView.h"
#include "sirius/PreparationViewState.h"
#include "sirius/SessionFormat.h"
#include "sirius/TapeId.h"
#include "sirius/TimelineView.h"
#include "sirius/TimelineViewState.h"
#include "sirius/VideoPreview.h"

#include <stdexcept>

namespace sirius
{

namespace
{
    /// The playhead slider works in sixteenths of an LMC second so its value
    /// converts to an exact Rational — the engine still never sees a double.
    constexpr int ticksPerSecond = 16;

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
}

// =============================================================================
// PreparationPane — PreparationView on top, a diagnostics row at the bottom
// =============================================================================
class MainComponent::PreparationPane final : public juce::Component
{
public:
    PreparationPane()
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

        addAndMakeVisible (preparationView_);
        addAndMakeVisible (timelineView_);
        diagnosticsLabel_.setJustificationType (juce::Justification::topLeft);
        diagnosticsLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (diagnosticsLabel_);
    }

    void setState (PreparationViewState s)    { preparationView_.setState (std::move (s)); }
    void setTimelineState (TimelineViewState s) { timelineView_.setState  (std::move (s)); }
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

        diagnosticsLabel_.setBounds (area.removeFromBottom (60));
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
    juce::TextButton saveButton_;
    juce::TextButton loadButton_;
    juce::TextButton reloadDemoButton_;
    juce::Label      statusLabel_;
    PreparationView  preparationView_;
    TimelineView     timelineView_;
    juce::Label      diagnosticsLabel_;
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
    // Demo edit so undo/redo have something to traverse: a renamed session.
    {
        const auto renamed = std::make_shared<const Constituent> (
            demo_.root->withName ("renamed session"));
        undoStack_.push (renamed, "rename session");
    }

    // --- Performance tab ---
    tabs_.addTab ("Performance", juce::Colours::black, &performanceView_, false);

    // --- Preparation tab ---
    preparationPane_ = std::make_unique<PreparationPane>();
    preparationPane_->saveButton().onClick       = [this] { chooseFileAndSave(); };
    preparationPane_->loadButton().onClick       = [this] { chooseFileAndLoad(); };
    preparationPane_->reloadDemoButton().onClick = [this] { reloadDemo(); };
    preparationPane_->timelineView().onArmClicked   = [this] (TapeId t) { toggleArm  (t); };
    preparationPane_->timelineView().onFocusClicked = [this] (TapeId t) { setFocused (t); };
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
    refreshCaptureControls();

    undoButton_.onClick = [this] { onUndo(); };
    redoButton_.onClick = [this] { onRedo(); };
    addAndMakeVisible (undoButton_);
    addAndMakeVisible (redoButton_);

    bottomInfo_.setJustificationType (juce::Justification::centredRight);
    bottomInfo_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (bottomInfo_);

    setSize (1024, 720);

    refreshPerformance();
    refreshPreparation();
    refreshTimeline();
    refreshDiagnostics();

    startTimerHz (30);
}

MainComponent::~MainComponent() = default;

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
    captureLine << "    Regions: " << juce::String ((int) capturedRegions_.size());
    if (! capturedRegions_.empty())
    {
        const auto& last = capturedRegions_.back();
        const double in  = last.inLmcSeconds.toDouble();
        const double out = last.outLmcSeconds.toDouble();
        captureLine << "  (last: " << juce::String (in, 2)
                    << " s → "    << juce::String (out, 2)
                    << " s, "     << juce::String (out - in, 2) << " s long)";
    }

    preparationPane_->setDiagnostics (
        tierLine + "\n" + latencyLine + "\n" + undoLine + "\n" + captureLine);

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
        capturedRegions_.push_back (*region);

    refreshCaptureControls();
    refreshDiagnostics();
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
        undoStack_.undo();
        refreshPerformance();
        refreshPreparation();
    }
}

void MainComponent::onRedo()
{
    if (undoStack_.canRedo())
    {
        undoStack_.redo();
        refreshPerformance();
        refreshPreparation();
    }
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
            catch (const std::runtime_error& e)
            {
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
