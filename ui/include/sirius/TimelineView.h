#pragma once

#include "sirius/ConstituentId.h"
#include "sirius/TapeId.h"
#include "sirius/TimelineViewState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace sirius
{

/// The Preparation tab's timeline: a strip per input, Pills laid across.
/// Pure renderer over a TimelineViewState — the selector decides what
/// shows; this component decides how it looks. Click-to-arm is the only
/// gesture wired today; group capture by chording arms is preserved by
/// the data model (per-tape arm state on the host) and will be honoured
/// once CaptureSession grows a per-tape mode (M8 work).
class TimelineView final : public juce::Component
{
public:
    /// Fired when the user clicks a strip's arm region. The host toggles
    /// arm state for `tape` and re-pushes a refreshed TimelineViewState.
    std::function<void (TapeId tape)> onArmClicked;

    /// Fired when the user clicks anywhere on a strip outside the arm
    /// region. The host shifts focus to `tape` without changing its arm
    /// state — focus drives which TapeId the bottom bar's Mark In stamps.
    std::function<void (TapeId tape)> onFocusClicked;

    /// Fired when the user right-clicks (or ctrl-clicks) a Pill — the
    /// context-gesture entry point for per-Pill commands. The host shows
    /// a popup menu keyed off the wrapper's ConstituentId. No menu is
    /// shown when the gesture lands outside any Pill rectangle.
    std::function<void (ConstituentId wrapperId)> onPillContextMenuRequested;

    TimelineView() = default;

    void setState (TimelineViewState newState);
    const TimelineViewState& state() const noexcept { return state_; }

    /// Set the playhead position in LMC seconds. Pass `std::nullopt` to
    /// hide the overlay entirely. The playhead is renderer state, not
    /// view-state — the timeline's structure is selector-derived, the
    /// playhead is a gesture surface and lives with the renderer.
    void setPlayhead (std::optional<Rational> lmcSeconds);
    const std::optional<Rational>& playhead() const noexcept { return playhead_; }

    void paint     (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    /// Pixel dimensions exposed so a parent can size scrolling viewports.
    static constexpr int rowHeight        = 52;
    static constexpr int stripColumnWidth = 200;
    static constexpr int rulerHeight      = 22;

    int totalHeight() const;

private:
    juce::Rectangle<int> stripBounds (int rowIndex) const;
    juce::Rectangle<int> armHitBox   (int rowIndex) const;
    juce::Rectangle<int> contentArea (int rowIndex) const;

    /// Maps an LMC-seconds value to the X coordinate on the content area.
    int timeToX (Rational t) const;

    TimelineViewState        state_;
    std::optional<Rational>  playhead_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace sirius
