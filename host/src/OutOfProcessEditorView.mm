// =============================================================================
// OutOfProcessEditorView.mm — engine-side embed shim (M7 S6)
// =============================================================================
// Counterpart to OutOfProcessEditorView.cpp. The Cocoa work lives here so
// the plain C++ side stays free of AppKit includes. Each shim is
// `extern "C"`; the NSView lifetime is bracketed by
// `juce::NSViewComponent::setView(nullptr)` on the calling side.
//
// Primary path (contextId != 0): `+[CALayer layerWithRemoteClientId:]`
// converts the child's published CARemoteLayerClient.clientId into a
// server-side CALayer and installs it as the NSView's backing layer. The
// result composites the child plug-in GUI directly inside the engine window
// without any pixel copy. Apple's contract: returns nil if the clientId does
// not match a live CARemoteLayerClient — guard and fall through to the tinted
// placeholder in that case.
//
// Fallback path (contextId == 0, or layerWithRemoteClientId returns nil):
// A tinted layer-backed NSView stands in for the real surface. Used by ctest
// (no XPC bridge, child publishes a placeholder counter that is not a real
// clientId), and during the brief window between plug-in launch and the
// child's first contextId publish. The hue shifts with contextId so a
// supervisor-restart re-publish is visually obvious.
//
// Compiled with -fno-objc-arc (see host/CMakeLists.txt). Manual
// CGColorRelease is correct; CALayer objects are autoreleased by Cocoa.
// =============================================================================

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CARemoteLayerServer.h>

#include <cstdint>

extern "C" void* sirius_make_layerhost_nsview (std::uint32_t contextId,
                                               int width, int height)
{
    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;

    NSView* view = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, width, height)];
    view.wantsLayer = YES;

    if (contextId != 0)
    {
        CALayer* remote = [CALayer layerWithRemoteClientId: contextId];
        if (remote != nil)
        {
            view.layer = remote;
            return (void*) view;
        }
    }

    // Fallback: tinted placeholder (S5 behaviour). Operator sees a
    // coloured rectangle if the XPC bridge is missing OR the child is
    // still bootstrapping. Colour shifts with contextId so a
    // supervisor-restart re-publish is visually obvious.
    const CGFloat hue = (CGFloat) (contextId % 360) / 360.0;
    CGColorRef bg = CGColorCreateGenericRGB (
        0.35 + 0.4 * hue, 0.25, 0.55, 1.0);
    view.layer.backgroundColor = bg;
    CGColorRelease (bg);
    return (void*) view;
}

extern "C" void sirius_update_layerhost_contextid (void* nsView,
                                                   std::uint32_t contextId)
{
    if (nsView == nullptr)
        return;
    NSView* view = (NSView*) nsView;

    if (contextId != 0)
    {
        CALayer* remote = [CALayer layerWithRemoteClientId: contextId];
        if (remote != nil)
        {
            view.layer = remote;
            return;
        }
    }

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
