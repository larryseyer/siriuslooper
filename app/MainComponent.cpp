#include "MainComponent.h"

#include "StripContextOverlay.h"
#include "components/ChannelDetailPanWidTab.h"
#include "components/CompactFaderStrip.h"
#include "ida/CalibrationStore.h"
#include "ida/ChannelDetail.h"
#include "ida/ConstituentValidator.h"
#include "ida/IdaPalette.h"
#include "ida/InsertChainPopup.h"
#include "ida/PerformanceViewState.h"
#include "ida/PreparationView.h"
#include "ida/PreparationViewState.h"
#include "ida/SessionFormat.h"
#include "ida/SessionSnapshot.h"
#include "ida/TapeId.h"
#include "ida/TapePoolMirror.h"
#include "ida/TimelineView.h"
#include "ida/TimelineViewState.h"
#include "ida/VideoPreview.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace ida
{

namespace
{
    /// Session-file envelope schema version. The envelope is a thin wrapper
    /// (written/read only here) carrying the session and tape-pool documents;
    /// it is distinct from SessionFormat's session/mixer-graph versions. Bumped
    /// only when the envelope's own layout changes.
    /// v2 (2026-05-24, slice P): added "input_mixer", "output_mixer", and
    /// "phrase_channel_map" keys. v1 envelopes load clean — the new keys are
    /// optional and default to "mixers untouched / map empty" (current pre-v2
    /// behavior).
    constexpr int kSessionEnvelopeVersion = 2;

    /// Small local Timer subclass — IDA's vendored JUCE has no
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

    /// M8 S6 — the device-scoped calibration sidecar. Calibration describes one
    /// hardware clock's drift (white paper Part 4.3), so it belongs to the
    /// machine, not to a session document — it lives under app-support and never
    /// travels inside a session moved to other hardware.
    juce::File calibrationSidecarFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("IDA")
            .getChildFile ("calibration.json");
    }

    /// Tape slice 3 — root directory for per-tape FLAC files. Mirrors the
    /// calibration sidecar location pattern: under app-support, never inside
    /// a session document, so recordings survive session moves.
    juce::File tapesDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("IDA")
            .getChildFile ("tapes");
    }

    /// The playhead slider works in sixteenths of an LMC second so its value
    /// converts to an exact Rational — the engine still never sees a double.
    constexpr int ticksPerSecond = 16;

    /// Default stereo-channel count — matches initialiseWithDefaultDevices(2, 2)
    /// and the M4 default identity routes (input N → output N for N in [0, count)).
    /// If a IDA device with a different default ever lands, change this in
    /// one place.
    constexpr int kDefaultStereoChannels = 2;

    /// Max block size passed to each input strip's LUFS meter prepare(). Sized
    /// generously so the meter never clamps a large device buffer.
    constexpr int kInputLufsMaxBlock = 8192;

    /// M6 Session 3 — vertical pixel budget for the rolling notifications
    /// list inside the Preparation pane. The list sits above the
    /// 84px diagnostics row (which already lives at the bottom). 100px holds
    /// roughly six 12pt monospaced lines with scrollbars exposed for older
    /// entries — sized so the operator sees the recent few at a glance
    /// without dominating the timeline area. M22 redesigns this surface.
    constexpr int kNotificationsRowHeightPx = 100;

    /// Minimum vertical space reserved for the preparation tree (the
    /// structural readout that sits ABOVE the timeline). The notifications +
    /// diagnostics stack has already been removed from `area` by the time
    /// this clamp executes, so this constant only governs the tree/timeline
    /// split. Pre-M6 value (M4): 80px; unchanged by M6 — the notifications
    /// row is independently reserved at the bottom via
    /// `kNotificationsRowHeightPx` + `removeFromBottom`.
    constexpr int kPreparationTreeMinHeightPx = 80;

    /// Format a single Notification into a one-line operator-facing string:
    /// `[level] category: message (Δt ago)`. Δt computed at render time
    /// against `nowTicks` so the surface ages naturally on every refresh.
    /// Per-line allocation lives on the message thread — drain cadence is
    /// 30Hz and history is bounded at `kNotificationHistorySize = 20`.
    const char* notificationLevelName (NotificationLevel level) noexcept
    {
        switch (level)
        {
            case NotificationLevel::Info:        return "Info";
            case NotificationLevel::Degradation: return "Degradation";
            case NotificationLevel::Warning:     return "Warning";
            case NotificationLevel::Error:       return "Error";
        }
        return "?";
    }

    const char* notificationCategoryName (Category category) noexcept
    {
        switch (category)
        {
            case Category::DiskPressure: return "DiskPressure";
            case Category::CpuPressure:  return "CpuPressure";
            case Category::RamPressure:  return "RamPressure";
            case Category::DeviceEvent:  return "DeviceEvent";
            case Category::PluginEvent:  return "PluginEvent";
            case Category::ClockEvent:   return "ClockEvent";
            case Category::NetworkEvent: return "NetworkEvent";
            case Category::StateRepair:  return "StateRepair";
            case Category::TapeRotation: return "TapeRotation";
        }
        return "?";
    }

    juce::String formatNotificationLine (const Notification& n, std::int64_t nowTicks)
    {
        const double ageSeconds = juce::Time::highResolutionTicksToSeconds (
            nowTicks - n.postedTicks);

        juce::String line;
        line.preallocateBytes (160);
        line << "[" << notificationLevelName (n.level) << "] "
             << notificationCategoryName (n.category) << ": "
             << juce::String (n.message.data())
             << " (" << juce::String (ageSeconds, 1) << "s ago)";
        return line;
    }

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
    ida::Constituent deepCopyWithFreshIds (
        const ida::Constituent& src,
        const ida::promotion::IdAllocator& allocate)
    {
        ida::Constituent copy (allocate(), src.conceptualIn(), src.conceptualOut());
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
                std::make_shared<const ida::Constituent> (
                    deepCopyWithFreshIds (*child, allocate)));
        return copy;
    }

    /// Locate the wrapper by id in `root` and return its index path from the
    /// root's children. Returns empty optional if not found (caller can no-op).
    std::optional<std::vector<std::size_t>> findWrapperPath (
        const ida::Constituent& root, ida::ConstituentId wrapperId)
    {
        std::optional<std::vector<std::size_t>> found;
        std::vector<std::size_t> path;
        std::function<void (const ida::Constituent&)> walk;
        walk = [&] (const ida::Constituent& c)
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
// PreparationPane — PreparationView on top, a diagnostics row at the bottom.
// The audio-device picker lives in the Settings tab (SettingsPane). Per V9
// §7.2 monitoring is per-channel: each input strip's MON button is the only
// monitoring control — there is no global enable toggle.
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

        // M6 Session 3 — read-only multi-line monospaced editor for the rolling
        // notification history. Read-only because notifications are engine→UI
        // output; the operator never types into them. Scrollbars exposed for
        // older entries beyond
        // what fits in `kNotificationsRowHeightPx`.
        notificationsList_.setMultiLine (true);
        notificationsList_.setReadOnly (true);
        notificationsList_.setScrollbarsShown (true);
        notificationsList_.setFont (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(), 12.0f, 0));
        notificationsList_.setText ("(no notifications)", false);
        addAndMakeVisible (notificationsList_);
    }

    void setState (PreparationViewState s)    { preparationView_.setState (std::move (s)); }
    void setTimelineState (TimelineViewState s) { timelineView_.setState  (std::move (s)); }
    void setTimelinePlayhead (std::optional<Rational> t) { timelineView_.setPlayhead (t); }
    void setDiagnostics (const juce::String& text) { diagnosticsLabel_.setText (text, juce::dontSendNotification); }
    void setStatus (const juce::String& text)      { statusLabel_.setText (text, juce::dontSendNotification); }

    /// M6 Session 3 — re-render the notification list from the message-thread
    /// rolling history. Called on every 30Hz timer tick AFTER the drain has
    /// trimmed the deque to `kNotificationHistorySize`. Per-line formatting
    /// allocates juce::Strings on the message thread; that's fine for a
    /// bounded 20-entry list at 30Hz.
    void setNotifications (const std::deque<Notification>& history)
    {
        if (history.empty())
        {
            notificationsList_.setText ("(no notifications)", false);
            return;
        }

        const std::int64_t nowTicks = juce::Time::getHighResolutionTicks();
        juce::String text;
        text.preallocateBytes (history.size() * 160);
        // Newest-first order matches the operator's reading direction — the
        // event that just landed is what they want at eye level, history
        // scrolls beneath. Iterate in reverse over the chronological deque
        // (drain appends back, so back() is most recent).
        for (auto it = history.rbegin(); it != history.rend(); ++it)
        {
            text << formatNotificationLine (*it, nowTicks) << "\n";
        }
        notificationsList_.setText (text, false);
    }

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

        // The audio-device picker lives in the Settings tab (SettingsPane),
        // reclaiming the ~244px it used to occupy here for the tree/timeline.
        diagnosticsLabel_.setBounds (area.removeFromBottom (84));
        area.removeFromBottom (6);
        // M6 Session 3 — notifications list above diagnostics, both
        // bottom-anchored so the timeline keeps the dominant share.
        notificationsList_.setBounds (area.removeFromBottom (kNotificationsRowHeightPx));
        area.removeFromBottom (6);

        // The timeline gets the dominant share of vertical space — it's the
        // surface the performer reaches for. The tree readout sits above it
        // as a structural reference. Minimum heights guard tiny windows. The
        // notifications + diagnostics stack was already `removeFromBottom`ed
        // above, so this clamp only governs the tree/timeline split.
        const int timelineMin = timelineView_.totalHeight() + 8;
        const int timelineH   = std::max (timelineMin,
                                          juce::jmin (area.getHeight() * 6 / 10,
                                                      area.getHeight()
                                                          - kPreparationTreeMinHeightPx));
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
    juce::TextEditor notificationsList_;
};

// =============================================================================
// SettingsPane — the Settings tab. Holds the audio-device picker. Settings
// that don't belong in the performer's glanceable surfaces live here. (The
// V8 global monitoring toggle is gone — V9 §7.2 moved MON to a per-channel
// button on each input strip.)
// =============================================================================
class MainComponent::SettingsPane final : public juce::Component
{
public:
    SettingsPane (juce::AudioDeviceManager& deviceManager)
        : deviceSelector_ (deviceManager,
                           /*minInputChannels*/  0, /*maxInputChannels*/  2,
                           /*minOutputChannels*/ 0, /*maxOutputChannels*/ 2,
                           /*showMidiInputOptions*/  false,
                           /*showMidiOutputSelector*/ false,
                           /*showChannelsAsStereoPairs*/ true,
                           /*hideAdvancedOptionsWithButton*/ true)
    {
        audioHeaderLabel_.setText ("Audio device", juce::dontSendNotification);
        audioHeaderLabel_.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (audioHeaderLabel_);
        addAndMakeVisible (deviceSelector_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        audioHeaderLabel_.setBounds (area.removeFromTop (22));
        deviceSelector_.setBounds   (area.removeFromTop (220));
    }

private:
    juce::Label                        audioHeaderLabel_;
    juce::AudioDeviceSelectorComponent deviceSelector_;
};

// =============================================================================
// InputMixerPane — the Input Mixer tab (white paper Part VI, the capture
// console). A horizontal row of OTTO-skinned CompactFaderStrips, one stereo
// strip per stereo pair of active device inputs. The pane is pure presentation
// + gesture relay: it owns the strips and forwards fader/mute/solo/select
// changes to MainComponent via std::function hooks, which apply them to the
// engine ChannelStrips. MainComponent pushes meter levels back in on its timer.
// Tape-output routing (the strip's bottom region) and the pan/width detail
// panel are later slices, so the strip's output combo is hidden for now.
// =============================================================================
class MainComponent::InputMixerPane final : public juce::Component,
                                            public otto::ui::CompactFaderStripListener,
                                            public otto::ui::ChannelDetailPanWidTabListener,
                                            public ida::ui::ChannelDetailSendsTabListener,
                                            public ida::ui::ChannelDetailEQTabListener,
                                            public ida::ui::ChannelDetailCMPTabListener,
                                            public ida::ui::ChannelDetailListener,
                                            private juce::Timer
{
public:
    InputMixerPane()
    {
        // Tabbed detail panel: every tab is real after slice EC. Each tab's
        // listener wires independently so the pane only sees the gestures
        // it actually relays. The ChannelDetailListener on the panel itself
        // catches tab-change events so resized() can expand the panel to
        // full-screen when EQ / CMP are active (slice EC-Polish).
        detailPanel_.panWidTab().addListener (this);
        detailPanel_.sendsTab() .addListener (this);
        detailPanel_.eqTab()    .addListener (this);
        detailPanel_.cmpTab()   .addListener (this);
        detailPanel_.addListener (this);
        addChildComponent (detailPanel_);

        // Escape-to-deselect — the operator's escape gesture from a
        // selection. Especially load-bearing in full-screen EQ / CMP
        // mode where the strip row is hidden and there's no other
        // visible way to back out.
        setWantsKeyboardFocus (true);

        // Touch-friendly back affordance for the same gesture — visible
        // only in EQ / CMP full-screen mode (resized() controls visibility).
        backButton_ = std::make_unique<juce::TextButton> ("Back");
        backButton_->onClick = [this] { deselectAll(); };
        addChildComponent (*backButton_);
    }

    /// Clear any current selection + hide the detail panel + restore strip
    /// row visibility. Public so showDetailFor / showBusDetailFor callers
    /// can also force it.
    void deselectAll()
    {
        for (int i = 0; i < stripCount(); ++i)
            strips_[static_cast<std::size_t> (i)]->setSelected (false);
        for (int i = 0; i < busStripCount(); ++i)
            busStrips_[static_cast<std::size_t> (i)]->setSelected (false);
        selectedStrip_ = -1;
        selectedBus_   = -1;
        detailPanel_.setVisible (false);
        resized();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            deselectAll();
            return true;
        }
        return false;
    }

    // --- ChannelDetailListener (slice EC-Polish full-screen layout) -----------
    void channelDetailTabChanged (int /*tabIndex*/) override { resized(); }

    /// One strip's display state: its name and whether its source pair is in
    /// stereo mode (drives the Split/Collapse menu wording). Stereo = one strip;
    /// mono = two strips (the pair's L and R halves).
    struct StripInfo { juce::String name; bool stereo; };

    /// One bus/FX-return strip's display state. `isFxReturn` picks the
    /// CompactFaderStrip ChannelType (FXReturn vs Bus).
    struct BusInfo { juce::String name; bool isFxReturn; };

    /// A routing destination kind for a channel's main-out.
    enum class DestKind { Tape, Bus, HardwareOutput };
    /// One selectable destination in a strip's picker. `id` is the TapeId or
    /// BusId raw value (unused / 0 for HardwareOutput). Identity for ticking is
    /// the (kind, id) pair — names are user-editable and non-unique.
    struct DestChoice { DestKind kind; std::int64_t id; juce::String name; };
    /// A strip's current destination (button label + what ticks in the popup).
    /// Defaults to Tape + id 0 (no-match sentinel; pool ids start at 1).
    struct StripDest { DestKind currentKind { DestKind::Tape };
                       std::int64_t currentId { 0 };
                       juce::String currentName; };

    // Gesture relays (idx = strip index). Set by MainComponent.
    std::function<void (int idx, float gainLinear)> onGain;
    std::function<void (int idx, bool muted)>       onMute;
    std::function<void (int idx, bool soloed)>      onSolo;
    /// Right-click "Split to two mono channels" / "Collapse to stereo" (RME).
    std::function<void (int idx)>                   onToggleStereoMono;
    /// A strip was selected — MainComponent reads its pan/width and calls
    /// showDetailFor() to populate + reveal the detail panel.
    std::function<void (int idx)>                   onSelect;
    /// Detail-panel pan knob moved. `pan` is industry-standard [-1, +1].
    std::function<void (int idx, float pan)>        onPan;
    /// Detail-panel width knob moved. `width` is [0, 2] (1 = unity stereo).
    std::function<void (int idx, float width)>      onWidth;
    /// Sends-tab knob moved for the selected channel. `fxReturnIdx` indexes
    /// into the FX-return list MainComponent passed via `showDetailFor`.
    /// `level` is linear 0..1. MainComponent translates back to BusId +
    /// `InputMixer::setChannelSend`.
    std::function<void (int idx, int fxReturnIdx, float level)> onSendChanged;
    /// Sends-tab PRE FADER toggle changed for the selected channel.
    /// MainComponent calls `InputMixer::setChannelSendIsPreFader`.
    std::function<void (int idx, bool preFader)>    onPreFaderToggled;
    /// EQ-tab control moved for the selected channel. MainComponent
    /// pushes `cfg` through `setInternalEqConfigAt` against the strip's
    /// EQ slot, bracketed in the audio-callback detach pattern.
    std::function<void (int idx, ida::EqConfig cfg)> onEqConfigChanged;
    /// EQ-tab "+ Add EQ" empty-state button. MainComponent appends an
    /// EQ slot to the strip's EffectChain via setEffectChain.
    std::function<void (int idx)>                      onEqSlotAddRequested;
    /// CMP-tab control moved for the selected channel.
    std::function<void (int idx, ida::CmpConfig cfg)> onCmpConfigChanged;
    /// CMP-tab "+ Add CMP" empty-state button.
    std::function<void (int idx)>                      onCmpSlotAddRequested;
    /// A destination was chosen from strip `idx`'s picker. MainComponent applies
    /// the matching engine main-out edit (tape / bus / hardware output).
    std::function<void (int idx, DestChoice dest)> onDestinationChosen;
    /// Blank-pane-area "Add tape" gesture fired (right-click / long-press on
    /// empty pane). MainComponent creates a pooled tape (T3 addTape). The pane
    /// owns no pool/mixer state — it only relays the intent.
    std::function<void()>                              onAddTape;
    /// Blank-pane-area "Add bus" / "Add FX return" gestures. MainComponent
    /// creates the engine node (bracketed) and rebuilds the bus-strip row.
    std::function<void()>                              onAddBus;
    std::function<void()>                              onAddFxReturn;

    /// A bus/FX-return strip's fader/mute/solo changed (busIdx = index into the
    /// pane's bus-strip row, parallel to MainComponent::busStripIds_).
    std::function<void (int busIdx, float gainLinear)> onBusGain;
    std::function<void (int busIdx, bool muted)>       onBusMute;
    std::function<void (int busIdx, bool soloed)>      onBusSolo;
    /// A bus/FX-return strip was clicked. MainComponent reads the bus's
    /// EffectChain + config snapshots and calls `showBusDetailFor` to populate
    /// the EQ + CMP tabs (Pan/Width + Sends are hidden for buses — stereo bus
    /// has no pan, and per-bus sends aren't modeled yet). Mirrors `onSelect`.
    std::function<void (int busIdx)>                   onBusSelect;
    /// EQ/CMP gestures for the currently-selected bus. Same shape as the
    /// channel-strip versions but addresses the bus's nodeKey (BusId.value())
    /// instead of a channel's. MainComponent routes through the same
    /// `effectChainHost_.setInternalEqConfigAt` / slot-append patterns.
    std::function<void (int busIdx, ida::EqConfig cfg)> onBusEqConfigChanged;
    std::function<void (int busIdx)>                    onBusEqSlotAddRequested;
    std::function<void (int busIdx, ida::CmpConfig cfg)> onBusCmpConfigChanged;
    std::function<void (int busIdx)>                    onBusCmpSlotAddRequested;
    /// Bus-side Pan/Width + Sends gestures (slice EC-Polish-fix-2). Same
    /// shape as their channel counterparts but indexed by bus row.
    /// `pan` is industry-standard [-1, +1]; `width` is [0, 2]; `level`
    /// linear 0..1. MainComponent routes pan/width through Bus::setPan /
    /// Bus::setWidth and sends through InputMixer::setBusSend.
    std::function<void (int busIdx, float pan)>                        onBusPan;
    std::function<void (int busIdx, float width)>                      onBusWidth;
    std::function<void (int busIdx, int fxReturnIdx, float level)>     onBusSendChanged;
    /// A destination was chosen from bus strip `busIdx`'s picker. MainComponent
    /// applies the matching bus main-out edit (tape / plain bus / hardware out).
    std::function<void (int busIdx, DestChoice dest)>  onBusDestinationChosen;
    /// The INS (insert chain) button on input strip `idx` was clicked. MainComponent
    /// opens the per-strip InsertChainPopup anchored to the button (P7 T5 slice 5).
    std::function<void (int idx)>                      onInputInsertChainClicked;
    /// 2026-05-24 monitor slice. Operator changed the Monitor button state on
    /// input strip `idx`. `mode` is the new MonitorMode (Off/On).
    /// MainComponent applies it via `InputMixer::setChannelMonitorMode` inside
    /// the audio-callback-detached bracket (same pattern as `onDestinationChosen`).
    std::function<void (int idx, ida::MonitorMode mode)> onMonitorModeChanged;
    /// The INS button on bus strip `busIdx` was clicked. Same shape as the channel
    /// callback; MainComponent reads the bus's EffectChain instead of a channel's.
    std::function<void (int busIdx)>                   onBusInsertChainClicked;
    /// Rename committed for bus/FX-return strip `busIdx` via the StripContextOverlay's
    /// inline editor. MainComponent applies it via `InputMixer::renameBus`. Mirrors
    /// `OutputMixerPane::onBusRename` (slice 4 parity).
    std::function<void (int busIdx, juce::String newName)> onBusRename;

    /// Populates the detail panel with `idx`'s current values and reveals it.
    /// `panMinus1to1` is the knob-domain pan ([-1, +1]); `width` is [0, 2].
    /// `fxReturns` is the operator's current FX-return roster; `sendLevels`
    /// is parallel (same order). `preFader` is the per-channel pre-fader-sends
    /// flag (E2). The pane is mixer-agnostic — MainComponent collects and
    /// passes these in.
    void showDetailFor (int idx, float panMinus1to1, float width,
                        std::vector<ida::ui::FxReturnInfo> fxReturns,
                        std::vector<float> sendLevels,
                        bool preFader,
                        ida::EqConfig eqConfig, bool hasEqSlot,
                        ida::CmpConfig cmpConfig, bool hasCmpSlot)
    {
        if (idx < 0 || idx >= stripCount()) return;
        detailPanel_.setChannel (idx, otto::ui::ChannelType::Instrument);
        detailPanel_.panWidTab().setPan (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   preFader });
        detailPanel_.eqTab().setChannelState ({ eqConfig, hasEqSlot });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        // Channel strips: all four tabs available (default after construction,
        // but a previous bus selection may have hidden Pan/Width + Sends).
        detailPanel_.setTabsAvailable ({ true, true, true, true });
        detailPanel_.setVisible (true);
        // Grab focus so Escape (the deselect gesture) reaches this pane
        // even from a freshly opened detail panel.
        grabKeyboardFocus();
        resized();
    }

    /// Bus-side detail surface — all four tabs functional. Buses get
    /// pan/width via Bus engine atomics; bus-to-bus sends via
    /// InputMixer::setBusSend. FX returns get pan/width + EQ/CMP; their
    /// Sends tab is hidden ({true,false,true,true}) because FX returns
    /// receive — they don't send. (Edit-FX swap on the Sends tab for
    /// FX returns lands in the next slice.)
    void showBusDetailFor (int busIdx,
                           float panMinus1to1, float width,
                           std::vector<ida::ui::FxReturnInfo> fxReturns,
                           std::vector<float>                 sendLevels,
                           ida::EqConfig  eqConfig,  bool hasEqSlot,
                           ida::CmpConfig cmpConfig, bool hasCmpSlot,
                           bool isFxReturn)
    {
        if (busIdx < 0 || busIdx >= busStripCount()) return;
        detailPanel_.setChannel (busIdx, isFxReturn
                                              ? otto::ui::ChannelType::FXReturn
                                              : otto::ui::ChannelType::Bus);
        detailPanel_.panWidTab().setPan   (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   /*preFader=*/ false });
        detailPanel_.eqTab().setChannelState  ({ eqConfig,  hasEqSlot  });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        detailPanel_.setTabsAvailable (isFxReturn
                                        ? ida::ui::ChannelDetail::TabMask { true, false, true, true }
                                        : ida::ui::ChannelDetail::TabMask { true, true,  true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }

    /// Rebuilds the row from `infos` (one CompactFaderStrip each). The pane
    /// listens for each strip's right-click via addMouseListener so the
    /// vendored CompactFaderStrip stays unmodified.
    void setStrips (const std::vector<StripInfo>& infos)
    {
        strips_.clear();
        stripStereo_.clear();
        destButtons_.clear();
        inputStripInsButtons_.clear();
        monitorButtons_.clear();
        monitorModes_.clear();
        stripDests_.clear();
        // Channel identities change on a rebuild, so the prior channel
        // selection no longer maps to a strip — drop it. The detail panel
        // hides only if no bus selection is keeping it bound (bus identities
        // live in a separate row that survives a channel rebuild).
        selectedStrip_ = -1;
        if (selectedBus_ < 0)
            detailPanel_.setVisible (false);
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, otto::ui::ChannelType::Instrument);
            strip->setChannelName (infos[static_cast<std::size_t> (i)].name);
            strip->setOutputComboVisible (false);   // CompactFaderStrip's combo is OTTO's
                                                     // hardware-pair model; the tape picker
                                                     // below is InputMixerPane's own control
            strip->addListener (this);
            strip->addMouseListener (this, /*wantsNestedEvents*/ true);
            addAndMakeVisible (*strip);
            strips_.push_back (std::move (strip));
            stripStereo_.push_back (infos[static_cast<std::size_t> (i)].stereo);

            // The per-strip tape destination picker — InputMixerPane's own
            // control beneath the strip. Opens a tapes-only PopupMenu on click.
            auto button = std::make_unique<juce::TextButton>();
            button->setButtonText ("—");
            const int idx = i;
            button->onClick = [this, idx] { showDestinationMenu (idx); };
            addAndMakeVisible (*button);
            destButtons_.push_back (std::move (button));
            stripDests_.push_back ({});

            // INS button — opens the per-strip InsertChainPopup (P7 T5 slice 5).
            // Sits above the destination picker so both fit a narrow strip.
            auto ins = std::make_unique<juce::TextButton>();
            ins->setButtonText ("INS");
            ins->onClick = [this, idx]
            {
                if (onInputInsertChainClicked) onInputInsertChainClicked (idx);
            };
            addAndMakeVisible (*ins);
            inputStripInsButtons_.push_back (std::move (ins));

            // V9 Slice 5 — MON button. Two-state toggle per whitepaper §7.2
            // (Off ↔ On). One click flips. Default Off — operator opts in
            // explicitly. (Right-click + long-press are reserved for other
            // per-strip gestures landing in later slices.)
            auto mon = std::make_unique<juce::TextButton>();
            mon->setButtonText ("Off");
            mon->setTooltip ("MON off — you do not hear this input through IDA. "
                             "Click to enable monitoring.");
            mon->onClick = [this, idx] { toggleMonitorModeAt (idx); };
            addAndMakeVisible (*mon);
            monitorButtons_.push_back (std::move (mon));
            monitorModes_.push_back (ida::MonitorMode::Off);
        }
        rebuildChannelPills();
        resized();

        // First non-empty strip set after launch → default-select strip 0 so
        // the operator sees a populated detail panel without having to click.
        // Operator intent (Escape, or a manual selection elsewhere) wins on
        // subsequent rebuilds.
        if (! defaultSelectionDone_ && ! infos.empty() && onSelect)
        {
            defaultSelectionDone_ = true;
            onSelect (0);
        }
    }

    /// Pushes the destination state: the shared `choices` list (stored once for
    /// the popup; tapes then buses then direct-out) plus a per-strip `perStrip`
    /// entry parallel to the strips set by setStrips. `perStrip` length should
    /// equal stripCount(); the guard is a production fallback against a stale
    /// push, the jassert catches a real desync.
    void setDestinations (const std::vector<DestChoice>& choices,
                          const std::vector<StripDest>& perStrip)
    {
        choices_ = choices;
        jassert (static_cast<int> (perStrip.size()) == stripCount());
        for (int i = 0; i < stripCount() && i < static_cast<int> (perStrip.size()); ++i)
        {
            stripDests_[static_cast<std::size_t> (i)] = perStrip[static_cast<std::size_t> (i)];
            const auto& label = perStrip[static_cast<std::size_t> (i)].currentName;
            destButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }

    /// 2026-05-24 monitor slice — refresh the per-strip Monitor button state.
    /// Length must equal stripCount() (jassert; production-fallback guards
    /// just take the prefix that fits). Called from MainComponent on engine
    /// state changes (e.g. after a project load) so the buttons reflect the
    /// channel's actual MonitorMode.
    void setMonitorModes (const std::vector<ida::MonitorMode>& modes)
    {
        jassert (static_cast<int> (modes.size()) == stripCount());
        for (int i = 0; i < stripCount() && i < static_cast<int> (modes.size()); ++i)
            setMonitorModeAt (i, modes[static_cast<std::size_t> (i)], /*notify*/ false);
    }

    /// Pushes the bus-row destination state. Unlike the channel picker, each bus
    /// has its OWN choice list (`perBusChoices[i]`) — cycle-excluded targets
    /// differ per bus — alongside its current destination (`perBus[i]`). Both are
    /// parallel to the strips set by setBusStrips.
    void setBusDestinations (const std::vector<std::vector<DestChoice>>& perBusChoices,
                             const std::vector<StripDest>& perBus)
    {
        busChoices_ = perBusChoices;
        jassert (static_cast<int> (perBus.size()) == busStripCount());
        for (int i = 0; i < busStripCount() && i < static_cast<int> (perBus.size()); ++i)
        {
            busStripDests_[static_cast<std::size_t> (i)] = perBus[static_cast<std::size_t> (i)];
            const auto& label = perBus[static_cast<std::size_t> (i)].currentName;
            busDestButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }

    /// Rebuilds the bus/FX-return strip row from `infos`. Each becomes a
    /// CompactFaderStrip typed Bus or FXReturn. Unlike channel strips these are
    /// NOT given addMouseListener (no pane gesture applies) — only addListener
    /// for fader/mute/solo. Each gets its own destination picker button beneath
    /// it (a bus/FX-return output routes to a tape, a plain bus, or direct out).
    void setBusStrips (const std::vector<BusInfo>& infos)
    {
        busStrips_.clear();
        busDestButtons_.clear();
        busStripInsButtons_.clear();
        busNameOverlays_.clear();
        busStripDests_.clear();
        busChoices_.clear();
        // Bus identities change on a rebuild, so a prior bus selection no
        // longer maps to a strip — drop it and hide the detail panel iff the
        // bus was what was selected. A channel-strip selection is independent
        // and survives a bus-row rebuild (different identity space).
        if (selectedBus_ >= 0)
        {
            selectedBus_ = -1;
            detailPanel_.setVisible (false);
        }
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            const auto& info = infos[static_cast<std::size_t> (i)];
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, info.isFxReturn ? otto::ui::ChannelType::FXReturn
                                   : otto::ui::ChannelType::Bus);
            strip->setChannelName (info.name);
            strip->setOutputComboVisible (false);   // pane owns routing, not the combo
            strip->addListener (this);               // fader/mute/solo only
            addAndMakeVisible (*strip);
            busStrips_.push_back (std::move (strip));

            auto button = std::make_unique<juce::TextButton>();
            button->setButtonText ("—");
            const int idx = i;
            button->onClick = [this, idx] { showBusDestinationMenu (idx); };
            addAndMakeVisible (*button);
            busDestButtons_.push_back (std::move (button));
            busStripDests_.push_back ({});

            // INS button — same pattern as channel strips (P7 T5 slice 5).
            auto ins = std::make_unique<juce::TextButton>();
            ins->setButtonText ("INS");
            ins->onClick = [this, idx]
            {
                if (onBusInsertChainClicked) onBusInsertChainClicked (idx);
            };
            addAndMakeVisible (*ins);
            busStripInsButtons_.push_back (std::move (ins));

            // Strip context overlay — invisible click-catcher on the bus/FX-return
            // strip's top name band. Right-click (desktop) + 500 ms long-press (iOS)
            // → "Rename…" → inline TextEditor. Mirrors OutputMixerPane slice 3.
            // Slice EC-Polish-fix: a short left-tap routes to the bus-select
            // path (the overlay was swallowing the operator's most natural
            // click target — the name band — so bus / FX strips appeared
            // unselectable).
            auto overlay = std::make_unique<ida::app::StripContextOverlay> (
                idx,
                [this] (int who) { showBusContextMenu (who); },
                [this] (int who, juce::String s)
                {
                    if (onBusRename) onBusRename (who, std::move (s));
                },
                [this] (int who)
                {
                    // Mirror what stripChannelSelected does for Bus / FXReturn,
                    // without depending on which CompactFaderStrip subarea was
                    // tapped (the overlay covers the name band — the bus's
                    // own listener can't see this click).
                    const auto type = busStrips_[static_cast<std::size_t> (who)]
                                          ->getChannelType();
                    stripChannelSelected (who, type);
                });
            addAndMakeVisible (*overlay);
            busNameOverlays_.push_back (std::move (overlay));
        }
        rebuildChannelPills();
        resized();
    }

    /// Updates a single bus/FX-return strip's visible name without rebuilding
    /// the row (preserves fader / mute / meter state on rename). Mirrors
    /// OutputMixerPane::updateBusName.
    void updateBusName (int busIdx, const juce::String& newName)
    {
        if (busIdx < 0 || busIdx >= busStripCount()) return;
        busStrips_[static_cast<std::size_t> (busIdx)]->setChannelName (newName);
    }

    [[nodiscard]] int busStripCount() const noexcept
    {
        return static_cast<int> (busStrips_.size());
    }

    void setBusStripLevelDb (int busIdx, float dbL, float dbR)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLevel (dbL, dbR);
    }

    void setBusStripLufs (int busIdx, float lufs)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLUFSLevel (lufs);
    }

    /// RME split/collapse affordance — gesture-only so the strip face stays
    /// uncluttered (it's tight on iPhone): **right-click** on desktop and
    /// **long-press** on touch both open the same menu. A drag past the
    /// threshold cancels the long-press so it never fights the fader.
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = stripIndexOf (e.eventComponent);

        if (idx < 0)                          // empty pane area — the "Add tape" surface
        {
            if (e.mods.isPopupMenu())         // desktop right-click — fire now
            {
                showBlankAreaMenu (e.getScreenPosition());
                return;
            }
            // Touch / left press — arm the long-press with the blank sentinel.
            longPressBlank_     = true;
            longPressIdx_       = -1;
            longPressScreenPos_ = e.getScreenPosition();
            startTimer (kLongPressMs);
            return;
        }

        longPressBlank_ = false;             // a strip gesture — clear any blank intent
        if (e.mods.isPopupMenu())            // desktop right-click — fire now
        {
            showToggleMenu (idx, e.getScreenPosition());
            return;
        }
        // Touch / left press — arm the long-press; a drag or release cancels it.
        longPressIdx_      = idx;
        longPressScreenPos_ = e.getScreenPosition();
        startTimer (kLongPressMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isTimerRunning() && e.getDistanceFromDragStart() > kLongPressMoveTolerancePx)
        {
            stopTimer();                     // it's a fader drag, not a long-press
            longPressBlank_ = false;
        }
    }

    void mouseUp (const juce::MouseEvent&) override { stopTimer(); longPressBlank_ = false; }

    [[nodiscard]] int stripCount() const noexcept { return static_cast<int> (strips_.size()); }

    void setStripLevelDb (int idx, float dbL, float dbR)
    {
        if (idx >= 0 && idx < stripCount()) strips_[static_cast<std::size_t> (idx)]->setLevel (dbL, dbR);
    }

    void setStripLufs (int idx, float lufs)
    {
        if (idx >= 0 && idx < stripCount()) strips_[static_cast<std::size_t> (idx)]->setLUFSLevel (lufs);
    }

    void setEffectiveMute (int idx, bool effectivelyMuted)
    {
        if (idx >= 0 && idx < stripCount())
            strips_[static_cast<std::size_t> (idx)]->setEffectivelyMuted (effectivelyMuted);
    }

    /// Slice EC-Polish-fix: build the bottom-of-screen channel selector
    /// pill row that mirrors OTTO's iPad EQ-tab kit-piece selector. Pills
    /// show channel-strip names first, then bus / FX-return names.
    /// Visible only in full-screen detail mode where the operator can't
    /// see the normal strip row.
    void rebuildChannelPills()
    {
        channelPills_.clear();
        auto makePill = [this] (juce::String name, int idx, otto::ui::ChannelType type)
        {
            auto pill = std::make_unique<juce::TextButton> (name);
            pill->setClickingTogglesState (true);
            pill->setRadioGroupId (3);
            pill->setColour (juce::TextButton::buttonColourId,   otto::Colours::bg3);
            pill->setColour (juce::TextButton::buttonOnColourId, otto::Colours::accent);
            pill->setColour (juce::TextButton::textColourOffId,  otto::Colours::textSecondary);
            pill->setColour (juce::TextButton::textColourOnId,   otto::Colours::textPrimary);
            pill->onClick = [this, idx, type] { stripChannelSelected (idx, type); };
            addChildComponent (*pill);
            channelPills_.push_back (std::move (pill));
        };
        for (int i = 0; i < stripCount(); ++i)
            makePill (strips_[static_cast<std::size_t> (i)]->getChannelName(),
                      i, otto::ui::ChannelType::Instrument);
        for (int i = 0; i < busStripCount(); ++i)
            makePill (busStrips_[static_cast<std::size_t> (i)]->getChannelName(),
                      i, busStrips_[static_cast<std::size_t> (i)]->getChannelType());
    }

    /// Full-screen detail trigger: when EQ or CMP is the active tab AND
    /// the slot is actually wired, the graphical curve / meter needs the
    /// whole pane. Empty-slot (showing "+ Add EQ" / "+ Add CMP") stays at
    /// the small detail band so the operator can still see the strip row
    /// and click another strip to escape. PanWid + Sends never full-screen.
    bool isDetailFullScreen() const noexcept
    {
        if (! detailPanel_.isVisible()) return false;
        const auto t = detailPanel_.getActiveTab();
        if (t == ida::ui::ChannelDetail::Tab::EQ)
            return detailPanel_.eqTab().hasEqSlot();
        if (t == ida::ui::ChannelDetail::Tab::CMP)
            return detailPanel_.cmpTab().hasCmpSlot();
        return false;
    }

    void resized() override
    {
        constexpr int kGap          = 6;
        constexpr int kGroupDividerW = kGap * 3;   // visual gap between channel + bus strip groups
        auto area = getLocalBounds().reduced (kGap);

        // Full-screen detail (EQ / CMP active): the panel takes the bulk
        // of the pane; strips + INS + picker rows hide. A bottom channel-pill
        // row mirrors OTTO's iPad EQ-tab kit-piece selector so the operator
        // can switch channels without leaving full-screen.
        constexpr int kPillRowHeight = 36;
        if (isDetailFullScreen())
        {
            for (auto& s : strips_)              s->setVisible (false);
            for (auto& b : destButtons_)         b->setVisible (false);
            for (auto& b : inputStripInsButtons_) b->setVisible (false);
            for (auto& b : monitorButtons_)      b->setVisible (false);
            for (auto& s : busStrips_)           s->setVisible (false);
            for (auto& b : busDestButtons_)      b->setVisible (false);
            for (auto& b : busStripInsButtons_)  b->setVisible (false);
            for (auto& o : busNameOverlays_)     o->setVisible (false);

            // Channel pill row at the bottom of the pane — slice EC-Polish-fix.
            // Pills are radio-grouped; the currently-selected channel pill is
            // toggled on so the operator sees which channel they're editing.
            auto pillRow = area.removeFromBottom (kPillRowHeight);
            area.removeFromBottom (kGap);
            detailPanel_.setBounds (area);

            // Back button overlays the top-right corner of the panel. Same
            // gesture as Escape; load-bearing on touch devices that have no
            // keyboard.
            if (backButton_)
            {
                constexpr int kBackW = 72;
                constexpr int kBackH = 28;
                constexpr int kBackMargin = 6;
                backButton_->setBounds (area.getRight() - kBackW - kBackMargin,
                                        area.getY()     + kBackMargin,
                                        kBackW, kBackH);
                backButton_->setVisible (true);
                backButton_->toFront (false);
            }

            const int n = static_cast<int> (channelPills_.size());
            if (n > 0)
            {
                const int totalPillGap = 4 * (n - 1);
                const int pillW = juce::jmax (40,
                                              (pillRow.getWidth() - totalPillGap) / n);
                int px = pillRow.getX();
                for (int i = 0; i < n; ++i)
                {
                    if (! channelPills_[static_cast<std::size_t> (i)]) continue;
                    auto& pill = *channelPills_[static_cast<std::size_t> (i)];
                    pill.setBounds (px, pillRow.getY(), pillW, pillRow.getHeight());
                    pill.setVisible (true);
                    // Sync toggle state: pill `i` lights up when it points at
                    // the currently-bound channel or bus.
                    const bool isChannelPill = (i < stripCount());
                    const bool selected = isChannelPill
                                            ? (i == selectedStrip_)
                                            : ((i - stripCount()) == selectedBus_);
                    pill.setToggleState (selected, juce::dontSendNotification);
                    px += pillW + 4;
                }
            }
            return;
        }

        // Restore visibility (a previous tab change may have hidden them).
        for (auto& s : strips_)              s->setVisible (true);
        for (auto& b : destButtons_)         b->setVisible (true);
        for (auto& b : inputStripInsButtons_) b->setVisible (true);
        for (auto& b : monitorButtons_)      b->setVisible (true);
        for (auto& s : busStrips_)           s->setVisible (true);
        for (auto& b : busDestButtons_)      b->setVisible (true);
        for (auto& b : busStripInsButtons_)  b->setVisible (true);
        for (auto& o : busNameOverlays_)     o->setVisible (true);
        for (auto& p : channelPills_)        if (p) p->setVisible (false);
        if (backButton_) backButton_->setVisible (false);

        // The detail panel (when a strip is selected) takes a fixed band across
        // the top; the strip row fills the remainder below it.
        if (detailPanel_.isVisible())
        {
            detailPanel_.setBounds (area.removeFromTop (kDetailHeight));
            area.removeFromTop (kGap);
        }

        // Three fixed bands at the bottom: destination picker (lowest),
        // INS button (middle), Monitor button (top — 2026-05-24 monitor
        // slice). Stacking vertically keeps each per-strip control the full
        // strip width — narrow iPhone strips can't fit multiple buttons
        // side-by-side (risk #2 in the T5 plan).
        auto pickerRow  = area.removeFromBottom (kDestHeight);
        area.removeFromBottom (kGap);
        auto insRow     = area.removeFromBottom (kInsHeight);
        area.removeFromBottom (kGap);
        auto monitorRow = area.removeFromBottom (kMonitorHeight);
        area.removeFromBottom (kGap);

        // Left-to-right row of fixed-width strips. A few strips fit a typical
        // window; wide input counts get a horizontal Viewport in a later slice.
        constexpr int kStripW = otto::ui::CompactFaderStrip::kStripWidth;
        for (int i = 0; i < stripCount(); ++i)
        {
            strips_[static_cast<std::size_t> (i)]->setBounds (area.removeFromLeft (kStripW));
            monitorButtons_[static_cast<std::size_t> (i)]->setBounds (monitorRow.removeFromLeft (kStripW));
            inputStripInsButtons_[static_cast<std::size_t> (i)]->setBounds (insRow.removeFromLeft (kStripW));
            destButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromLeft (kStripW));
            area.removeFromLeft (kGap);
            monitorRow.removeFromLeft (kGap);
            insRow.removeFromLeft (kGap);
            pickerRow.removeFromLeft (kGap);
        }

        // Bus / FX-return strips sit to the right of the channel strips, after a
        // wider divider gap. Each gets a picker button in the same bottom band as
        // the channel pickers (pickerRow has tracked the column cursor in lock-step).
        // Buses have no Monitor button — the per-channel direct-layer decision
        // lives on input channels per whitepaper §7.1 ("A channel can ... feed
        // direct ..."), not on intermediate subgroups.
        if (busStripCount() > 0)
        {
            area.removeFromLeft (kGroupDividerW);       // visual divider between the two groups
            monitorRow.removeFromLeft (kGroupDividerW); // keep the monitor band column-aligned
            insRow.removeFromLeft (kGroupDividerW);     // keep the INS band column-aligned
            pickerRow.removeFromLeft (kGroupDividerW);  // keep the picker band column-aligned
        }
        for (int i = 0; i < busStripCount(); ++i)
        {
            auto stripBounds = area.removeFromLeft (kStripW);
            busStrips_[static_cast<std::size_t> (i)]->setBounds (stripBounds);
            // Overlay covers the top name-label band so right-click / long-press
            // there triggers the per-bus context menu (parity with OutputMixerPane).
            if (i < static_cast<int> (busNameOverlays_.size()))
                busNameOverlays_[static_cast<std::size_t> (i)]->setBounds (
                    stripBounds.withHeight (kNameOverlayHeight));
            busStripInsButtons_[static_cast<std::size_t> (i)]->setBounds (insRow.removeFromLeft (kStripW));
            busDestButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromLeft (kStripW));
            area.removeFromLeft (kGap);
            monitorRow.removeFromLeft (kGap);
            insRow.removeFromLeft (kGap);
            pickerRow.removeFromLeft (kGap);
        }
    }

    /// Screen bounds of the INS button on input strip `idx`, used by
    /// MainComponent to anchor the InsertChainPopup CallOutBox. Returns an
    /// empty rect if `idx` is out of range.
    juce::Rectangle<int> inputInsButtonScreenArea (int idx) const
    {
        if (idx < 0 || idx >= static_cast<int> (inputStripInsButtons_.size())) return {};
        return inputStripInsButtons_[static_cast<std::size_t> (idx)]->getScreenBounds();
    }

    /// Screen bounds of the INS button on bus strip `busIdx`. Empty rect if
    /// out of range.
    juce::Rectangle<int> busInsButtonScreenArea (int busIdx) const
    {
        if (busIdx < 0 || busIdx >= static_cast<int> (busStripInsButtons_.size())) return {};
        return busStripInsButtons_[static_cast<std::size_t> (busIdx)]->getScreenBounds();
    }

    void paint (juce::Graphics& g) override { g.fillAll (otto::Colours::bg2); }

    // --- CompactFaderStripListener ---
    void stripGainChanged (int idx, otto::ui::ChannelType type, float gain) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusGain) onBusGain (idx, gain); }
        else if (onGain) onGain (idx, gain);
    }
    void stripMuteChanged (int idx, otto::ui::ChannelType type, bool muted) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusMute) onBusMute (idx, muted); }
        else if (onMute) onMute (idx, muted);
    }
    void stripSoloChanged (int idx, otto::ui::ChannelType type, bool soloed) override
    {
        if (type == otto::ui::ChannelType::Bus || type == otto::ui::ChannelType::FXReturn)
        {   if (onBusSolo) onBusSolo (idx, soloed); }
        else if (onSolo) onSolo (idx, soloed);
    }
    void stripChannelSelected (int idx, otto::ui::ChannelType type) override
    {
        const bool isBusRow = (type == otto::ui::ChannelType::Bus
                            || type == otto::ui::ChannelType::FXReturn);

        // Channel + bus selections are mutually exclusive — the detail panel
        // binds to exactly one strip at a time. Clearing the OTHER row's
        // highlights keeps the affordance honest.
        if (isBusRow)
        {
            for (int i = 0; i < stripCount(); ++i)
                strips_[static_cast<std::size_t> (i)]->setSelected (false);
            for (int i = 0; i < busStripCount(); ++i)
                busStrips_[static_cast<std::size_t> (i)]->setSelected (i == idx);
            selectedStrip_ = -1;
            selectedBus_   = idx;
            if (onBusSelect) onBusSelect (idx);
        }
        else
        {
            for (int i = 0; i < stripCount(); ++i)
                strips_[static_cast<std::size_t> (i)]->setSelected (i == idx);
            for (int i = 0; i < busStripCount(); ++i)
                busStrips_[static_cast<std::size_t> (i)]->setSelected (false);
            selectedStrip_ = idx;
            selectedBus_   = -1;
            if (onSelect) onSelect (idx);   // MainComponent loads + reveals the panel
        }
    }

    // --- ChannelDetailPanWidTabListener (the pan/width knobs) ---
    void panWidTabPanChanged (int, otto::ui::ChannelType, float pan) override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusPan) onBusPan (selectedBus_, pan);
            return;
        }
        if (onPan && selectedStrip_ >= 0) onPan (selectedStrip_, pan);
    }
    void panWidTabWidthChanged (int, otto::ui::ChannelType, float width) override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusWidth) onBusWidth (selectedBus_, width);
            return;
        }
        if (onWidth && selectedStrip_ >= 0) onWidth (selectedStrip_, width);
    }

    // --- ChannelDetailSendsTabListener (slice U) ---
    void sendsTabSendChanged (int fxReturnIdx, float level) override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusSendChanged) onBusSendChanged (selectedBus_, fxReturnIdx, level);
            return;
        }
        if (onSendChanged && selectedStrip_ >= 0)
            onSendChanged (selectedStrip_, fxReturnIdx, level);
    }
    void sendsTabPreFaderToggled (bool preFader) override
    {
        // Pre-fader sends flag is per-channel only — buses use a fixed
        // post-fader send path. Ignore the toggle for a bus selection.
        if (selectedBus_ >= 0) return;
        if (onPreFaderToggled && selectedStrip_ >= 0)
            onPreFaderToggled (selectedStrip_, preFader);
    }

    // --- ChannelDetailEQTabListener (slice EC) ---
    // Route to bus-side callbacks when a bus is selected, channel-side
    // otherwise. selectedBus_ + selectedStrip_ are mutually exclusive.
    void eqTabConfigChanged (const ida::EqConfig& cfg) override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusEqConfigChanged) onBusEqConfigChanged (selectedBus_, cfg);
        }
        else if (onEqConfigChanged && selectedStrip_ >= 0)
            onEqConfigChanged (selectedStrip_, cfg);
    }
    void eqTabRequestSlotAdd() override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusEqSlotAddRequested) onBusEqSlotAddRequested (selectedBus_);
        }
        else if (onEqSlotAddRequested && selectedStrip_ >= 0)
            onEqSlotAddRequested (selectedStrip_);
    }

    // --- ChannelDetailCMPTabListener (slice EC) ---
    void cmpTabConfigChanged (const ida::CmpConfig& cfg) override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusCmpConfigChanged) onBusCmpConfigChanged (selectedBus_, cfg);
        }
        else if (onCmpConfigChanged && selectedStrip_ >= 0)
            onCmpConfigChanged (selectedStrip_, cfg);
    }
    void cmpTabRequestSlotAdd() override
    {
        if (selectedBus_ >= 0)
        {
            if (onBusCmpSlotAddRequested) onBusCmpSlotAddRequested (selectedBus_);
        }
        else if (onCmpSlotAddRequested && selectedStrip_ >= 0)
            onCmpSlotAddRequested (selectedStrip_);
    }

private:
    static constexpr int kLongPressMs              = 500;
    static constexpr int kLongPressMoveTolerancePx = 8;
    static constexpr int kDetailHeight             = 180;
    static constexpr int kDestHeight               = 26;
    static constexpr int kInsHeight                = 26;
    static constexpr int kMonitorHeight            = 22;   // 2026-05-24 monitor slice — third bottom band
    static constexpr int kNameOverlayHeight        = 22;   // strip name band height

    void timerCallback() override
    {
        stopTimer();                         // one-shot
        if (longPressBlank_)
        {
            longPressBlank_ = false;
            showBlankAreaMenu (longPressScreenPos_);
            return;
        }
        if (longPressIdx_ >= 0 && longPressIdx_ < stripCount())
            showToggleMenu (longPressIdx_, longPressScreenPos_);
    }

    /// Builds + shows the Split/Collapse menu for strip `idx` at `screenPos`
    /// (a screen-point target works for both mouse and touch).
    void showToggleMenu (int idx, juce::Point<int> screenPos)
    {
        if (idx < 0 || idx >= stripCount()) return;
        juce::PopupMenu menu;
        const bool stereo = stripStereo_[static_cast<std::size_t> (idx)];
        menu.addItem (stereo ? "Split to two mono channels" : "Collapse to stereo",
                      [this, idx] { if (onToggleStereoMono) onToggleStereoMono (idx); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
    }

    /// Builds + shows the blank-area menu (right-click / long-press on empty
    /// pane). Three items: "Add bus", "Add FX return", and "Add tape" — each
    /// relays out via its matching on* callback. MainComponent handles creation.
    /// `screenPos` targets both mouse and touch.
    void showBlankAreaMenu (juce::Point<int> screenPos)
    {
        juce::PopupMenu menu;
        menu.addItem ("Add bus",       [this] { if (onAddBus)       onAddBus(); });
        menu.addItem ("Add FX return", [this] { if (onAddFxReturn) onAddFxReturn(); });
        menu.addItem ("Add tape",      [this] { if (onAddTape)      onAddTape(); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
    }

    /// Builds + shows strip `idx`'s destination menu from its stored choices
    /// (pooled tapes, buses/FX returns, and the direct hardware-output entry).
    /// Selecting fires onDestinationChosen with the chosen DestChoice.
    void showDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= stripCount()) return;
        if (choices_.empty()) return;
        const auto& cur = stripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices_)
        {
            const bool ticked = choice.kind == cur.currentKind && choice.id == cur.currentId;
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onDestinationChosen) onDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            destButtons_[static_cast<std::size_t> (idx)].get()));
    }

    /// Builds + shows bus strip `idx`'s destination menu from its OWN choice list
    /// (busChoices_[idx]; cycle-excluded targets differ per bus). Selecting fires
    /// onBusDestinationChosen with the chosen DestChoice.
    void showBusDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= busStripCount()) return;
        if (idx >= static_cast<int> (busChoices_.size())) return;
        const auto& choices = busChoices_[static_cast<std::size_t> (idx)];
        if (choices.empty()) return;
        const auto& cur = busStripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices)
        {
            const bool ticked = choice.kind == cur.currentKind && choice.id == cur.currentId;
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onBusDestinationChosen) onBusDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            busDestButtons_[static_cast<std::size_t> (idx)].get()));
    }

    /// Per-bus context menu — currently just "Rename…". Triggered by the
    /// StripContextOverlay's right-click or long-press gesture. Anchored to
    /// the overlay so the menu opens over the strip's name area. Mirrors
    /// OutputMixerPane::showBusContextMenu.
    void showBusContextMenu (int idx)
    {
        if (idx < 0 || idx >= busStripCount()) return;
        if (idx >= static_cast<int> (busNameOverlays_.size())) return;
        juce::PopupMenu menu;
        const int who = idx;
        menu.addItem ("Rename…", [this, who]
        {
            if (who >= 0 && who < static_cast<int> (busNameOverlays_.size()))
                busNameOverlays_[static_cast<std::size_t> (who)]->beginRename (
                    busStrips_[static_cast<std::size_t> (who)]->getChannelName());
        });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            busNameOverlays_[static_cast<std::size_t> (idx)].get()));
    }

    /// Maps a mouse event's source component back to a strip index (the event
    /// component may be a nested child of the strip), or -1 if it is none.
    [[nodiscard]] int stripIndexOf (juce::Component* c) const
    {
        for (int i = 0; i < stripCount(); ++i)
        {
            auto* s = strips_[static_cast<std::size_t> (i)].get();
            if (s == c || s->isParentOf (c)) return i;
        }
        return -1;
    }

    // Central monitor-mode setter. Updates the cached mode, syncs the
    // button label/tooltip, and (when `notify`) fires the engine-bound
    // callback. Called from the click handler and the message-thread
    // setMonitorModes refresh. V9 §7.2: two-state toggle (Off ↔ On).
    void setMonitorModeAt (int idx, ida::MonitorMode mode, bool notify)
    {
        if (idx < 0 || idx >= static_cast<int> (monitorModes_.size())) return;
        monitorModes_[static_cast<std::size_t> (idx)] = mode;
        auto& button = *monitorButtons_[static_cast<std::size_t> (idx)];
        const char* label   = nullptr;
        const char* tooltip = nullptr;
        switch (mode)
        {
            case ida::MonitorMode::Off:
                label   = "Off";
                tooltip = "MON off — you do not hear this input through IDA. "
                          "Click to enable monitoring.";
                break;
            case ida::MonitorMode::On:
                label   = "MON";
                tooltip = "MON on — you hear this input through IDA, "
                          "post-strip processing. Click to disable.";
                break;
        }
        button.setButtonText (label);
        button.setTooltip (tooltip);
        if (notify && onMonitorModeChanged) onMonitorModeChanged (idx, mode);
    }

    // V9 Slice 5 — MON is a two-state toggle (Off ↔ On). The single click
    // is the only gesture; the V8 tri-state cycle (Off → Processed → Raw)
    // is gone.
    void toggleMonitorModeAt (int idx)
    {
        if (idx < 0 || idx >= static_cast<int> (monitorModes_.size())) return;
        const auto cur  = monitorModes_[static_cast<std::size_t> (idx)];
        const auto next = (cur == ida::MonitorMode::Off) ? ida::MonitorMode::On
                                                         : ida::MonitorMode::Off;
        setMonitorModeAt (idx, next, /*notify*/ true);
    }


    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>> strips_;
    std::vector<std::unique_ptr<juce::TextButton>>            destButtons_;
    /// Per-input-strip INS buttons (P7 T5 slice 5). Parallel to strips_.
    std::vector<std::unique_ptr<juce::TextButton>>            inputStripInsButtons_;
    /// V9 Slice 5 — per-input-strip MON button. Parallel to strips_. Click
    /// toggles Off ↔ On (whitepaper §7.2 — two-state).
    std::vector<std::unique_ptr<juce::TextButton>>            monitorButtons_;
    std::vector<ida::MonitorMode>                             monitorModes_;
    std::vector<StripDest>                                    stripDests_;
    std::vector<DestChoice>                                   choices_;   // shared, stored once
    std::vector<bool>                                         stripStereo_;
    ida::ui::ChannelDetail                                    detailPanel_;
    int                                                       selectedStrip_ { -1 };
    int                                                       selectedBus_   { -1 };
    std::vector<std::unique_ptr<juce::TextButton>>            channelPills_;
    /// Back button — only visible in EQ/CMP full-screen mode. Clicking it
    /// calls deselectAll() (same as Escape key) so the operator can exit
    /// full-screen without needing the keyboard.
    std::unique_ptr<juce::TextButton>                         backButton_;
    /// True once the pane has fired its initial default-selection
    /// (`onSelect(0)`) after the first non-empty setStrips. Subsequent
    /// rebuilds don't re-trigger — operator intent wins.
    bool                                                      defaultSelectionDone_ { false };
    int                                                       longPressIdx_ { -1 };
    bool                                                      longPressBlank_ { false };
    juce::Point<int>                                          longPressScreenPos_;

    /// Bus + FX-return strips — a SECOND row to the right of the channel strips.
    /// Kept separate so channel-strip gestures/pickers/selection stay untouched.
    /// These get fader/mute/solo + meter + their own destination picker (no detail
    /// panel); their index space is independent of the channel strips.
    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>> busStrips_;
    std::vector<std::unique_ptr<juce::TextButton>>            busDestButtons_;
    /// Per-bus-strip INS buttons (P7 T5 slice 5). Parallel to busStrips_.
    std::vector<std::unique_ptr<juce::TextButton>>            busStripInsButtons_;
    /// Per-bus-strip rename overlays (slice 4). Parallel to busStrips_.
    std::vector<std::unique_ptr<ida::app::StripContextOverlay>> busNameOverlays_;
    std::vector<StripDest>                                    busStripDests_;
    /// Per-bus choice lists (cycle-excluded targets differ per bus, so unlike the
    /// channel picker this is NOT one shared list).
    std::vector<std::vector<DestChoice>>                      busChoices_;
};

// =============================================================================
// OutputMixerPane — the Output Mixer tab (white paper §5.2/§6.6/§7.1, the
// mixdown console). Slice 2: the master bus strip (always present, rightmost
// per pro mixing-console convention) plus an aux-bus row to its LEFT — each
// aux strip with a fader, mute, INS button, and destination picker (route to
// master, to another aux bus, or direct to hardware out, bypassing master).
// Blank-area right-click / long-press → "Add bus". Slice 3+ will bring
// phrase channels to the leftmost group once the M6+ rendering surface
// lands. Per project_two_mixers_totally_separate this shares NO state with
// InputMixerPane; reuses generic CompactFaderStrip + InsertChainPopup TYPES
// per-instance, not a shared base class. Listener disambiguation: master
// uses sentinel id kMasterStripId (-1); aux strips use 0..N-1.
// =============================================================================
class MainComponent::OutputMixerPane final : public juce::Component,
                                             public otto::ui::CompactFaderStripListener,
                                             public otto::ui::ChannelDetailPanWidTabListener,
                                             public ida::ui::ChannelDetailSendsTabListener,
                                             public ida::ui::ChannelDetailEQTabListener,
                                             public ida::ui::ChannelDetailCMPTabListener,
                                             public ida::ui::ChannelDetailListener,
                                             private juce::Timer
{
public:
    /// Sentinel strip id distinguishing master from aux buses in the shared
    /// CompactFaderStripListener callbacks. Aux strips use their 0-based
    /// row index; -1 cannot collide with that.
    static constexpr int kMasterStripId = -1;

    OutputMixerPane()
    {
        master_ = std::make_unique<otto::ui::CompactFaderStrip> (
            kMasterStripId, otto::ui::ChannelType::Bus);
        master_->setChannelName ("Master");
        master_->setOutputComboVisible (false);   // master is terminal — no destination picker
        master_->addListener (this);
        addAndMakeVisible (*master_);
        rebuildChannelPills();   // master pill exists from startup

        // Tabbed detail panel: same shape as InputMixerPane post slice EC.
        // After slice EC-Polish aux buses + master CAN select (EQ+CMP only)
        // so the panel binds to phrase, aux bus, or master interchangeably.
        // The pane also listens to ChannelDetail itself for tab-change events
        // so resized() flips into full-screen layout on EQ / CMP.
        detailPanel_.panWidTab().addListener (this);
        detailPanel_.sendsTab() .addListener (this);
        detailPanel_.eqTab()    .addListener (this);
        detailPanel_.cmpTab()   .addListener (this);
        detailPanel_.addListener (this);
        addChildComponent (detailPanel_);

        // Escape-to-deselect — required when the operator lands in
        // full-screen EQ / CMP and needs out.
        setWantsKeyboardFocus (true);

        // Touch-friendly back affordance for the same gesture — only
        // visible in EQ / CMP full-screen mode (resized() gates this).
        backButton_ = std::make_unique<juce::TextButton> ("Back");
        backButton_->onClick = [this] { deselectAll(); };
        addChildComponent (*backButton_);

        masterIns_ = std::make_unique<juce::TextButton>();
        masterIns_->setButtonText ("INS");
        masterIns_->onClick = [this]
        {
            if (onMasterInsertChainClicked) onMasterInsertChainClicked();
        };
        addAndMakeVisible (*masterIns_);

        // Master per-pair destination button — visible only when the audio
        // device exposes more than one stereo output pair (multi-output
        // interfaces). On 2-channel devices the button stays hidden because
        // there is no choice to make. Populated via setMasterDestination().
        masterDestButton_ = std::make_unique<juce::TextButton>();
        masterDestButton_->setButtonText ("—");
        masterDestButton_->onClick = [this] { showMasterDestinationMenu(); };
        addChildComponent (*masterDestButton_);   // hidden until setMasterDestination runs
    }

    /// One aux bus strip's display state. Output Mixer has no FX-return concept
    /// (no input-side returns) — every entry here is a plain bus.
    struct BusInfo { juce::String name; };

    /// One phrase-channel strip's display state (slice 5b). LEFT-band strip
    /// in PillState (DFS) order; name is read-only (phrase rename is a
    /// separate future slice). The Constituent id is round-tripped so the
    /// pane-side callbacks can identify which phrase the operator touched.
    struct PhraseStripInfo { ida::ConstituentId id { 0 }; juce::String name; };

    /// One MON-channel strip's display state. LEFTMOST band in the pane
    /// (left of phrase strips, mirroring signal flow: live monitoring →
    /// phrase playback → buses → master). Carries the input ChannelId
    /// the operator can use to map back to the corresponding input strip,
    /// and a name string MainComponent supplies (typically "MON N" where
    /// N is the 1-based input-strip row). Whitepaper V9 §6.3.1 / §7.2:
    /// peer of phrase channels, full strip controls. Discriminated from
    /// phrase strips on the listener callbacks via `ChannelType::FXReturn`
    /// (Output Mixer has no FX-return concept of its own — see the class
    /// comment above — so the enum tag is free for MON's use).
    struct MonStripInfo { ida::ChannelId inputChannelId { 0 }; juce::String name; };

    /// A bus destination kind. Output buses have no Tape option (the Output
    /// Mixer is the mixdown side; tape is the Input Mixer's terminal).
    enum class DestKind { Bus, HardwareOutput };
    /// One selectable destination in a bus's picker. `id` is the target BusId
    /// raw value when `kind == Bus` (0 = master), 0 for HardwareOutput.
    /// `pairIndex` is the physical-output stereo-pair offset (0 = outs [0,1],
    /// 1 = outs [2,3], …) and is ignored when `kind == Bus`. Identity for
    /// ticking is the (kind, id, pairIndex) triple.
    struct DestChoice { DestKind kind;
                        std::int64_t id;
                        juce::String name;
                        int pairIndex { 0 }; };
    /// A bus's current destination (button label + what ticks in the popup).
    /// Defaults to Bus + id 0 (the master — engine default for a fresh aux bus).
    struct StripDest { DestKind currentKind { DestKind::Bus };
                       std::int64_t currentId { 0 };
                       int          currentPairIndex { 0 };
                       juce::String currentName; };

    // --- Master gesture relays ---
    std::function<void (float gainLinear)> onMasterGain;
    std::function<void (bool muted)>       onMasterMute;
    std::function<void()>                  onMasterInsertChainClicked;
    std::function<void (DestChoice dest)>  onMasterDestinationChosen;

    // --- Aux-bus gesture relays (idx = bus-strip row index, parallel to setBusStrips) ---
    std::function<void()>                              onAddBus;
    /// Blank-pane-area "Add FX return" gesture (mirror of InputMixerPane).
    /// MainComponent calls `OutputMixer::addBus` with `BusKind::FxReturn`
    /// so the phrase-channel Sends tab has targets to route into.
    std::function<void()>                              onAddFxReturn;
    std::function<void (int busIdx, float gainLinear)> onBusGain;
    std::function<void (int busIdx, bool muted)>       onBusMute;
    std::function<void (int busIdx)>                   onBusInsertChainClicked;
    std::function<void (int busIdx, DestChoice dest)>  onBusDestinationChosen;
    std::function<void (int busIdx, juce::String newName)> onBusRename;
    /// An aux-bus strip was clicked. Mirrors `onPhraseSelect`. Only EQ + CMP
    /// tabs apply (no pan/width on a stereo bus; no per-bus sends yet).
    std::function<void (int busIdx)>                   onBusSelect;
    /// EQ/CMP gestures for the currently-selected aux bus.
    std::function<void (int busIdx, ida::EqConfig cfg)> onBusEqConfigChanged;
    std::function<void (int busIdx)>                    onBusEqSlotAddRequested;
    std::function<void (int busIdx, ida::CmpConfig cfg)> onBusCmpConfigChanged;
    std::function<void (int busIdx)>                    onBusCmpSlotAddRequested;
    /// Bus-side Pan/Width + Sends (slice EC-Polish-fix-2). Same shape as
    /// the channel-side phrase callbacks but addressed by bus row.
    std::function<void (int busIdx, float pan)>                        onBusPan;
    std::function<void (int busIdx, float width)>                      onBusWidth;
    std::function<void (int busIdx, int fxReturnIdx, float level)>     onBusSendChanged;
    /// The master strip was clicked. EQ + CMP tabs apply to the master bus
    /// (BusId{0}); MainComponent resolves and dispatches.
    std::function<void()>                              onMasterSelect;
    std::function<void (ida::EqConfig cfg)>            onMasterEqConfigChanged;
    std::function<void()>                              onMasterEqSlotAddRequested;
    std::function<void (ida::CmpConfig cfg)>           onMasterCmpConfigChanged;
    std::function<void()>                              onMasterCmpSlotAddRequested;
    /// Master-side Pan/Width (slice EC-Polish-fix-2). No Sends — master is
    /// terminal.
    std::function<void (float pan)>                    onMasterPan;
    std::function<void (float width)>                  onMasterWidth;

    // --- Phrase-channel gesture relays (slice 5b; idx = phrase-strip row index,
    // parallel to setPhraseStrips). ChannelType::Instrument distinguishes
    // phrase strips from aux buses in the shared CompactFaderStripListener.
    std::function<void (int phraseIdx, float gainLinear)> onPhraseGain;
    std::function<void (int phraseIdx, bool muted)>       onPhraseMute;
    std::function<void (int phraseIdx)>                   onPhraseInsertChainClicked;
    std::function<void (int phraseIdx, DestChoice dest)>  onPhraseDestinationChosen;
    /// A phrase strip was selected — MainComponent reads its pan/width and
    /// calls showPhraseDetailFor() to populate + reveal the detail panel.
    std::function<void (int phraseIdx)>                   onPhraseSelect;
    /// Detail-panel pan knob moved. `pan` is industry-standard [-1, +1];
    /// MainComponent converts to engine's [0,1] before writing.
    std::function<void (int phraseIdx, float pan)>        onPhrasePan;
    /// Sends-tab knob moved for the selected phrase strip. `fxReturnIdx`
    /// indexes into the FX-return list MainComponent passed via
    /// `showPhraseDetailFor`. `level` is linear 0..1. MainComponent translates
    /// back to BusId + `OutputMixer::routeChannelToBus` (additive on
    /// FxReturn-kind targets — see E3 semantics).
    std::function<void (int phraseIdx, int fxReturnIdx, float level)>
                                                          onPhraseSendChanged;
    /// Sends-tab PRE FADER toggle changed for the selected phrase strip.
    /// MainComponent calls `OutputMixer::setChannelSendIsPreFader`.
    std::function<void (int phraseIdx, bool preFader)>    onPhrasePreFaderToggled;
    /// Detail-panel width knob moved. `width` is [0, 2] (1 = unity stereo).
    std::function<void (int phraseIdx, float width)>      onPhraseWidth;
    /// EQ-tab control moved for the selected phrase strip (slice EC).
    std::function<void (int phraseIdx, ida::EqConfig cfg)> onPhraseEqConfigChanged;
    /// EQ-tab "+ Add EQ" empty-state button.
    std::function<void (int phraseIdx)>                    onPhraseEqSlotAddRequested;
    /// CMP-tab control moved for the selected phrase strip.
    std::function<void (int phraseIdx, ida::CmpConfig cfg)> onPhraseCmpConfigChanged;
    /// CMP-tab "+ Add CMP" empty-state button.
    std::function<void (int phraseIdx)>                    onPhraseCmpSlotAddRequested;

    // --- MON-channel gesture relays. Mirror of the onPhrase* surface;
    // idx = mon-strip row index, parallel to setMonStrips. Engine-side
    // mapping (mon row index → OutputChannelId) lives in MainComponent.
    // Discriminated from phrase strips at the listener layer by
    // ChannelType::FXReturn on the strip (IDA's Output Mixer has no
    // FX-return concept of its own).
    std::function<void (int monIdx, float gainLinear)> onMonGain;
    std::function<void (int monIdx, bool muted)>       onMonMute;
    std::function<void (int monIdx)>                   onMonInsertChainClicked;
    std::function<void (int monIdx, DestChoice dest)>  onMonDestinationChosen;
    std::function<void (int monIdx)>                   onMonSelect;
    std::function<void (int monIdx, float pan)>        onMonPan;
    std::function<void (int monIdx, float width)>      onMonWidth;
    std::function<void (int monIdx, int fxReturnIdx, float level)>
                                                       onMonSendChanged;
    std::function<void (int monIdx, bool preFader)>    onMonPreFaderToggled;
    std::function<void (int monIdx, ida::EqConfig cfg)>  onMonEqConfigChanged;
    std::function<void (int monIdx)>                     onMonEqSlotAddRequested;
    std::function<void (int monIdx, ida::CmpConfig cfg)> onMonCmpConfigChanged;
    std::function<void (int monIdx)>                     onMonCmpSlotAddRequested;

    void setMasterLevelDb (float dbL, float dbR)
    {
        if (master_) master_->setLevel (dbL, dbR);
    }

    void setMasterLufs (float lufs)
    {
        if (master_) master_->setLUFSLevel (lufs);
    }

    /// Channel pill row builder — mirrors InputMixerPane::rebuildChannelPills.
    /// Order: phrase strips, then aux buses, then master.
    void rebuildChannelPills()
    {
        channelPills_.clear();
        auto makePill = [this] (juce::String name, int idx, otto::ui::ChannelType type)
        {
            auto pill = std::make_unique<juce::TextButton> (name);
            pill->setClickingTogglesState (true);
            pill->setRadioGroupId (4);
            pill->setColour (juce::TextButton::buttonColourId,   otto::Colours::bg3);
            pill->setColour (juce::TextButton::buttonOnColourId, otto::Colours::accent);
            pill->setColour (juce::TextButton::textColourOffId,  otto::Colours::textSecondary);
            pill->setColour (juce::TextButton::textColourOnId,   otto::Colours::textPrimary);
            pill->onClick = [this, idx, type] { stripChannelSelected (idx, type); };
            addChildComponent (*pill);
            channelPills_.push_back (std::move (pill));
        };
        for (int i = 0; i < monStripCount(); ++i)
            makePill (monStrips_[static_cast<std::size_t> (i)]->getChannelName(),
                      i, otto::ui::ChannelType::FXReturn);
        for (int i = 0; i < phraseStripCount(); ++i)
            makePill (phraseStrips_[static_cast<std::size_t> (i)]->getChannelName(),
                      i, otto::ui::ChannelType::Instrument);
        for (int i = 0; i < busStripCount(); ++i)
            makePill (busStrips_[static_cast<std::size_t> (i)]->getChannelName(),
                      i, otto::ui::ChannelType::Bus);
        if (master_)
            makePill (master_->getChannelName(), kMasterStripId, otto::ui::ChannelType::Bus);
    }

    juce::Rectangle<int> masterInsButtonScreenArea() const
    {
        return masterIns_ ? masterIns_->getScreenBounds() : juce::Rectangle<int>{};
    }

    /// Rebuilds the aux-bus row from `infos`. Mirrors InputMixerPane::setBusStrips
    /// shape minus the FX-return ChannelType (Output Mixer aux buses are Bus only).
    void setBusStrips (const std::vector<BusInfo>& infos)
    {
        busStrips_.clear();
        busDestButtons_.clear();
        busInsButtons_.clear();
        busNameOverlays_.clear();
        busStripDests_.clear();
        busChoices_.clear();
        // Bus identities change on a rebuild — drop the bus selection. Hide
        // the detail panel only if nothing else (phrase / master) is bound.
        if (selectedBus_ >= 0)
        {
            selectedBus_ = -1;
            if (selectedPhrase_ < 0 && ! selectedMaster_)
                detailPanel_.setVisible (false);
        }
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, otto::ui::ChannelType::Bus);
            strip->setChannelName (infos[static_cast<std::size_t> (i)].name);
            strip->setOutputComboVisible (false);   // pane owns routing, not the combo
            strip->addListener (this);
            addAndMakeVisible (*strip);
            busStrips_.push_back (std::move (strip));

            auto destBtn = std::make_unique<juce::TextButton>();
            destBtn->setButtonText ("—");
            const int idx = i;
            destBtn->onClick = [this, idx] { showBusDestinationMenu (idx); };
            addAndMakeVisible (*destBtn);
            busDestButtons_.push_back (std::move (destBtn));
            busStripDests_.push_back ({});

            auto ins = std::make_unique<juce::TextButton>();
            ins->setButtonText ("INS");
            ins->onClick = [this, idx]
            {
                if (onBusInsertChainClicked) onBusInsertChainClicked (idx);
            };
            addAndMakeVisible (*ins);
            busInsButtons_.push_back (std::move (ins));

            // Strip context overlay — invisible click-catcher pinned to the
            // top sliver of the strip (where the name label paints). Catches
            // right-click + long-press for the "Rename…" context menu, and
            // hosts the inline TextEditor when the operator commits to a
            // rename. Sits in front in Z order so its mouseDown fires before
            // the CompactFaderStrip's fader interaction below it.
            // Slice EC-Polish-fix: short-tap routes to bus select (overlay
            // was swallowing every click on the name band; mirror of the
            // input-pane fix).
            auto overlay = std::make_unique<ida::app::StripContextOverlay> (
                idx,
                [this] (int who) { showBusContextMenu (who); },
                [this] (int who, juce::String s)
                {
                    if (onBusRename) onBusRename (who, std::move (s));
                },
                [this] (int who)
                {
                    const auto type = busStrips_[static_cast<std::size_t> (who)]
                                          ->getChannelType();
                    stripChannelSelected (who, type);
                });
            addAndMakeVisible (*overlay);
            busNameOverlays_.push_back (std::move (overlay));
        }
        rebuildChannelPills();
        resized();
    }

    /// Pushes per-bus choice lists + current destinations. Each `perBusChoices[i]`
    /// is bus `i`'s own picker contents (cycle-excluded targets differ per bus —
    /// see MainComponent::refreshOutputDestinations). `perBus` is parallel to
    /// busStrips_; the label sets the picker button text.
    void setBusDestinations (const std::vector<std::vector<DestChoice>>& perBusChoices,
                             const std::vector<StripDest>& perBus)
    {
        busChoices_ = perBusChoices;
        jassert (static_cast<int> (perBus.size()) == busStripCount());
        for (int i = 0; i < busStripCount() && i < static_cast<int> (perBus.size()); ++i)
        {
            busStripDests_[static_cast<std::size_t> (i)] = perBus[static_cast<std::size_t> (i)];
            const auto& label = perBus[static_cast<std::size_t> (i)].currentName;
            busDestButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }

    /// Pushes the master strip's destination state. On multi-output devices the
    /// master destination button becomes visible and ticks the current pair;
    /// on a 2-channel device the choice list contains a single entry, the
    /// button stays hidden, and the bus is implicitly parked on pair 0.
    void setMasterDestination (const std::vector<DestChoice>& choices,
                               const StripDest& current)
    {
        masterChoices_ = choices;
        masterDest_    = current;
        const bool multiPair = choices.size() > 1;
        if (masterDestButton_)
        {
            masterDestButton_->setVisible (multiPair);
            masterDestButton_->setButtonText (current.currentName.isEmpty() ? "—"
                                                                            : current.currentName);
        }
        resized();
    }

    /// Updates a single aux strip's visible name without rebuilding the strip
    /// row (preserves fader / mute / meter state on rename).
    void updateBusName (int busIdx, const juce::String& newName)
    {
        if (busIdx < 0 || busIdx >= busStripCount()) return;
        busStrips_[static_cast<std::size_t> (busIdx)]->setChannelName (newName);
    }

    /// Populates the detail panel with `phraseIdx`'s current values and reveals
    /// it. `panMinus1to1` is the knob-domain pan ([-1, +1]); `width` is [0, 2].
    /// `fxReturns` + `sendLevels` + `preFader` populate the Sends tab.
    /// Mirrors InputMixerPane::showDetailFor.
    void showPhraseDetailFor (int phraseIdx, float panMinus1to1, float width,
                              std::vector<ida::ui::FxReturnInfo> fxReturns,
                              std::vector<float> sendLevels,
                              bool preFader,
                              ida::EqConfig eqConfig, bool hasEqSlot,
                              ida::CmpConfig cmpConfig, bool hasCmpSlot)
    {
        if (phraseIdx < 0 || phraseIdx >= phraseStripCount()) return;
        detailPanel_.setChannel (phraseIdx, otto::ui::ChannelType::Instrument);
        detailPanel_.panWidTab().setPan (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   preFader });
        detailPanel_.eqTab().setChannelState ({ eqConfig, hasEqSlot });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        // Phrase strips: all four tabs available (reset any prior hide from
        // a bus / master selection).
        detailPanel_.setTabsAvailable ({ true, true, true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }

    /// MON-strip detail surface — identical surface to phrase (Pan/Width +
    /// Sends + EQ + CMP) per the 2026-05-24 operator design lock. `monIdx`
    /// is the row index into `monStrips_`, parallel to
    /// MainComponent::monStripInputChannelIds_.
    void showMonDetailFor (int monIdx, float panMinus1to1, float width,
                           std::vector<ida::ui::FxReturnInfo> fxReturns,
                           std::vector<float> sendLevels,
                           bool preFader,
                           ida::EqConfig eqConfig, bool hasEqSlot,
                           ida::CmpConfig cmpConfig, bool hasCmpSlot)
    {
        if (monIdx < 0 || monIdx >= monStripCount()) return;
        detailPanel_.setChannel (monIdx, otto::ui::ChannelType::FXReturn);
        detailPanel_.panWidTab().setPan (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   preFader });
        detailPanel_.eqTab().setChannelState ({ eqConfig, hasEqSlot });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        detailPanel_.setTabsAvailable ({ true, true, true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }

    /// Aux-bus detail surface (EQ + CMP only). Mirrors
    /// `InputMixerPane::showBusDetailFor`. `busIdx` is the row index into
    /// `busStrips_`, parallel to MainComponent::outputBusStripIds_.
    /// Slice EC-Polish-fix-2: full Pan/Width + Sends + EQ + CMP for buses;
    /// FX returns get Pan/Width + EQ + CMP (Sends → Edit FX lands next slice).
    void showBusDetailFor (int busIdx,
                           float panMinus1to1, float width,
                           std::vector<ida::ui::FxReturnInfo> fxReturns,
                           std::vector<float>                 sendLevels,
                           ida::EqConfig  eqConfig,  bool hasEqSlot,
                           ida::CmpConfig cmpConfig, bool hasCmpSlot,
                           bool isFxReturn)
    {
        if (busIdx < 0 || busIdx >= busStripCount()) return;
        detailPanel_.setChannel (busIdx, isFxReturn
                                              ? otto::ui::ChannelType::FXReturn
                                              : otto::ui::ChannelType::Bus);
        detailPanel_.panWidTab().setPan   (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.sendsTab().setChannelState ({ std::move (fxReturns),
                                                   std::move (sendLevels),
                                                   /*preFader=*/ false });
        detailPanel_.eqTab().setChannelState  ({ eqConfig,  hasEqSlot  });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        detailPanel_.setTabsAvailable (isFxReturn
                                        ? ida::ui::ChannelDetail::TabMask { true, false, true, true }
                                        : ida::ui::ChannelDetail::TabMask { true, true,  true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }

    /// Master-strip detail surface. Master is terminal — it has Pan/Width
    /// + EQ + CMP but no Sends (signal flows TO master, not from). The
    /// master is unique on this pane — its BusId is fixed at 0.
    void showMasterDetailFor (float panMinus1to1, float width,
                              ida::EqConfig eqConfig, bool hasEqSlot,
                              ida::CmpConfig cmpConfig, bool hasCmpSlot)
    {
        detailPanel_.setChannel (-1, otto::ui::ChannelType::Bus);
        detailPanel_.panWidTab().setPan   (panMinus1to1);
        detailPanel_.panWidTab().setWidth (width);
        detailPanel_.eqTab().setChannelState  ({ eqConfig,  hasEqSlot  });
        detailPanel_.cmpTab().setChannelState ({ cmpConfig, hasCmpSlot });
        detailPanel_.setTabsAvailable ({ true, false, true, true });
        detailPanel_.setVisible (true);
        grabKeyboardFocus();
        resized();
    }

    /// Default-select Master on first launch. MainComponent calls this once,
    /// after all output-pane callbacks are wired AND output mixer init has
    /// run, so onMasterSelect can populate the EQ/CMP probes safely.
    /// Subsequent calls no-op — operator intent wins on later interactions.
    void triggerDefaultMasterSelection()
    {
        if (defaultSelectionDone_) return;
        if (! onMasterSelect)      return;
        defaultSelectionDone_ = true;
        if (master_) master_->setSelected (true);
        selectedPhrase_ = -1;
        selectedBus_    = -1;
        selectedMaster_ = true;
        onMasterSelect();
    }

    /// Rebuilds the LEFT-band phrase-strip row from `infos` (slice 5b). Order
    /// matches PillState (DFS) order — MainComponent owns the enumeration.
    /// One strip per phrase, name read-only in 5b. Mirrors setBusStrips shape
    /// minus the name overlay (no rename gesture in this slice).
    void setPhraseStrips (const std::vector<PhraseStripInfo>& infos)
    {
        phraseStrips_.clear();
        phraseDestButtons_.clear();
        phraseInsButtons_.clear();
        phraseStripDests_.clear();
        phraseChoices_.clear();
        // Phrase identities change on a rebuild — drop the phrase selection.
        // Hide the detail panel only if nothing else (bus / master) is still
        // bound to it.
        selectedPhrase_ = -1;
        if (selectedBus_ < 0 && ! selectedMaster_)
            detailPanel_.setVisible (false);
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, otto::ui::ChannelType::Instrument);
            strip->setChannelName (infos[static_cast<std::size_t> (i)].name);
            strip->setOutputComboVisible (false);   // pane owns routing, not the combo
            strip->addListener (this);
            addAndMakeVisible (*strip);
            phraseStrips_.push_back (std::move (strip));

            auto destBtn = std::make_unique<juce::TextButton>();
            destBtn->setButtonText ("—");
            const int idx = i;
            destBtn->onClick = [this, idx] { showPhraseDestinationMenu (idx); };
            addAndMakeVisible (*destBtn);
            phraseDestButtons_.push_back (std::move (destBtn));
            phraseStripDests_.push_back ({});

            auto ins = std::make_unique<juce::TextButton>();
            ins->setButtonText ("INS");
            ins->onClick = [this, idx]
            {
                if (onPhraseInsertChainClicked) onPhraseInsertChainClicked (idx);
            };
            addAndMakeVisible (*ins);
            phraseInsButtons_.push_back (std::move (ins));
        }
        rebuildChannelPills();
        resized();
    }

    /// Mirrors setBusDestinations for phrase strips. `perPhraseChoices[i]` is
    /// phrase `i`'s picker contents; `perPhrase[i]` is its current destination
    /// (drives button label + ticked item in the popup). Phrase→bus routing
    /// has no cycle filter in 5b (phrases are leaves in the routing graph).
    void setPhraseDestinations (const std::vector<std::vector<DestChoice>>& perPhraseChoices,
                                const std::vector<StripDest>& perPhrase)
    {
        phraseChoices_ = perPhraseChoices;
        jassert (static_cast<int> (perPhrase.size()) == phraseStripCount());
        for (int i = 0; i < phraseStripCount() && i < static_cast<int> (perPhrase.size()); ++i)
        {
            phraseStripDests_[static_cast<std::size_t> (i)] = perPhrase[static_cast<std::size_t> (i)];
            const auto& label = perPhrase[static_cast<std::size_t> (i)].currentName;
            phraseDestButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }

    [[nodiscard]] int phraseStripCount() const noexcept
    {
        return static_cast<int> (phraseStrips_.size());
    }

    /// Mirror of setPhraseStrips for MON strips. Drives the leftmost
    /// strip band — strips appear when MainComponent::refreshOutputMixer-
    /// MonChannels sees a MON-on input. Order matches the input-mixer
    /// row order so the operator's spatial mental model is preserved.
    /// MON strips carry `ChannelType::FXReturn` so the listener callbacks
    /// can discriminate them from phrase strips (ChannelType::Instrument)
    /// even though both use 0-based row indices.
    void setMonStrips (const std::vector<MonStripInfo>& infos)
    {
        monStrips_.clear();
        monDestButtons_.clear();
        monInsButtons_.clear();
        monStripDests_.clear();
        monChoices_.clear();
        // MON strip identities change on a rebuild — drop the selection.
        if (selectedMon_ >= 0)
        {
            selectedMon_ = -1;
            if (selectedPhrase_ < 0 && selectedBus_ < 0 && ! selectedMaster_)
                detailPanel_.setVisible (false);
        }
        for (int i = 0; i < static_cast<int> (infos.size()); ++i)
        {
            auto strip = std::make_unique<otto::ui::CompactFaderStrip> (
                i, otto::ui::ChannelType::FXReturn);
            strip->setChannelName (infos[static_cast<std::size_t> (i)].name);
            strip->setOutputComboVisible (false);   // pane owns routing, not the combo
            strip->addListener (this);
            addAndMakeVisible (*strip);
            monStrips_.push_back (std::move (strip));

            auto destBtn = std::make_unique<juce::TextButton>();
            destBtn->setButtonText ("—");
            const int idx = i;
            destBtn->onClick = [this, idx] { showMonDestinationMenu (idx); };
            addAndMakeVisible (*destBtn);
            monDestButtons_.push_back (std::move (destBtn));
            monStripDests_.push_back ({});

            auto ins = std::make_unique<juce::TextButton>();
            ins->setButtonText ("INS");
            ins->onClick = [this, idx]
            {
                if (onMonInsertChainClicked) onMonInsertChainClicked (idx);
            };
            addAndMakeVisible (*ins);
            monInsButtons_.push_back (std::move (ins));
        }
        rebuildChannelPills();
        resized();
    }

    /// Mirror of setPhraseDestinations for MON strips.
    void setMonDestinations (const std::vector<std::vector<DestChoice>>& perMonChoices,
                             const std::vector<StripDest>& perMon)
    {
        monChoices_ = perMonChoices;
        jassert (static_cast<int> (perMon.size()) == monStripCount());
        for (int i = 0; i < monStripCount() && i < static_cast<int> (perMon.size()); ++i)
        {
            monStripDests_[static_cast<std::size_t> (i)] = perMon[static_cast<std::size_t> (i)];
            const auto& label = perMon[static_cast<std::size_t> (i)].currentName;
            monDestButtons_[static_cast<std::size_t> (i)]->setButtonText (label.isEmpty() ? "—" : label);
        }
    }

    [[nodiscard]] int monStripCount() const noexcept
    {
        return static_cast<int> (monStrips_.size());
    }

    /// Screen bounds of MON strip `monIdx`'s INS button, mirror of
    /// `phraseInsButtonScreenArea`.
    juce::Rectangle<int> monInsButtonScreenArea (int monIdx) const
    {
        if (monIdx < 0 || monIdx >= static_cast<int> (monInsButtons_.size())) return {};
        return monInsButtons_[static_cast<std::size_t> (monIdx)]->getScreenBounds();
    }

    /// Screen bounds of phrase strip `phraseIdx`'s INS button, used to anchor
    /// the InsertChainPopup CallOutBox. Empty rect if out of range.
    juce::Rectangle<int> phraseInsButtonScreenArea (int phraseIdx) const
    {
        if (phraseIdx < 0 || phraseIdx >= static_cast<int> (phraseInsButtons_.size())) return {};
        return phraseInsButtons_[static_cast<std::size_t> (phraseIdx)]->getScreenBounds();
    }

    [[nodiscard]] int busStripCount() const noexcept
    {
        return static_cast<int> (busStrips_.size());
    }

    void setBusStripLevelDb (int busIdx, float dbL, float dbR)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLevel (dbL, dbR);
    }

    void setBusStripLufs (int busIdx, float lufs)
    {
        if (busIdx >= 0 && busIdx < busStripCount())
            busStrips_[static_cast<std::size_t> (busIdx)]->setLUFSLevel (lufs);
    }

    void setMonStripLevelDb (int monIdx, float dbL, float dbR)
    {
        if (monIdx >= 0 && monIdx < monStripCount())
            monStrips_[static_cast<std::size_t> (monIdx)]->setLevel (dbL, dbR);
    }

    void setMonStripLufs (int monIdx, float lufs)
    {
        if (monIdx >= 0 && monIdx < monStripCount())
            monStrips_[static_cast<std::size_t> (monIdx)]->setLUFSLevel (lufs);
    }

    /// Screen bounds of aux bus `busIdx`'s INS button, used to anchor the
    /// InsertChainPopup CallOutBox. Empty rect if out of range.
    juce::Rectangle<int> busInsButtonScreenArea (int busIdx) const
    {
        if (busIdx < 0 || busIdx >= static_cast<int> (busInsButtons_.size())) return {};
        return busInsButtons_[static_cast<std::size_t> (busIdx)]->getScreenBounds();
    }

    /// Blank-area gesture: right-click on desktop fires immediately;
    /// long-press on touch arms the timer. A drag past tolerance cancels.
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Only the pane itself opens the menu — never a child component
        // (strip / button absorbed the click otherwise).
        if (e.eventComponent != this) return;
        if (e.mods.isPopupMenu())
        {
            showBlankAreaMenu (e.getScreenPosition());
            return;
        }
        longPressBlank_     = true;
        longPressScreenPos_ = e.getScreenPosition();
        startTimer (kLongPressMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isTimerRunning() && e.getDistanceFromDragStart() > kLongPressMoveTolerancePx)
        {
            stopTimer();
            longPressBlank_ = false;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stopTimer();
        longPressBlank_ = false;
    }

    /// Full-screen trigger — same contract as InputMixerPane. Empty-slot
    /// EQ / CMP stays at the small detail band so the operator can see
    /// the strip row and escape.
    bool isDetailFullScreen() const noexcept
    {
        if (! detailPanel_.isVisible()) return false;
        const auto t = detailPanel_.getActiveTab();
        if (t == ida::ui::ChannelDetail::Tab::EQ)
            return detailPanel_.eqTab().hasEqSlot();
        if (t == ida::ui::ChannelDetail::Tab::CMP)
            return detailPanel_.cmpTab().hasCmpSlot();
        return false;
    }

    void resized() override
    {
        constexpr int kGap          = 6;
        constexpr int kGroupDividerW = kGap * 3;
        auto area = getLocalBounds().reduced (kGap);

        // Full-screen detail (EQ / CMP) — panel fills the bulk of the pane.
        // A channel-pill row at the bottom mirrors OTTO's iPad EQ-tab kit
        // selector so the operator can switch channels without leaving
        // full-screen.
        constexpr int kPillRowHeight = 36;
        if (isDetailFullScreen())
        {
            for (auto& s : phraseStrips_)        s->setVisible (false);
            for (auto& b : phraseDestButtons_)   b->setVisible (false);
            for (auto& b : phraseInsButtons_)    b->setVisible (false);
            for (auto& s : busStrips_)           s->setVisible (false);
            for (auto& b : busDestButtons_)      b->setVisible (false);
            for (auto& b : busInsButtons_)       b->setVisible (false);
            for (auto& o : busNameOverlays_)     o->setVisible (false);
            if (master_)             master_           ->setVisible (false);
            if (masterIns_)          masterIns_        ->setVisible (false);
            if (masterDestButton_)   masterDestButton_ ->setVisible (false);

            auto pillRow = area.removeFromBottom (kPillRowHeight);
            area.removeFromBottom (kGap);
            detailPanel_.setBounds (area);

            // Back button overlays the top-right corner of the detail panel.
            // Same gesture as Escape; load-bearing on touch devices.
            if (backButton_)
            {
                constexpr int kBackW = 72;
                constexpr int kBackH = 28;
                constexpr int kBackMargin = 6;
                backButton_->setBounds (area.getRight() - kBackW - kBackMargin,
                                        area.getY()     + kBackMargin,
                                        kBackW, kBackH);
                backButton_->setVisible (true);
                backButton_->toFront (false);
            }

            const int n = static_cast<int> (channelPills_.size());
            if (n > 0)
            {
                const int totalPillGap = 4 * (n - 1);
                const int pillW = juce::jmax (40,
                                              (pillRow.getWidth() - totalPillGap) / n);
                int px = pillRow.getX();
                for (int i = 0; i < n; ++i)
                {
                    if (! channelPills_[static_cast<std::size_t> (i)]) continue;
                    auto& pill = *channelPills_[static_cast<std::size_t> (i)];
                    pill.setBounds (px, pillRow.getY(), pillW, pillRow.getHeight());
                    pill.setVisible (true);
                    // Toggle state: phrases first, then buses, then master.
                    const int phraseN = phraseStripCount();
                    const int busN    = busStripCount();
                    bool selected = false;
                    if (i < phraseN)
                        selected = (i == selectedPhrase_);
                    else if (i < phraseN + busN)
                        selected = ((i - phraseN) == selectedBus_);
                    else
                        selected = selectedMaster_;
                    pill.setToggleState (selected, juce::dontSendNotification);
                    px += pillW + 4;
                }
            }
            return;
        }

        // Restore visibility (master destination button has its own visibility
        // rule — multi-pair devices only — handled by setMasterDestination).
        for (auto& s : phraseStrips_)        s->setVisible (true);
        for (auto& b : phraseDestButtons_)   b->setVisible (true);
        for (auto& b : phraseInsButtons_)    b->setVisible (true);
        for (auto& s : busStrips_)           s->setVisible (true);
        for (auto& b : busDestButtons_)      b->setVisible (true);
        for (auto& b : busInsButtons_)       b->setVisible (true);
        for (auto& o : busNameOverlays_)     o->setVisible (true);
        for (auto& p : channelPills_)        if (p) p->setVisible (false);
        if (master_)    master_   ->setVisible (true);
        if (masterIns_) masterIns_->setVisible (true);
        if (masterDestButton_)
            masterDestButton_->setVisible (masterChoices_.size() > 1);

        // Detail panel (slice 5b polish) takes a fixed top band when a phrase
        // is selected; the strip row + button stack fills the remainder below.
        // Mirrors InputMixerPane's layout exactly.
        if (detailPanel_.isVisible())
        {
            detailPanel_.setBounds (area.removeFromTop (kDetailHeight));
            area.removeFromTop (kGap);
        }

        // Two fixed bands at the bottom: destination picker (lowest) +
        // INS button (just above) — same stacking as InputMixerPane.
        auto pickerRow = area.removeFromBottom (kDestHeight);
        area.removeFromBottom (kGap);
        auto insRow    = area.removeFromBottom (kInsHeight);
        area.removeFromBottom (kGap);

        constexpr int kStripW = otto::ui::CompactFaderStrip::kStripWidth;

        // Pro mixing-console layout (Pro Tools / Logic / Reaper): channel
        // strips fill the LEFT band (M6+ Constituent rendering), aux buses
        // and master pin to the RIGHT as a single group. Master rightmost,
        // then aux buses immediately to its left in index order (Bus 1
        // leftmost-of-group, Bus N closest to master).
        if (master_)    master_->setBounds    (area.removeFromRight (kStripW));
        if (masterIns_) masterIns_->setBounds (insRow.removeFromRight (kStripW));
        if (masterDestButton_ && masterDestButton_->isVisible())
            masterDestButton_->setBounds (pickerRow.removeFromRight (kStripW));

        if (busStripCount() > 0)
        {
            // Visual divider so master reads as the terminal, not "just another bus."
            area.removeFromRight (kGroupDividerW);
            insRow.removeFromRight (kGroupDividerW);
            pickerRow.removeFromRight (kGroupDividerW);
        }

        // Aux strips right-to-left so the bus group is right-anchored. The
        // last bus (highest index) sits closest to master; the first sits
        // leftmost of the group. Inter-strip gap is skipped after the last
        // (leftmost) strip so the group has no trailing gap on its left.
        for (int i = busStripCount() - 1; i >= 0; --i)
        {
            auto stripBounds = area.removeFromRight (kStripW);
            busStrips_[static_cast<std::size_t> (i)]->setBounds (stripBounds);
            // Overlay covers the top name-label band of the strip (where
            // CompactFaderStrip paints the channel name). Sized to a thin
            // sliver so it doesn't block fader interaction below it.
            if (i < static_cast<int> (busNameOverlays_.size()))
                busNameOverlays_[static_cast<std::size_t> (i)]->setBounds (
                    stripBounds.withHeight (kNameOverlayHeight));
            busInsButtons_[static_cast<std::size_t> (i)]->setBounds (insRow.removeFromRight (kStripW));
            busDestButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromRight (kStripW));
            if (i > 0)
            {
                area.removeFromRight (kGap);
                insRow.removeFromRight (kGap);
                pickerRow.removeFromRight (kGap);
            }
        }

        // MON strips (V9 §6.3.1 / §7.2) — LEFTMOST band, left-to-right, in
        // input-strip row order (refreshOutputMixerMonChannels owns the
        // enumeration). Inter-strip gap between MON strips; a final gap
        // after the last MON strip separates the MON band from the phrase
        // band (which itself eats from the same `area` left edge below).
        for (int i = 0; i < monStripCount(); ++i)
        {
            auto stripBounds = area.removeFromLeft (kStripW);
            monStrips_[static_cast<std::size_t> (i)]->setBounds (stripBounds);
            monInsButtons_[static_cast<std::size_t> (i)]->setBounds (insRow.removeFromLeft (kStripW));
            monDestButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromLeft (kStripW));
            // Always insert an inter-strip gap; after the last MON strip
            // that same gap acts as the band separator before phrase strips.
            area.removeFromLeft (kGap);
            insRow.removeFromLeft (kGap);
            pickerRow.removeFromLeft (kGap);
        }

        // Phrase strips (slice 5b) fill the remaining LEFT band, left-to-right,
        // in PillState (DFS) order — MainComponent::refreshOutputMixerPhraseChannels
        // owns the enumeration. No inter-strip gap after the last (rightmost)
        // strip so the phrase group has no trailing gap before the bus divider.
        for (int i = 0; i < phraseStripCount(); ++i)
        {
            auto stripBounds = area.removeFromLeft (kStripW);
            phraseStrips_[static_cast<std::size_t> (i)]->setBounds (stripBounds);
            phraseInsButtons_[static_cast<std::size_t> (i)]->setBounds (insRow.removeFromLeft (kStripW));
            phraseDestButtons_[static_cast<std::size_t> (i)]->setBounds (pickerRow.removeFromLeft (kStripW));
            if (i + 1 < phraseStripCount())
            {
                area.removeFromLeft (kGap);
                insRow.removeFromLeft (kGap);
                pickerRow.removeFromLeft (kGap);
            }
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (otto::Colours::bg2); }

    // --- ChannelDetailListener (slice EC-Polish full-screen layout) -----------
    void channelDetailTabChanged (int /*tabIndex*/) override { resized(); }

    /// Clear any current selection + hide the detail panel. Mirrors
    /// `InputMixerPane::deselectAll`.
    void deselectAll()
    {
        for (int i = 0; i < monStripCount(); ++i)
            monStrips_[static_cast<std::size_t> (i)]->setSelected (false);
        for (int i = 0; i < phraseStripCount(); ++i)
            phraseStrips_[static_cast<std::size_t> (i)]->setSelected (false);
        for (int i = 0; i < busStripCount(); ++i)
            busStrips_[static_cast<std::size_t> (i)]->setSelected (false);
        if (master_) master_->setSelected (false);
        selectedPhrase_ = -1;
        selectedBus_    = -1;
        selectedMon_    = -1;
        selectedMaster_ = false;
        detailPanel_.setVisible (false);
        resized();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            deselectAll();
            return true;
        }
        return false;
    }

    // --- CompactFaderStripListener — master (idx == kMasterStripId) vs aux vs
    // phrase. Phrase strips are distinguished from aux buses by their
    // ChannelType::Instrument tag (aux buses use ChannelType::Bus); the row
    // index alone can collide because both rows are 0-based.
    void stripGainChanged (int idx, otto::ui::ChannelType type, float gain) override
    {
        if (idx == kMasterStripId) { if (onMasterGain) onMasterGain (gain); }
        else if (type == otto::ui::ChannelType::FXReturn)
        { if (onMonGain) onMonGain (idx, gain); }
        else if (type == otto::ui::ChannelType::Instrument)
        { if (onPhraseGain) onPhraseGain (idx, gain); }
        else if (onBusGain)        onBusGain (idx, gain);
    }
    void stripMuteChanged (int idx, otto::ui::ChannelType type, bool muted) override
    {
        if (idx == kMasterStripId) { if (onMasterMute) onMasterMute (muted); }
        else if (type == otto::ui::ChannelType::FXReturn)
        { if (onMonMute) onMonMute (idx, muted); }
        else if (type == otto::ui::ChannelType::Instrument)
        { if (onPhraseMute) onPhraseMute (idx, muted); }
        else if (onBusMute)        onBusMute (idx, muted);
    }
    void stripSoloChanged (int, otto::ui::ChannelType, bool) override {}
    void stripChannelSelected (int idx, otto::ui::ChannelType type) override
    {
        const bool isMaster = (idx == kMasterStripId);
        const bool isMon    = (! isMaster) && (type == otto::ui::ChannelType::FXReturn);
        const bool isPhrase = (! isMaster) && (! isMon)
                              && (type == otto::ui::ChannelType::Instrument);
        const bool isAux    = (! isMaster) && (! isMon) && (! isPhrase);

        // MON, phrase, aux-bus, and master are mutually exclusive selections.
        // Clear the OTHER rows' highlights so the affordance stays honest.
        auto clearAll = [this]
        {
            for (int i = 0; i < monStripCount(); ++i)
                monStrips_[static_cast<std::size_t> (i)]->setSelected (false);
            for (int i = 0; i < phraseStripCount(); ++i)
                phraseStrips_[static_cast<std::size_t> (i)]->setSelected (false);
            for (int i = 0; i < busStripCount(); ++i)
                busStrips_[static_cast<std::size_t> (i)]->setSelected (false);
            if (master_) master_->setSelected (false);
        };

        if (isMon)
        {
            clearAll();
            if (idx >= 0 && idx < monStripCount())
                monStrips_[static_cast<std::size_t> (idx)]->setSelected (true);
            selectedMon_    = idx;
            selectedPhrase_ = -1;
            selectedBus_    = -1;
            selectedMaster_ = false;
            if (onMonSelect) onMonSelect (idx);
        }
        else if (isPhrase)
        {
            clearAll();
            phraseStrips_[static_cast<std::size_t> (idx)]->setSelected (true);
            selectedPhrase_ = idx;
            selectedBus_    = -1;
            selectedMon_    = -1;
            selectedMaster_ = false;
            if (onPhraseSelect) onPhraseSelect (idx);
        }
        else if (isAux)
        {
            clearAll();
            if (idx >= 0 && idx < busStripCount())
                busStrips_[static_cast<std::size_t> (idx)]->setSelected (true);
            selectedPhrase_ = -1;
            selectedBus_    = idx;
            selectedMon_    = -1;
            selectedMaster_ = false;
            if (onBusSelect) onBusSelect (idx);
        }
        else // isMaster
        {
            clearAll();
            if (master_) master_->setSelected (true);
            selectedPhrase_ = -1;
            selectedBus_    = -1;
            selectedMon_    = -1;
            selectedMaster_ = true;
            if (onMasterSelect) onMasterSelect();
        }
    }

    // --- ChannelDetailPanWidTabListener (the pan/width knobs) -----------------
    // Route to bus / master / phrase callbacks based on the active selection;
    // the three are mutually exclusive (enforced in stripChannelSelected).
    void panWidTabPanChanged (int, otto::ui::ChannelType, float pan) override
    {
        if (selectedMaster_)        { if (onMasterPan) onMasterPan (pan); return; }
        if (selectedBus_ >= 0)      { if (onBusPan)    onBusPan (selectedBus_, pan); return; }
        if (onPhrasePan && selectedPhrase_ >= 0) onPhrasePan (selectedPhrase_, pan);
    }
    void panWidTabWidthChanged (int, otto::ui::ChannelType, float width) override
    {
        if (selectedMaster_)        { if (onMasterWidth) onMasterWidth (width); return; }
        if (selectedBus_ >= 0)      { if (onBusWidth)    onBusWidth (selectedBus_, width); return; }
        if (onPhraseWidth && selectedPhrase_ >= 0) onPhraseWidth (selectedPhrase_, width);
    }

    // --- ChannelDetailSendsTabListener (slice U) ------------------------------
    void sendsTabSendChanged (int fxReturnIdx, float level) override
    {
        // Master has no Sends tab (terminal); only buses + phrases route here.
        if (selectedBus_ >= 0)
        {
            if (onBusSendChanged) onBusSendChanged (selectedBus_, fxReturnIdx, level);
            return;
        }
        if (onPhraseSendChanged && selectedPhrase_ >= 0)
            onPhraseSendChanged (selectedPhrase_, fxReturnIdx, level);
    }
    void sendsTabPreFaderToggled (bool preFader) override
    {
        // Pre-fader is a per-channel concept; buses + master ignore.
        if (selectedMaster_ || selectedBus_ >= 0) return;
        if (onPhrasePreFaderToggled && selectedPhrase_ >= 0)
            onPhrasePreFaderToggled (selectedPhrase_, preFader);
    }

    // --- ChannelDetailEQTabListener (slice EC) --------------------------------
    // Route to bus / master / phrase callbacks based on which selection
    // owns the detail panel right now. The three are mutually exclusive.
    void eqTabConfigChanged (const ida::EqConfig& cfg) override
    {
        if (selectedMaster_)
        {
            if (onMasterEqConfigChanged) onMasterEqConfigChanged (cfg);
        }
        else if (selectedBus_ >= 0)
        {
            if (onBusEqConfigChanged) onBusEqConfigChanged (selectedBus_, cfg);
        }
        else if (onPhraseEqConfigChanged && selectedPhrase_ >= 0)
            onPhraseEqConfigChanged (selectedPhrase_, cfg);
    }
    void eqTabRequestSlotAdd() override
    {
        if (selectedMaster_)
        {
            if (onMasterEqSlotAddRequested) onMasterEqSlotAddRequested();
        }
        else if (selectedBus_ >= 0)
        {
            if (onBusEqSlotAddRequested) onBusEqSlotAddRequested (selectedBus_);
        }
        else if (onPhraseEqSlotAddRequested && selectedPhrase_ >= 0)
            onPhraseEqSlotAddRequested (selectedPhrase_);
    }

    // --- ChannelDetailCMPTabListener (slice EC) -------------------------------
    void cmpTabConfigChanged (const ida::CmpConfig& cfg) override
    {
        if (selectedMaster_)
        {
            if (onMasterCmpConfigChanged) onMasterCmpConfigChanged (cfg);
        }
        else if (selectedBus_ >= 0)
        {
            if (onBusCmpConfigChanged) onBusCmpConfigChanged (selectedBus_, cfg);
        }
        else if (onPhraseCmpConfigChanged && selectedPhrase_ >= 0)
            onPhraseCmpConfigChanged (selectedPhrase_, cfg);
    }
    void cmpTabRequestSlotAdd() override
    {
        if (selectedMaster_)
        {
            if (onMasterCmpSlotAddRequested) onMasterCmpSlotAddRequested();
        }
        else if (selectedBus_ >= 0)
        {
            if (onBusCmpSlotAddRequested) onBusCmpSlotAddRequested (selectedBus_);
        }
        else if (onPhraseCmpSlotAddRequested && selectedPhrase_ >= 0)
            onPhraseCmpSlotAddRequested (selectedPhrase_);
    }

private:
    static constexpr int kLongPressMs              = 500;
    static constexpr int kLongPressMoveTolerancePx = 8;
    static constexpr int kDestHeight               = 26;
    static constexpr int kInsHeight                = 26;
    static constexpr int kDetailHeight             = 180;   // matches InputMixerPane

    void timerCallback() override
    {
        stopTimer();
        if (longPressBlank_)
        {
            longPressBlank_ = false;
            showBlankAreaMenu (longPressScreenPos_);
        }
    }

    void showBlankAreaMenu (juce::Point<int> screenPos)
    {
        juce::PopupMenu menu;
        menu.addItem ("Add bus",       [this] { if (onAddBus)       onAddBus(); });
        menu.addItem ("Add FX return", [this] { if (onAddFxReturn) onAddFxReturn(); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
    }

    /// Tick predicate: a choice matches the current destination iff kind +
    /// id agree, and additionally (for HardwareOutput choices) the pair
    /// index agrees. Bus destinations don't carry a meaningful pairIndex.
    static bool destMatches (const DestChoice& choice, const StripDest& cur) noexcept
    {
        if (choice.kind != cur.currentKind || choice.id != cur.currentId) return false;
        if (choice.kind == DestKind::HardwareOutput)
            return choice.pairIndex == cur.currentPairIndex;
        return true;
    }

    void showBusDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= busStripCount()) return;
        if (idx >= static_cast<int> (busChoices_.size())) return;
        const auto& choices = busChoices_[static_cast<std::size_t> (idx)];
        if (choices.empty()) return;
        const auto& cur = busStripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices)
        {
            const bool ticked = destMatches (choice, cur);
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onBusDestinationChosen) onBusDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            busDestButtons_[static_cast<std::size_t> (idx)].get()));
    }

    void showMasterDestinationMenu()
    {
        if (masterChoices_.empty() || masterDestButton_ == nullptr) return;
        juce::PopupMenu menu;
        for (const auto& choice : masterChoices_)
        {
            const bool ticked = destMatches (choice, masterDest_);
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, d] { if (onMasterDestinationChosen) onMasterDestinationChosen (d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            masterDestButton_.get()));
    }

    void showPhraseDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= phraseStripCount()) return;
        if (idx >= static_cast<int> (phraseChoices_.size())) return;
        const auto& choices = phraseChoices_[static_cast<std::size_t> (idx)];
        if (choices.empty()) return;
        const auto& cur = phraseStripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices)
        {
            const bool ticked = destMatches (choice, cur);
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onPhraseDestinationChosen) onPhraseDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            phraseDestButtons_[static_cast<std::size_t> (idx)].get()));
    }

    void showMonDestinationMenu (int idx)
    {
        if (idx < 0 || idx >= monStripCount()) return;
        if (idx >= static_cast<int> (monChoices_.size())) return;
        const auto& choices = monChoices_[static_cast<std::size_t> (idx)];
        if (choices.empty()) return;
        const auto& cur = monStripDests_[static_cast<std::size_t> (idx)];
        juce::PopupMenu menu;
        for (const auto& choice : choices)
        {
            const bool ticked = destMatches (choice, cur);
            const DestChoice d = choice;
            menu.addItem (choice.name, /*enabled*/ true, ticked,
                          [this, idx, d] { if (onMonDestinationChosen) onMonDestinationChosen (idx, d); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            monDestButtons_[static_cast<std::size_t> (idx)].get()));
    }

    /// Per-aux-strip context menu — currently just "Rename…". Triggered by
    /// the StripContextOverlay's right-click or long-press gesture.
    void showBusContextMenu (int idx)
    {
        if (idx < 0 || idx >= busStripCount()) return;
        if (idx >= static_cast<int> (busNameOverlays_.size())) return;
        juce::PopupMenu menu;
        const int who = idx;
        menu.addItem ("Rename…", [this, who]
        {
            if (who >= 0 && who < static_cast<int> (busNameOverlays_.size()))
                busNameOverlays_[static_cast<std::size_t> (who)]->beginRename (
                    busStrips_[static_cast<std::size_t> (who)]->getChannelName());
        });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (
            busNameOverlays_[static_cast<std::size_t> (idx)].get()));
    }

    // Master strip (always present)
    std::unique_ptr<otto::ui::CompactFaderStrip> master_;
    std::unique_ptr<juce::TextButton>            masterIns_;
    std::unique_ptr<juce::TextButton>            masterDestButton_;
    std::vector<DestChoice>                      masterChoices_;
    StripDest                                    masterDest_;

    // Aux bus row (slice 2)
    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>>  busStrips_;
    std::vector<std::unique_ptr<juce::TextButton>>             busDestButtons_;
    std::vector<std::unique_ptr<juce::TextButton>>             busInsButtons_;
    std::vector<std::unique_ptr<ida::app::StripContextOverlay>> busNameOverlays_;
    std::vector<StripDest>                                     busStripDests_;
    std::vector<std::vector<DestChoice>>                       busChoices_;

    // Phrase-channel row (slice 5b) — LEFT-anchored, no name overlay
    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>>  phraseStrips_;
    std::vector<std::unique_ptr<juce::TextButton>>             phraseDestButtons_;
    std::vector<std::unique_ptr<juce::TextButton>>             phraseInsButtons_;
    std::vector<StripDest>                                     phraseStripDests_;
    std::vector<std::vector<DestChoice>>                       phraseChoices_;

    // MON-channel row (V9 §6.3.1 / §7.2) — LEFTMOST band, in input-strip
    // row order. Auto-created peer of phrase channels: appears when an
    // input strip flips MON on, vanishes when MON is off. Same shape as
    // the phrase row.
    std::vector<std::unique_ptr<otto::ui::CompactFaderStrip>>  monStrips_;
    std::vector<std::unique_ptr<juce::TextButton>>             monDestButtons_;
    std::vector<std::unique_ptr<juce::TextButton>>             monInsButtons_;
    std::vector<StripDest>                                     monStripDests_;
    std::vector<std::vector<DestChoice>>                       monChoices_;

    // Tabbed detail panel (slice U + slice EC + slice EC-Polish). One of
    // selectedPhrase_ / selectedBus_ / selectedMaster_ is "active" at a time;
    // others sit at sentinel values. Mutual exclusion is enforced in
    // stripChannelSelected.
    ida::ui::ChannelDetail                                     detailPanel_;
    int                                                        selectedPhrase_ { -1 };
    int                                                        selectedBus_    { -1 };
    int                                                        selectedMon_    { -1 };
    bool                                                       selectedMaster_ { false };
    std::vector<std::unique_ptr<juce::TextButton>>             channelPills_;
    /// Back button — only visible in EQ/CMP full-screen mode. Clicking
    /// it calls deselectAll() (same as Escape).
    std::unique_ptr<juce::TextButton>                          backButton_;
    /// True once initial default-select fired (Master on first run).
    bool                                                       defaultSelectionDone_ { false };

    bool                                                      longPressBlank_ { false };
    juce::Point<int>                                          longPressScreenPos_;

    static constexpr int kNameOverlayHeight = 22;   // strip name band
};

// =============================================================================
// TapesPane — the Tapes tab: an operator-facing management surface for the
// project tape pool (one of the few places tape internals are deliberately
// exposed — names + create/rename/remove). Pure presentation + intent relay:
// it never touches tapePool_/inputMixer_ directly; every mutation goes out
// through onCreate/onRename/onRemove to the MainComponent T3 methods, which own
// pool/mixer/sink consistency. MainComponent pushes the tape list in via
// setTapes() and the capture-overflow diagnostic in via setDroppedBlocks() on
// its 30 Hz timer. A non-zero dropped-block count means the FLAC capture writer
// could not keep up — visible here as a warning rather than a silent failure.
// =============================================================================
class MainComponent::TapesPane final : public juce::Component
{
public:
    /// One pool entry's display state: the tape's id and current name.
    struct TapeInfo { ida::TapeId id; juce::String name; };

    // Intent relays. Set by MainComponent; wired to the T3 pool methods.
    std::function<void()>                              onCreate;
    std::function<void (ida::TapeId, juce::String)> onRename;
    std::function<void (ida::TapeId)>               onRemove;

    TapesPane()
    {
        title_.setText ("Tapes", juce::dontSendNotification);
        title_.setColour (juce::Label::textColourId, otto::Colours::textPrimary);
        title_.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        addAndMakeVisible (title_);

        newButton_.setButtonText ("New tape");
        newButton_.onClick = [this] { if (onCreate) onCreate(); };
        addAndMakeVisible (newButton_);

        dropped_.setColour (juce::Label::textColourId, otto::Colours::textSecondary);
        dropped_.setJustificationType (juce::Justification::centredRight);
        dropped_.setText ("Dropped capture blocks: 0", juce::dontSendNotification);
        addAndMakeVisible (dropped_);
    }

    /// Rebuilds the row list from `infos` (one Row each). Remove is disabled
    /// (a) when only one tape remains so the >=1 pool floor is unbreakable, and
    /// (b) on the primary tape, which is permanent (TapePool/InputMixer/MainComponent
    /// all refuse to remove it). Both must be reflected in the UI or the operator
    /// can click a Remove that silently desyncs pool from mixer.
    void setTapes (const std::vector<TapeInfo>& infos, ida::TapeId primary)
    {
        rows_.clear();
        const bool poolAboveFloor = infos.size() > 1;
        for (const auto& info : infos)
        {
            const bool canRemove = poolAboveFloor && info.id != primary;
            auto row = std::make_unique<Row> (info.id, info.name, canRemove);
            row->onRename = [this] (ida::TapeId id, juce::String n)
            {
                if (onRename) onRename (id, n);
            };
            row->onRemove = [this] (ida::TapeId id) { if (onRemove) onRemove (id); };
            addAndMakeVisible (*row);
            rows_.push_back (std::move (row));
        }
        resized();
    }

    /// Capture-overflow diagnostic. Non-zero = the FLAC writer dropped blocks
    /// (the audio thread out-ran the disk worker) — surfaced, never hidden.
    void setDroppedBlocks (std::uint64_t count)
    {
        if (count == lastDropped_) return;
        lastDropped_ = count;
        dropped_.setText (count == 0
                              ? juce::String ("Dropped capture blocks: 0")
                              : "Dropped capture blocks: " + juce::String (count) + "  (capture overflow)",
                          juce::dontSendNotification);
        dropped_.setColour (juce::Label::textColourId,
                            count == 0 ? otto::Colours::textSecondary : otto::Colours::error);
    }

    void resized() override
    {
        constexpr int kGap = 6, kHeaderH = 32, kRowH = 34;
        auto area = getLocalBounds().reduced (kGap);

        auto header = area.removeFromTop (kHeaderH);
        newButton_.setBounds (header.removeFromRight (110));
        header.removeFromRight (kGap);
        title_.setBounds (header.removeFromLeft (120));
        dropped_.setBounds (header);                 // remaining middle band, right-justified
        area.removeFromTop (kGap);

        for (auto& row : rows_)
        {
            row->setBounds (area.removeFromTop (kRowH));
            area.removeFromTop (kGap);
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (otto::Colours::bg2); }

private:
    // One tape's row: an editable name field (commits on return / focus-loss)
    // and a Remove button (disabled at the >=1 floor). Stateless beyond its id.
    class Row final : public juce::Component
    {
    public:
        std::function<void (ida::TapeId, juce::String)> onRename;
        std::function<void (ida::TapeId)>               onRemove;

        Row (ida::TapeId id, const juce::String& name, bool canRemove) : id_ (id)
        {
            name_.setText (name, juce::dontSendNotification);
            name_.setColour (juce::TextEditor::backgroundColourId, otto::Colours::bg3);
            name_.setColour (juce::TextEditor::textColourId, otto::Colours::textPrimary);
            name_.onReturnKey   = [this] { commitName(); };
            name_.onFocusLost   = [this] { commitName(); };
            addAndMakeVisible (name_);

            remove_.setButtonText ("Remove");
            remove_.setEnabled (canRemove);
            remove_.onClick = [this] { if (onRemove) onRemove (id_); };
            addAndMakeVisible (remove_);
        }

        void resized() override
        {
            constexpr int kGap = 6;
            auto area = getLocalBounds();
            remove_.setBounds (area.removeFromRight (90));
            area.removeFromRight (kGap);
            name_.setBounds (area);
        }

    private:
        void commitName()
        {
            if (committed_) return;
            committed_ = true;
            const auto trimmed = name_.getText().trim();
            if (trimmed.isNotEmpty() && onRename) onRename (id_, trimmed);
        }

        ida::TapeId   id_;
        juce::TextEditor name_;
        juce::TextButton remove_;
        bool             committed_ { false };
    };

    juce::Label                            title_;
    juce::TextButton                       newButton_;
    juce::Label                            dropped_;
    std::uint64_t                          lastDropped_ { std::numeric_limits<std::uint64_t>::max() };
    std::vector<std::unique_ptr<Row>>      rows_;
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
// PluginListBox — one row per scanned descriptor + an "Open editor" button.
// Replaces the S5-era read-only TextEditor descriptor dump (M7 S7).
// Declared at MainComponent scope (sibling to PluginsPane) so the nested-
// nested forward-declaration complexity is avoided.
// =============================================================================
class MainComponent::PluginListBox final : public juce::ListBoxModel
{
public:
    /// Callback fired when a row's "Open editor" button is clicked.
    /// Message-thread only; wired by MainComponent::PluginsPane.
    std::function<void(const PluginDescriptor&)> onOpenEditor;

    /// Called by PluginsPane when hostBinaryPath() availability is known.
    /// When unavailable, all "Open editor" buttons are disabled with a tooltip.
    void setHostBinaryAvailable (bool available)
    {
        hostBinaryAvailable_ = available;
    }

    void setDescriptors (const std::vector<PluginDescriptor>& descriptors)
    {
        descriptors_ = descriptors;
    }

    const std::vector<PluginDescriptor>& descriptors() const noexcept
    {
        return descriptors_;
    }

    bool hostBinaryAvailable() const noexcept { return hostBinaryAvailable_; }

    // ---- juce::ListBoxModel ------------------------------------------------
    int getNumRows() override { return (int) descriptors_.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int /*height*/, bool /*rowIsSelected*/) override
    {
        if (rowNumber < 0 || rowNumber >= (int) descriptors_.size())
            return;
        const auto& d = descriptors_[(std::size_t) rowNumber];
        g.fillAll (rowNumber % 2 ? juce::Colour (0xff1a1a1a)
                                 : juce::Colour (0xff222222));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (d.name) + "   (" + juce::String (d.manufacturer) + ")",
                    8, 4, width - 110, 18, juce::Justification::topLeft);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
        g.drawText (juce::String (d.uniqueId),
                    8, 22, width - 110, 14, juce::Justification::topLeft);
        g.setColour (juce::Colour (0xff808080));
        g.drawText (juce::String (d.filePath),
                    8, 36, width - 110, 14, juce::Justification::topLeft);
    }

    juce::Component* refreshComponentForRow (int rowNumber, bool /*rowIsSelected*/,
                                             juce::Component* existing) override
    {
        if (rowNumber < 0 || rowNumber >= (int) descriptors_.size())
        {
            delete existing;
            return nullptr;
        }

        auto* button = dynamic_cast<RowButton*> (existing);
        if (button == nullptr)
        {
            delete existing;
            button = new RowButton();
        }
        button->setRow (rowNumber);
        button->setEnabled (hostBinaryAvailable_);
        button->setTooltip (hostBinaryAvailable_
            ? juce::String()
            : juce::String ("Open editor unavailable — launch the .app for plug-in hosting"));
        button->onClick = [this, rowNumber]
        {
            if (rowNumber >= 0 && rowNumber < (int) descriptors_.size() && onOpenEditor)
                onOpenEditor (descriptors_[(std::size_t) rowNumber]);
        };
        return button;
    }

    static constexpr int kRowHeight = 56;

private:
    class RowButton final : public juce::TextButton
    {
    public:
        RowButton() : juce::TextButton ("Open editor") {}
        void setRow (int row) { (void) row; /* JUCE re-bounds us each row */ }

        void resized() override
        {
            // I am the row component JUCE positions; on resize, I shrink
            // to a right-aligned 88x24 button slot. JUCE's setBounds
            // short-circuits when bounds don't change, so the self-shrink
            // is idempotent. The descriptor text painted by the model is
            // already laid out behind me (paintListBoxItem reserves
            // width - 110 for text — leaves 110 px for this 88-wide
            // button + an 8-px right gutter at width - 8).
            const auto bounds = getParentComponent() != nullptr
                ? juce::Rectangle<int> (getParentWidth() - 96,
                                        (kRowHeight - 24) / 2,
                                        88, 24)
                : juce::Rectangle<int> (0, 0, 88, 24);
            setBounds (bounds);
        }
    };

    std::vector<PluginDescriptor> descriptors_;
    bool                          hostBinaryAvailable_ { false };
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

       #if JUCE_MAC
        scanGlobalButton_.setButtonText ("Scan /Library/Audio/Plug-Ins");
        addAndMakeVisible (scanGlobalButton_);

        scanUserButton_.setButtonText ("Scan ~/Library/Audio/Plug-Ins");
        addAndMakeVisible (scanUserButton_);

        // Debug-only: bypasses PluginScanner (which doesn't register CLAP)
        // and opens the M7 fixture .clap directly. Lets the operator
        // eyes-on the CARemoteLayer pixel path without needing a real
        // installed CLAP plug-in.
        openSyntheticButton_.setButtonText ("Open synthetic test plug-in (debug)");
        openSyntheticButton_.setColour (juce::TextButton::buttonColourId,
                                        juce::Colour (0xff334455));
        addAndMakeVisible (openSyntheticButton_);
       #endif

        scanStatusLabel_.setMinimumHorizontalScale (1.0f);
        scanStatusLabel_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (scanStatusLabel_);

        descriptorsListBox_.setRowHeight (PluginListBox::kRowHeight);
        descriptorsListBox_.setColour (juce::ListBox::backgroundColourId,
                                       juce::Colour (0xff1a1a1a));
        addAndMakeVisible (descriptorsListBox_);

        failedFilesLabel_.setMinimumHorizontalScale (1.0f);
        failedFilesLabel_.setColour (juce::Label::textColourId,
                                     juce::Colour (0xffff8888));
        failedFilesLabel_.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
        addAndMakeVisible (failedFilesLabel_);
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
        listBoxModel_.setDescriptors (descriptors);
        descriptorsListBox_.updateContent();

        if (failed.empty())
        {
            failedFilesLabel_.setText (juce::String(), juce::dontSendNotification);
        }
        else
        {
            juce::String text = "Failed to load:\n";
            for (const auto& f : failed)
                text << "  " << juce::String (f) << "\n";
            failedFilesLabel_.setText (text, juce::dontSendNotification);
        }

        // resized() inspects failedFilesLabel_.getText().isEmpty() to
        // decide whether to reserve a 60 px strip at the bottom; rerun
        // it now so the layout reflects the new content immediately
        // (without waiting for the parent to resize).
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        headerLabel_.setBounds (area.removeFromTop (24));
        formatsLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        scanButton_.setBounds (area.removeFromTop (28).reduced (0, 2));
       #if JUCE_MAC
        scanGlobalButton_.setBounds (area.removeFromTop (28).reduced (0, 2));
        scanUserButton_.setBounds   (area.removeFromTop (28).reduced (0, 2));
        openSyntheticButton_.setBounds (area.removeFromTop (28).reduced (0, 2));
       #endif
        scanStatusLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        // Failed-files label takes the bottom strip; list box fills the rest.
        auto failedArea = area.removeFromBottom (
            failedFilesLabel_.getText().isEmpty() ? 0 : 60);
        failedFilesLabel_.setBounds (failedArea);
        descriptorsListBox_.setBounds (area);
    }

private:
    PluginListBox    listBoxModel_;
    juce::ListBox    descriptorsListBox_ { juce::String(), &listBoxModel_ };
    juce::Label      failedFilesLabel_;
    juce::Label      headerLabel_;
    juce::Label      formatsLabel_;
    juce::TextButton scanButton_;
   #if JUCE_MAC
    juce::TextButton scanGlobalButton_;
    juce::TextButton scanUserButton_;
    juce::TextButton openSyntheticButton_;
   #endif
    juce::Label      scanStatusLabel_;
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
// MainComponent helpers
// =============================================================================

juce::File MainComponent::hostBinaryPath() const
{
    // Inside a .app bundle this is Contents/MacOS/IDA; the helper
    // sits in the same MacOS directory. Outside a bundle (dev-loop test
    // runs from build/...), the sibling doesn't exist and the returned
    // juce::File reports existsAsFile() == false — callers must check.
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto sibling = self.getParentDirectory().getChildFile ("ida_plugin_host");
    return sibling;
}

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

    // Audio callback drives engine-side mixers. Mixers are constructed
    // before the callback so the raw pointers handed to setInputMixer /
    // setOutputMixer are live for the callback's entire lifetime.
    //
    // V9 §7.2: MON is per channel (Off ↔ On) via the button on each input
    // strip. InputMixer::attachOutputMixer wires the seam — MON-on auto-
    // creates an OutputMixer channel whose audio source reads the matching
    // input's post-strip stereo buffer (whitepaper V9 §5.2 / §7.2). The V8
    // identity-auto-wire and the global monitoring gate are both gone.
    inputMixer_      = std::make_unique<InputMixer>();
    outputMixer_     = std::make_unique<OutputMixer>();
    inputMixer_->attachOutputMixer (outputMixer_.get());

    // M6 Session 2 — construct the truthfulness bus before the audio callback
    // so the setter below sees a live pointer, and pre-reserve the drain
    // buffer to its documented worst-case payload so `drain()` on the timer
    // tick never reallocates (the bus header documents that drain may throw
    // bad_alloc otherwise).
    notificationBus_ = std::make_unique<NotificationBus>();
    notificationDrainBuffer_.reserve (kCategoryCount * NotificationBus::kRingCapacity);
    inputMixer_->setNotificationBus (notificationBus_.get());

    // Tape subsystem slice 3 — bind the live per-tape FLAC recorder. Sample rate
    // is set from the device setup (and on device change) below; 256 queue slots
    // cover the worst-case touched-tapes-per-block burst with headroom.
    flacTapeSink_ = std::make_unique<ida::FlacTapeSink> (
        tapesDirectory(),
        audioDeviceManager_.getAudioDeviceSetup().sampleRate,
        256);

    // TAPECOLOR Slice 2 — wrap the FLAC sink in the per-tape coloring
    // decorator. For tapes with mode == BeforeWrite, the decorator applies
    // TAPECOLOR before forwarding bytes downstream; for None/AfterRead it's
    // a bit-identical passthrough. The default for every tape is None, so
    // wiring this on costs nothing at runtime until the operator opts in.
    const auto deviceSetupForColor = audioDeviceManager_.getAudioDeviceSetup();
    tapeColoringSink_ = std::make_unique<ida::TapeColoringSink> (
        flacTapeSink_.get(),
        deviceSetupForColor.sampleRate > 0.0 ? deviceSetupForColor.sampleRate : 48000.0,
        deviceSetupForColor.bufferSize  > 0  ? deviceSetupForColor.bufferSize  : 512);

    inputMixer_->setTapeSink (tapeColoringSink_.get());

    // Tape-UI slice — TapePool is the single source of truth for which tapes exist.
    // Mirror it into the input mixer's routing terminals at startup.
    ida::mirrorTapePool (tapePool_, *inputMixer_);
    // And mirror the same set into the TAPECOLOR decorator so each pool tape
    // owns its own adapter from the moment it exists. Modes follow whatever
    // the descriptor carries (None for a default TapePool; honors any value
    // a loaded project supplies).
    for (const auto& t : tapePool_.tapes())
    {
        tapeColoringSink_->addTape (t.id);
        tapeColoringSink_->setMode (t.id, t.tapeColor);
    }

    audioCallback_   = std::make_unique<AudioCallback> (engineConfig_);
    audioCallback_->setLmc (lmc_.get());
    audioCallback_->setInputMixer  (inputMixer_.get());
    audioCallback_->setOutputMixer (outputMixer_.get());
    audioCallback_->setNotificationBus (notificationBus_.get());
    // M7 S7 — supervisor restart / permanent-bypass events post via
    // INotificationSink. Without this wiring the operator opens an
    // editor, the child silently dies, and nothing surfaces in the
    // Preparation tab's notifications.
    effectChainHost_.setNotificationSink (notificationBus_.get());

    // P7 T3a I-2 — give both mixers the audio-thread effect-chain host so
    // any internal-FX or plugin entry the operator drops into a bus chain
    // actually dispatches at runtime. Without this wiring, EffectChain
    // entries on Input- or Output-side buses are held in the data model
    // but never reach the host's pumpSlot dispatch (silent no-op).
    inputMixer_->setEffectChainHost  (&effectChainHost_);
    outputMixer_->setEffectChainHost (&effectChainHost_);
    // M8 S2 — scan-progress + state-save-timeout notifications surface in
    // the Preparation tab's notification history. Bound once here, before
    // any scan call; the scanner posts only when a sink is present.
    pluginScanner_.setNotificationSink (notificationBus_.get());
    // TapeWriter::setNotificationBus deferred — MainComponent doesn't
    // currently construct a TapeWriter (per M6 S2 audit). When TapeWriter
    // joins the owned app graph (M11 IAF wiring, likely), add a parallel
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

    // M8 S6 — validate the calibration sidecar's checksum before the calibration
    // reaches the audio callback. An absent or corrupt table recovers to identity,
    // re-persists a {identity, recalibration-pending} sidecar so the future
    // loopback engine knows a real measurement is owed, and warns the operator
    // through the existing NotificationBus (live since line 826). The real
    // loopback DSP recalibration is deferred to its own session (todo.md).
    {
        const juce::File sidecar = calibrationSidecarFile();
        const std::string contents = sidecar.existsAsFile()
            ? sidecar.loadFileAsString().toStdString()
            : std::string {};
        const auto result = parseAndValidateCalibration (contents);
        if (result.status == CalibrationLoadStatus::Ok)
        {
            calibration_ = result.document.calibration;
        }
        else
        {
            const auto dirResult = sidecar.getParentDirectory().createDirectory();
            const bool wrote = dirResult.wasOk()
                && sidecar.replaceWithText (juce::String (serializeCalibration (
                       { AudioDeviceCalibration::identity(), /*recalibrationPending*/ true })));
            if (! wrote)
                juce::Logger::writeToLog (
                    "calibration: could not persist recovery sidecar at "
                    + sidecar.getFullPathName()
                    + (dirResult.failed() ? " — " + dirResult.getErrorMessage()
                                          : juce::String()));
            if (notificationBus_)
                postCalibrationRecoveryNotification (result.status, *notificationBus_);
        }
    }

    audioCallback_->setCalibration (&calibration_);

    const auto deviceInitError = audioDeviceManager_.initialiseWithDefaultDevices (
        /*numInputChannelsNeeded*/  kDefaultStereoChannels,
        /*numOutputChannelsNeeded*/ kDefaultStereoChannels);
    if (deviceInitError.isNotEmpty())
        audioDeviceLastError_ = deviceInitError;
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    // Tape slice 3 — update the sink with the device's actual sample rate now
    // that the device is open. The sink was constructed with whatever
    // getAudioDeviceSetup returned before initialise (often 0); this call
    // guarantees the worker sees a non-zero rate before the first audio block.
    if (flacTapeSink_ != nullptr)
        flacTapeSink_->setSampleRate (audioDeviceManager_.getAudioDeviceSetup().sampleRate);

    // P7 T3a-C — prepare any internal-FX adapters bound to the OOP host so
    // their first audio-thread `process` returns true rather than the
    // unprepared-miss `false`. Today no internal-FX is bound at startup (the
    // insert-chain UI hasn't shipped), but adapter binds that happen later
    // (via Bus::setEffectChain when an Internal slot lands) will read these
    // values back out of the host and auto-prepare. Re-issued from
    // rebuildInputStrips() / rebuildBusStrips() on sample-rate change, with
    // the audio callback already detached there.
    {
        const auto setup = audioDeviceManager_.getAudioDeviceSetup();
        if (setup.sampleRate > 0.0 && setup.bufferSize > 0)
            effectChainHost_.prepareInternalFx (setup.sampleRate, setup.bufferSize);
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
    preparationPane_->timelineView().onPillContextMenuRequested =
        [this] (ConstituentId wrapperId)
    {
        const auto& root = *undoStack_.current();
        const auto path = findWrapperPath (root, wrapperId);
        if (! path) return;
        auto target = root.children()[(*path)[0]];
        for (std::size_t depth = 1; depth < path->size(); ++depth)
            target = target->children()[(*path)[depth]];

        if (! ida::isPlacementWrapper (*target)) return;

        juce::PopupMenu menu;
        menu.addItem ("Vary this one", [this, wrapperId] { forkPlacement (wrapperId); });
        menu.showMenuAsync (juce::PopupMenu::Options{}.withMousePosition());
    };
    preparationPane_->setStatus ("");
    tabs_.addTab ("Preparation", juce::Colours::black, preparationPane_.get(), false);

    // --- Settings tab ---
    settingsPane_ = std::make_unique<SettingsPane> (audioDeviceManager_);
    tabs_.addTab ("Settings", juce::Colours::black, settingsPane_.get(), false);

    // --- Input Mixer tab (white paper Part VI) ---
    // Each active device-input stereo pair becomes one InputPair, stereo by
    // default (RME convention). rebuildInputStrips() registers the engine
    // channels + builds the pane strips from inputPairs_; the right-click
    // split/collapse toggle flips a pair between one stereo strip and two
    // mono-source strips. renderInputGraph (AudioCallback Step 2) is now the
    // single live input path — strip processing, metering, routing, and tape
    // delivery all run in one pass.
    {
        int numInputs = 0;
        if (auto* dev = audioDeviceManager_.getCurrentAudioDevice())
            numInputs = dev->getActiveInputChannels().countNumberOfSetBits();
        if (numInputs < 1)
            numInputs = kDefaultStereoChannels;   // no device → a default stereo strip

        // Full stereo pairs, then any odd leftover (or a single-input device like
        // a built-in mono mic) as ONE mono-source strip — leftCh == rightCh marks
        // a single device channel that dual-monos to both sides and cannot split.
        int ch = 0;
        for (; ch + 1 < numInputs; ch += 2)
            inputPairs_.push_back (InputPair { ch, ch + 1, /*stereo*/ true });
        if (ch < numInputs)
            // A single physical input (built-in mono mic, or an odd leftover):
            // default to one stereo (dual-mono) strip; toggling to mono yields
            // two independently-pannable strips of the same source.
            inputPairs_.push_back (InputPair { ch, ch, /*stereo*/ true });

        inputMixerPane_ = std::make_unique<InputMixerPane>();
        inputMixerPane_->onGain = [this] (int idx, float gain)
        {
            if (auto* s = inputStripAt (idx)) s->setGain (gain);
        };
        inputMixerPane_->onMute = [this] (int idx, bool muted)
        {
            if (idx >= 0 && idx < static_cast<int> (inputStripMuted_.size()))
                inputStripMuted_[static_cast<std::size_t> (idx)] = muted;
            recomputeInputStripMutes();
        };
        inputMixerPane_->onSolo = [this] (int idx, bool soloed)
        {
            if (idx >= 0 && idx < static_cast<int> (inputStripSoloed_.size()))
                inputStripSoloed_[static_cast<std::size_t> (idx)] = soloed;
            recomputeInputStripMutes();
        };
        inputMixerPane_->onToggleStereoMono = [this] (int idx) { toggleInputPairStereo (idx); };
        // Selection reveals the detail panel loaded with the strip's current
        // values. The engine pan is normalized [0,1] (0.5 = center); the knob is
        // industry-standard [-1,+1], so map both ways at the boundary.
        inputMixerPane_->onSelect = [this] (int idx)
        {
            if (auto* s = inputStripAt (idx))
            {
                auto sends = collectInputSendsView (idx);
                const auto eqProbe  = collectInputEqView  (idx);
                const auto cmpProbe = collectInputCmpView (idx);
                inputMixerPane_->showDetailFor (idx,
                                                s->pan() * 2.0f - 1.0f,
                                                s->width(),
                                                std::move (sends.fxReturns),
                                                std::move (sends.sendLevels),
                                                sends.preFader,
                                                eqProbe.config,  eqProbe.hasSlot,
                                                cmpProbe.config, cmpProbe.hasSlot);
            }
        };
        inputMixerPane_->onPan = [this] (int idx, float pan)
        {
            if (auto* s = inputStripAt (idx)) s->setPan ((pan + 1.0f) * 0.5f);
        };
        inputMixerPane_->onWidth = [this] (int idx, float width)
        {
            if (auto* s = inputStripAt (idx)) s->setWidth (width);
        };
        // Sends-tab gestures: translate the (stripIdx, fxReturnIdx) the tab fired
        // back to the engine's (ChannelId, BusId) shape. fxReturnIdx indexes into
        // the snapshot we built in collectInputSendsView at showDetailFor time;
        // we re-walk the InputMixer's FxReturn buses in the same order to recover
        // the BusId. Skipping the live walk would risk a stale index after the
        // operator adds/removes an FX return between the show and the gesture.
        inputMixerPane_->onSendChanged =
            [this] (int stripIdx, int fxReturnIdx, float level)
        {
            if (inputMixer_ == nullptr) return;
            if (stripIdx < 0
                || stripIdx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];
            int seen = 0;
            const int n = inputMixer_->busCount();
            for (int i = 0; i < n; ++i)
            {
                if (inputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                if (seen == fxReturnIdx)
                {
                    inputMixer_->setChannelSend (chId, inputMixer_->busIdAt (i), level);
                    return;
                }
                ++seen;
            }
        };
        inputMixerPane_->onPreFaderToggled =
            [this] (int stripIdx, bool preFader)
        {
            if (inputMixer_ == nullptr) return;
            if (stripIdx < 0
                || stripIdx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];
            inputMixer_->setChannelSendIsPreFader (chId, preFader);
        };
        // Slice EC — EQ + CMP tab gestures on the input pane. Config changes
        // route through the host's typed setter, bracketed in the standard
        // audio-callback detach pattern (same shape as setEffectChain calls
        // elsewhere). The "+ Add" empty-state button appends an Internal
        // entry to the strip's EffectChain so the slot becomes present and
        // the host auto-binds an adapter on the slot-sweep.
        inputMixerPane_->onEqConfigChanged =
            [this] (int stripIdx, ida::EqConfig cfg)
        {
            const auto probe = collectInputEqView (stripIdx);
            if (! probe.hasSlot) return;
            if (stripIdx < 0
                || stripIdx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (chId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        inputMixerPane_->onCmpConfigChanged =
            [this] (int stripIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectInputCmpView (stripIdx);
            if (! probe.hasSlot) return;
            if (stripIdx < 0
                || stripIdx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (chId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        inputMixerPane_->onEqSlotAddRequested = [this] (int stripIdx)
        {
            auto* strip = inputStripAt (stripIdx);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            // Refresh the detail panel so the EQ tab flips out of empty state.
            if (inputMixerPane_) inputMixerPane_->onSelect (stripIdx);
        };
        inputMixerPane_->onCmpSlotAddRequested = [this] (int stripIdx)
        {
            auto* strip = inputStripAt (stripIdx);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (inputMixerPane_) inputMixerPane_->onSelect (stripIdx);
        };
        // Routing the channel main-out to a tape is a TOPOLOGY mutation, not a
        // strip-local edit: setChannelMainOutToTape -> MixerGraph::setMainOut ->
        // recomputeOrder(), which clears and rebuilds order_ — the very vector the
        // audio thread reads via evaluationOrder() on the hot path (MixerGraph.h
        // forbids reading it mid-mutation). So this MUST be bracketed by
        // remove/addAudioCallback, exactly like removeTape and the load path.
        // (onGain/onPan above touch only strip-local atomics — no order_ mutation —
        // which is why they correctly skip the bracket.)
        inputMixerPane_->onDestinationChosen = [this] (int idx, InputMixerPane::DestChoice dest)
        {
            if (idx < 0 || idx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (idx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case InputMixerPane::DestKind::Tape:
                    inputMixer_->setChannelMainOutToTape (chId, ida::TapeId (dest.id));
                    break;
                case InputMixerPane::DestKind::Bus:
                    inputMixer_->setChannelMainOutToBus (chId, ida::BusId (dest.id));
                    break;
                case InputMixerPane::DestKind::HardwareOutput:
                    inputMixer_->setChannelMainOutToHardwareOutput (chId);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshInputMixer();
        };
        // Bus-row routing — same TOPOLOGY-mutation bracket as the channel picker
        // above (setBusMainOutTo* -> MixerGraph::setMainOut -> recomputeOrder()).
        inputMixerPane_->onBusDestinationChosen = [this] (int busIdx, InputMixerPane::DestChoice dest)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case InputMixerPane::DestKind::Tape:
                    inputMixer_->setBusMainOutToTape (busId, ida::TapeId (dest.id));
                    break;
                case InputMixerPane::DestKind::Bus:
                    inputMixer_->setBusMainOutToBus (busId, ida::BusId (dest.id));
                    break;
                case InputMixerPane::DestKind::HardwareOutput:
                    inputMixer_->setBusMainOutToHardwareOutput (busId);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshInputMixer();
        };
        // Blank-area "Add tape" gesture — same creation path + auto-name as the
        // Tapes-tab "New tape" button (T5), so both surfaces stay consistent.
        inputMixerPane_->onAddTape = [this] { addNextTape(); };
        inputMixerPane_->onBusGain = [this] (int busIdx, float gain)
        {
            if (busIdx >= 0 && busIdx < static_cast<int> (busStripIds_.size()))
                if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (busIdx)]))
                    bus->setGain (gain);
        };
        inputMixerPane_->onBusMute = [this] (int busIdx, bool muted)
        {
            if (busIdx >= 0 && busIdx < static_cast<int> (busStripIds_.size()))
                if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (busIdx)]))
                    bus->setMuted (muted);
        };
        inputMixerPane_->onBusSolo = [] (int busIdx, bool soloed)
        {
            // Bus solo is not yet an engine concept on the input side; reflect it
            // on the strip only (no-op on the mix) until a bus-solo slice lands.
            juce::ignoreUnused (busIdx, soloed);
        };
        inputMixerPane_->onAddBus = [this]
        {
            if (inputMixer_->busCount() >= ida::InputMixer::kMaxInputBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->addBus (ida::BusConfig{ /*channelCount*/ 2,
                                 "Bus " + std::to_string (inputMixer_->busCount() + 1),
                                 ida::BusKind::Bus });
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildBusStrips();
            refreshInputDestinations();   // a new bus is a new channel destination
        };
        inputMixerPane_->onAddFxReturn = [this]
        {
            if (inputMixer_->busCount() >= ida::InputMixer::kMaxInputBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->addFxReturn ("FX " + std::to_string (inputMixer_->busCount() + 1));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildBusStrips();
            refreshInputDestinations();
        };
        // P7 T5 slice 5 — INS button → InsertChainPopup. The helpers translate
        // each popup callback into a detach/setEffectChain/re-attach cycle on
        // the strip or bus; slice 3 made setEffectChain re-propagate both the
        // adapter binding and the bypass flag through every Internal slot, so a
        // single uniform call covers add / remove / reorder / bypass.
        inputMixerPane_->onInputInsertChainClicked = [this] (int idx)
        {
            openInsertChainPopupForChannel (idx);
        };
        // V9 Slice 5 — MON button gesture. setChannelMonitorMode now also
        // auto-creates the OutputMixer channel that owns the post-strip tap
        // (Slice 3), so bracket the audio callback the same way the routing
        // pickers do (`onDestinationChosen` above).
        inputMixerPane_->onMonitorModeChanged = [this] (int idx, ida::MonitorMode mode)
        {
            if (idx < 0 || idx >= static_cast<int> (inputStripChannelIds_.size())) return;
            const auto chId = inputStripChannelIds_[static_cast<std::size_t> (idx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            inputMixer_->setChannelMonitorMode (chId, mode);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());

            // V9 §6.3.1 / §7.2: MON-on auto-mints an OutputMixer channel
            // (Slice 3) with its own ChannelStrip (this slice's T1) — so the
            // Output Mixer pane needs to gain/lose a visible strip in lockstep.
            refreshOutputMixerMonChannels();
        };
        inputMixerPane_->onBusRename = [this] (int busIdx, juce::String newName)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            const auto id = busStripIds_[static_cast<std::size_t> (busIdx)];
            // renameBus is message-thread only; bracket the audio callback to be
            // symmetric with every other input-side config mutator on this pane.
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            const bool ok = inputMixer_->renameBus (id, newName.toStdString());
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (! ok) return;
            inputMixerPane_->updateBusName (busIdx, newName);
            // Other strips' destination pickers may reference this bus by its
            // old name — refresh so their labels and menu entries update too.
            refreshInputDestinations();
        };
        inputMixerPane_->onBusInsertChainClicked = [this] (int busIdx)
        {
            openInsertChainPopupForBus (busIdx);
        };

        // Slice EC-Polish — bus / FX-return selection opens the detail panel
        // with EQ + CMP tabs only (no pan on stereo bus; no per-bus send
        // model yet). Config-change + slot-add callbacks mirror the
        // channel-strip wiring, addressing the bus by BusId.value() as
        // nodeKey (matching the Bus's own host-dispatch contract).
        inputMixerPane_->onBusSelect = [this] (int busIdx)
        {
            const auto eqProbe  = collectInputBusEqView  (busIdx);
            const auto cmpProbe = collectInputBusCmpView (busIdx);
            if (inputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size()))
                return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            auto* bus = inputMixer_->busForId (busId);
            const float pan01 = bus != nullptr ? bus->pan()   : 0.5f;
            const float width = bus != nullptr ? bus->width() : 1.0f;
            const bool  isFxReturn = bus != nullptr
                                      && bus->config().kind == ida::BusKind::FxReturn;

            // Collect bus-to-bus send levels into each FX return (skipping
            // self). Buses CAN send to FX returns via setBusSend (cycle-
            // detected by the routing graph), so the Sends tab is useful
            // even when the source is a bus.
            std::vector<ida::ui::FxReturnInfo> fxReturns;
            std::vector<float>                 sendLevels;
            const int busTotal = inputMixer_->busCount();
            for (int i = 0; i < busTotal; ++i)
            {
                if (inputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                const auto fxId = inputMixer_->busIdAt (i);
                if (fxId == busId) continue;   // no self-send
                auto* fxBus = inputMixer_->busForId (fxId);
                if (fxBus == nullptr) continue;
                fxReturns.push_back ({ juce::String (fxBus->config().name),
                                       ida::palette::hueForId (fxId.value()) });
                sendLevels.push_back (inputMixer_->busSendLevel (busId, fxId));
            }
            inputMixerPane_->showBusDetailFor (busIdx,
                                               pan01 * 2.0f - 1.0f,
                                               width,
                                               std::move (fxReturns),
                                               std::move (sendLevels),
                                               eqProbe.config,  eqProbe.hasSlot,
                                               cmpProbe.config, cmpProbe.hasSlot,
                                               isFxReturn);
        };
        inputMixerPane_->onBusPan = [this] (int busIdx, float pan)
        {
            if (inputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size()))
                return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            if (auto* bus = inputMixer_->busForId (busId))
                bus->setPan ((pan + 1.0f) * 0.5f);
        };
        inputMixerPane_->onBusWidth = [this] (int busIdx, float width)
        {
            if (inputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size()))
                return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            if (auto* bus = inputMixer_->busForId (busId))
                bus->setWidth (width);
        };
        inputMixerPane_->onBusSendChanged =
            [this] (int busIdx, int fxReturnIdx, float level)
        {
            if (inputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size()))
                return;
            const auto sourceId = busStripIds_[static_cast<std::size_t> (busIdx)];
            // Re-walk the FX-return list in the same order onBusSelect did
            // so fxReturnIdx maps deterministically to a BusId.
            int seen = 0;
            const int n = inputMixer_->busCount();
            for (int i = 0; i < n; ++i)
            {
                if (inputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                const auto fxId = inputMixer_->busIdAt (i);
                if (fxId == sourceId) continue;
                if (seen == fxReturnIdx)
                {
                    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
                    inputMixer_->setBusSend (sourceId, fxId, level);
                    audioDeviceManager_.addAudioCallback (audioCallback_.get());
                    return;
                }
                ++seen;
            }
        };
        inputMixerPane_->onBusEqConfigChanged =
            [this] (int busIdx, ida::EqConfig cfg)
        {
            const auto probe = collectInputBusEqView (busIdx);
            if (! probe.hasSlot) return;
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (busId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        inputMixerPane_->onBusCmpConfigChanged =
            [this] (int busIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectInputBusCmpView (busIdx);
            if (! probe.hasSlot) return;
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (busId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        inputMixerPane_->onBusEqSlotAddRequested = [this] (int busIdx)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            auto* bus = inputMixer_->busForId (
                busStripIds_[static_cast<std::size_t> (busIdx)]);
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            // Re-show with the freshly-bound EQ slot so the empty state
            // collapses into the wired editor.
            inputMixerPane_->onBusSelect (busIdx);
        };
        inputMixerPane_->onBusCmpSlotAddRequested = [this] (int busIdx)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
            auto* bus = inputMixer_->busForId (
                busStripIds_[static_cast<std::size_t> (busIdx)]);
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            inputMixerPane_->onBusSelect (busIdx);
        };

        tabs_.addTab ("Input Mixer", juce::Colours::black, inputMixerPane_.get(), false);

        rebuildInputStrips();
        rebuildBusStrips();

        // Output Mixer tab (whitepaper §5.2/§6.6/§7.1 — the mixdown console).
        // Slice 1: master bus strip only. Fader → Bus::setGain, mute →
        // Bus::setMuted, INS → openInsertChainPopupForMasterBus. Channels +
        // aux buses land in subsequent slices once the operator UX for adding
        // them is wired (per project_tapecolor_per_bus_model: IDA's bus count
        // is dynamic, operator-driven; nothing auto-seeds).
        outputMixerPane_ = std::make_unique<OutputMixerPane>();
        outputMixerPane_->onMasterGain = [this] (float gain)
        {
            if (auto* b = outputMixer_->busForId (ida::BusId{ 0 })) b->setGain (gain);
        };
        outputMixerPane_->onMasterMute = [this] (bool muted)
        {
            if (auto* b = outputMixer_->busForId (ida::BusId{ 0 })) b->setMuted (muted);
        };
        outputMixerPane_->onMasterInsertChainClicked = [this]
        {
            openInsertChainPopupForMasterBus();
        };
        // Aux bus row (slice 2) — operator builds buses dynamically per
        // project_tapecolor_per_bus_model; nothing auto-seeds.
        outputMixerPane_->onAddBus = [this]
        {
            if (outputMixer_->busCount() >= ida::OutputMixer::kMaxBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            outputMixer_->addBus (ida::BusConfig{ /*channelCount*/ 2,
                                  "Bus " + std::to_string (outputMixer_->busCount()),
                                  ida::BusKind::Bus });
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildOutputBusStrips();
            refreshOutputDestinations();
        };
        // Mirror InputMixerPane's "Add FX return" gesture so phrase-channel
        // strips have FX-return targets to route into via the Sends tab.
        // Engine surface is E1 (BusKind::FxReturn on OutputMixer); UI surface
        // was missing — the Sends tab on phrase strips was always empty.
        outputMixerPane_->onAddFxReturn = [this]
        {
            if (outputMixer_->busCount() >= ida::OutputMixer::kMaxBuses) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            outputMixer_->addBus (ida::BusConfig{ /*channelCount*/ 2,
                                  "FX " + std::to_string (outputMixer_->busCount()),
                                  ida::BusKind::FxReturn });
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            rebuildOutputBusStrips();
            refreshOutputDestinations();
        };
        outputMixerPane_->onBusGain = [this] (int busIdx, float gain)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            if (auto* b = outputMixer_->busForId (outputBusStripIds_[static_cast<std::size_t> (busIdx)]))
                b->setGain (gain);
        };
        outputMixerPane_->onBusMute = [this] (int busIdx, bool muted)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            if (auto* b = outputMixer_->busForId (outputBusStripIds_[static_cast<std::size_t> (busIdx)]))
                b->setMuted (muted);
        };
        outputMixerPane_->onBusInsertChainClicked = [this] (int busIdx)
        {
            openInsertChainPopupForOutputBus (busIdx);
        };
        outputMixerPane_->onBusDestinationChosen = [this] (int busIdx,
                                                           OutputMixerPane::DestChoice dest)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            const auto fromId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            // Topology mutation — bracket the audio callback the same way the
            // input destination picker does (graph recomputeOrder is non-atomic).
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case OutputMixerPane::DestKind::Bus:
                    outputMixer_->routeBusToBus (fromId, ida::BusId{ dest.id });
                    break;
                case OutputMixerPane::DestKind::HardwareOutput:
                    outputMixer_->setBusMainOutToHardwareOutput (fromId, dest.pairIndex);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshOutputDestinations();
        };
        outputMixerPane_->onMasterDestinationChosen = [this] (OutputMixerPane::DestChoice dest)
        {
            // Master is always HardwareOutput-kind from setMasterDestination();
            // the switch defends against a future surface that adds bus entries.
            if (dest.kind != OutputMixerPane::DestKind::HardwareOutput) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            outputMixer_->setBusMainOutToHardwareOutput (ida::BusId{ 0 }, dest.pairIndex);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshOutputDestinations();
        };
        outputMixerPane_->onBusRename = [this] (int busIdx, juce::String newName)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            const auto id = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            // renameBus is message-thread only; bracket the audio callback to
            // be symmetric with every other config mutator on this pane.
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            const bool ok = outputMixer_->renameBus (id, newName.toStdString());
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (! ok) return;
            outputMixerPane_->updateBusName (busIdx, newName);
            // Other strips' destination pickers may reference this bus by its
            // old name — refresh so their labels and menu entries update too.
            refreshOutputDestinations();
        };

        // --- Phrase-channel relays (slice 5b) --------------------------------
        // Phrase gain/mute drive the engine ChannelStrip (mirror of MON wiring
        // at the resolveMonChannelId block below). INS-button popup is a
        // separate slice.
        outputMixerPane_->onPhraseInsertChainClicked = [] (int)        {};
        outputMixerPane_->onPhraseDestinationChosen = [this]
            (int phraseIdx, OutputMixerPane::DestChoice dest)
        {
            // Resolve the phrase row to its OutputChannelId. Out-of-range guards
            // defend against a stale callback firing during a mid-refresh race.
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size())) return;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return;
            const auto chId = it->second;

            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            // Slice E3: routeChannelToBus is now radio-aware for Bus-kind
            // targets — it sets the new main-out, zeros every other Bus-
            // kind send (master / aux), and keeps FX-return sends intact.
            // The manual zero loop slice 5b needed has gone away.
            switch (dest.kind)
            {
                case OutputMixerPane::DestKind::Bus:
                    outputMixer_->routeChannelToBus (chId, ida::BusId { dest.id }, 1.0f);
                    break;
                case OutputMixerPane::DestKind::HardwareOutput:
                    outputMixer_->setChannelMainOutToHardwareOutput (chId, dest.pairIndex);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshOutputDestinations();
        };

        // Pan/width detail-panel wiring (slice 5b polish). Resolve phraseIdx
        // to the engine's ChannelStrip via the OutputChannelId binding, then
        // read or write atomic [0,1]/[0,2] state. Pan converts between the
        // engine's [0,1] storage and the panel's [-1,+1] knob domain.
        auto resolvePhraseStrip = [this] (int phraseIdx) -> ChannelStrip<SignalType::Audio>*
        {
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size()))
                return nullptr;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return nullptr;
            return outputMixer_->audioStripForChannel (it->second);
        };
        outputMixerPane_->onPhraseSelect = [this, resolvePhraseStrip] (int phraseIdx)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx))
            {
                auto sends = collectOutputSendsView (phraseIdx);
                const auto eqProbe  = collectOutputEqView  (phraseIdx);
                const auto cmpProbe = collectOutputCmpView (phraseIdx);
                outputMixerPane_->showPhraseDetailFor (phraseIdx,
                                                       s->pan() * 2.0f - 1.0f,
                                                       s->width(),
                                                       std::move (sends.fxReturns),
                                                       std::move (sends.sendLevels),
                                                       sends.preFader,
                                                       eqProbe.config,  eqProbe.hasSlot,
                                                       cmpProbe.config, cmpProbe.hasSlot);
            }
        };
        outputMixerPane_->onPhrasePan = [resolvePhraseStrip] (int phraseIdx, float pan)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setPan ((pan + 1.0f) * 0.5f);
        };
        outputMixerPane_->onPhraseWidth = [resolvePhraseStrip] (int phraseIdx, float width)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setWidth (width);
        };
        outputMixerPane_->onPhraseGain = [resolvePhraseStrip] (int phraseIdx, float gainLinear)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setGain (gainLinear);
        };
        outputMixerPane_->onPhraseMute = [resolvePhraseStrip] (int phraseIdx, bool muted)
        {
            if (auto* s = resolvePhraseStrip (phraseIdx)) s->setMuted (muted);
        };
        // Sends-tab gestures on the output side. Same fxReturnIdx → BusId walk
        // as the input pane, but `routeChannelToBus` (additive on FxReturn-kind
        // targets per E3) is the engine call rather than setChannelSend.
        outputMixerPane_->onPhraseSendChanged =
            [this] (int phraseIdx, int fxReturnIdx, float level)
        {
            if (outputMixer_ == nullptr) return;
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size())) return;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return;
            const auto outChId = it->second;
            int seen = 0;
            const int n = outputMixer_->busCount();
            for (int i = 0; i < n; ++i)
            {
                if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                if (seen == fxReturnIdx)
                {
                    outputMixer_->routeChannelToBus (outChId,
                                                     outputMixer_->busIdAt (i),
                                                     level);
                    return;
                }
                ++seen;
            }
        };
        outputMixerPane_->onPhrasePreFaderToggled =
            [this] (int phraseIdx, bool preFader)
        {
            if (outputMixer_ == nullptr) return;
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size())) return;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return;
            outputMixer_->setChannelSendIsPreFader (it->second, preFader);
        };
        // Slice EC — EQ + CMP gestures on phrase strips. Mirrors the input
        // pane's wiring, but the strip lookup goes ConstituentId →
        // OutputChannelId via phraseChannelByConstituent_.
        auto resolvePhraseChannelId = [this] (int phraseIdx) -> std::optional<ida::OutputChannelId>
        {
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size()))
                return std::nullopt;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return std::nullopt;
            return it->second;
        };
        outputMixerPane_->onPhraseEqConfigChanged =
            [this] (int phraseIdx, ida::EqConfig cfg)
        {
            const auto probe = collectOutputEqView (phraseIdx);
            if (! probe.hasSlot) return;
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size())) return;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (it->second.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onPhraseCmpConfigChanged =
            [this] (int phraseIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectOutputCmpView (phraseIdx);
            if (! probe.hasSlot) return;
            if (phraseIdx < 0
                || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size())) return;
            const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
            const auto it  = phraseChannelByConstituent_.find (cid.value());
            if (it == phraseChannelByConstituent_.end()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (it->second.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onPhraseEqSlotAddRequested =
            [this, resolvePhraseChannelId] (int phraseIdx)
        {
            auto chOpt = resolvePhraseChannelId (phraseIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onPhraseSelect (phraseIdx);
        };
        outputMixerPane_->onPhraseCmpSlotAddRequested =
            [this, resolvePhraseChannelId] (int phraseIdx)
        {
            auto chOpt = resolvePhraseChannelId (phraseIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onPhraseSelect (phraseIdx);
        };

        // V9 MON strip relays. Mirror of the phrase wiring above, but the
        // strip lookup goes mon-row → input ChannelId →
        // InputMixer::channelMonitorOutputChannel → OutputChannelId. The
        // phrase Gain/Mute are currently stubbed because the phrase render
        // path's strip wiring is a follow-up slice; MON wires both because
        // those are the operator's primary mix surface for the live MON
        // signal (whitepaper V9 §6.3.1 / §7.2).
        auto resolveMonChannelId = [this] (int monIdx) -> std::optional<ida::OutputChannelId>
        {
            if (inputMixer_ == nullptr) return std::nullopt;
            if (monIdx < 0
                || monIdx >= static_cast<int> (monStripInputChannelIds_.size()))
                return std::nullopt;
            const auto chId = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
            return inputMixer_->channelMonitorOutputChannel (chId);
        };
        outputMixerPane_->onMonGain = [this, resolveMonChannelId] (int monIdx, float gainLinear)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*chOpt))
                strip->setGain (gainLinear);
        };
        outputMixerPane_->onMonMute = [this, resolveMonChannelId] (int monIdx, bool muted)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*chOpt))
                strip->setMuted (muted);
        };
        outputMixerPane_->onMonInsertChainClicked = [] (int) {};   // parity with onPhraseInsertChainClicked stub
        outputMixerPane_->onMonDestinationChosen = [this, resolveMonChannelId]
            (int monIdx, OutputMixerPane::DestChoice dest)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            const auto outCh = *chOpt;

            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            switch (dest.kind)
            {
                case OutputMixerPane::DestKind::Bus:
                    outputMixer_->routeChannelToBus (outCh, ida::BusId { dest.id }, 1.0f);
                    break;
                case OutputMixerPane::DestKind::HardwareOutput:
                    outputMixer_->setChannelMainOutToHardwareOutput (outCh, dest.pairIndex);
                    break;
            }
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            refreshOutputDestinations();
        };
        outputMixerPane_->onMonPan = [this, resolveMonChannelId] (int monIdx, float pan)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*chOpt))
                strip->setPan ((pan + 1.0f) * 0.5f);   // [-1,+1] → [0,1]
        };
        outputMixerPane_->onMonWidth = [this, resolveMonChannelId] (int monIdx, float width)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            if (auto* strip = outputMixer_->audioStripForChannel (*chOpt))
                strip->setWidth (width);
        };
        outputMixerPane_->onMonSendChanged = [this, resolveMonChannelId]
            (int monIdx, int fxReturnIdx, float level)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            const auto outCh = *chOpt;
            int seen = 0;
            const int n = outputMixer_->busCount();
            for (int i = 0; i < n; ++i)
            {
                if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                if (seen == fxReturnIdx)
                {
                    outputMixer_->routeChannelToBus (outCh,
                                                     outputMixer_->busIdAt (i),
                                                     level);
                    return;
                }
                ++seen;
            }
        };
        outputMixerPane_->onMonPreFaderToggled = [this, resolveMonChannelId]
            (int monIdx, bool preFader)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            outputMixer_->setChannelSendIsPreFader (*chOpt, preFader);
        };
        // MON detail panel binding — identical surface to phrase (Pan/Width
        // + Sends + EQ + CMP) per the 2026-05-24 operator design lock.
        // Mirror of the phrase detail-panel wiring (lines ~4200+ / ~4280+),
        // with monIdx → input ChannelId → OutputMixer channel resolution
        // via inputMixer_->channelMonitorOutputChannel.
        outputMixerPane_->onMonSelect = [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* s = outputMixer_->audioStripForChannel (*chOpt);
            if (s == nullptr) return;
            auto sends = collectMonSendsView (monIdx);
            const auto eqProbe  = collectMonEqView  (monIdx);
            const auto cmpProbe = collectMonCmpView (monIdx);
            outputMixerPane_->showMonDetailFor (monIdx,
                                                s->pan() * 2.0f - 1.0f,
                                                s->width(),
                                                std::move (sends.fxReturns),
                                                std::move (sends.sendLevels),
                                                sends.preFader,
                                                eqProbe.config,  eqProbe.hasSlot,
                                                cmpProbe.config, cmpProbe.hasSlot);
        };
        outputMixerPane_->onMonEqConfigChanged =
            [this, resolveMonChannelId] (int monIdx, ida::EqConfig cfg)
        {
            const auto probe = collectMonEqView (monIdx);
            if (! probe.hasSlot) return;
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (chOpt->value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMonCmpConfigChanged =
            [this, resolveMonChannelId] (int monIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectMonCmpView (monIdx);
            if (! probe.hasSlot) return;
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value()) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (chOpt->value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMonEqSlotAddRequested =
            [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onMonSelect (monIdx);
        };
        outputMixerPane_->onMonCmpSlotAddRequested =
            [this, resolveMonChannelId] (int monIdx)
        {
            auto chOpt = resolveMonChannelId (monIdx);
            if (! chOpt.has_value() || outputMixer_ == nullptr) return;
            auto* strip = outputMixer_->audioStripForChannel (*chOpt);
            if (strip == nullptr) return;
            auto chain = strip->effectChain()
                              .withAppended (ida::EffectChainEntry::makeInternal (
                                                  ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            strip->setEffectChain (chain);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            if (outputMixerPane_) outputMixerPane_->onMonSelect (monIdx);
        };

        // Slice EC-Polish — aux-bus + master selection opens the EQ/CMP-only
        // detail panel on the output pane. Same pattern as the input pane;
        // addressing differs (outputBusStripIds_ for aux buses, BusId{0} for
        // master).
        outputMixerPane_->onBusSelect = [this] (int busIdx)
        {
            const auto eqProbe  = collectOutputBusEqView  (busIdx);
            const auto cmpProbe = collectOutputBusCmpView (busIdx);
            if (outputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size()))
                return;
            const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            auto* bus = outputMixer_->busForId (busId);
            const float pan01 = bus != nullptr ? bus->pan()   : 0.5f;
            const float width = bus != nullptr ? bus->width() : 1.0f;
            const bool  isFxReturn = bus != nullptr
                                      && bus->config().kind == ida::BusKind::FxReturn;

            // Collect bus-to-bus send levels into each FX return (skipping
            // self). Mirror of the InputMixerPane onBusSelect wiring — the
            // OutputMixer now ships level-controlled bus→FX-return sends via
            // `setBusSend` (2026-05-24 mixer-symmetry slice). FX-return
            // selection still hides the Sends tab uniformly here; the
            // Edit-FX swap is the next slice.
            std::vector<ida::ui::FxReturnInfo> fxReturns;
            std::vector<float>                 sendLevels;
            const int busTotal = outputMixer_->busCount();
            for (int i = 0; i < busTotal; ++i)
            {
                if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                const auto fxId = outputMixer_->busIdAt (i);
                if (fxId == busId) continue;   // no self-send
                auto* fxBus = outputMixer_->busForId (fxId);
                if (fxBus == nullptr) continue;
                fxReturns.push_back ({ juce::String (fxBus->config().name),
                                       ida::palette::hueForId (fxId.value()) });
                sendLevels.push_back (outputMixer_->busSendLevel (busId, fxId));
            }
            outputMixerPane_->showBusDetailFor (busIdx,
                                                pan01 * 2.0f - 1.0f, width,
                                                std::move (fxReturns),
                                                std::move (sendLevels),
                                                eqProbe.config,  eqProbe.hasSlot,
                                                cmpProbe.config, cmpProbe.hasSlot,
                                                isFxReturn);
        };
        outputMixerPane_->onBusSendChanged =
            [this] (int busIdx, int fxReturnIdx, float level)
        {
            if (outputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size()))
                return;
            const auto sourceId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            // Re-walk the FX-return list in the same order onBusSelect did
            // so fxReturnIdx maps deterministically to a BusId. Mirror of
            // the InputMixerPane onBusSendChanged wiring.
            int seen = 0;
            const int n = outputMixer_->busCount();
            for (int i = 0; i < n; ++i)
            {
                if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
                const auto fxId = outputMixer_->busIdAt (i);
                if (fxId == sourceId) continue;
                if (seen == fxReturnIdx)
                {
                    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
                    outputMixer_->setBusSend (sourceId, fxId, level);
                    audioDeviceManager_.addAudioCallback (audioCallback_.get());
                    return;
                }
                ++seen;
            }
        };
        outputMixerPane_->onBusPan = [this] (int busIdx, float pan)
        {
            if (outputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size()))
                return;
            const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            if (auto* bus = outputMixer_->busForId (busId))
                bus->setPan ((pan + 1.0f) * 0.5f);
        };
        outputMixerPane_->onBusWidth = [this] (int busIdx, float width)
        {
            if (outputMixer_ == nullptr
                || busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size()))
                return;
            const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            if (auto* bus = outputMixer_->busForId (busId))
                bus->setWidth (width);
        };
        outputMixerPane_->onBusEqConfigChanged =
            [this] (int busIdx, ida::EqConfig cfg)
        {
            const auto probe = collectOutputBusEqView (busIdx);
            if (! probe.hasSlot) return;
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (busId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onBusCmpConfigChanged =
            [this] (int busIdx, ida::CmpConfig cfg)
        {
            const auto probe = collectOutputBusCmpView (busIdx);
            if (! probe.hasSlot) return;
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (busId.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onBusEqSlotAddRequested = [this] (int busIdx)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            auto* bus = outputMixer_->busForId (
                outputBusStripIds_[static_cast<std::size_t> (busIdx)]);
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            outputMixerPane_->onBusSelect (busIdx);
        };
        outputMixerPane_->onBusCmpSlotAddRequested = [this] (int busIdx)
        {
            if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
            auto* bus = outputMixer_->busForId (
                outputBusStripIds_[static_cast<std::size_t> (busIdx)]);
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            outputMixerPane_->onBusSelect (busIdx);
        };

        // Master strip on the output pane is BusId{0} — fixed.
        outputMixerPane_->onMasterSelect = [this]
        {
            const auto eqProbe  = collectOutputMasterEqView();
            const auto cmpProbe = collectOutputMasterCmpView();
            auto* master = (outputMixer_ != nullptr)
                            ? outputMixer_->busForId (ida::BusId{0})
                            : nullptr;
            const float pan01 = master != nullptr ? master->pan()   : 0.5f;
            const float width = master != nullptr ? master->width() : 1.0f;
            outputMixerPane_->showMasterDetailFor (pan01 * 2.0f - 1.0f, width,
                                                   eqProbe.config,  eqProbe.hasSlot,
                                                   cmpProbe.config, cmpProbe.hasSlot);
        };
        outputMixerPane_->onMasterPan = [this] (float pan)
        {
            if (outputMixer_ == nullptr) return;
            if (auto* master = outputMixer_->busForId (ida::BusId{0}))
                master->setPan ((pan + 1.0f) * 0.5f);
        };
        outputMixerPane_->onMasterWidth = [this] (float width)
        {
            if (outputMixer_ == nullptr) return;
            if (auto* master = outputMixer_->busForId (ida::BusId{0}))
                master->setWidth (width);
        };
        outputMixerPane_->onMasterEqConfigChanged = [this] (ida::EqConfig cfg)
        {
            const auto probe = collectOutputMasterEqView();
            if (! probe.hasSlot) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalEqConfigAt (ida::BusId{0}.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMasterCmpConfigChanged = [this] (ida::CmpConfig cfg)
        {
            const auto probe = collectOutputMasterCmpView();
            if (! probe.hasSlot) return;
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            effectChainHost_.setInternalCmpConfigAt (ida::BusId{0}.value(), probe.slotIdx, cfg);
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
        };
        outputMixerPane_->onMasterEqSlotAddRequested = [this]
        {
            if (outputMixer_ == nullptr) return;
            auto* bus = outputMixer_->busForId (ida::BusId{0});
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kEq));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            outputMixerPane_->onMasterSelect();
        };
        outputMixerPane_->onMasterCmpSlotAddRequested = [this]
        {
            if (outputMixer_ == nullptr) return;
            auto* bus = outputMixer_->busForId (ida::BusId{0});
            if (bus == nullptr) return;
            auto chain = bus->effectChain()
                            .withAppended (ida::EffectChainEntry::makeInternal (
                                               ida::InternalFxId::kCmp));
            audioDeviceManager_.removeAudioCallback (audioCallback_.get());
            bus->setEffectChain (std::move (chain));
            audioDeviceManager_.addAudioCallback (audioCallback_.get());
            outputMixerPane_->onMasterSelect();
        };

        tabs_.addTab ("Output Mixer", juce::Colours::black, outputMixerPane_.get(), false);
        rebuildOutputBusStrips();          // no-ops at construction time (zero aux buses)
        refreshOutputDestinations();

        // Default-select Master on first launch — operator sees a populated
        // detail panel instead of the blank pane. All callbacks are now wired
        // and the output mixer is initialized, so collectOutputMaster*View()
        // probes are safe to call.
        outputMixerPane_->triggerDefaultMasterSelection();
    }

    // --- Tapes tab (tape-UI T5 — the operator-facing tape-pool management
    // surface). Every gesture relays out to the T3 pool methods, which keep
    // pool/mixer/sink consistent and call refreshTapesPane() to push the new
    // list back. ---
    {
        tapesPane_ = std::make_unique<TapesPane>();
        tapesPane_->onCreate = [this] { addNextTape(); };
        tapesPane_->onRename = [this] (ida::TapeId id, juce::String name)
        {
            renameTape (id, name);
        };
        tapesPane_->onRemove = [this] (ida::TapeId id) { removeTape (id); };
        tabs_.addTab ("Tapes", juce::Colours::black, tapesPane_.get(), false);

        refreshTapesPane();
    }

    // --- Plugins tab ---
    pluginsPane_ = std::make_unique<PluginsPane>();
    pluginsPane_->scanButton_.onClick = [this] { chooseFolderAndScan(); };
   #if JUCE_MAC
    pluginsPane_->scanGlobalButton_.onClick    = [this] { scanGlobalPluginFolder(); };
    pluginsPane_->scanUserButton_.onClick      = [this] { scanUserPluginFolder(); };
    pluginsPane_->openSyntheticButton_.onClick = [this] { openSyntheticTestPlugin(); };
   #endif
    pluginsPane_->listBoxModel_.onOpenEditor =
        [this] (const PluginDescriptor& d) { openPluginEditor (d); };
    pluginsPane_->listBoxModel_.setHostBinaryAvailable (
        hostBinaryPath().existsAsFile());
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
    // M7 S9 — close all open plug-in editors BEFORE the chain host
    // destructs. The child processes own their NSWindows; tearing down
    // each slot via configureBus(empty) makes the supervisor reap them
    // cleanly and the OS reclaims the windows.
    for (const auto busId : openEditorBusIds_)
        effectChainHost_.configureBus (
            busId, ida::EffectChain{}, hostBinaryPath(), juce::File{});
    openEditorBusIds_.clear();

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

    // Tape slice 3 — keep the FLAC sink tracking the live device sample rate.
    // The rate is otherwise latched once during construction (rebuildInputStrips),
    // which can run BEFORE audioDeviceAboutToStart has set currentSampleRate_ — a
    // 0 there makes the sink's lazy writer drop every block (no file). Refreshing
    // on this 30 Hz tick guarantees the sink sees the real rate within ~33 ms of
    // the device starting (or changing); setSampleRate is an idempotent atomic
    // store and only affects writers created AFTER it, so this never rewrites an
    // open tape's header.
    if (flacTapeSink_ != nullptr && rate > 0.0)
        flacTapeSink_->setSampleRate (rate);

    // tape-UI T5 — surface the capture-overflow diagnostic on the Tapes tab.
    // droppedBlockCount() is a monotonic counter the FLAC sink bumps when the
    // audio thread out-runs the disk worker; non-zero means lost capture.
    if (tapesPane_ != nullptr && flacTapeSink_ != nullptr)
        tapesPane_->setDroppedBlocks (flacTapeSink_->droppedBlockCount());

    // M6 Sessions 2+3 — drain the engine→UI truthfulness channel on the same
    // 30 Hz cadence as the diagnostics refresh, append drained entries onto
    // the rolling history deque, trim the front to `kNotificationHistorySize`
    // so the surface holds the most-recent N, and push the deque into the
    // Preparation pane for re-render. The drain buffer is pre-reserved in
    // the ctor so `drain()` does not reallocate here; deque ops on the
    // history are O(1) per entry. The pane re-renders every tick so the
    // "(Δt ago)" timestamps age naturally even when no new notifications
    // arrive (cheap — 20 entries × 30Hz on the message thread).
    if (notificationBus_ != nullptr)
    {
        notificationBus_->drain (notificationDrainBuffer_);
        for (const auto& n : notificationDrainBuffer_)
        {
            notificationHistory_.push_back (n);
            if (notificationHistory_.size() > kNotificationHistorySize)
                notificationHistory_.pop_front();
        }
        if (preparationPane_ != nullptr)
            preparationPane_->setNotifications (notificationHistory_);
    }

    refreshInputMixer();
    refreshOutputMixer();
    refreshDiagnostics();
}

void MainComponent::refreshPerformance()
{
    const Rational t = playheadValueToLmc (playhead_.getValue());
    performanceView_.setState (selectPerformanceView (*undoStack_.current(),
                                                      demo_.sessionToLmc, t));
}

ChannelStrip<SignalType::Audio>* MainComponent::inputStripAt (int index) noexcept
{
    if (index < 0 || index >= static_cast<int> (inputStripChannelIds_.size()))
        return nullptr;
    auto* chain = inputMixer_->processingChainFor (
        inputStripChannelIds_[static_cast<std::size_t> (index)]);
    return dynamic_cast<ChannelStrip<SignalType::Audio>*> (chain);
}

MainComponent::ChannelSendsView
MainComponent::collectInputSendsView (int stripIdx) const
{
    ChannelSendsView view;
    if (inputMixer_ == nullptr) return view;
    if (stripIdx < 0 || stripIdx >= static_cast<int> (inputStripChannelIds_.size()))
        return view;

    const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];
    const int n = inputMixer_->busCount();
    for (int i = 0; i < n; ++i)
    {
        if (inputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
        const auto busId = inputMixer_->busIdAt (i);
        auto* bus = inputMixer_->busForId (busId);
        if (bus == nullptr) continue;
        view.fxReturns.push_back ({ juce::String (bus->config().name),
                                    ida::palette::hueForId (busId.value()) });
        view.sendLevels.push_back (inputMixer_->channelSendLevel (chId, busId));
    }
    view.preFader = inputMixer_->channelSendIsPreFader (chId);
    return view;
}

MainComponent::ChannelSendsView
MainComponent::collectOutputSendsView (int phraseIdx) const
{
    ChannelSendsView view;
    if (outputMixer_ == nullptr) return view;
    if (phraseIdx < 0
        || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size()))
        return view;
    const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
    const auto it  = phraseChannelByConstituent_.find (cid.value());
    if (it == phraseChannelByConstituent_.end()) return view;
    const auto outChId = it->second;

    const int n = outputMixer_->busCount();
    for (int i = 0; i < n; ++i)
    {
        if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
        const auto busId = outputMixer_->busIdAt (i);
        auto* bus = outputMixer_->busForId (busId);
        if (bus == nullptr) continue;
        view.fxReturns.push_back ({ juce::String (bus->config().name),
                                    ida::palette::hueForId (busId.value()) });
        view.sendLevels.push_back (outputMixer_->sendLevelFor (outChId, busId));
    }
    view.preFader = outputMixer_->channelSendIsPreFader (outChId);
    return view;
}

namespace
{
    /// Walk an EffectChain for the first Internal slot with the given id.
    /// Returns the slot index or nullopt. Used by the slice EC probes.
    std::optional<std::size_t> findInternalSlot (const ida::EffectChain& chain,
                                                  ida::InternalFxId        id)
    {
        const auto& entries = chain.entries();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].kind       == ida::EffectChainSlotKind::Internal
             && entries[i].internalId == id)
                return i;
        }
        return std::nullopt;
    }
}

MainComponent::ChannelFxProbe
MainComponent::collectInputEqView (int stripIdx) const
{
    ChannelFxProbe probe;
    if (stripIdx < 0 || stripIdx >= static_cast<int> (inputStripChannelIds_.size()))
        return probe;
    auto* strip = const_cast<MainComponent*> (this)->inputStripAt (stripIdx);
    if (strip == nullptr) return probe;
    const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (chId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectInputCmpView (int stripIdx) const
{
    ChannelCmpProbe probe;
    if (stripIdx < 0 || stripIdx >= static_cast<int> (inputStripChannelIds_.size()))
        return probe;
    auto* strip = const_cast<MainComponent*> (this)->inputStripAt (stripIdx);
    if (strip == nullptr) return probe;
    const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (chId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelFxProbe
MainComponent::collectOutputEqView (int phraseIdx) const
{
    ChannelFxProbe probe;
    if (outputMixer_ == nullptr) return probe;
    if (phraseIdx < 0
        || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size()))
        return probe;
    const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
    const auto it  = phraseChannelByConstituent_.find (cid.value());
    if (it == phraseChannelByConstituent_.end()) return probe;
    const auto outChId = it->second;
    auto* strip = outputMixer_->audioStripForChannel (outChId);
    if (strip == nullptr) return probe;

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (outChId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectOutputCmpView (int phraseIdx) const
{
    ChannelCmpProbe probe;
    if (outputMixer_ == nullptr) return probe;
    if (phraseIdx < 0
        || phraseIdx >= static_cast<int> (phraseStripConstituentIds_.size()))
        return probe;
    const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (phraseIdx)];
    const auto it  = phraseChannelByConstituent_.find (cid.value());
    if (it == phraseChannelByConstituent_.end()) return probe;
    const auto outChId = it->second;
    auto* strip = outputMixer_->audioStripForChannel (outChId);
    if (strip == nullptr) return probe;

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (outChId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

// =============================================================================
// MON-strip probes — mirror of the phrase collectors above, but the row →
// OutputChannelId resolver goes monIdx → input ChannelId →
// InputMixer::channelMonitorOutputChannel. The MON-output channel is owned by
// the OutputMixer (auto-minted by setChannelMonitorMode(On)), so the EQ/CMP
// and send lookups use the same outputMixer_/effectChainHost_ surfaces as
// phrase strips.
// =============================================================================

MainComponent::ChannelSendsView
MainComponent::collectMonSendsView (int monIdx) const
{
    ChannelSendsView view;
    if (outputMixer_ == nullptr || inputMixer_ == nullptr) return view;
    if (monIdx < 0
        || monIdx >= static_cast<int> (monStripInputChannelIds_.size())) return view;
    const auto chId  = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
    const auto outOpt = inputMixer_->channelMonitorOutputChannel (chId);
    if (! outOpt.has_value()) return view;
    const auto outChId = *outOpt;

    const int n = outputMixer_->busCount();
    for (int i = 0; i < n; ++i)
    {
        if (outputMixer_->busKindAt (i) != ida::BusKind::FxReturn) continue;
        const auto busId = outputMixer_->busIdAt (i);
        auto* bus = outputMixer_->busForId (busId);
        if (bus == nullptr) continue;
        view.fxReturns.push_back ({ juce::String (bus->config().name),
                                    ida::palette::hueForId (busId.value()) });
        view.sendLevels.push_back (outputMixer_->sendLevelFor (outChId, busId));
    }
    view.preFader = outputMixer_->channelSendIsPreFader (outChId);
    return view;
}

MainComponent::ChannelFxProbe
MainComponent::collectMonEqView (int monIdx) const
{
    ChannelFxProbe probe;
    if (outputMixer_ == nullptr || inputMixer_ == nullptr) return probe;
    if (monIdx < 0
        || monIdx >= static_cast<int> (monStripInputChannelIds_.size())) return probe;
    const auto chId  = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
    const auto outOpt = inputMixer_->channelMonitorOutputChannel (chId);
    if (! outOpt.has_value()) return probe;
    const auto outChId = *outOpt;
    auto* strip = outputMixer_->audioStripForChannel (outChId);
    if (strip == nullptr) return probe;

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (outChId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectMonCmpView (int monIdx) const
{
    ChannelCmpProbe probe;
    if (outputMixer_ == nullptr || inputMixer_ == nullptr) return probe;
    if (monIdx < 0
        || monIdx >= static_cast<int> (monStripInputChannelIds_.size())) return probe;
    const auto chId  = monStripInputChannelIds_[static_cast<std::size_t> (monIdx)];
    const auto outOpt = inputMixer_->channelMonitorOutputChannel (chId);
    if (! outOpt.has_value()) return probe;
    const auto outChId = *outOpt;
    auto* strip = outputMixer_->audioStripForChannel (outChId);
    if (strip == nullptr) return probe;

    const auto slotOpt = findInternalSlot (strip->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (outChId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

// =============================================================================
// Slice EC-Polish — bus + master EQ/CMP probes
// =============================================================================
//
// The bus probes mirror the channel-strip probes (findInternalSlot on the
// bus's EffectChain, then internalEqConfigAt/internalCmpConfigAt keyed by
// BusId.value() — the same nodeKey the Bus itself dispatches with).

MainComponent::ChannelFxProbe
MainComponent::collectInputBusEqView (int busIdx) const
{
    ChannelFxProbe probe;
    if (inputMixer_ == nullptr) return probe;
    if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return probe;
    const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = inputMixer_->busForId (busId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (busId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectInputBusCmpView (int busIdx) const
{
    ChannelCmpProbe probe;
    if (inputMixer_ == nullptr) return probe;
    if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return probe;
    const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = inputMixer_->busForId (busId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (busId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelFxProbe
MainComponent::collectOutputBusEqView (int busIdx) const
{
    ChannelFxProbe probe;
    if (outputMixer_ == nullptr) return probe;
    if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return probe;
    const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = outputMixer_->busForId (busId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (busId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectOutputBusCmpView (int busIdx) const
{
    ChannelCmpProbe probe;
    if (outputMixer_ == nullptr) return probe;
    if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return probe;
    const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = outputMixer_->busForId (busId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (busId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelFxProbe
MainComponent::collectOutputMasterEqView() const
{
    ChannelFxProbe probe;
    if (outputMixer_ == nullptr) return probe;
    const auto masterId = ida::BusId{ 0 };
    auto* bus = outputMixer_->busForId (masterId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kEq);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalEqConfigAt (masterId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

MainComponent::ChannelCmpProbe
MainComponent::collectOutputMasterCmpView() const
{
    ChannelCmpProbe probe;
    if (outputMixer_ == nullptr) return probe;
    const auto masterId = ida::BusId{ 0 };
    auto* bus = outputMixer_->busForId (masterId);
    if (bus == nullptr) return probe;
    const auto slotOpt = findInternalSlot (bus->effectChain(), ida::InternalFxId::kCmp);
    if (! slotOpt.has_value()) return probe;
    probe.slotIdx = *slotOpt;
    probe.hasSlot = true;
    if (auto cfg = effectChainHost_.internalCmpConfigAt (masterId.value(), probe.slotIdx))
        probe.config = *cfg;
    return probe;
}

namespace
{
    /// Grow `chain` (immutably, via withAppended) with default-Empty entries
    /// until `slot` is in range — the precondition `withReplaced` / `withMoved`
    /// require. Caller-supplied `slot` is clamped to kMaxSlots-1 internally; an
    /// over-range slot becomes a no-op.
    ida::EffectChain growChainToSlot (ida::EffectChain chain, std::size_t slot)
    {
        const auto cap = ida::EffectChain::kMaxSlots;
        const auto target = std::min (slot, cap - 1);
        while (chain.size() <= target && chain.size() < cap)
            chain = chain.withAppended (ida::EffectChainEntry{});
        return chain;
    }
}

void MainComponent::openInsertChainPopupForChannel (int stripIdx)
{
    auto* strip = inputStripAt (stripIdx);
    if (strip == nullptr || inputMixerPane_ == nullptr) return;
    if (stripIdx < 0 || stripIdx >= static_cast<int> (inputStripChannelIds_.size())) return;
    const auto chId = inputStripChannelIds_[static_cast<std::size_t> (stripIdx)];

    auto popup = std::make_unique<ida::InsertChainPopup>();
    popup->setInitialChain (strip->effectChain());

    // The popup mutates its own internal SlotState[]; we maintain a parallel
    // EffectChain copy that is the actual engine truth (round-trips through
    // save/load via Bus / ChannelStrip persistence). Each callback applies the
    // delta then pushes the chain back through the engine API inside an audio
    // detach/re-attach bracket — slice 3's setEffectChain sweep re-binds every
    // Internal slot AND re-asserts bypass per slot.
    auto chainRef = std::make_shared<ida::EffectChain> (strip->effectChain());

    auto apply = [this, chId, chainRef]
    {
        // The strip lookup must succeed at apply-time (the channel is still
        // present); it can disappear if the operator rebuilds inputs mid-popup.
        // Bail without touching the audio thread if so.
        ida::ChannelStrip<ida::SignalType::Audio>* s = nullptr;
        for (int i = 0; i < static_cast<int> (inputStripChannelIds_.size()); ++i)
            if (inputStripChannelIds_[static_cast<std::size_t> (i)] == chId)
                s = inputStripAt (i);
        if (s == nullptr) return;
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
        s->setEffectChain (*chainRef);
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
    };

    popup->setOnSlotChanged ([chainRef, apply] (std::size_t slot,
                                                std::optional<ida::InternalFxId> id)
    {
        auto updated = growChainToSlot (*chainRef, slot);
        if (slot >= updated.size()) return;   // chain full or out of range
        ida::EffectChainEntry entry;
        if (id.has_value()) entry = ida::EffectChainEntry::makeInternal (*id);
        *chainRef = updated.withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotBypassToggled ([chainRef, apply] (std::size_t slot, bool bypassed)
    {
        if (slot >= chainRef->size()) return;   // bypass on never-populated slot
        auto entry = chainRef->at (slot);
        entry.bypassed = bypassed;
        *chainRef = chainRef->withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotsReordered ([chainRef, apply] (std::size_t from, std::size_t to)
    {
        const auto maxSlot = std::max (from, to);
        auto updated = growChainToSlot (*chainRef, maxSlot);
        if (from >= updated.size() || to >= updated.size()) return;
        *chainRef = updated.withMoved (from, to);
        apply();
    });

    popup->setOnClose ([]{});   // CallOutBox manages its own teardown

    popup->setSize (static_cast<int> (otto::Sizing::kMenuMinWidth),
                    static_cast<int> (ida::EffectChain::kMaxSlots)
                    * static_cast<int> (otto::Sizing::kMenuRowHeight));

    const auto target = inputMixerPane_->inputInsButtonScreenArea (stripIdx);
    juce::CallOutBox::launchAsynchronously (std::move (popup), target, nullptr);
}

void MainComponent::openInsertChainPopupForBus (int busIdx)
{
    if (inputMixerPane_ == nullptr) return;
    if (busIdx < 0 || busIdx >= static_cast<int> (busStripIds_.size())) return;
    const auto busId = busStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = inputMixer_->busForId (busId);
    if (bus == nullptr) return;

    auto popup = std::make_unique<ida::InsertChainPopup>();
    popup->setInitialChain (bus->effectChain());

    auto chainRef = std::make_shared<ida::EffectChain> (bus->effectChain());

    auto apply = [this, busId, chainRef]
    {
        auto* b = inputMixer_->busForId (busId);
        if (b == nullptr) return;
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
        b->setEffectChain (*chainRef);
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
    };

    popup->setOnSlotChanged ([chainRef, apply] (std::size_t slot,
                                                std::optional<ida::InternalFxId> id)
    {
        auto updated = growChainToSlot (*chainRef, slot);
        if (slot >= updated.size()) return;
        ida::EffectChainEntry entry;
        if (id.has_value()) entry = ida::EffectChainEntry::makeInternal (*id);
        *chainRef = updated.withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotBypassToggled ([chainRef, apply] (std::size_t slot, bool bypassed)
    {
        if (slot >= chainRef->size()) return;
        auto entry = chainRef->at (slot);
        entry.bypassed = bypassed;
        *chainRef = chainRef->withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotsReordered ([chainRef, apply] (std::size_t from, std::size_t to)
    {
        const auto maxSlot = std::max (from, to);
        auto updated = growChainToSlot (*chainRef, maxSlot);
        if (from >= updated.size() || to >= updated.size()) return;
        *chainRef = updated.withMoved (from, to);
        apply();
    });

    popup->setOnClose ([]{});

    popup->setSize (static_cast<int> (otto::Sizing::kMenuMinWidth),
                    static_cast<int> (ida::EffectChain::kMaxSlots)
                    * static_cast<int> (otto::Sizing::kMenuRowHeight));

    const auto target = inputMixerPane_->busInsButtonScreenArea (busIdx);
    juce::CallOutBox::launchAsynchronously (std::move (popup), target, nullptr);
}

void MainComponent::openInsertChainPopupForMasterBus()
{
    if (outputMixerPane_ == nullptr) return;
    auto* bus = outputMixer_->busForId (ida::BusId{ 0 });
    if (bus == nullptr) return;

    auto popup = std::make_unique<ida::InsertChainPopup>();
    popup->setInitialChain (bus->effectChain());

    auto chainRef = std::make_shared<ida::EffectChain> (bus->effectChain());

    auto apply = [this, chainRef]
    {
        auto* b = outputMixer_->busForId (ida::BusId{ 0 });
        if (b == nullptr) return;
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
        b->setEffectChain (*chainRef);
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
    };

    popup->setOnSlotChanged ([chainRef, apply] (std::size_t slot,
                                                std::optional<ida::InternalFxId> id)
    {
        auto updated = growChainToSlot (*chainRef, slot);
        if (slot >= updated.size()) return;
        ida::EffectChainEntry entry;
        if (id.has_value()) entry = ida::EffectChainEntry::makeInternal (*id);
        *chainRef = updated.withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotBypassToggled ([chainRef, apply] (std::size_t slot, bool bypassed)
    {
        if (slot >= chainRef->size()) return;
        auto entry = chainRef->at (slot);
        entry.bypassed = bypassed;
        *chainRef = chainRef->withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotsReordered ([chainRef, apply] (std::size_t from, std::size_t to)
    {
        const auto maxSlot = std::max (from, to);
        auto updated = growChainToSlot (*chainRef, maxSlot);
        if (from >= updated.size() || to >= updated.size()) return;
        *chainRef = updated.withMoved (from, to);
        apply();
    });

    popup->setOnClose ([]{});

    popup->setSize (static_cast<int> (otto::Sizing::kMenuMinWidth),
                    static_cast<int> (ida::EffectChain::kMaxSlots)
                    * static_cast<int> (otto::Sizing::kMenuRowHeight));

    const auto target = outputMixerPane_->masterInsButtonScreenArea();
    juce::CallOutBox::launchAsynchronously (std::move (popup), target, nullptr);
}

void MainComponent::openInsertChainPopupForOutputBus (int busIdx)
{
    if (outputMixerPane_ == nullptr) return;
    if (busIdx < 0 || busIdx >= static_cast<int> (outputBusStripIds_.size())) return;
    const auto busId = outputBusStripIds_[static_cast<std::size_t> (busIdx)];
    auto* bus = outputMixer_->busForId (busId);
    if (bus == nullptr) return;

    auto popup = std::make_unique<ida::InsertChainPopup>();
    popup->setInitialChain (bus->effectChain());

    auto chainRef = std::make_shared<ida::EffectChain> (bus->effectChain());

    auto apply = [this, busId, chainRef]
    {
        auto* b = outputMixer_->busForId (busId);
        if (b == nullptr) return;
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
        b->setEffectChain (*chainRef);
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
    };

    popup->setOnSlotChanged ([chainRef, apply] (std::size_t slot,
                                                std::optional<ida::InternalFxId> id)
    {
        auto updated = growChainToSlot (*chainRef, slot);
        if (slot >= updated.size()) return;
        ida::EffectChainEntry entry;
        if (id.has_value()) entry = ida::EffectChainEntry::makeInternal (*id);
        *chainRef = updated.withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotBypassToggled ([chainRef, apply] (std::size_t slot, bool bypassed)
    {
        if (slot >= chainRef->size()) return;
        auto entry = chainRef->at (slot);
        entry.bypassed = bypassed;
        *chainRef = chainRef->withReplaced (slot, entry);
        apply();
    });

    popup->setOnSlotsReordered ([chainRef, apply] (std::size_t from, std::size_t to)
    {
        const auto maxSlot = std::max (from, to);
        auto updated = growChainToSlot (*chainRef, maxSlot);
        if (from >= updated.size() || to >= updated.size()) return;
        *chainRef = updated.withMoved (from, to);
        apply();
    });

    popup->setOnClose ([]{});

    popup->setSize (static_cast<int> (otto::Sizing::kMenuMinWidth),
                    static_cast<int> (ida::EffectChain::kMaxSlots)
                    * static_cast<int> (otto::Sizing::kMenuRowHeight));

    const auto target = outputMixerPane_->busInsButtonScreenArea (busIdx);
    juce::CallOutBox::launchAsynchronously (std::move (popup), target, nullptr);
}

void MainComponent::recomputeInputStripMutes()
{
    // Solo-in-place: if any strip is soloed, every non-soloed strip is silenced.
    // A strip's own mute always silences it. The effective mute drives both the
    // engine strip (audible result) and the strip's visual mute indication.
    const bool anySolo = std::any_of (inputStripSoloed_.begin(), inputStripSoloed_.end(),
                                      [] (bool s) { return s; });
    for (int i = 0; i < static_cast<int> (inputStripChannelIds_.size()); ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const bool effective = inputStripMuted_[idx] || (anySolo && ! inputStripSoloed_[idx]);
        if (auto* s = inputStripAt (i)) s->setMuted (effective);
        if (inputMixerPane_ != nullptr) inputMixerPane_->setEffectiveMute (i, effective);
    }
}

void MainComponent::refreshInputMixer()
{
    if (inputMixerPane_ == nullptr) return;

    // Linear post-fader peak → dBFS for the meter (FaderMeter clamps to its
    // own [-60, +6] window). -60 dB is the silence floor.
    const auto linToDb = [] (float linear) -> float
    {
        return linear <= 1.0e-6f ? -60.0f : 20.0f * std::log10 (linear);
    };

    for (int i = 0; i < inputMixerPane_->stripCount(); ++i)
        if (auto* s = inputStripAt (i))
        {
            inputMixerPane_->setStripLevelDb (i, linToDb (s->peakLeft()),
                                              linToDb (s->peakRight()));
            inputMixerPane_->setStripLufs (i, s->lufsShortTerm());
        }

    for (int i = 0; i < static_cast<int> (busStripIds_.size()); ++i)
        if (auto* bus = inputMixer_->busForId (busStripIds_[static_cast<std::size_t> (i)]))
        {
            inputMixerPane_->setBusStripLevelDb (i, linToDb (bus->peakLeft()),
                                                 linToDb (bus->peakRight()));
            inputMixerPane_->setBusStripLufs (i, bus->lufsShortTerm());
        }

    refreshInputDestinations();
}

void MainComponent::refreshOutputMixer()
{
    if (outputMixerPane_ == nullptr) return;

    const auto linToDb = [] (float linear) -> float
    {
        return linear <= 1.0e-6f ? -60.0f : 20.0f * std::log10 (linear);
    };

    if (auto* master = outputMixer_->busForId (ida::BusId{ 0 }))
    {
        outputMixerPane_->setMasterLevelDb (linToDb (master->peakLeft()),
                                            linToDb (master->peakRight()));
        outputMixerPane_->setMasterLufs (master->lufsShortTerm());
    }

    for (int i = 0; i < static_cast<int> (outputBusStripIds_.size()); ++i)
        if (auto* bus = outputMixer_->busForId (outputBusStripIds_[static_cast<std::size_t> (i)]))
        {
            outputMixerPane_->setBusStripLevelDb (i, linToDb (bus->peakLeft()),
                                                  linToDb (bus->peakRight()));
            outputMixerPane_->setBusStripLufs (i, bus->lufsShortTerm());
        }

    // MON strip meters — walk the same input ChannelIds the MON-strip pane
    // was built from, resolve each to its auto-minted OutputMixer channel,
    // and feed the strip's peak + LUFS from the OutputMixer-side strip
    // (the live post-fader/post-EQ/post-CMP signal the operator is mixing).
    if (inputMixer_ != nullptr)
    {
        for (std::size_t i = 0; i < monStripInputChannelIds_.size(); ++i)
        {
            const auto chId = monStripInputChannelIds_[i];
            const auto outOpt = inputMixer_->channelMonitorOutputChannel (chId);
            if (! outOpt.has_value()) continue;
            auto* s = outputMixer_->audioStripForChannel (*outOpt);
            if (s == nullptr) continue;
            outputMixerPane_->setMonStripLevelDb (static_cast<int> (i),
                                                  linToDb (s->peakLeft()),
                                                  linToDb (s->peakRight()));
            outputMixerPane_->setMonStripLufs (static_cast<int> (i),
                                               s->lufsShortTerm());
        }
    }
}

void MainComponent::rebuildOutputBusStrips()
{
    if (outputMixerPane_ == nullptr) return;

    outputBusStripIds_.clear();
    std::vector<OutputMixerPane::BusInfo> infos;
    const int n = outputMixer_->busCount();   // includes master at index 0
    infos.reserve (static_cast<std::size_t> (std::max (0, n - 1)));
    outputBusStripIds_.reserve (static_cast<std::size_t> (std::max (0, n - 1)));

    const double sampleRate = audioCallback_->currentSampleRate();

    // bus->prepare reallocates the LufsMeter buffers — must NOT race the
    // audio thread. Bracket the prepare loop the same way rebuildBusStrips
    // does on the input side.
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    for (int i = 1; i < n; ++i)   // skip master at BusId{0}
    {
        const auto id = ida::BusId{ static_cast<std::int64_t> (i) };
        if (auto* bus = outputMixer_->busForId (id))
        {
            bus->prepare (sampleRate, kInputLufsMaxBlock);
            infos.push_back ({ juce::String (bus->config().name) });
            outputBusStripIds_.push_back (id);
        }
    }
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    outputMixerPane_->setBusStrips (infos);
}

void MainComponent::refreshOutputMixerPhraseChannels()
{
    if (outputMixerPane_ == nullptr) return;

    // Pull the current pill list from the same selector the Preparation tab
    // uses (TimelineViewState::pills is DFS order via the constituent walk).
    // This is called from both refreshPreparation and refreshPerformance so
    // it stays in lockstep with timeline edits.
    const auto timeline = selectTimelineView (*undoStack_.current(),
                                              demo_.sessionToLmc,
                                              inputs_,
                                              armedTapesVec(),
                                              focusedTape_);

    // Build the new pane-row order (one strip per pill, in pill order).
    std::vector<ida::ConstituentId> newOrder;
    newOrder.reserve (timeline.pills.size());
    for (const auto& pill : timeline.pills) newOrder.push_back (pill.id);

    // Compute the set of pills for delta detection.
    std::unordered_set<std::int64_t> newSet;
    newSet.reserve (newOrder.size());
    for (const auto& id : newOrder) newSet.insert (id.value());

    // Engine mutations (add / remove channels) bracket the audio callback —
    // OutputMixer::addChannel / removeChannel are message-thread only and the
    // free-list / parallel-vector swap-erase isn't safe against a concurrent
    // renderBuffer. Skip the bracket entirely when there's nothing to mutate.
    std::vector<std::int64_t> toRemove;
    for (const auto& kv : phraseChannelByConstituent_)
        if (newSet.find (kv.first) == newSet.end()) toRemove.push_back (kv.first);
    std::vector<ida::ConstituentId> toAdd;
    for (const auto& id : newOrder)
        if (phraseChannelByConstituent_.find (id.value()) == phraseChannelByConstituent_.end())
            toAdd.push_back (id);

    if (! toRemove.empty() || ! toAdd.empty())
    {
        audioDeviceManager_.removeAudioCallback (audioCallback_.get());
        for (auto cidValue : toRemove)
        {
            outputMixer_->removeChannel (phraseChannelByConstituent_.at (cidValue));
            phraseChannelByConstituent_.erase (cidValue);
        }
        for (const auto& cid : toAdd)
        {
            const auto chId = outputMixer_->addChannel (ida::SignalType::Audio);
            if (chId.value() == 0) continue; // engine at kMaxOutputChannels cap
            outputMixer_->setChannelStrip (chId,
                std::make_unique<ChannelStrip<SignalType::Audio>>());
            phraseChannelByConstituent_.emplace (cid.value(), chId);
        }
        audioDeviceManager_.addAudioCallback (audioCallback_.get());
    }

    // Skip the pane rebuild when nothing structural changed — setPhraseStrips
    // recreates CompactFaderStrip instances and would nuke an in-progress
    // fader drag's visual state. Order matters: a pure pill reorder still
    // requires rebuild because phrase strips render left-to-right in pill
    // order.
    if (newOrder == phraseStripConstituentIds_)
    {
        // Still refresh destination labels in case the operator added/removed
        // an aux bus that's now (un)available as a phrase target — no-op when
        // the bus list also didn't change.
        refreshOutputDestinations();
        return;
    }

    phraseStripConstituentIds_ = newOrder;

    std::vector<OutputMixerPane::PhraseStripInfo> infos;
    infos.reserve (timeline.pills.size());
    for (const auto& pill : timeline.pills)
        infos.push_back ({ pill.id, juce::String (pill.name) });
    outputMixerPane_->setPhraseStrips (infos);

    refreshOutputDestinations();
}

void MainComponent::refreshOutputMixerMonChannels()
{
    if (outputMixerPane_ == nullptr) return;
    if (inputMixer_ == nullptr || outputMixer_ == nullptr) return;

    // Walk the input strips in operator-visible row order so the MON
    // band mirrors input-strip order left-to-right. inputStripChannelIds_
    // is the same vector inputMixerPane_ uses for its strips, so the
    // order is exactly the operator's row order.
    std::vector<OutputMixerPane::MonStripInfo> infos;
    std::vector<ida::ChannelId>                newIds;
    infos.reserve  (inputStripChannelIds_.size());
    newIds.reserve (inputStripChannelIds_.size());

    for (std::size_t i = 0; i < inputStripChannelIds_.size(); ++i)
    {
        const auto chId = inputStripChannelIds_[i];
        if (inputMixer_->channelMonitorMode (chId) != ida::MonitorMode::On)
            continue;
        if (! inputMixer_->channelMonitorOutputChannel (chId).has_value())
            continue;

        // Display name: "MON N" where N is the 1-based input strip row.
        // Operator-named inputs are a future polish slice.
        infos.push_back ({ chId, "MON " + juce::String ((int) i + 1) });
        newIds.push_back (chId);
    }

    // Skip the pane rebuild when nothing structural changed (mirrors the
    // phrase-strip refresh's short-circuit): avoids nuking an in-progress
    // fader drag on an unrelated MON strip. Destinations still need a
    // refresh in case an aux bus was added/removed since the last call —
    // mirror of the phrase short-circuit branch.
    if (newIds == monStripInputChannelIds_)
    {
        refreshOutputDestinations();
        return;
    }

    monStripInputChannelIds_ = std::move (newIds);
    outputMixerPane_->setMonStrips (infos);
    refreshOutputDestinations();
}

void MainComponent::refreshOutputDestinations()
{
    if (outputMixerPane_ == nullptr) return;

    using DestKind = OutputMixerPane::DestKind;

    // Enumerate active hardware output pairs from the live audio device.
    // Pair entries are labelled "Out 1-2", "Out 3-4", … matching the
    // physical-output channel numbers (1-based for the operator). On a
    // 2-channel device this yields a single entry.
    struct HwPair { int pairIndex; juce::String label; };
    std::vector<HwPair> hwPairs;
    if (auto* dev = audioDeviceManager_.getCurrentAudioDevice())
    {
        const int activeOuts = dev->getActiveOutputChannels().countNumberOfSetBits();
        for (int ch = 0, p = 0; ch + 1 < activeOuts; ch += 2, ++p)
            hwPairs.push_back ({ p, "Out " + juce::String (ch + 1)
                                      + "-" + juce::String (ch + 2) });
    }
    if (hwPairs.empty())
        hwPairs.push_back ({ 0, "Out 1-2" });   // device-less fallback (test harness)

    auto labelForPair = [&hwPairs] (int pairIndex) -> juce::String
    {
        for (const auto& p : hwPairs) if (p.pairIndex == pairIndex) return p.label;
        return "Out 1-2";   // fallback when the device shrank below the recorded pair
    };

    const int n = static_cast<int> (outputBusStripIds_.size());
    std::vector<std::vector<OutputMixerPane::DestChoice>> perBusChoices (static_cast<std::size_t> (n));
    std::vector<OutputMixerPane::StripDest>               perBus (static_cast<std::size_t> (n));

    for (int i = 0; i < n; ++i)
    {
        const auto myId = outputBusStripIds_[static_cast<std::size_t> (i)];
        auto& choices = perBusChoices[static_cast<std::size_t> (i)];

        // Master is always the first option (and the engine default for any
        // freshly-added aux bus's main-out).
        choices.push_back ({ DestKind::Bus, /*id*/ 0, "Master", /*pairIndex*/ 0 });

        // Every OTHER aux bus is a candidate, pre-filtered against cycles so
        // the picker never offers a target the engine would silently reject.
        for (int j = 0; j < n; ++j)
        {
            if (i == j) continue;
            const auto otherId = outputBusStripIds_[static_cast<std::size_t> (j)];
            if (outputMixer_->busMainOutToBusWouldCycle (myId, otherId)) continue;
            if (auto* otherBus = outputMixer_->busForId (otherId))
                choices.push_back ({ DestKind::Bus, otherId.value(),
                                     juce::String (otherBus->config().name),
                                     /*pairIndex*/ 0 });
        }

        // One entry per physical output pair — "direct out" = bypass master,
        // land at that specific pair on the audio device.
        for (const auto& p : hwPairs)
            choices.push_back ({ DestKind::HardwareOutput, /*id*/ 0, p.label, p.pairIndex });

        // Current destination → label the picker button + tick the matching item.
        auto& dest = perBus[static_cast<std::size_t> (i)];
        if (outputMixer_->busMainOut (myId) == ida::OutputMixer::MainOutDest::HardwareOutput)
        {
            const int pair = outputMixer_->busHardwareOutPair (myId);
            dest.currentKind      = DestKind::HardwareOutput;
            dest.currentId        = 0;
            dest.currentPairIndex = pair;
            dest.currentName      = labelForPair (pair);
        }
        else
        {
            const auto targetId = outputMixer_->busMainOutBus (myId);
            dest.currentKind      = DestKind::Bus;
            dest.currentId        = targetId.value();
            dest.currentPairIndex = 0;
            if (targetId.value() == 0)
                dest.currentName = "Master";
            else if (auto* tgt = outputMixer_->busForId (targetId))
                dest.currentName = juce::String (tgt->config().name);
        }
    }

    outputMixerPane_->setBusDestinations (perBusChoices, perBus);

    // Master destination picker — per-pair only (no bus entries; master is
    // the terminal of the output graph). Hidden on single-pair devices.
    std::vector<OutputMixerPane::DestChoice> masterChoices;
    masterChoices.reserve (hwPairs.size());
    for (const auto& p : hwPairs)
        masterChoices.push_back ({ DestKind::HardwareOutput, /*id*/ 0, p.label, p.pairIndex });

    OutputMixerPane::StripDest masterDest;
    masterDest.currentKind      = DestKind::HardwareOutput;
    masterDest.currentId        = 0;
    masterDest.currentPairIndex = outputMixer_->busHardwareOutPair (ida::BusId{ 0 });
    masterDest.currentName      = labelForPair (masterDest.currentPairIndex);
    outputMixerPane_->setMasterDestination (masterChoices, masterDest);

    // Phrase-strip picker. One picker per phrase row; choice list is master
    // + every aux bus + every physical pair (no cycle filter — phrase
    // channels are leaves in the routing graph). Current destination is
    // read DIRECTLY from the engine's main-out manifest (slice E3): no
    // more "all sends == 0 ⇒ HardwareOutput" inference rule.
    const int pn = static_cast<int> (phraseStripConstituentIds_.size());
    std::vector<std::vector<OutputMixerPane::DestChoice>> perPhraseChoices (static_cast<std::size_t> (pn));
    std::vector<OutputMixerPane::StripDest>               perPhrase (static_cast<std::size_t> (pn));

    for (int i = 0; i < pn; ++i)
    {
        const auto cid = phraseStripConstituentIds_[static_cast<std::size_t> (i)];
        const auto it  = phraseChannelByConstituent_.find (cid.value());
        if (it == phraseChannelByConstituent_.end()) continue;
        const auto chId = it->second;

        auto& choices = perPhraseChoices[static_cast<std::size_t> (i)];
        choices.push_back ({ DestKind::Bus, /*id*/ 0, "Master", /*pairIndex*/ 0 });
        for (const auto& busId : outputBusStripIds_)
            if (auto* bus = outputMixer_->busForId (busId))
                choices.push_back ({ DestKind::Bus, busId.value(),
                                     juce::String (bus->config().name),
                                     /*pairIndex*/ 0 });
        for (const auto& p : hwPairs)
            choices.push_back ({ DestKind::HardwareOutput, /*id*/ 0, p.label, p.pairIndex });

        auto& dest = perPhrase[static_cast<std::size_t> (i)];
        if (outputMixer_->channelMainOut (chId) == ida::OutputMixer::MainOutDest::HardwareOutput)
        {
            const int pair = outputMixer_->channelMainOutHardwareOutPair (chId);
            dest.currentKind      = DestKind::HardwareOutput;
            dest.currentId        = 0;
            dest.currentPairIndex = pair;
            dest.currentName      = labelForPair (pair);
        }
        else
        {
            const auto activeBus = outputMixer_->channelMainOutBus (chId);
            dest.currentKind      = DestKind::Bus;
            dest.currentId        = activeBus.value();
            dest.currentPairIndex = 0;
            if (activeBus.value() == 0)
                dest.currentName = "Master";
            else if (auto* bus = outputMixer_->busForId (activeBus))
                dest.currentName = juce::String (bus->config().name);
        }
    }

    outputMixerPane_->setPhraseDestinations (perPhraseChoices, perPhrase);

    // MON-strip picker. Same shape as the phrase block: each MON strip's
    // destination is its auto-minted OutputMixer channel's main-out (Bus or
    // HardwareOutput). Without this wiring `setMonDestinations` never runs
    // and the dest button keeps the construction-time "—" placeholder, which
    // renders as a garbled `â` on macOS because the strip-side font doesn't
    // honor the construction-time UTF-8 bytes.
    const int mn = static_cast<int> (monStripInputChannelIds_.size());
    std::vector<std::vector<OutputMixerPane::DestChoice>> perMonChoices (static_cast<std::size_t> (mn));
    std::vector<OutputMixerPane::StripDest>               perMon (static_cast<std::size_t> (mn));

    for (int i = 0; i < mn; ++i)
    {
        const auto inChId = monStripInputChannelIds_[static_cast<std::size_t> (i)];
        const auto outOpt = (inputMixer_ != nullptr)
                                ? inputMixer_->channelMonitorOutputChannel (inChId)
                                : std::optional<ida::OutputChannelId> {};
        if (! outOpt.has_value()) continue;
        const auto chId = *outOpt;

        auto& choices = perMonChoices[static_cast<std::size_t> (i)];
        choices.push_back ({ DestKind::Bus, /*id*/ 0, "Master", /*pairIndex*/ 0 });
        for (const auto& busId : outputBusStripIds_)
            if (auto* bus = outputMixer_->busForId (busId))
                choices.push_back ({ DestKind::Bus, busId.value(),
                                     juce::String (bus->config().name),
                                     /*pairIndex*/ 0 });
        for (const auto& p : hwPairs)
            choices.push_back ({ DestKind::HardwareOutput, /*id*/ 0, p.label, p.pairIndex });

        auto& dest = perMon[static_cast<std::size_t> (i)];
        if (outputMixer_->channelMainOut (chId) == ida::OutputMixer::MainOutDest::HardwareOutput)
        {
            const int pair = outputMixer_->channelMainOutHardwareOutPair (chId);
            dest.currentKind      = DestKind::HardwareOutput;
            dest.currentId        = 0;
            dest.currentPairIndex = pair;
            dest.currentName      = labelForPair (pair);
        }
        else
        {
            const auto activeBus = outputMixer_->channelMainOutBus (chId);
            dest.currentKind      = DestKind::Bus;
            dest.currentId        = activeBus.value();
            dest.currentPairIndex = 0;
            if (activeBus.value() == 0)
                dest.currentName = "Master";
            else if (auto* bus = outputMixer_->busForId (activeBus))
                dest.currentName = juce::String (bus->config().name);
        }
    }

    outputMixerPane_->setMonDestinations (perMonChoices, perMon);
}

// Resolves each strip's current main-out destination and builds the full choice
// list (pooled tapes, then buses/FX returns, then direct hardware output), then
// pushes both into the pane so picker buttons track route changes and pool renames.
void MainComponent::refreshInputDestinations()
{
    if (inputMixerPane_ == nullptr) return;
    using Pane = InputMixerPane;

    // Choice list (shared for every strip's popup): pooled tapes, then plain
    // buses, then the direct (hardware-output) terminal. FX returns are
    // DELIBERATELY EXCLUDED — a channel reaches RVB/DLY via a post-fader SEND
    // (the Sends tab, P7), never as a main-out destination. Only BusKind::Bus
    // subgroups are valid main-out targets.
    std::vector<Pane::DestChoice> choices;
    for (const auto& t : tapePool_.tapes())
        choices.push_back (Pane::DestChoice { Pane::DestKind::Tape, t.id.value(), juce::String (t.name) });
    for (int i = 0; i < inputMixer_->busCount(); ++i)
    {
        if (inputMixer_->busKindAt (i) != ida::BusKind::Bus)
            continue;   // FX returns are send-only, not main-out destinations
        const auto bid = inputMixer_->busIdAt (i);
        if (auto* bus = inputMixer_->busForId (bid))
            choices.push_back (Pane::DestChoice { Pane::DestKind::Bus,
                                                  bid.value(),
                                                  juce::String (bus->config().name) });
    }
    // 2026-05-24 monitor slice: the "Direct out" choice was DELETED from this
    // picker. Inputs reach physical outputs ONLY via the per-channel Monitor
    // button (whitepaper §5.2 / §7.1 — input mixer never writes physical
    // outputs directly). The engine-level MainOutDest::HardwareOutput
    // infrastructure is retained for legacy/back-compat (separate cleanup
    // slice — see todo.md). Channels that were previously routed to
    // HardwareOutput still display correctly via the case below.

    // Per-strip current destination, read back from the engine's main-out.
    std::vector<Pane::StripDest> perStrip;
    perStrip.reserve (inputStripChannelIds_.size());
    for (const auto& chId : inputStripChannelIds_)
    {
        Pane::StripDest dest;
        switch (inputMixer_->channelMainOut (chId))
        {
            case ida::InputMixer::MainOutDest::Tape:
                for (const auto& t : tapePool_.tapes())
                    if (inputMixer_->channelMainOutIsTape (chId, t.id))
                    {
                        dest.currentKind = Pane::DestKind::Tape;
                        dest.currentId   = t.id.value();
                        dest.currentName = juce::String (t.name);
                        break;
                    }
                break;
            case ida::InputMixer::MainOutDest::Bus:
            {
                const auto bid = inputMixer_->channelMainOutBus (chId);
                if (auto* bus = inputMixer_->busForId (bid))
                {
                    dest.currentKind = Pane::DestKind::Bus;
                    dest.currentId   = bid.value();
                    dest.currentName = juce::String (bus->config().name);
                }
                break;
            }
            case ida::InputMixer::MainOutDest::HardwareOutput:
                dest.currentKind = Pane::DestKind::HardwareOutput;
                dest.currentId   = 0;
                dest.currentName = "Direct out";
                break;
        }
        perStrip.push_back (std::move (dest));
    }
    inputMixerPane_->setDestinations (choices, perStrip);

    // 2026-05-24 monitor slice — refresh the Monitor button state in lockstep
    // with the destination labels. Engine-side mode is the source of truth
    // (loaded projects, programmatic edits, future automation).
    std::vector<ida::MonitorMode> monitorModes;
    monitorModes.reserve (inputStripChannelIds_.size());
    for (const auto& chId : inputStripChannelIds_)
        monitorModes.push_back (inputMixer_->channelMonitorMode (chId));
    inputMixerPane_->setMonitorModes (monitorModes);

    // Bus-row pickers. Each bus/FX-return strip can route its output to a pooled
    // tape, another PLAIN bus, or direct out. The choice list is built PER BUS:
    // feedback-cycle targets (and the bus itself) are filtered out, so the lists
    // differ between buses. FX returns are excluded as targets (send-only).
    std::vector<std::vector<Pane::DestChoice>> busChoices;
    std::vector<Pane::StripDest>               busDests;
    busChoices.reserve (busStripIds_.size());
    busDests.reserve (busStripIds_.size());
    for (const auto& busId : busStripIds_)
    {
        std::vector<Pane::DestChoice> choicesForBus;
        for (const auto& t : tapePool_.tapes())
            choicesForBus.push_back (Pane::DestChoice { Pane::DestKind::Tape, t.id.value(), juce::String (t.name) });
        for (int i = 0; i < inputMixer_->busCount(); ++i)
        {
            if (inputMixer_->busKindAt (i) != ida::BusKind::Bus)
                continue;   // FX returns are send-only, not main-out destinations
            const auto target = inputMixer_->busIdAt (i);
            if (target.value() == busId.value())
                continue;   // a bus cannot route to itself
            if (inputMixer_->busMainOutToBusWouldCycle (busId, target))
                continue;   // omit targets that would close a feedback cycle
            if (auto* bus = inputMixer_->busForId (target))
                choicesForBus.push_back (Pane::DestChoice { Pane::DestKind::Bus,
                                                            target.value(),
                                                            juce::String (bus->config().name) });
        }
        // 2026-05-24 monitor slice: "Direct out" was also dropped from the bus
        // picker for the same reason (input-side buses sum FX returns for
        // tape capture; they do not write physical outputs directly).
        busChoices.push_back (std::move (choicesForBus));

        Pane::StripDest dest;
        switch (inputMixer_->busMainOut (busId))
        {
            case ida::InputMixer::MainOutDest::Tape:
                for (const auto& t : tapePool_.tapes())
                    if (inputMixer_->busMainOutIsTape (busId, t.id))
                    {
                        dest.currentKind = Pane::DestKind::Tape;
                        dest.currentId   = t.id.value();
                        dest.currentName = juce::String (t.name);
                        break;
                    }
                break;
            case ida::InputMixer::MainOutDest::Bus:
            {
                const auto bid = inputMixer_->busMainOutBus (busId);
                if (auto* bus = inputMixer_->busForId (bid))
                {
                    dest.currentKind = Pane::DestKind::Bus;
                    dest.currentId   = bid.value();
                    dest.currentName = juce::String (bus->config().name);
                }
                break;
            }
            case ida::InputMixer::MainOutDest::HardwareOutput:
                dest.currentKind = Pane::DestKind::HardwareOutput;
                dest.currentId   = 0;
                dest.currentName = "Direct out";
                break;
        }
        busDests.push_back (std::move (dest));
    }
    inputMixerPane_->setBusDestinations (busChoices, busDests);
}

void MainComponent::rebuildInputStrips()
{
    // Mutating the InputMixer channel registry races the audio thread (it reads
    // channelSources_/channels_ in processDeviceInputs/processBuffer). Bracket
    // the rebuild with removeAudioCallback/addAudioCallback: JUCE guarantees the
    // callback is not executing between them, so the maps are mutated safely.
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());

    for (const auto& id : inputStripChannelIds_)
        inputMixer_->removeChannel (id);
    inputStripChannelIds_.clear();
    inputStripPair_.clear();

    std::vector<InputMixerPane::StripInfo> infos;
    const auto registerStrip = [&] (int pairIndex, int leftCh, int rightCh,
                                    bool stereo, const juce::String& name)
    {
        const auto chId = inputMixer_->addChannel (InputId (leftCh), SignalType::Audio);
        inputMixer_->setChannelInputSource (chId, leftCh, rightCh, stereo);
        // Looper invariant — ≥1 channel must feed ≥1 tape at all times, or this is a
        // mixer, not a looper. Input strips therefore default to capturing their
        // processed signal to the primary tape (CommitToTape = what renderInputGraph
        // delivers; the default main-out is already the primary tape). A channel MAY
        // later be routed direct-to-output (TapeMode::NoTape) via the slice-4 picker,
        // but enforcement must never let the count of channels-recording-to-tape reach
        // zero (active floor-enforcement lands with that picker — see todo.md).
        inputMixer_->setChannelTapeMode (chId, TapeMode::CommitToTape);
        inputStripChannelIds_.push_back (chId);
        inputStripPair_.push_back (pairIndex);
        infos.push_back ({ name, stereo });
    };

    for (int p = 0; p < static_cast<int> (inputPairs_.size()); ++p)
    {
        const auto& pair = inputPairs_[static_cast<std::size_t> (p)];
        const bool single = (pair.leftCh == pair.rightCh);   // one physical input
        if (pair.stereo)
        {
            // STEREO → one strip. A single physical input is dual-mono (L == R
            // source); a true pair takes its two device channels.
            registerStrip (p, pair.leftCh, pair.rightCh, /*stereo*/ true,
                           single ? "In " + juce::String (pair.leftCh + 1)
                                  : "In " + juce::String (pair.leftCh + 1)
                                    + "-" + juce::String (pair.rightCh + 1));
        }
        else
        {
            // MONO → two strips (the stereo channel's L and R halves). For a
            // single physical input both halves carry the same dual-mono signal,
            // independently pannable; a true pair splits to its two channels.
            if (single)
            {
                const auto base = juce::String (pair.leftCh + 1);
                registerStrip (p, pair.leftCh, -1, /*stereo*/ false, "In " + base + "L");
                registerStrip (p, pair.leftCh, -1, /*stereo*/ false, "In " + base + "R");
            }
            else
            {
                registerStrip (p, pair.leftCh,  -1, /*stereo*/ false,
                               "In " + juce::String (pair.leftCh + 1));
                registerStrip (p, pair.rightCh, -1, /*stereo*/ false,
                               "In " + juce::String (pair.rightCh + 1));
            }
        }
    }

    // A rebuild changes channel identities, so mute/solo do not carry over.
    inputStripMuted_.assign (inputStripChannelIds_.size(), false);
    inputStripSoloed_.assign (inputStripChannelIds_.size(), false);

    if (inputMixerPane_ != nullptr) inputMixerPane_->setStrips (infos);
    recomputeInputStripMutes();
    refreshInputDestinations();   // populate the picker labels for the new strips

    // Prepare each strip's EBU R128 loudness meter for the device's sample rate
    // (off the audio thread — the callback is removed). A generous max block
    // keeps the meter from ever clamping a large device buffer.
    const double sampleRate = audioCallback_->currentSampleRate();
    for (int i = 0; i < static_cast<int> (inputStripChannelIds_.size()); ++i)
        if (auto* s = inputStripAt (i))
            s->prepare (sampleRate, kInputLufsMaxBlock);

    // Tape slice 3 — propagate any sample-rate change to the FLAC sink while
    // the callback is detached (the sink header requires this on the message
    // thread, before audio starts or between removeAudioCallback/addAudioCallback).
    if (flacTapeSink_ != nullptr)
        flacTapeSink_->setSampleRate (sampleRate);
    // TAPECOLOR Slice 2 — re-prepare every per-tape adapter at the new rate
    // so BeforeWrite coloring stays in step with the device. Same RT-safety
    // posture as the FLAC sink: message thread, audio callback detached.
    if (tapeColoringSink_ != nullptr)
    {
        const auto setup = audioDeviceManager_.getAudioDeviceSetup();
        const int  blk   = setup.bufferSize > 0 ? setup.bufferSize : 512;
        tapeColoringSink_->setSampleRate (sampleRate, blk);
    }

    // P7 T3a-C — re-prepare every bound internal-FX adapter against the live
    // device configuration. The audio callback is detached here, so the host's
    // internal-adapter table can be walked safely. No-op when no adapters are
    // bound (today's startup state).
    {
        const auto setup = audioDeviceManager_.getAudioDeviceSetup();
        if (setup.sampleRate > 0.0 && setup.bufferSize > 0)
            effectChainHost_.prepareInternalFx (setup.sampleRate, setup.bufferSize);
    }

    audioDeviceManager_.addAudioCallback (audioCallback_.get());
}

// Rebuilds the bus/FX-return strip row from the engine bus set. Call this after
// ANY change to the bus set (setup, addBus/addFxReturn) — it is intentionally
// NOT chained off rebuildInputStrips(), because a channel-pair stereo toggle
// leaves the bus set untouched and a gratuitous rebuild would reset bus fader
// visuals while the engine gain persists. Buses are unaffected by channel rebuilds.
void MainComponent::rebuildBusStrips()
{
    if (inputMixerPane_ == nullptr || inputMixer_ == nullptr) return;

    busStripIds_.clear();
    std::vector<InputMixerPane::BusInfo> infos;
    const int n = inputMixer_->busCount();
    infos.reserve (static_cast<std::size_t> (n));
    busStripIds_.reserve (static_cast<std::size_t> (n));

    const double sampleRate = audioCallback_->currentSampleRate();

    // bus->prepare() reallocates LufsMeter buffers + writes non-atomic state the
    // audio thread reads in Bus::process. Bracket the prepare loop so it can never
    // run concurrently with the audio callback (this method is called from
    // onAddBus/onAddFxReturn while audio is live). Mirrors rebuildInputStrips().
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    for (int i = 0; i < n; ++i)
    {
        const auto id   = inputMixer_->busIdAt (i);
        const bool isFx = inputMixer_->busKindAt (i) == ida::BusKind::FxReturn;
        if (auto* bus = inputMixer_->busForId (id))
        {
            bus->prepare (sampleRate, kInputLufsMaxBlock);
            infos.push_back ({ juce::String (bus->config().name), isFx });
            busStripIds_.push_back (id);
        }
    }
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    inputMixerPane_->setBusStrips (infos);   // UI work — safe after re-add
}

void MainComponent::toggleInputPairStereo (int stripIndex)
{
    if (stripIndex < 0 || stripIndex >= static_cast<int> (inputStripPair_.size())) return;
    const int pairIndex = inputStripPair_[static_cast<std::size_t> (stripIndex)];
    if (pairIndex < 0 || pairIndex >= static_cast<int> (inputPairs_.size())) return;

    auto& pair = inputPairs_[static_cast<std::size_t> (pairIndex)];
    pair.stereo = ! pair.stereo;   // stereo (1 strip) ↔ mono (2 strips), every input
    rebuildInputStrips();
}

void MainComponent::refreshPreparation()
{
    refreshOutputMixerPhraseChannels();   // slice 5b: keep phrase strips in lockstep with the pill list
    refreshOutputMixerMonChannels();      // V9: keep MON strips in lockstep with input-side MON toggles
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

// --- tape pool management (tape-UI T3) ---

void MainComponent::addTape (const juce::String& name)
{
    if (inputMixer_->tapeCount() >= ida::InputMixer::kMaxTapes)
        return;                                         // capacity guard: pool and mixer stay in lockstep
    const auto id = tapePool_.add (name.toStdString());
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    const bool ok = inputMixer_->addTape (id);
    jassert (ok); juce::ignoreUnused (ok);
    // TAPECOLOR Slice 2 — give the new tape its own (default-OFF) adapter.
    // Mode follows the descriptor (None for a freshly-added tape).
    if (tapeColoringSink_ != nullptr)
    {
        tapeColoringSink_->addTape (id);
        if (const auto* d = tapePool_.find (id))
            tapeColoringSink_->setMode (id, d->tapeColor);
    }
    audioDeviceManager_.addAudioCallback (audioCallback_.get());
    refreshTapesPane();
}

void MainComponent::addNextTape()
{
    addTape ("Tape " + juce::String (tapePool_.count() + 1));
}

void MainComponent::renameTape (ida::TapeId id, const juce::String& name)
{
    const bool ok = tapePool_.rename (id, name.toStdString());   // pool-only; no engine/sink effect
    jassert (ok); juce::ignoreUnused (ok);
    refreshTapesPane();
}

void MainComponent::removeTape (ida::TapeId id)
{
    if (tapePool_.count() <= 1) return;          // >=1 pool floor (TapePool also refuses)
    if (id == tapePool_.primary()) return;       // primary is permanent (InputMixer pins it
                                                 // too) — must bail BEFORE closeTape, or we'd
                                                 // close the primary writer then desync when
                                                 // inputMixer_->removeTape refuses TapeId{1}.

    audioDeviceManager_.removeAudioCallback (audioCallback_.get());
    // Route any channel that targeted this tape back to the primary tape.
    for (const auto& chId : inputStripChannelIds_)
        if (inputMixer_->channelMainOutIsTape (chId, id))
            inputMixer_->setChannelMainOutToTape (chId);   // primary
    flacTapeSink_->closeTape (id);               // SPSC: inside the bracket only
    inputMixer_->removeTape (id);
    // TAPECOLOR Slice 2 — release the per-tape adapter alongside the FLAC
    // writer. After this point deliverTapeBlock for `id` is passthrough.
    if (tapeColoringSink_ != nullptr)
        tapeColoringSink_->removeTape (id);
    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    const bool ok = tapePool_.remove (id);
    jassert (ok); juce::ignoreUnused (ok);

    // Auto-disarm: mirror toggleArm's disarm path for the removed tape.
    armedTapeIds_.erase (id.value());
    if (armedTapeIds_.empty() && captureSession_.isArmed())
        captureSession_.disarm();

    // Prevent focusedTape_ from dangling on the now-removed tape.
    if (focusedTape_ == id)
        focusedTape_ = tapePool_.primary();

    refreshTapesPane();
    refreshTimeline();
    refreshCaptureControls();
    refreshDiagnostics();
}

void MainComponent::refreshTapesPane()
{
    if (tapesPane_ == nullptr) return;
    std::vector<TapesPane::TapeInfo> infos;
    infos.reserve (tapePool_.tapes().size());
    for (const auto& t : tapePool_.tapes())
        infos.push_back ({ t.id, juce::String (t.name) });
    tapesPane_->setTapes (infos, tapePool_.primary());
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
        const ida::CaptureRestorePoint restorePoint {
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
        // Contract: when resolvedMode == Overlay, promote() guarantees both
        // fields are populated. Trap loudly in debug rather than silently
        // rendering "Added to the phrase here 0 only" (CLAUDE.md rule 8).
        jassert (result.hostPhraseName.has_value()
                 && result.overlayPlacementIndex.has_value());
        const juce::String hostName (*result.hostPhraseName);
        const auto idx = *result.overlayPlacementIndex;
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

void MainComponent::refreshAll()
{
    refreshPerformance();
    refreshPreparation();
    refreshCaptureControls();
    refreshDiagnostics();
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

        refreshAll();
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

        refreshAll();
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
    std::function<ida::Constituent (const ida::Constituent&, std::size_t)>
        forkedSplice;
    forkedSplice = [&] (const ida::Constituent& c, std::size_t depth)
                       -> ida::Constituent
    {
        if (depth == wrapperPath->size())
        {
            if (! ida::isPlacementWrapper (c)) return c;
            const auto& sharedPhrase = *c.children()[0];
            auto allocate = [this] { return ConstituentId (nextConstituentId_++); };
            const auto deepCopy = std::make_shared<const ida::Constituent> (
                deepCopyWithFreshIds (sharedPhrase, allocate));

            ida::PhraseMetadata forkedMeta = *c.phraseMetadata();
            forkedMeta.role = "forked-placement";
            return c.withPhraseMetadata (std::move (forkedMeta))
                    .withChildReplaced (0, deepCopy);
        }
        const std::size_t i = (*wrapperPath)[depth];
        auto childCopy = std::make_shared<const ida::Constituent> (
            forkedSplice (*c.children()[i], depth + 1));
        return c.withChildReplaced (i, childCopy);
    };

    ida::Constituent newRoot = forkedSplice (root, 0);

    undoStack_.push (
        std::make_shared<const ida::Constituent> (std::move (newRoot)),
        "vary this placement");

    refreshAll();
}

void MainComponent::chooseFileAndSave()
{
    sessionFileChooser_ = std::make_unique<juce::FileChooser> (
        "Save IDA session as...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("session.ida.json"),
        "*.json");

    sessionFileChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            const auto target = fc.getResult();
            if (target == juce::File()) return;
            if (notificationBus_ == nullptr) return;

            // M8 S1: populate persistedSnapshot on every VersionPinning
            // entry before serializing so the saved JSON carries the
            // frozen plug-in identity needed for reopen-time drift
            // detection. Copy-on-write — unchanged subtrees stay shared
            // with the undo-stack version.
            // M8 S2: the populator consults the live host through
            // slotLookup() so each VersionPinning entry freezes the
            // descriptor + live state blob of the bus its instance runs on.
            const auto populated = ida::populateVersionPinningRecords (
                undoStack_.current(), effectChainHost_, slotLookup(), *notificationBus_);
            const auto sessionJson = persistence::serializeSession (*populated);
            const auto poolJson    = persistence::serializeTapePool (tapePool_);
            // Slice P (2026-05-24): persist both mixer graphs and the
            // phrase-channel binding so per-phrase mix state (fader, sends,
            // routing) and the ConstituentId -> OutputChannelId binding
            // survive save/load. Without this the OutputMixer reset on load
            // would reshuffle phrase strips into fresh ids.
            const auto inputMixerJson  = persistence::serializeMixerGraphState (
                                             inputMixer_->exportGraphState());
            const auto outputMixerJson = persistence::serializeMixerGraphState (
                                             outputMixer_->exportGraphState());
            std::vector<std::pair<std::int64_t, std::int64_t>> phraseMapEntries;
            phraseMapEntries.reserve (phraseChannelByConstituent_.size());
            for (const auto& kv : phraseChannelByConstituent_)
                phraseMapEntries.emplace_back (kv.first, kv.second.value());
            const auto phraseMapJson = persistence::serializePhraseChannelMap (phraseMapEntries);

            // Embed all sections as string values in a thin envelope so each
            // sub-document stays self-contained and the envelope itself is
            // forward-compat (unknown top-level keys are ignored on load).
            // Pre-envelope files are still loadable — see chooseFileAndLoad.
            auto envelope = juce::DynamicObject::Ptr { new juce::DynamicObject() };
            envelope->setProperty ("ida_version",         kSessionEnvelopeVersion);
            envelope->setProperty ("session",             sessionJson);
            envelope->setProperty ("pool",                poolJson);
            envelope->setProperty ("input_mixer",         inputMixerJson);
            envelope->setProperty ("output_mixer",        outputMixerJson);
            envelope->setProperty ("phrase_channel_map",  phraseMapJson);
            const auto fileText = juce::JSON::toString (juce::var (envelope.get()));
            if (target.replaceWithText (fileText))
                preparationPane_->setStatus ("Saved to " + target.getFullPathName());
            else
                preparationPane_->setStatus ("Failed to write " + target.getFullPathName());
        });
}

void MainComponent::chooseFileAndLoad()
{
    sessionFileChooser_ = std::make_unique<juce::FileChooser> (
        "Load IDA session...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.json");

    // `openMode` alone leaves `canSelectFiles` unset, which JUCE maps to
    // [NSOpenPanel setCanChooseFiles: NO] on macOS — the picker shows the
    // .json files but greys them out (juce_FileChooser_mac.mm:71,115). The
    // save site doesn't need this because NSSavePanel always accepts a typed
    // filename. Mirror the directory-picker site below which sets canSelectXxx
    // explicitly.
    sessionFileChooser_->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto source = fc.getResult();
            if (source == juce::File()) return;

            const auto fileText = source.loadFileAsString();
            try
            {
                // Detect envelope format (written by the current save path) vs
                // the legacy format (a raw session JSON document). The envelope
                // carries a "ida_version" key; legacy files carry "version".
                juce::String sessionJson;
                TapePool loadedPool;                    // default: 1 tape, id=1
                // Slice P (2026-05-24): envelope v2 carries mixer graphs + the
                // phrase-channel binding. Default-constructed = "no change to
                // current mixer state / map" (v1 and pre-envelope back-compat).
                std::optional<ida::InputMixerGraphState>  loadedInputMixer;
                std::optional<ida::OutputMixerGraphState> loadedOutputMixer;
                std::vector<std::pair<std::int64_t, std::int64_t>> loadedPhraseMap;

                juce::var envelope;
                if (juce::JSON::parse (fileText, envelope).wasOk()
                    && envelope.isObject()
                    && envelope.getDynamicObject()->hasProperty ("ida_version"))
                {
                    sessionJson = envelope.getProperty ("session", {}).toString();
                    if (sessionJson.isEmpty())
                        throw std::runtime_error (
                            "session envelope is missing a valid 'session' document");
                    const auto poolStr = envelope.getProperty ("pool", {}).toString();
                    if (poolStr.isNotEmpty())
                        loadedPool = persistence::deserializeTapePool (poolStr);

                    // v2 keys — absent on v1 envelopes, in which case the
                    // mixers stay at their pre-load state (current behavior).
                    const auto inMixStr = envelope.getProperty ("input_mixer", {}).toString();
                    if (inMixStr.isNotEmpty())
                        loadedInputMixer = persistence::deserializeInputMixerGraphState (inMixStr);
                    const auto outMixStr = envelope.getProperty ("output_mixer", {}).toString();
                    if (outMixStr.isNotEmpty())
                        loadedOutputMixer = persistence::deserializeOutputMixerGraphState (outMixStr);
                    const auto mapStr = envelope.getProperty ("phrase_channel_map", {}).toString();
                    if (mapStr.isNotEmpty())
                        loadedPhraseMap = persistence::deserializePhraseChannelMap (mapStr);
                }
                else
                {
                    // Pre-envelope file: the whole text is the session JSON;
                    // default TapePool() is the back-compat choice per the
                    // SessionFormat.h §52 contract.
                    sessionJson = fileText;
                }

                auto loaded = persistence::deserializeSession (sessionJson);
                // M8 S1: warn-on-drift via the existing NotificationBus.
                // The Preparation pane's notification-history line picks
                // up the message without any new UI code. Per white paper
                // line 1500: warn, do not refuse — the session still loads.
                // M8 S2: the verifier consults the live host through
                // slotLookup(), comparing the saved snapshot against the
                // current bus state and warning on drift.
                if (notificationBus_ != nullptr)
                    ida::verifyVersionPinningOnLoad (
                        *loaded, effectChainHost_, slotLookup(), *notificationBus_);
                // M8 S3: derive Constituent state and warn on Broken/Invalid
                // nodes (white paper §17.7). The session still loads — the
                // performer is informed and can repair. Broken detection uses
                // the honest always-true resolver until the TapeId->content
                // manifest exists (M8 S7–S8); structural Invalid is live now.
                if (notificationBus_ != nullptr)
                {
                    const auto validation =
                        ida::validate (*loaded, ida::alwaysResolves);
                    ida::postConstituentStateNotifications (
                        *loaded, validation, *notificationBus_);
                }
                // M8 S6: re-validate the device-scoped calibration sidecar on
                // load — the spec's "on session load" detection. Startup owns
                // re-persistence; here we only surface corruption that occurred
                // after launch (the sidecar is independent of the session file).
                if (notificationBus_ != nullptr)
                {
                    const juce::File sidecar = calibrationSidecarFile();
                    const std::string contents = sidecar.existsAsFile()
                        ? sidecar.loadFileAsString().toStdString()
                        : std::string {};
                    ida::postCalibrationRecoveryNotification (
                        ida::parseAndValidateCalibration (contents).status,
                        *notificationBus_);
                }
                // Re-mirror the loaded tape pool into the InputMixer. The audio
                // thread reads the mixer's tape-terminal list on the hot path, so
                // all mutation happens inside the removeAudioCallback bracket.
                // Sequencing: remove old non-primary terminals first, then assign
                // the loaded pool and add the new tapes. removeTape auto-reroutes
                // any channel that pointed at a removed terminal to the primary
                // (see MixerGraph::removeTerminal), so terminals being referenced
                // stay valid throughout.
                //
                // Slice P (2026-05-24): mixer graph imports share the same
                // bracket — importGraphState mutates the routing graph the
                // audio thread reads. Order: stop callback → tape pool →
                // mixer imports → restart callback. The freshly-constructed
                // mixer precondition that importGraphState relies on is met
                // by replacing the mixer instances with brand-new ones.
                {
                    audioDeviceManager_.removeAudioCallback (audioCallback_.get());

                    // Snapshot the current pool's non-primary ids before the
                    // move so we can prune them from both the InputMixer and
                    // (TAPECOLOR Slice 2) the per-tape decorator. The primary
                    // is permanent in both subsystems.
                    std::vector<ida::TapeId> staleNonPrimary;
                    for (const auto& tape : tapePool_.tapes())
                        if (tape.id != tapePool_.primary())
                            staleNonPrimary.push_back (tape.id);

                    for (auto id : staleNonPrimary)
                        inputMixer_->removeTape (id);

                    if (tapeColoringSink_ != nullptr)
                        for (auto id : staleNonPrimary)
                            tapeColoringSink_->removeTape (id);

                    tapePool_ = std::move (loadedPool);
                    ida::mirrorTapePool (tapePool_, *inputMixer_);

                    // TAPECOLOR Slice 2 — re-seed the per-tape decorator from
                    // the freshly-loaded pool and apply each descriptor's
                    // tapeColor. addTape is a no-op for ids already present
                    // (e.g. the primary), so the primary's mode also gets
                    // refreshed via setMode.
                    if (tapeColoringSink_ != nullptr)
                        for (const auto& t : tapePool_.tapes())
                        {
                            tapeColoringSink_->addTape (t.id);
                            tapeColoringSink_->setMode (t.id, t.tapeColor);
                        }

                    if (loadedInputMixer.has_value() || loadedOutputMixer.has_value())
                    {
                        // The mixers' importGraphState requires a fresh
                        // instance (asserts no bus collisions on the input
                        // side). Replace the wiring around the audio
                        // callback while it's detached.
                        if (loadedInputMixer.has_value())
                        {
                            inputMixer_ = std::make_unique<ida::InputMixer>();
                            inputMixer_->setNotificationBus (notificationBus_.get());
                            inputMixer_->setTapeSink (tapeColoringSink_.get());
                            inputMixer_->setEffectChainHost (&effectChainHost_);
                            ida::mirrorTapePool (tapePool_, *inputMixer_);
                            inputMixer_->importGraphState (*loadedInputMixer);
                        }
                        if (loadedOutputMixer.has_value())
                        {
                            outputMixer_ = std::make_unique<ida::OutputMixer>();
                            outputMixer_->setEffectChainHost (&effectChainHost_);
                            outputMixer_->importGraphState (*loadedOutputMixer);
                        }
                        // V9: re-bind the InputMixer's OutputMixer attachment
                        // AFTER both are re-minted so MON replay (in a future
                        // session-format extension) can engage routes.
                        // Always re-attach regardless of which envelope side
                        // was present: if only `output_mixer` was loaded, the
                        // OutputMixer instance was destroyed and replaced, so
                        // the surviving InputMixer's stored OutputMixer* would
                        // otherwise be left dangling. attachOutputMixer is
                        // idempotent and both pointers are live here.
                        inputMixer_->attachOutputMixer (outputMixer_.get());
                        // V9 MON-replay (whitepaper §6.3.1 / §7.2): the
                        // inputMixer_->importGraphState above ran with no
                        // attached OutputMixer, so per-channel MON state was
                        // tracked but no OutputMixer channels were minted.
                        // Now that the attachment is live, replay every
                        // MON-on channel — setChannelMonitorMode(On) is
                        // idempotent and mints the auto-created OutputMixer
                        // channel (+ its ChannelStrip) for any channel
                        // whose route was deferred.
                        if (loadedInputMixer.has_value())
                            for (const auto& c : loadedInputMixer->channels)
                                if (c.monitorMode == ida::MonitorMode::On)
                                    inputMixer_->setChannelMonitorMode (
                                        ida::ChannelId (c.channelId),
                                        ida::MonitorMode::On);
                        // V9 follow-up: realign inputStripChannelIds_ to the
                        // loaded channel ids so refreshOutputMixerMonChannels()
                        // resolves the freshly-imported MON state. The strip
                        // count comes from inputPairs_ (unchanged by load);
                        // when it matches the loaded channel count (common
                        // case — same device, same pairs) every strip rebinds
                        // to its saved ChannelId. Mismatch (cross-device load)
                        // leaves the overlap rebound and surfaces an existing
                        // wider gap not in scope here.
                        if (loadedInputMixer.has_value())
                        {
                            const auto& loadedChans = loadedInputMixer->channels;
                            const auto n = std::min (inputStripChannelIds_.size(),
                                                     loadedChans.size());
                            for (std::size_t i = 0; i < n; ++i)
                                inputStripChannelIds_[i] =
                                    ida::ChannelId (loadedChans[i].channelId);
                        }
                        // The AudioCallback holds raw pointers to the mixers;
                        // re-bind it to the new instances before re-arming.
                        audioCallback_->setInputMixer  (inputMixer_.get());
                        audioCallback_->setOutputMixer (outputMixer_.get());
                    }
                    audioDeviceManager_.addAudioCallback (audioCallback_.get());
                }
                refreshTapesPane();

                // Slice P (2026-05-24): restore the phrase-channel binding
                // BEFORE refreshPreparation/refreshPerformance — those
                // cascade into refreshOutputMixerPhraseChannels, which adds
                // channels for any ConstituentId not already in the map. A
                // pre-populated map means surviving Constituents bind back
                // to their saved OutputChannelId rather than minting fresh
                // ones (which would orphan the saved per-channel mix state).
                phraseChannelByConstituent_.clear();
                for (const auto& [cid, chId] : loadedPhraseMap)
                    phraseChannelByConstituent_.emplace (
                        cid, ida::OutputChannelId (chId));

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

void MainComponent::scanFolder (const juce::File& chosen)
{
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
}

void MainComponent::chooseFolderAndScan()
{
    pluginFolderChooser_ = std::make_unique<juce::FileChooser> (
        "Choose a folder to scan for plugins...",
       #if JUCE_MAC
        juce::File ("/Library/Audio/Plug-Ins")
       #else
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
       #endif
        );

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    pluginFolderChooser_->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        scanFolder (fc.getResult());
    });
}

#if JUCE_MAC
void MainComponent::scanGlobalPluginFolder()
{
    scanFolder (juce::File ("/Library/Audio/Plug-Ins"));
}

void MainComponent::scanUserPluginFolder()
{
    scanFolder (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("Library/Audio/Plug-Ins"));
}

void MainComponent::openSyntheticTestPlugin()
{
   #ifdef IDA_SYNTHETIC_CLAP_PATH
    const juce::File bundle (IDA_SYNTHETIC_CLAP_PATH);
    if (! bundle.exists())
    {
        pluginsPane_->setScanStatus (
            juce::String ("Synthetic test plug-in not found at ")
            + IDA_SYNTHETIC_CLAP_PATH);
        return;
    }

    PluginDescriptor d;
    d.format       = PluginFormat::Clap;
    d.uniqueId     = "com.ida.synthetic.test";
    d.version      = "1.0.0"; // matches kDescriptor.version in tests/fixtures/SyntheticTestPlugin.cpp
    d.name         = "Synthetic Test Plug-in";
    d.manufacturer = "IDA";
    d.filePath     = bundle.getFullPathName().toStdString();
    openPluginEditor (d);
   #else
    pluginsPane_->setScanStatus (
        "Synthetic test plug-in path not compiled in "
        "(IDA_SYNTHETIC_CLAP_PATH undefined).");
   #endif
}
#endif

ida::SlotLookup MainComponent::slotLookup() const
{
    // M7 S9 "one editor per bus" model: openPluginEditor allocates a fresh
    // bus per editor and pushes its busId onto openEditorBusIds_, with the
    // plug-in living at slot 0 of that bus's single-entry chain. The save
    // tree's walk visits each chain-bearing Constituent in the same order
    // editors were opened, so the entry index collapses to the bus index.
    // Message-thread only (save/load callbacks + open/close all run there),
    // so reading openEditorBusIds_ needs no lock. When the engine grows
    // nested chains (M11+), this resolves through the save orchestrator.
    return [this] (const ida::Constituent&, std::size_t entryIndex)
        -> std::optional<ida::SlotLocation>
    {
        if (entryIndex >= openEditorBusIds_.size())
            return std::nullopt;
        return ida::SlotLocation { openEditorBusIds_[entryIndex], 0 };
    };
}

void MainComponent::openPluginEditor (const PluginDescriptor& descriptor)
{
    const auto hostBinary = hostBinaryPath();
    if (! hostBinary.existsAsFile())
    {
        // Dev-loop or broken bundle. The UI button should be disabled in
        // this case (Task 5 handles enable/disable + tooltip), so reaching
        // here is a contract violation. Bail silently rather than spawning
        // a child that will fail.
        return;
    }

    const juce::File clapBundle (juce::String (descriptor.filePath));
    if (! clapBundle.exists())
        return;

    const auto busId = nextScratchBusId_++;

    ida::EffectChain chain;
    chain = chain.withAppended (ida::EffectChainEntry::makePlugin (descriptor, descriptor.name));

    effectChainHost_.configureBus (busId, chain, hostBinary, clapBundle);

    // M7 S9 — Reaper-style: the child process owns the NSWindow. Ask
    // it to show its editor; the child's gui_cocoa.mm creates a
    // top-level NSWindow + delegates close events back to the engine
    // via PluginGuiState.responseContextId on the next poll cycle.
    constexpr std::uint32_t kDefaultEditorWidth  = 600;
    constexpr std::uint32_t kDefaultEditorHeight = 400;
    effectChainHost_.requestEditorShow (
        busId, /*slot*/ 0, kDefaultEditorWidth, kDefaultEditorHeight);

    openEditorBusIds_.push_back (busId);
}

std::int64_t MainComponent::firstOpenBusIdForTesting() const noexcept
{
    if (openEditorBusIds_.empty())
        return -1;
    return openEditorBusIds_.front();
}

std::int64_t MainComponent::childPidForTestingAtBusId (std::int64_t busId) const noexcept
{
    return effectChainHost_.childPidForTestingAtSlot (busId, 0);
}

void MainComponent::closePluginEditor (std::int64_t busId)
{
    // Empty chain → host's stale-slot eraser removes the slot in
    // configureBus, supervisor reaps the child. The child's NSWindow
    // dies with the process; no engine-side window to dispose of.
    const auto hostBinary = hostBinaryPath();
    effectChainHost_.configureBus (busId, ida::EffectChain{}, hostBinary, juce::File{});

    openEditorBusIds_.erase (
        std::remove (openEditorBusIds_.begin(), openEditorBusIds_.end(), busId),
        openEditorBusIds_.end());
}

} // namespace ida
