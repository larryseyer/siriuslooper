#include "ida/PerformanceView.h"

namespace sirius
{

void PerformanceView::setState (PerformanceViewState newState)
{
    state_ = std::move (newState);
    repaint();
}

void PerformanceView::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds().reduced (24);
    if (state_.isSilent)
    {
        g.setColour (juce::Colours::darkgrey);
        g.setFont (juce::FontOptions (44.0f));
        g.drawText ("—", area, juce::Justification::centred, false);
        return;
    }

    auto top = area.removeFromTop (area.getHeight() * 2 / 3);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (juce::jmax (32.0f, static_cast<float> (top.getHeight()) * 0.45f)));
    g.drawText (state_.currentPhraseName, top, juce::Justification::centred, false);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (juce::jmax (16.0f, static_cast<float> (area.getHeight()) * 0.45f)));
    g.drawText (state_.cycleStatus, area, juce::Justification::centred, false);
}

} // namespace sirius
