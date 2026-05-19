#include "sirius/OutOfProcessEditorView.h"

#include "sirius/PluginGuiBridge.h"

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
    // Stamp now so the failed-to-load deadline runs from construction
    // time, not from a Show that may or may not get issued. Operator
    // gets feedback in 2 s regardless of upstream wiring state.
    showRequestedAt_ = std::chrono::steady_clock::now();
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

    // A non-zero contextId is only "real" when the engine's XPC bridge
    // connection is live — otherwise the child fell back to the S5
    // placeholder counter (e.g. 1, 2, 3...), and +[CALayer
    // layerWithRemoteClientId:] hands back a CALayer pointing at a
    // nonexistent remote client that renders BLACK. Treat that as
    // failed-to-load so the operator sees the explanatory overlay.
    const bool ctxIsBogus = (currentContextId_ != 0
                             && ! PluginGuiBridge::instance().isReady());

    if ((currentContextId_ == 0 || ctxIsBogus) && ! failedToLoad_)
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
    else if (failedToLoad_ && currentContextId_ != 0 && ! ctxIsBogus)
    {
        // Real late publish (e.g. supervisor restarted the child AND
        // the bridge is live AND the child handed us a real
        // CARemoteLayerClient.clientId). Clear the overlay so the
        // embedded NSView gets the spotlight. NOT triggered by the
        // placeholder counter — that would flash the overlay on/off
        // every tick.
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
    // Stamp the request time BEFORE the call and set shownOnce_ regardless
    // of the host's return value. A `false` from requestEditorShow means
    // the slot is gone / permanently bypassed — operator deserves the
    // failed-to-load overlay just as much in that case as in the
    // never-published case. Without this, a false return left the view
    // in permanent black with no feedback.
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
