#include "FileInputPlayerWindow.h"

#include <juce_audio_formats/juce_audio_formats.h>

#if JUCE_MAC
 #import <AppKit/AppKit.h>
#endif

namespace ida
{

namespace
{

// JUCE's Component::setAlwaysOnTop maps to NSWindow level = NSFloatingWindowLevel,
// which floats above same-app windows but can be displaced by other apps when
// IDA isn't the active app. NSStatusWindowLevel is the canonical macOS level
// for tool windows that must persist above other apps' windows — matches the
// floating-tool convention used by DAWs (Pro Tools, Logic helper windows).
// No-op on non-macOS platforms.
void bumpNativeAlwaysOnTopLevel (juce::Component& c, bool onTop)
{
   #if JUCE_MAC
    if (auto* peer = c.getPeer())
    {
        if (auto* nsView = static_cast<NSView*> (peer->getNativeHandle()))
        {
            if (NSWindow* nsWindow = [nsView window])
                [nsWindow setLevel: onTop ? NSStatusWindowLevel : NSNormalWindowLevel];
        }
    }
   #else
    juce::ignoreUnused (c, onTop);
   #endif
}

} // namespace

// ============================================================================
// Content — the actual UI panel hosted by the DocumentWindow. Internal class;
// only its forward declaration is in the header.
//
// Layout (top→bottom):
//   - Transport row:   ▶ ⏸ ⏹      [scrubber slider]      [loop-scope cycle]
//   - Track list:      ListBox of FileInputEntry rows
//   - List toolbar:    +  ▲  ▼  –
// ============================================================================
class FileInputPlayerWindow::Content : public juce::Component,
                                       public juce::ListBoxModel
{
public:
    // Reaper-style pin glyph. Filled circle + downward stem when pinned;
    // hollow circle + stem when not. Click toggles always-on-top via
    // FileInputPlayerWindow::setAlwaysOnTopWithNativeBump (which also bumps
    // the NSWindow level — see header).
    class PinButton : public juce::Button
    {
    public:
        PinButton() : juce::Button ("Always on top")
        {
            setTooltip ("Pin window above all others");
        }

        void setPinned (bool p) { if (pinned_ != p) { pinned_ = p; repaint(); } }
        bool isPinned() const noexcept { return pinned_; }

    private:
        void paintButton (juce::Graphics& g, bool isOverMouse, bool /*isDown*/) override
        {
            const auto b = getLocalBounds().toFloat().reduced (4.0f);
            const float cx = b.getCentreX();
            const float headR = 3.5f;
            const float headTopY = b.getY();
            const float shaftTopY = headTopY + headR * 2.0f;
            const float shaftBotY = b.getBottom();

            const auto colour = pinned_ ? juce::Colours::white
                                        : juce::Colours::grey.withAlpha (isOverMouse ? 0.9f : 0.65f);
            g.setColour (colour);

            if (pinned_)
                g.fillEllipse (cx - headR, headTopY, headR * 2.0f, headR * 2.0f);
            else
                g.drawEllipse (cx - headR, headTopY, headR * 2.0f, headR * 2.0f, 1.2f);

            g.drawLine (cx, shaftTopY, cx, shaftBotY, 1.5f);
        }

        bool pinned_ { false };
    };

    Content (FileInputRegistry& registry, InputId id)
        : registry_ (registry), id_ (id)
    {
        playButton_.setButtonText ("Play");
        pauseButton_.setButtonText ("Pause");
        stopButton_.setButtonText ("Stop");
        loopButton_.setButtonText (loopGlyph (LoopScope::Off));
        addButton_.setButtonText ("+");
        upButton_.setButtonText ("Up");
        downButton_.setButtonText ("Dn");
        removeButton_.setButtonText ("-");

        playButton_.onClick   = [this] { registry_.playFileInput  (id_); };
        pauseButton_.onClick  = [this] { registry_.pauseFileInput (id_); };
        stopButton_.onClick   = [this] { registry_.stopFileInput  (id_); };
        loopButton_.onClick   = [this] { cycleLoopScope(); };
        addButton_.onClick    = [this] { pickAndAppendFile(); };
        upButton_.onClick     = [this] { moveSelected (-1); };
        downButton_.onClick   = [this] { moveSelected (+1); };
        removeButton_.onClick = [this] { removeSelected(); };
        pinButton_.onClick    = [this]
        {
            if (auto* win = findParentComponentOfClass<FileInputPlayerWindow>())
                win->setAlwaysOnTopWithNativeBump (! pinButton_.isPinned());
        };

        scrubber_.setRange (0.0, 1.0, 0.0);
        scrubber_.setSliderStyle (juce::Slider::LinearHorizontal);
        scrubber_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        scrubber_.onDragEnd = [this]
        {
            // Seek lands on drag-end, not while scrubbing — avoids spamming
            // the source with tens of seeks per second.
            const auto frames = static_cast<std::int64_t> (scrubber_.getValue());
            registry_.seekFileInput (id_, frames);
        };

        // Spec §4.6 CurrentTrackLabel — always shows the playing entry's
        // filename, independent of whether the list is scrolled out of view.
        currentTrackLabel_.setText ("Now: —", juce::dontSendNotification);
        currentTrackLabel_.setJustificationType (juce::Justification::centredLeft);
        currentTrackLabel_.setMinimumHorizontalScale (1.0f);

        trackList_.setModel (this);
        trackList_.setRowHeight (22);
        trackList_.setMultipleSelectionEnabled (false);

        addAndMakeVisible (playButton_);
        addAndMakeVisible (pauseButton_);
        addAndMakeVisible (stopButton_);
        addAndMakeVisible (scrubber_);
        addAndMakeVisible (loopButton_);
        addAndMakeVisible (pinButton_);
        addAndMakeVisible (currentTrackLabel_);
        addAndMakeVisible (trackList_);
        addAndMakeVisible (addButton_);
        addAndMakeVisible (upButton_);
        addAndMakeVisible (downButton_);
        addAndMakeVisible (removeButton_);

        setSize (520, 320);
    }

    ~Content() override
    {
        trackList_.setModel (nullptr);
    }

    // ------------------------------------------------------------------
    // Layout
    // ------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto* d = registry_.fileInputDescriptor (id_);
        const float alpha = (d != nullptr)
            ? juce::jlimit (0.5f, 1.0f, d->windowOpacity)
            : 0.92f;
        const auto bg = getLookAndFeel().findColour (
            juce::ResizableWindow::backgroundColourId);
        g.fillAll (bg.withAlpha (alpha));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        // Transport row.
        auto transport = r.removeFromTop (28);
        playButton_  .setBounds (transport.removeFromLeft (52));
        transport.removeFromLeft (4);
        pauseButton_ .setBounds (transport.removeFromLeft (56));
        transport.removeFromLeft (4);
        stopButton_  .setBounds (transport.removeFromLeft (52));
        transport.removeFromLeft (8);
        // Pin sits at the very top-right of the transport row (Reaper convention
        // — small floating-pin glyph in the window's top-right corner).
        pinButton_   .setBounds (transport.removeFromRight (24));
        transport.removeFromRight (4);
        loopButton_  .setBounds (transport.removeFromRight (72));
        transport.removeFromRight (8);
        scrubber_    .setBounds (transport);

        r.removeFromTop (6);

        // CurrentTrackLabel — between scrubber and track list.
        currentTrackLabel_.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);

        // List toolbar (bottom).
        auto toolbar = r.removeFromBottom (26);
        addButton_   .setBounds (toolbar.removeFromLeft (32));
        toolbar.removeFromLeft (8);
        upButton_    .setBounds (toolbar.removeFromLeft (40));
        toolbar.removeFromLeft (4);
        downButton_  .setBounds (toolbar.removeFromLeft (40));
        toolbar.removeFromLeft (8);
        removeButton_.setBounds (toolbar.removeFromLeft (32));

        r.removeFromBottom (4);

        // Track list fills the middle.
        trackList_.setBounds (r);
    }

    // ------------------------------------------------------------------
    // ListBoxModel
    // ------------------------------------------------------------------
    int getNumRows() override
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        return desc == nullptr ? 0 : (int) desc->entries.size();
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool rowIsSelected) override
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        if (desc == nullptr || rowNumber < 0 || rowNumber >= (int) desc->entries.size())
            return;

        const auto& entry = desc->entries[(std::size_t) rowNumber];
        const bool isCurrent = (entry.entryId == lastState_.currentEntry);

        if (rowIsSelected)
            g.fillAll (juce::Colour (0xff3a4a6a));
        else
            g.fillAll (rowNumber % 2 ? juce::Colour (0xff1a1a1a)
                                     : juce::Colour (0xff222222));

        const auto filename = juce::File (entry.path).getFileName();

        g.setColour (entry.missing ? juce::Colours::red
                                   : (isCurrent ? juce::Colours::yellow
                                                : juce::Colours::white));
        g.setFont (juce::FontOptions (13.0f,
                                      isCurrent ? juce::Font::bold : 0));
        const auto rowText = "#" + juce::String (rowNumber + 1) + "  " + filename;
        g.drawText (rowText, 8, 0, width - 16, height, juce::Justification::centredLeft);
    }

    void selectedRowsChanged (int /*lastRowSelected*/) override
    {
        refreshButtonEnables();
    }

    // ------------------------------------------------------------------
    // Drag-to-move + right-click forwarding. JUCE does NOT bubble
    // mouseDown to parents, so right-clicks on Content background must
    // be forwarded explicitly to the parent window's context menu —
    // otherwise the menu becomes unreachable once the native title bar
    // goes away in Task 4. Child controls (buttons/scrubber/list)
    // consume their own mouseDown first, so only background clicks
    // reach here.
    // ------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (auto* win = findParentComponentOfClass<FileInputPlayerWindow>())
                win->showOpacityMenu();
            return;
        }
        if (auto* top = getTopLevelComponent())
            dragger_.startDraggingComponent (top, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;   // no drag-start happened on a right-click mouseDown
        if (auto* top = getTopLevelComponent())
            dragger_.dragComponent (top, e, nullptr);
    }

    // ------------------------------------------------------------------
    // Driven by the window's 30 Hz timer
    // ------------------------------------------------------------------
    void refresh()
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        if (desc == nullptr)
            return;

        const auto state = registry_.fileInputTransportState (id_);

        // Sync pin glyph from descriptor — covers external toggles (right-click
        // menu "Always on top" item) as well as the persisted-on-construction
        // case. PinButton's repaint is no-op when state hasn't changed.
        pinButton_.setPinned (desc->alwaysOnTop);

        // Detect entry-change / list-size-change to repaint the list.
        const bool currentEntryChanged = (state.currentEntry != lastState_.currentEntry);
        const bool listSizeChanged     = (lastListSize_ != (int) desc->entries.size());
        const bool loopScopeChanged    = (state.loopScope != lastState_.loopScope);

        lastState_     = state;
        lastListSize_  = (int) desc->entries.size();

        if (listSizeChanged)
            trackList_.updateContent();
        if (currentEntryChanged || listSizeChanged)
            trackList_.repaint();

        if (loopScopeChanged)
            loopButton_.setButtonText (loopGlyph (state.loopScope));

        // CurrentTrackLabel — reflect the playing entry's filename.
        if (currentEntryChanged || listSizeChanged)
        {
            juce::String label = "Now: —";
            if (state.currentEntry != PlaylistEntryId (-1))
            {
                for (const auto& e : desc->entries)
                {
                    if (e.entryId == state.currentEntry)
                    {
                        label = "Now: " + juce::File (e.path).getFileName();
                        break;
                    }
                }
            }
            currentTrackLabel_.setText (label, juce::dontSendNotification);
        }

        // Scrubber. Range derives from the current entry's cached duration
        // (when known); otherwise we fall back to a stable [0,1] range so
        // the playhead-as-fraction still moves.
        std::int64_t totalFrames = 0;
        for (const auto& e : desc->entries)
        {
            if (e.entryId == state.currentEntry && e.durationFrames.has_value())
            {
                totalFrames = *e.durationFrames;
                break;
            }
        }

        if (totalFrames > 0)
        {
            // Avoid spamming setRange when it hasn't changed.
            if (std::abs (scrubber_.getMaximum() - (double) totalFrames) > 0.5)
                scrubber_.setRange (0.0, (double) totalFrames, 1.0);

            if (! scrubber_.isMouseButtonDown())
                scrubber_.setValue ((double) state.playheadFrames,
                                    juce::dontSendNotification);
        }
        else
        {
            if (std::abs (scrubber_.getMaximum() - 1.0) > 0.0001)
                scrubber_.setRange (0.0, 1.0, 0.0);

            if (! scrubber_.isMouseButtonDown())
                scrubber_.setValue (0.0, juce::dontSendNotification);
        }

        // Pause vs Play feedback — bold the active one.
        playButton_ .setToggleState (state.isPlaying,    juce::dontSendNotification);
        pauseButton_.setToggleState (! state.isPlaying,  juce::dontSendNotification);

        refreshButtonEnables();
    }

    int selectedRow() const { return trackList_.getSelectedRow(); }

private:
    static juce::String loopGlyph (LoopScope s)
    {
        // Per plan watchpoint: "Off / trk / list" text labels (the unicode
        // arrows render inconsistently across system fonts).
        switch (s)
        {
            case LoopScope::Off:   return "Loop: Off";
            case LoopScope::Track: return "Loop: trk";
            case LoopScope::List:  return "Loop: list";
        }
        return "Loop: Off";
    }

    void cycleLoopScope()
    {
        const auto next = [] (LoopScope s) -> LoopScope
        {
            switch (s)
            {
                case LoopScope::Off:   return LoopScope::Track;
                case LoopScope::Track: return LoopScope::List;
                case LoopScope::List:  return LoopScope::Off;
            }
            return LoopScope::Off;
        };
        registry_.setFileInputLoopScope (id_, next (lastState_.loopScope));
    }

    void pickAndAppendFile()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Append audio file to playlist",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.flac");

        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                registry_.addFileInputEntry (id_, f.getFullPathName().toStdString());
                trackList_.updateContent();
                trackList_.repaint();
            });
    }

    void moveSelected (int delta)
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        if (desc == nullptr) return;

        const int row = trackList_.getSelectedRow();
        if (row < 0 || row >= (int) desc->entries.size()) return;

        const int newIndex = juce::jlimit (0, (int) desc->entries.size() - 1, row + delta);
        if (newIndex == row) return;

        const auto entry = desc->entries[(std::size_t) row].entryId;
        if (registry_.reorderFileInput (id_, entry, newIndex))
        {
            trackList_.selectRow (newIndex);
            trackList_.repaint();
        }
    }

    void removeSelected()
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        if (desc == nullptr) return;

        const int row = trackList_.getSelectedRow();
        if (row < 0 || row >= (int) desc->entries.size()) return;

        const auto entry = desc->entries[(std::size_t) row].entryId;
        // Defensive: removeFileInputEntry refuses the currently-playing
        // entry; the button is also disabled in that state but the source
        // is the source of truth.
        if (registry_.removeFileInputEntry (id_, entry))
        {
            trackList_.updateContent();
            trackList_.repaint();
        }
    }

    void refreshButtonEnables()
    {
        const auto* desc = registry_.fileInputDescriptor (id_);
        const int row = trackList_.getSelectedRow();
        const int size = desc == nullptr ? 0 : (int) desc->entries.size();
        const bool haveRow = (row >= 0 && row < size);

        upButton_  .setEnabled (haveRow && row > 0);
        downButton_.setEnabled (haveRow && row < size - 1);

        // Cannot remove the currently-playing entry — matches the source's
        // own refusal logic (so the button reflects reality).
        bool canRemove = haveRow;
        if (canRemove && desc != nullptr)
        {
            const auto entry = desc->entries[(std::size_t) row].entryId;
            if (entry == lastState_.currentEntry)
                canRemove = false;
        }
        removeButton_.setEnabled (canRemove);
    }

    FileInputRegistry& registry_;
    InputId            id_;

    juce::TextButton playButton_, pauseButton_, stopButton_;
    juce::TextButton loopButton_;
    juce::TextButton addButton_, upButton_, downButton_, removeButton_;
    juce::Slider     scrubber_;
    juce::Label      currentTrackLabel_;
    juce::ListBox    trackList_;
    PinButton        pinButton_;

    std::unique_ptr<juce::FileChooser> chooser_;

    FileInputRegistry::FileInputTransportState lastState_ {};
    int lastListSize_ { -1 };

    juce::ComponentDragger dragger_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
};

// ============================================================================
// FileInputPlayerWindow
// ============================================================================
FileInputPlayerWindow::FileInputPlayerWindow (FileInputRegistry& registry, InputId id)
    : juce::DocumentWindow (
          // Title = descriptor displayName when available, else "File Input N".
          [&]
          {
              if (const auto* d = registry.fileInputDescriptor (id);
                  d != nullptr && ! d->displayName.empty())
                  return juce::String (d->displayName);
              return juce::String ("File Input ") + juce::String (id.value());
          }(),
          juce::Desktop::getInstance().getDefaultLookAndFeel()
              .findColour (juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      registry_ (registry),
      id_ (id)
{
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);
    setWantsKeyboardFocus (true);
    setOpaque (false);
    // ResizableWindow::paint fills the whole window with the bg colour BEFORE
    // Content::paint runs. Without this, that fill is opaque and Content's
    // alpha is invisible inside the content area. Force the parent fill
    // transparent so Content's per-paint alpha-fill is the only visible bg.
    setBackgroundColour (getBackgroundColour().withAlpha (0.0f));

    content_ = std::make_unique<Content> (registry_, id_);
    setContentNonOwned (content_.get(), true);

    setResizable (true, true);
    setResizeLimits (380, 220, 1200, 900);
    centreWithSize (getWidth(), getHeight());

    // Apply persisted always-on-top flag once on construction. Subsequent
    // toggles come through the pin button or the right-click menu, both of
    // which call setAlwaysOnTopWithNativeBump.
    bool persistedOnTop = false;
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        persistedOnTop = d->alwaysOnTop;
    setAlwaysOnTop (persistedOnTop);

    startTimerHz (30);
    setVisible (true);

    // Bump the native window level AFTER the peer is fully realized
    // (setVisible orders the window front and finalises the NSWindow).
    bumpNativeAlwaysOnTopLevel (*this, persistedOnTop);
}

void FileInputPlayerWindow::setAlwaysOnTopWithNativeBump (bool onTop)
{
    registry_.setFileInputAlwaysOnTop (id_, onTop);
    setAlwaysOnTop (onTop);
    bumpNativeAlwaysOnTopLevel (*this, onTop);
}

FileInputPlayerWindow::~FileInputPlayerWindow()
{
    stopTimer();
    clearContentComponent();   // detaches content_ before its unique_ptr destructs
}

void FileInputPlayerWindow::closeButtonPressed()
{
    // Hide; the owner (MainComponent — Task 11) destroys the window from
    // its own slot at a safe moment. `delete this` here would race the
    // owner's unique_ptr.
    setVisible (false);
}

void FileInputPlayerWindow::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showOpacityMenu();
        return;
    }
    juce::DocumentWindow::mouseDown (e);
}

bool FileInputPlayerWindow::keyPressed (const juce::KeyPress& key)
{
    // Cmd-W on macOS, Ctrl-W on Win/Linux — JUCE's commandModifier resolves
    // to the platform-correct modifier so one handler covers both.
    if (key == juce::KeyPress ('w', juce::ModifierKeys::commandModifier, 0))
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}

int FileInputPlayerWindow::getDesktopWindowStyleFlags() const
{
    // Add `windowIsSemiTransparent` so Content::paint's alpha-fill renders
    // through the OS compositor. TopLevelWindow's recreateDesktopWindow()
    // (triggered by setUsingNativeTitleBar(false) in the ctor) calls this
    // and creates the peer with the full flag set automatically.
    return juce::DocumentWindow::getDesktopWindowStyleFlags()
         | juce::ComponentPeer::windowIsSemiTransparent;
}

void FileInputPlayerWindow::timerCallback()
{
    if (content_ != nullptr)
        content_->refresh();
}

void FileInputPlayerWindow::showOpacityMenu()
{
    juce::PopupMenu opacity;
    const std::array<int, 5> presets { 60, 75, 85, 92, 100 };

    float current = 0.92f;
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        current = d->windowOpacity;

    for (int pct : presets)
    {
        const bool ticked = std::abs (current - pct / 100.0f) < 0.01f;
        opacity.addItem (juce::String (pct) + "%",
                         true, ticked,
                         [this, pct]
                         {
                             const float a = pct / 100.0f;
                             registry_.setFileInputWindowOpacity (id_, a);
                             if (content_ != nullptr) content_->repaint();
                         });
    }

    // Spec §3 / §4.6 — sixth `Custom…` entry opens a slider in [0.5, 1.0].
    opacity.addSeparator();
    opacity.addItem ("Custom…", true, false, [this] { showCustomOpacityDialog(); });

    bool currentOnTop = false;
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        currentOnTop = d->alwaysOnTop;

    juce::PopupMenu root;
    root.addItem ("Close window", [this] { closeButtonPressed(); });
    root.addSeparator();
    root.addItem ("Always on top",
                  true,                     // enabled
                  currentOnTop,             // ticked
                  [this, currentOnTop] { setAlwaysOnTopWithNativeBump (! currentOnTop); });
    root.addSubMenu ("Window opacity", opacity);
    root.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (this)
                            .withMousePosition());
}

void FileInputPlayerWindow::showCustomOpacityDialog()
{
    float current = 0.92f;
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        current = juce::jlimit (0.5f, 1.0f, d->windowOpacity);

    // Modal AlertWindow with an inline slider — simpler and more
    // operator-obvious than wedging a CustomComponent into the popup,
    // and the slider stays put while dragging.
    auto* aw = new juce::AlertWindow ("Window opacity",
                                      "Drag to set window opacity (50–100%).",
                                      juce::MessageBoxIconType::NoIcon, this);

    auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                  juce::Slider::TextBoxRight);
    slider->setRange (0.50, 1.00, 0.01);
    slider->setValue (current, juce::dontSendNotification);
    slider->setSize (320, 28);
    slider->setNumDecimalPlacesToDisplay (2);

    // Live preview as the operator drags — the registry write happens on
    // OK so cancelling reverts cleanly.
    auto* sliderPtr = slider.get();
    slider->onValueChange = [this, sliderPtr]
    {
        const float a = juce::jlimit (0.5f, 1.0f,
                                      static_cast<float> (sliderPtr->getValue()));
        registry_.setFileInputWindowOpacity (id_, a);
        if (content_ != nullptr) content_->repaint();
    };

    aw->addCustomComponent (slider.get());
    aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const float originalOpacity = current;
    auto sliderOwned = std::shared_ptr<juce::Slider> (slider.release());

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [this, aw, sliderOwned, originalOpacity] (int result)
            {
                if (result == 1)
                {
                    const float a = juce::jlimit (0.5f, 1.0f,
                                                  static_cast<float> (sliderOwned->getValue()));
                    registry_.setFileInputWindowOpacity (id_, a);
                }
                else
                {
                    // Cancel: restore the pre-dialog value through the registry
                    // so Content::paint reverts on the next repaint.
                    registry_.setFileInputWindowOpacity (id_, originalOpacity);
                }
                if (content_ != nullptr) content_->repaint();
                delete aw;
            }),
        false);
}

} // namespace ida
