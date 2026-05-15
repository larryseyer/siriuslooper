#include "sirius/PreparationView.h"

namespace sirius
{

void PreparationView::setState (PreparationViewState newState)
{
    state_ = std::move (newState);
    repaint();
}

int PreparationView::totalHeight() const
{
    return static_cast<int> (state_.rows.size()) * rowHeight;
}

void PreparationView::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds();
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, 0));

    for (const auto& row : state_.rows)
    {
        auto line = area.removeFromTop (rowHeight);
        line.removeFromLeft (8 + row.indentLevel * 24);

        const juce::String idText = "#" + juce::String (row.id.value());
        const juce::String nameText = row.name.empty() ? juce::String ("(unnamed)")
                                                       : juce::String (row.name);
        juce::String text;
        text << idText << "  " << row.kind << "  " << nameText
             << "  " << juce::String (row.durationWholeNotes.toString()) << " wn";
        if (row.hasEffectChain) text << "  fx:" << juce::String (row.effectCount);
        if (row.hasLocalMeter)  text << "  meter";
        if (row.hasLocalTempoMap) text << "  tempo";
        if (row.isRoleFillable) text << "  role-fillable";

        g.setColour (juce::Colours::white);
        g.drawText (text, line, juce::Justification::centredLeft, false);
    }
}

} // namespace sirius
