// =============================================================================
// OutOfProcessEditorView.mm — engine-side embed shim (M7 S5)
// =============================================================================
// Counterpart to OutOfProcessEditorView.cpp. The Cocoa work lives here so
// the plain C++ side stays free of AppKit includes. Each shim is
// `extern "C"`; the NSView lifetime is bracketed by
// `juce::NSViewComponent::setView(nullptr)` on the calling side.
//
// **CARemoteLayer Mach-port handoff is deferred — see the matching
// docblock in host_process/gui_cocoa.mm.** This TU produces a layer-
// backed placeholder NSView that JUCE composites correctly inside the
// engine's window; the actual cross-process layer publishing (Apple's
// public path is CARemoteLayerServer + CARemoteLayerClient + Mach-port
// transfer) lands in a follow-on session along with the XPC plumbing.
// The contextId surfaces through the engine API unchanged, so the
// production wiring stays stable across the deferral.
// =============================================================================

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cstdint>

namespace
{
    NSView* createPlaceholderView (std::uint32_t contextId, int width, int height)
    {
        if (width  <= 0) width  = 1;
        if (height <= 0) height = 1;
        NSView* view = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, width, height)];
        view.wantsLayer = YES;
        // Tint the placeholder so a future operator-launch eyes-on
        // immediately distinguishes "embed surface is wired" from "embed
        // surface is missing entirely". The colour shifts slightly with
        // the contextId so a successful supervisor-restart re-publish is
        // visually obvious (different tint after the restart).
        const CGFloat hue = (CGFloat) (contextId % 360) / 360.0;
        CGColorRef bg = CGColorCreateGenericRGB (
            0.35 + 0.4 * hue, 0.25, 0.55, 1.0);
        view.layer.backgroundColor = bg;
        CGColorRelease (bg);
        return view;
    }
}

extern "C" void* sirius_make_layerhost_nsview (std::uint32_t contextId,
                                               int width, int height)
{
    return (void*) createPlaceholderView (contextId, width, height);
}

extern "C" void sirius_update_layerhost_contextid (void* nsView,
                                                   std::uint32_t contextId)
{
    if (nsView == nullptr)
        return;
    NSView* view = (NSView*) nsView;
    if (view.layer == nil)
        return;
    const CGFloat hue = (CGFloat) (contextId % 360) / 360.0;
    CGColorRef bg = CGColorCreateGenericRGB (
        0.35 + 0.4 * hue, 0.25, 0.55, 1.0);
    view.layer.backgroundColor = bg;
    CGColorRelease (bg);
}

extern "C" void sirius_resize_layerhost_nsview (void* nsView,
                                                int width, int height)
{
    if (nsView == nullptr)
        return;
    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;
    NSView* view = (NSView*) nsView;
    view.frame = NSMakeRect (0, 0, width, height);
    if (view.layer != nil)
        view.layer.frame = view.bounds;
}
