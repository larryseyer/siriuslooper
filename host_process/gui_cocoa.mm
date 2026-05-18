// =============================================================================
// gui_cocoa.mm — Cocoa shim for sirius_plugin_host (M7 S5)
// =============================================================================
// Companion translation unit to host_process/main.cpp. Pulled into a .mm so
// AppKit includes don't leak into the plain C++ pump loop. The pump calls
// `sirius_gui_*` entry points declared at the bottom; AppKit-shaped editor
// lifetime is owned here.
//
// **Out-of-process embedding model.** The CLAP `clap_plugin_gui` spec
// targets in-process embedding (host passes plug-in a parent NSView via
// `set_parent`; plug-in adds its content as a subview). That host NSView
// pointer is invalid in the engine process, so this child creates its OWN
// placeholder NSView, calls the plug-in's `set_parent` with that
// placeholder, and produces a process-unique "contextId" that the engine
// embeds via `OutOfProcessEditorView`. The contextId surfaces as a
// non-zero handle through the GUI shared-memory state region the engine
// polls.
//
// **CARemoteLayer Mach-port handoff is deferred to a follow-on session.**
// Apple's modern public API for cross-process layer compositing is
// `CARemoteLayerServer` (engine side) + `CARemoteLayerClient` (child
// side), which requires transferring a Mach port from engine to child.
// The legacy NSMachBootstrapServer path was removed in 10.10; the modern
// replacement is XPC, which carries enough setup overhead (XPC service
// manifest, endpoint registration, connection lifecycle) to belong in
// its own session. S5 ships the complete IPC + lifecycle + supervisor-
// restart re-publication contracts — the placeholder contextId proves
// the round-trip works end-to-end. Real GPU compositing lights up when
// the Mach-port session lands; nothing in the engine API changes.
//
// **Single-instance assumption.** Each `sirius_plugin_host` child hosts
// exactly one plug-in instance, so a single static editor slot suffices.
// =============================================================================

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    struct EditorState
    {
        NSView*       placeholder { nil };
        std::uint32_t contextId   { 0 };
        std::uint32_t width       { 0 };
        std::uint32_t height      { 0 };
        bool          created     { false };
    };

    EditorState g_editor {};

    /// Process-unique non-zero placeholder until the CARemoteLayer Mach-
    /// port path lands. Atomic so concurrent show requests from a future
    /// multi-instance host don't collide.
    std::atomic<std::uint32_t> g_nextContextId { 1 };

    const clap_plugin_gui_t* getGuiExt (const clap_plugin_t* plugin)
    {
        if (plugin == nullptr || plugin->get_extension == nullptr)
            return nullptr;
        return static_cast<const clap_plugin_gui_t*> (
            plugin->get_extension (plugin, CLAP_EXT_GUI));
    }

    void teardownEditor (const clap_plugin_t* plugin)
    {
        if (! g_editor.created)
            return;

        const auto* gui = getGuiExt (plugin);
        if (gui != nullptr && gui->destroy != nullptr)
            gui->destroy (plugin);

        if (g_editor.placeholder != nil)
        {
            [g_editor.placeholder release];
            g_editor.placeholder = nil;
        }
        g_editor.contextId = 0;
        g_editor.width     = 0;
        g_editor.height    = 0;
        g_editor.created   = false;
    }
}

extern "C" std::uint32_t sirius_gui_show (const struct clap_plugin* plugin,
                                          std::uint32_t width,
                                          std::uint32_t height)
{
    if (plugin == nullptr)
        return 0;

    const auto* gui = getGuiExt (plugin);
    if (gui == nullptr)
        return 0;

    if (g_editor.created)
    {
        if (gui->set_size != nullptr && (width != g_editor.width || height != g_editor.height))
            gui->set_size (plugin, width, height);
        g_editor.width  = width;
        g_editor.height = height;
        return g_editor.contextId;
    }

    if (gui->is_api_supported != nullptr
        && ! gui->is_api_supported (plugin, CLAP_WINDOW_API_COCOA, /*isFloating*/ false))
        return 0;
    if (gui->create == nullptr
        || ! gui->create (plugin, CLAP_WINDOW_API_COCOA, /*isFloating*/ false))
        return 0;

    if (gui->set_scale != nullptr)
        gui->set_scale (plugin, 1.0);

    if (gui->get_size != nullptr)
    {
        std::uint32_t w = width, h = height;
        if (gui->get_size (plugin, &w, &h))
        {
            if (w > 0) width  = w;
            if (h > 0) height = h;
        }
    }
    if (gui->set_size != nullptr)
        gui->set_size (plugin, width, height);

    NSView* placeholder = [[NSView alloc] initWithFrame:
        NSMakeRect (0, 0, width, height)];
    placeholder.wantsLayer = YES;

    if (gui->set_parent != nullptr)
    {
        clap_window_t window {};
        window.api   = CLAP_WINDOW_API_COCOA;
        window.cocoa = placeholder;
        if (! gui->set_parent (plugin, &window))
        {
            [placeholder release];
            if (gui->destroy != nullptr)
                gui->destroy (plugin);
            return 0;
        }
    }
    if (gui->show != nullptr)
        gui->show (plugin);

    // Placeholder contextId — see translation-unit docblock for the
    // CARemoteLayer Mach-port deferral.
    g_editor.placeholder = placeholder;
    g_editor.contextId   = g_nextContextId.fetch_add (1, std::memory_order_relaxed);
    g_editor.width       = width;
    g_editor.height      = height;
    g_editor.created     = true;
    return g_editor.contextId;
}

extern "C" bool sirius_gui_hide (const struct clap_plugin* plugin)
{
    teardownEditor (plugin);
    return true;
}

extern "C" std::uint32_t sirius_gui_resize (const struct clap_plugin* plugin,
                                            std::uint32_t width,
                                            std::uint32_t height)
{
    if (! g_editor.created)
        return 0;

    const auto* gui = getGuiExt (plugin);
    if (gui != nullptr && gui->set_size != nullptr)
        gui->set_size (plugin, width, height);

    if (g_editor.placeholder != nil)
        g_editor.placeholder.frame = NSMakeRect (0, 0, width, height);

    g_editor.width  = width;
    g_editor.height = height;
    return g_editor.contextId;
}
