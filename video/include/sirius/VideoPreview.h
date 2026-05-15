#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sirius
{

/// A minimal video-preview surface: it takes a `juce::Image` and paints it
/// letterboxed into the available area. The component does *not* decode
/// frames, hold a tape, or know about pixel formats. Producing a
/// `juce::Image` from the raw VideoFrame bytes the tape carries is the
/// FFmpeg-bound work the M0 spike will unlock; once available, the operator
/// wires the decode→Image conversion and feeds the result here per
/// animation tick.
///
/// White paper Part 5.3: the architecture above the membrane is identical
/// regardless of frame-rate-conversion strategy. The preview surface sits
/// above the membrane, so it stays small and strategy-agnostic.
class VideoPreview final : public juce::Component
{
public:
    VideoPreview() = default;

    /// Replaces the currently displayed frame and requests a repaint. Pass
    /// a null `juce::Image` to clear the preview to the background colour —
    /// the right behaviour when the tape has no frame at the playhead.
    void setFrame (juce::Image frame);

    /// Whether a frame is currently held.
    bool hasFrame() const noexcept { return current_.isValid(); }

    /// The currently displayed image, or a null image when none has been set.
    const juce::Image& currentFrame() const noexcept { return current_; }

    void paint (juce::Graphics& g) override;

private:
    juce::Image current_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoPreview)
};

} // namespace sirius
