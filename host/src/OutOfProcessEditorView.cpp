#include "sirius/OutOfProcessEditorView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sirius
{

#if JUCE_MAC
/// Bridge to OutOfProcessEditorView.mm. Returns a newly-created NSView*
/// (autoreleased) wrapping a CALayerHost bound to the supplied
/// CAContextID, sized to (width, height) points. The caller (this TU)
/// hands it to `juce::NSViewComponent::setView` which takes a void*.
extern "C" void* sirius_make_layerhost_nsview (std::uint32_t contextId,
                                               int width, int height);

/// Updates an existing layerhost NSView's contextId without recreating it.
extern "C" void  sirius_update_layerhost_contextid (void* nsView,
                                                    std::uint32_t contextId);

/// Resizes the layerhost NSView's frame.
extern "C" void  sirius_resize_layerhost_nsview (void* nsView,
                                                 int width, int height);
#endif

OutOfProcessEditorView::OutOfProcessEditorView (OutOfProcessEffectChainHost& host,
                                                std::int64_t busId,
                                                std::size_t slotIndex)
    : host_      (host)
    , busId_     (busId)
    , slotIndex_ (slotIndex)
{
   #if JUCE_MAC
    embed_ = std::make_unique<juce::NSViewComponent>();
    addAndMakeVisible (*embed_);
   #endif
    setSize (200, 100);
    startTimer (kPollMs);
}

OutOfProcessEditorView::~OutOfProcessEditorView()
{
    stopTimer();
    if (shownOnce_)
        host_.requestEditorHide (busId_, slotIndex_);
}

void OutOfProcessEditorView::resized()
{
   #if JUCE_MAC
    if (embed_ != nullptr)
        embed_->setBounds (getLocalBounds());
    if (shownOnce_)
    {
        const auto w = static_cast<std::uint32_t> (juce::jmax (1, getWidth()));
        const auto h = static_cast<std::uint32_t> (juce::jmax (1, getHeight()));
        host_.requestEditorResize (busId_, slotIndex_, w, h);
        if (auto* v = embed_->getView())
            sirius_resize_layerhost_nsview (v, getWidth(), getHeight());
    }
    else
    {
        // First non-zero bounds after the view was added to a parent.
        // parentHierarchyChanged often fires with bounds == 0; once
        // the DocumentWindow lays us out, retry the Show.
        issueShowIfSized();
    }
   #endif
}

void OutOfProcessEditorView::parentHierarchyChanged()
{
    issueShowIfSized();
}

void OutOfProcessEditorView::visibilityChanged()
{
    if (isVisible())
        issueShowIfSized();
}

void OutOfProcessEditorView::timerCallback()
{
    refreshFromHost();

    // Failed-to-load detection: if a Show was issued kFailedToLoadTimeoutMs
    // ago and the child still hasn't published a contextId, paint the
    // explanatory overlay. Common cause today: operator pointed at a
    // VST3 (sirius_plugin_host only dlopens CLAP bundles — VST3/AU
    // need their own dlopen paths in a later session).
    if (shownOnce_ && currentContextId_ == 0 && ! failedToLoad_)
    {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now() - showRequestedAt_).count();
        if (elapsedMs >= kFailedToLoadTimeoutMs)
        {
            failedToLoad_ = true;
           #if JUCE_MAC
            // juce::NSViewComponent is opaque even with no NSView set —
            // it would render black over our paint(). Hide it so the
            // explanatory overlay actually reaches the screen.
            if (embed_ != nullptr)
                embed_->setVisible (false);
           #endif
            repaint();
        }
    }
    else if (failedToLoad_ && currentContextId_ != 0)
    {
        // Late publish (e.g. supervisor restarted the child and it
        // finally came up). Clear the overlay so the embedded NSView
        // gets the spotlight.
        failedToLoad_ = false;
       #if JUCE_MAC
        if (embed_ != nullptr)
            embed_->setVisible (true);
       #endif
        repaint();
    }
}

void OutOfProcessEditorView::paint (juce::Graphics& g)
{
    if (! failedToLoad_)
        return;

    g.fillAll (juce::Colour (0xff1a1a1a));
    g.setColour (juce::Colour (0xffff6b6b));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("Plug-in failed to load",
                getLocalBounds().removeFromTop (getHeight() / 2 + 8),
                juce::Justification::centredBottom);
    g.setColour (juce::Colour (0xffb0b0b0));
    g.setFont (juce::FontOptions (12.0f, 0));
    g.drawText ("Check the Preparation tab notifications for details. "
                "VST3 / AU hosting is not yet implemented (M7 follow-on).",
                getLocalBounds().removeFromBottom (getHeight() / 2 - 8).reduced (16, 4),
                juce::Justification::centredTop);
}

void OutOfProcessEditorView::issueShowIfSized()
{
    if (shownOnce_ || getWidth() <= 0 || getHeight() <= 0)
        return;
    if (! isShowing())
        return;
    const auto w = static_cast<std::uint32_t> (getWidth());
    const auto h = static_cast<std::uint32_t> (getHeight());
    // Stamp the request time BEFORE the call so the failed-to-load
    // timeout fires even if the host returns false (slot already gone /
    // permanently bypassed). Without this, a false return left the view
    // in a permanent black state with no operator feedback.
    showRequestedAt_ = std::chrono::steady_clock::now();
    shownOnce_       = true;
    host_.requestEditorShow (busId_, slotIndex_, w, h);
}

void OutOfProcessEditorView::refreshFromHost()
{
    const auto fresh = host_.editorCaContextId (busId_, slotIndex_);
    if (fresh == currentContextId_)
        return;

    currentContextId_ = fresh;
   #if JUCE_MAC
    if (embed_ != nullptr)
    {
        if (fresh == 0)
        {
            embed_->setView (nullptr);
        }
        else if (auto* existing = embed_->getView())
        {
            sirius_update_layerhost_contextid (existing, fresh);
        }
        else
        {
            void* v = sirius_make_layerhost_nsview (fresh, getWidth(), getHeight());
            embed_->setView (v);
        }
    }
   #endif
}

} // namespace sirius
