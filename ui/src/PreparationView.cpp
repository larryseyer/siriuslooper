#include "ida/PreparationView.h"

#include "ida/IdaPalette.h"

namespace ida
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

    // Colour method (see docs/design/ida-colour-method.md): a phrase takes its
    // own OTTO hue (keyed by ConstituentId, matching its Pill on the timeline);
    // loops nested under it take a shade of that hue, stepped by loop order;
    // groups stay neutral. We track the current phrase as we walk the tree in
    // depth-first order, using indent to know when we have left its subtree.
    std::int64_t currentPhraseId    = 0;
    int          currentPhraseIndent = -1;
    int          loopOrderInPhrase   = 0;

    for (const auto& row : state_.rows)
    {
        // Leaving the current phrase's subtree clears it (any row at or above the
        // phrase's indent that is not deeper than it).
        if (currentPhraseIndent >= 0 && row.indentLevel <= currentPhraseIndent
            && row.kind != "loop")
            currentPhraseIndent = -1;

        juce::Colour rowColour = juce::Colours::white; // groups + fallback
        if (row.kind == "phrase")
        {
            currentPhraseId     = row.id.value();
            currentPhraseIndent = row.indentLevel;
            loopOrderInPhrase   = 0;
            rowColour           = palette::phraseColour (currentPhraseId);
        }
        else if (row.kind == "loop")
        {
            const auto phraseHue = (currentPhraseIndent >= 0)
                ? palette::phraseColour (currentPhraseId)
                : palette::phraseColour (row.id.value()); // loop with no phrase parent
            rowColour = palette::loopShade (phraseHue, loopOrderInPhrase++);
        }

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

        g.setColour (rowColour);
        g.drawText (text, line, juce::Justification::centredLeft, false);
    }
}

} // namespace ida
