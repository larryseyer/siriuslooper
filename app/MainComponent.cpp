#include "MainComponent.h"

#include "sirius/PerformanceViewState.h"
#include "sirius/PreparationView.h"
#include "sirius/PreparationViewState.h"
#include "sirius/SessionFormat.h"
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
        diagnosticsLabel_.setJustificationType (juce::Justification::topLeft);
        diagnosticsLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (diagnosticsLabel_);
    }

    void setState (PreparationViewState s) { preparationView_.setState (std::move (s)); }
    void setDiagnostics (const juce::String& text) { diagnosticsLabel_.setText (text, juce::dontSendNotification); }
    void setStatus (const juce::String& text)      { statusLabel_.setText (text, juce::dontSendNotification); }

    juce::TextButton& saveButton()       { return saveButton_; }
    juce::TextButton& loadButton()       { return loadButton_; }
    juce::TextButton& reloadDemoButton() { return reloadDemoButton_; }

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
        preparationView_.setBounds (area);
    }

private:
    juce::TextButton saveButton_;
    juce::TextButton loadButton_;
    juce::TextButton reloadDemoButton_;
    juce::Label      statusLabel_;
    PreparationView  preparationView_;
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
      tierPolicy_ (policyFor (tier_))
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
    undoButton_.setBounds (bottom.removeFromLeft (72));
    bottom.removeFromLeft (4);
    redoButton_.setBounds (bottom.removeFromLeft (72));
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

    preparationPane_->setDiagnostics (tierLine + "\n" + latencyLine + "\n" + undoLine);

    undoButton_.setEnabled (undoStack_.canUndo());
    redoButton_.setEnabled (undoStack_.canRedo());

    bottomInfo_.setText (
        juce::String (playheadValueToLmc (playhead_.getValue()).toDouble(), 2) + " s",
        juce::dontSendNotification);
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
    // Empty pattern: show every file. Filtering by "*.json" or
    // "*.sirius.json;*.json" caused macOS to grey out our saved files —
    // replaceWithText writes raw bytes without setting the macOS "kind"
    // metadata that NSOpenPanel uses when converting patterns to UTIs.
    sessionFileChooser_ = std::make_unique<juce::FileChooser> (
        "Load Sirius session...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String());

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
    undoStack_.push (buildDemoSession().root, "reload demo");
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
