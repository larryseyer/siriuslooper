#include "ida/VideoPreview.h"

namespace ida
{

void VideoPreview::setFrame (juce::Image frame)
{
    current_ = std::move (frame);
    repaint();
}

void VideoPreview::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    if (! current_.isValid())
        return;

    // Letterbox the frame inside the available bounds, preserving aspect.
    const float frameAspect = static_cast<float> (current_.getWidth())
                            / static_cast<float> (juce::jmax (1, current_.getHeight()));
    const auto area = getLocalBounds().toFloat();
    const float boundsAspect = area.getWidth() / juce::jmax (1.0f, area.getHeight());

    juce::Rectangle<float> target = area;
    if (frameAspect > boundsAspect)
    {
        const float height = area.getWidth() / frameAspect;
        target = area.withHeight (height).withY (area.getY() + (area.getHeight() - height) * 0.5f);
    }
    else
    {
        const float width = area.getHeight() * frameAspect;
        target = area.withWidth (width).withX (area.getX() + (area.getWidth() - width) * 0.5f);
    }

    g.drawImage (current_, target);
}

} // namespace ida
