#pragma once

#include "ida/PerformanceViewState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sirius
{

/// The Performance view: a glanceable, eyes-free surface (white paper 14.5,
/// 14.9). Renders a tiny number of large legible elements over a calm
/// background — the current phrase name and one cycle-status line — and never
/// more. Anything denser belongs to the Preparation view.
class PerformanceView final : public juce::Component
{
public:
    PerformanceView() = default;

    /// Replaces the displayed state. Cheap: a value-type assignment plus a
    /// repaint request. Safe to call from the message thread on every animation
    /// tick — the work the view does is bounded and small.
    void setState (PerformanceViewState newState);
    const PerformanceViewState& state() const noexcept { return state_; }

    void paint (juce::Graphics& g) override;

private:
    PerformanceViewState state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformanceView)
};

} // namespace sirius
