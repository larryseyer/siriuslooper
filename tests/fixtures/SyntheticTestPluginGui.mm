// =============================================================================
// SyntheticTestPluginGui — minimal clap_plugin_gui ("cocoa") implementation
// =============================================================================
// Companion translation unit to SyntheticTestPlugin.cpp. Pulled into a .mm
// file so the AppKit + QuartzCore includes don't leak into the otherwise-
// plain C++ plug-in body. The plug-in's pluginGetExtension hooks into
// `syntheticGetGuiExtension()` declared at the bottom of this file.
//
// Behaviour: when the host calls clap_plugin_gui->create("cocoa", false), we
// allocate a single layer-backed NSView (kEditorWidth × kEditorHeight pt,
// solid colour) and store it on the plug-in's state. On set_parent we add
// our view as a subview of the parent NSView the host passes in. That's
// the entire embedded model the synthetic plug-in needs — enough for M7
// S5's CAContext / CALayerHost round-trip integration test to see a
// non-zero contextId after the host wraps our parent placeholder's layer
// in a CAContext.
//
// No animation, no rendering, no event handling — this is a fixture, not
// a UI library. The CALayer's backgroundColor + its presence inside a
// layer-backed NSView are sufficient for `+[CAContext contextWithCGSConnection
// :options:]`-style publishing on the host side.
// =============================================================================

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <cstdint>
#include <cstring>

namespace
{
    constexpr uint32_t kEditorWidth  = 200;
    constexpr uint32_t kEditorHeight = 100;

    /// Per-instance editor state. Allocated on first `create("cocoa", ...)`,
    /// torn down on `destroy()`. Held off the plug-in's PluginState via the
    /// `syntheticAttachGuiState()` shim so the .cpp side doesn't need to
    /// know what an NSView is.
    struct GuiState
    {
        NSView* editorView { nil }; // strong via +1 retain on alloc/init
        bool    visible    { false };
    };

    /// We don't have a way to plug onto SyntheticTestPlugin's PluginState
    /// from this TU without bloating the public header, so the lookup goes
    /// through a single-slot map keyed by the clap_plugin_t pointer. The
    /// synthetic plug-in is single-instance per host process (test
    /// fixtures spawn one host per slot), so a single static slot is
    /// fine; if the synthetic plug-in ever needs to support multi-
    /// instance, replace with std::unordered_map.
    const clap_plugin_t* g_pluginKey { nullptr };
    GuiState             g_state {};

    GuiState* stateFor (const clap_plugin_t* plugin)
    {
        return (plugin == g_pluginKey) ? &g_state : nullptr;
    }

    void ensureSlot (const clap_plugin_t* plugin)
    {
        if (g_pluginKey == nullptr)
            g_pluginKey = plugin;
    }

    void clearSlot (const clap_plugin_t* plugin)
    {
        if (g_pluginKey == plugin)
        {
            g_pluginKey = nullptr;
            g_state     = {};
        }
    }

    // -------------------------------------------------------------------------
    // clap_plugin_gui vtable
    // -------------------------------------------------------------------------
    bool guiIsApiSupported (const clap_plugin_t*, const char* api, bool isFloating)
    {
        return ! isFloating && api != nullptr && std::strcmp (api, CLAP_WINDOW_API_COCOA) == 0;
    }

    bool guiGetPreferredApi (const clap_plugin_t*, const char** api, bool* isFloating)
    {
        if (api != nullptr)         *api        = CLAP_WINDOW_API_COCOA;
        if (isFloating != nullptr)  *isFloating = false;
        return true;
    }

    bool guiCreate (const clap_plugin_t* plugin, const char* api, bool isFloating)
    {
        if (isFloating || api == nullptr || std::strcmp (api, CLAP_WINDOW_API_COCOA) != 0)
            return false;

        ensureSlot (plugin);
        auto* state = stateFor (plugin);
        if (state == nullptr)
            return false;

        if (state->editorView != nil)
            return true; // idempotent

        NSView* view = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight)];
        view.wantsLayer       = YES;
        view.layer.backgroundColor = CGColorCreateGenericRGB (0.20, 0.55, 0.80, 1.0);
        state->editorView = view;
        return true;
    }

    void guiDestroy (const clap_plugin_t* plugin)
    {
        auto* state = stateFor (plugin);
        if (state == nullptr)
            return;
        if (state->editorView != nil)
        {
            [state->editorView removeFromSuperview];
            // ARC is off in this TU; release manually.
            [state->editorView release];
            state->editorView = nil;
        }
        state->visible = false;
        clearSlot (plugin);
    }

    bool guiSetScale (const clap_plugin_t*, double) { return true; }

    bool guiGetSize (const clap_plugin_t*, uint32_t* width, uint32_t* height)
    {
        if (width  != nullptr) *width  = kEditorWidth;
        if (height != nullptr) *height = kEditorHeight;
        return true;
    }

    bool guiCanResize     (const clap_plugin_t*) { return false; }
    bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
    bool guiAdjustSize    (const clap_plugin_t*, uint32_t* w, uint32_t* h)
    {
        if (w != nullptr) *w = kEditorWidth;
        if (h != nullptr) *h = kEditorHeight;
        return true;
    }
    bool guiSetSize       (const clap_plugin_t*, uint32_t, uint32_t) { return true; }

    bool guiSetParent (const clap_plugin_t* plugin, const clap_window_t* window)
    {
        auto* state = stateFor (plugin);
        if (state == nullptr || state->editorView == nil
            || window == nullptr || window->api == nullptr
            || std::strcmp (window->api, CLAP_WINDOW_API_COCOA) != 0)
            return false;

        NSView* parent = (NSView*) window->cocoa;
        if (parent == nil)
            return false;
        [parent addSubview: state->editorView];
        return true;
    }

    bool guiSetTransient (const clap_plugin_t*, const clap_window_t*) { return false; }
    void guiSuggestTitle (const clap_plugin_t*, const char*) {}

    bool guiShow (const clap_plugin_t* plugin)
    {
        auto* state = stateFor (plugin);
        if (state == nullptr || state->editorView == nil)
            return false;
        state->editorView.hidden = NO;
        state->visible           = true;
        return true;
    }

    bool guiHide (const clap_plugin_t* plugin)
    {
        auto* state = stateFor (plugin);
        if (state == nullptr || state->editorView == nil)
            return false;
        state->editorView.hidden = YES;
        state->visible           = false;
        return true;
    }

    const clap_plugin_gui_t kGui = {
        guiIsApiSupported,
        guiGetPreferredApi,
        guiCreate,
        guiDestroy,
        guiSetScale,
        guiGetSize,
        guiCanResize,
        guiGetResizeHints,
        guiAdjustSize,
        guiSetSize,
        guiSetParent,
        guiSetTransient,
        guiSuggestTitle,
        guiShow,
        guiHide
    };
}

extern "C" const void* syntheticGetGuiExtension()
{
    return &kGui;
}
