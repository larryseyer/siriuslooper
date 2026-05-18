// =============================================================================
// gui_cocoa.mm — Cocoa shim for sirius_plugin_host (M7 S5 + S6)
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
// placeholder, and binds a `CARemoteLayerClient` to placeholder.layer.
// The client's `clientId` is published as the contextId that the engine
// passes to `+[CALayer layerWithRemoteClientId:]`, which composites the
// child's layer tree cross-process via the window-server.
//
// **CARemoteLayer-first; counter-based placeholder as fallback.** When
// `sirius_engine_server_port()` returns the engine's
// `CARemoteLayerServer.sharedServer.serverPort` (Task 6 XPC bootstrap),
// the child constructs a `CARemoteLayerClient` and uses its `clientId`
// as the contextId — real GPU compositing. When that port is unavailable
// (binary run outside a signed .app bundle, e.g. ctest from build/), the
// child falls back to the S5 process-unique counter so the editor
// surface still publishes a non-zero contextId; the engine then renders
// a tinted placeholder NSView. Both paths exercise the same publish/poll
// IPC contract; only the visible pixels differ.
//
// **Single-instance assumption.** Each `sirius_plugin_host` child hosts
// exactly one plug-in instance, so a single static editor slot suffices.
// =============================================================================

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CARemoteLayerClient.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <atomic>
#include <cstdint>
#include <cstring>

extern "C" std::uint32_t sirius_engine_server_port();

namespace
{
    struct EditorState
    {
        NSView*               placeholder  { nil };
        CARemoteLayerClient*  remoteClient { nil };
        std::uint32_t         contextId    { 0 };
        std::uint32_t         width        { 0 };
        std::uint32_t         height       { 0 };
        bool                  created      { false };
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

        if (g_editor.remoteClient != nil)
        {
            [g_editor.remoteClient invalidate];
            [g_editor.remoteClient release];
            g_editor.remoteClient = nil;
        }

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

    // CARemoteLayer-first path: ask main.cpp for the engine's
    // CARemoteLayerServer.serverPort (resolved via XPC at startup); if
    // available, construct a CARemoteLayerClient, point it at the
    // plug-in's CALayer subtree, and publish the real clientId. Outside
    // the .app bundle (e.g. unit tests) serverPort is 0 and we fall
    // through to the S5 placeholder counter path.
    const std::uint32_t serverPort = sirius_engine_server_port();
    if (serverPort != 0)
    {
        CARemoteLayerClient* client = [[CARemoteLayerClient alloc]
            initWithServerPort: (mach_port_t) serverPort];
        if (client != nil && client.clientId != 0)
        {
            client.layer = placeholder.layer;
            g_editor.remoteClient = client;
            g_editor.placeholder  = placeholder;
            g_editor.contextId    = client.clientId;
            g_editor.width        = width;
            g_editor.height       = height;
            g_editor.created      = true;
            return g_editor.contextId;
        }
        if (client != nil)
            [client release];
        // fall through to S5 placeholder counter path
    }

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
