#pragma once

#include "ida/PreparationViewState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ida
{

/// The Preparation view: a dense readout of every Constituent in the tree.
/// Where the Performance view is eyes-free and three things at a time, the
/// Preparation view is "fine, precise, detailed" (white paper Part 14.6) —
/// the surface the performer uses when they are *allowed* to look at the
/// screen.
class PreparationView final : public juce::Component
{
public:
    PreparationView() = default;

    void setState (PreparationViewState newState);
    const PreparationViewState& state() const noexcept { return state_; }

    void paint (juce::Graphics& g) override;

    /// The fixed pixel height used per row, so a parent can size a scroll
    /// viewport without poking at the state.
    static constexpr int rowHeight = 22;
    int totalHeight() const;

private:
    PreparationViewState state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreparationView)
};

} // namespace ida
