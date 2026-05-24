#pragma once

#include "OTTOColours.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <utility>

namespace ida::app
{

/// Invisible click-catcher pinned to the top name-label band of a fader
/// strip. Catches right-click (desktop) and 500 ms long-press (touch) for
/// the per-strip context menu, and hosts an inline `juce::TextEditor` when
/// the operator commits to a rename. Reuses the TapesPane row's rename
/// pattern: return / focus-lost both commit; a `committed_` flag prevents
/// the double-fire when return fires first and the editor then loses focus.
///
/// Lifted out of OutputMixerPane (slice 3) so InputMixerPane (slice 4) can
/// share the same gesture without duplication. The Z order rule still holds
/// at the call site: the overlay must sit in front of the underlying strip
/// so its mouseDown fires before the strip's fader interaction below it.
class StripContextOverlay final : public juce::Component, private juce::Timer
{
public:
    StripContextOverlay (int idx,
                         std::function<void (int)> onContextMenu,
                         std::function<void (int, juce::String)> onCommitName,
                         std::function<void (int)> onSelect = {})
        : idx_ (idx),
          onContextMenu_ (std::move (onContextMenu)),
          onCommitName_  (std::move (onCommitName)),
          onSelect_      (std::move (onSelect))
    {
        // Transparent — the strip's painted name still shows through when no
        // rename editor is visible.
        setInterceptsMouseClicks (true, false);
    }

    void beginRename (const juce::String& currentName)
    {
        if (editor_ != nullptr) return;
        editor_ = std::make_unique<juce::TextEditor>();
        editor_->setText (currentName, false);
        editor_->selectAll();
        editor_->setColour (juce::TextEditor::backgroundColourId, otto::Colours::bg3);
        editor_->setColour (juce::TextEditor::textColourId,       otto::Colours::textPrimary);
        editor_->setColour (juce::TextEditor::outlineColourId,    otto::Colours::accent);
        editor_->onReturnKey = [this] { commit(); };
        editor_->onEscapeKey = [this] { cancel(); };
        editor_->onFocusLost = [this] { commit(); };
        addAndMakeVisible (*editor_);
        editor_->setBounds (getLocalBounds());
        editor_->grabKeyboardFocus();
        committed_ = false;
    }

private:
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (editor_ != nullptr) return;          // editor handles its own input
        if (e.mods.isPopupMenu())
        {
            onContextMenu_ (idx_);
            return;
        }
        longPressed_ = true;
        startTimer (500);                        // matches the pane's blank-area long-press
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isTimerRunning() && e.getDistanceFromDragStart() > 8)
        {
            stopTimer();
            longPressed_ = false;
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasArmed   = longPressed_;          // started a press
        const bool stillArmed = isTimerRunning();      // didn't get a drag-cancel
        stopTimer();
        longPressed_ = false;

        // Treat a short left-tap (no drag, no long-press fire, not popup) as a
        // SELECT gesture so the underlying bus / FX-return strip opens its
        // detail panel. Without this the overlay swallows every click and the
        // strip never sees a notifyChannelSelected. The bus strip's own
        // mouseDown only fires on areas the overlay doesn't cover, but the
        // overlay covers the natural target — the name band — so a select
        // path through the overlay is required.
        if (wasArmed && stillArmed && ! e.mods.isPopupMenu() && onSelect_)
            onSelect_ (idx_);
    }
    void timerCallback() override
    {
        stopTimer();
        if (longPressed_)
        {
            longPressed_ = false;
            onContextMenu_ (idx_);
        }
    }
    void commit()
    {
        if (committed_ || editor_ == nullptr) return;
        committed_ = true;
        const auto txt = editor_->getText().trim();
        editor_.reset();
        if (txt.isNotEmpty()) onCommitName_ (idx_, txt);
    }
    void cancel()
    {
        committed_ = true;   // skip the focus-lost path that follows
        editor_.reset();
    }

    int idx_ { -1 };
    std::function<void (int)> onContextMenu_;
    std::function<void (int, juce::String)> onCommitName_;
    std::function<void (int)> onSelect_;
    std::unique_ptr<juce::TextEditor> editor_;
    bool longPressed_ { false };
    bool committed_   { false };
};

} // namespace ida::app
