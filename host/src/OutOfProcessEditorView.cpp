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
}

void OutOfProcessEditorView::issueShowIfSized()
{
    if (shownOnce_ || getWidth() <= 0 || getHeight() <= 0)
        return;
    if (! isShowing())
        return;
    const auto w = static_cast<std::uint32_t> (getWidth());
    const auto h = static_cast<std::uint32_t> (getHeight());
    if (host_.requestEditorShow (busId_, slotIndex_, w, h))
        shownOnce_ = true;
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
