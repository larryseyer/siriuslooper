#include "SessionInspector.h"

#include "sirius/Constituent.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"

namespace sirius
{

namespace
{
    /// The playhead slider works in sixteenths of an LMC second, so the value
    /// it reports converts to an exact Rational — the engine never sees a
    /// floating-point time.
    constexpr int ticksPerSecond = 16;

    Rational playheadToLmc (double sliderValue)
    {
        return Rational (static_cast<std::int64_t> (sliderValue), ticksPerSecond);
    }

    juce::String spanText (const Constituent& c)
    {
        return "[" + juce::String (c.conceptualIn().wholeNotes().toString())
             + " .. " + juce::String (c.conceptualOut().wholeNotes().toString()) + ") wn";
    }

    /// Recursively draws the Constituent hierarchy, one indented line per node.
    /// Returns the y just below the subtree it drew.
    int drawNode (juce::Graphics& g, const Constituent& c, int x, int y, int width)
    {
        const juce::String role = c.isLoop() ? "loop" : (c.isPhrase() ? "phrase" : "group");
        juce::String line = c.name().empty() ? juce::String ("(unnamed)")
                                             : juce::String (c.name());
        line << "  #" << juce::String (c.id().value()) << "  " << role
             << "  " << spanText (c);

        g.drawText (line, x, y, width - x, 20, juce::Justification::centredLeft, true);
        y += 22;

        for (const auto& child : c.children())
            y = drawNode (g, *child, x + 24, y, width);

        return y;
    }
}

SessionInspector::SessionInspector()
    : session_ (buildDemoSession()),
      pipeline_ (session_.root, session_.sessionToLmc)
{
    const double maxTicks = session_.lengthLmcSeconds.toDouble()
                          * static_cast<double> (ticksPerSecond);

    playhead_.setSliderStyle (juce::Slider::LinearHorizontal);
    playhead_.setRange (0.0, maxTicks, 1.0);
    playhead_.setValue (0.0, juce::dontSendNotification);
    playhead_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    playhead_.onValueChange = [this] { repaint(); };
    addAndMakeVisible (playhead_);

    setSize (960, 600);
}

void SessionInspector::resized()
{
    auto area = getLocalBounds().reduced (16);
    playhead_.setBounds (area.removeFromBottom (32));
}

void SessionInspector::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds().reduced (16);
    area.removeFromBottom (40); // leave room for the playhead slider

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("Sirius Looper — session inspector",
                area.removeFromTop (32), juce::Justification::centredLeft, false);

    auto readsArea = area.removeFromBottom (160);
    area.removeFromBottom (12);

    paintTree (g, area);
    paintActiveReads (g, readsArea);
}

void SessionInspector::paintTree (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Constituent tree", area.removeFromTop (22),
                juce::Justification::centredLeft, false);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, 0));
    drawNode (g, *session_.root, area.getX(), area.getY(), area.getRight());
}

void SessionInspector::paintActiveReads (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const Rational lmcTime = playheadToLmc (playhead_.getValue());
    const auto reads = pipeline_.activeReadsAt (lmcTime);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Playhead " + juce::String (lmcTime.toDouble(), 3) + " s  —  "
                    + juce::String (reads.size()) + " loop(s) sounding",
                area.removeFromTop (22), juce::Justification::centredLeft, false);

    g.setColour (juce::Colours::aqua);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, 0));

    if (reads.empty())
    {
        g.drawText ("(silence — no loop is within its placement span here)",
                    area.removeFromTop (20), juce::Justification::centredLeft, false);
        return;
    }

    for (const auto& read : reads)
    {
        juce::String line;
        line << "loop #" << juce::String (read.loop.value())
             << "  <- tape #" << juce::String (read.tape.value())
             << "  @ " << juce::String (read.tapePosition.toString()) << " s"
             << "  cycle " << juce::String (read.cycle);
        g.drawText (line, area.removeFromTop (20), juce::Justification::centredLeft, false);
    }
}

} // namespace sirius
