#pragma once

#include "DemoSession.h"

#include "sirius/RenderPipeline.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace sirius
{

/// The M3 minimal functional UI: a session inspector. It builds a demo
/// Constituent tree, runs it through the RenderPipeline, and lets the operator
/// scrub a playhead across LMC time to watch which loops the pipeline reports
/// as sounding. There is no audio here — audio-device wiring is the
/// operator-deferred half of M2 — but the conceptual-time model and the render
/// pipeline are exercised end-to-end against a real tree.
class SessionInspector final : public juce::Component
{
public:
    SessionInspector();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void paintTree (juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintActiveReads (juce::Graphics& g, juce::Rectangle<int> area) const;

    DemoSession session_;
    RenderPipeline pipeline_;
    juce::Slider playhead_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionInspector)
};

} // namespace sirius
