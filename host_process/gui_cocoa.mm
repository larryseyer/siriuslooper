// =============================================================================
// gui_cocoa.mm — Cocoa shim for ida_plugin_host (M7 S9 Reaper-style)
// =============================================================================
// Each `ida_plugin_host` child owns its own top-level NSWindow per hosted
// plug-in editor. The plug-in's NSView is hosted as the window's contentView.
// Cross-process pixel transport (S6 CARemoteLayer) is gone — every other
// professional DAW on macOS ships plug-ins this way (Logic, Live, Reaper,
// Studio One, FL Studio), and the cross-process composite path requires
// either an entitlement we don't have or a launchd-registered helper we
// don't want.
//
// **IPC contract preservation.** The M7 S5 PluginGuiState shm wire format
// (requestKind / requestSeq / responseKind / responseSeq / responseContextId)
// is unchanged. The SEMANTICS of `responseContextId` shift: it was a real
// CALayer client id (S6) or a placeholder counter (S5). Now it's just a
// boolean-ish "editor is open" signal — non-zero means "child has a window
// up", 0 means "no window". The engine's 30 Hz polling cycle observes this
// directly via `OutOfProcessEffectChainHost::editorIsOpenForBus`.
//
// **AppKit run-loop integration.** The child process is single-threaded.
// `sirius_appkit_init()` (called once from main()) brings AppKit online
// with `NSApplicationActivationPolicyAccessory` (no Dock icon, no Cmd-Tab
// entry, windows still receive events). `sirius_appkit_drain_events()`
// (called per pump iteration AND inside the popMessageBlocking onIdle
// hook) services NSWindow input events without blocking. Empirically
// sub-microsecond when the event queue is empty.
//
// **Single-instance assumption.** Each `ida_plugin_host` child hosts
// exactly one plug-in instance, so a single static editor slot suffices.
// =============================================================================

#import <AppKit/AppKit.h>

#include "ida/PluginGuiState.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <atomic>
#include <cstdint>

@interface SiriusPluginWindowDelegate : NSObject<NSWindowDelegate>
@end

namespace
{
    struct EditorState
    {
        NSWindow*                       window   { nil };
        SiriusPluginWindowDelegate*     delegate { nil };
        const clap_plugin_t*            plugin   { nullptr };
        std::uint32_t                   width    { 0 };
        std::uint32_t                   height   { 0 };
        bool                            created  { false };
    };

    EditorState g_editor {};

    /// The engine's shm region for state echo. Set once by
    /// `sirius_gui_set_state` (called from main.cpp once the region is
    /// attached). The NSWindowDelegate uses it to publish editor-closed
    /// when the user clicks the X button — without that path, the
    /// engine's PluginsPane button would stay "Close editor" forever.
    ida::PluginGuiState* g_guiState { nullptr };

    /// Non-zero value published as `responseContextId` when the editor
    /// is up. The engine treats any non-zero value as "open"; the
    /// specific number is just a stable marker (1 = "open").
    constexpr std::uint32_t kEditorOpenMarker = 1u;

    const clap_plugin_gui_t* getGuiExt (const clap_plugin_t* plugin) noexcept
    {
        if (plugin == nullptr || plugin->get_extension == nullptr)
            return nullptr;
        return static_cast<const clap_plugin_gui_t*> (
            plugin->get_extension (plugin, CLAP_EXT_GUI));
    }

    void publishEditorClosed() noexcept
    {
        if (g_guiState == nullptr)
            return;
        g_guiState->responseContextId.store (0, std::memory_order_relaxed);
        g_guiState->responseWidth    .store (0, std::memory_order_relaxed);
        g_guiState->responseHeight   .store (0, std::memory_order_relaxed);
    }

    void teardownEditor (const clap_plugin_t* plugin) noexcept
    {
        if (! g_editor.created)
            return;

        const auto* gui = getGuiExt (plugin);
        if (gui != nullptr && gui->destroy != nullptr)
            gui->destroy (plugin);

        if (g_editor.window != nil)
        {
            g_editor.window.delegate = nil;
            [g_editor.window orderOut: nil];
            [g_editor.window release];
            g_editor.window = nil;
        }
        if (g_editor.delegate != nil)
        {
            [g_editor.delegate release];
            g_editor.delegate = nil;
        }
        g_editor.plugin  = nullptr;
        g_editor.width   = 0;
        g_editor.height  = 0;
        g_editor.created = false;

        publishEditorClosed();
    }
}

@implementation SiriusPluginWindowDelegate

- (BOOL)windowShouldClose: (NSWindow*) sender
{
    (void) sender;
    return YES;
}

- (void)windowWillClose: (NSNotification*) note
{
    (void) note;
    // User clicked the close box. Tear down the plug-in's editor in this
    // same run-loop iteration so the engine's next poll sees a closed
    // state. The pump loop calls sirius_appkit_drain_events from the
    // same thread, so the synchronous teardown is safe here.
    if (g_editor.created && g_editor.plugin != nullptr)
        teardownEditor (g_editor.plugin);
    else
        publishEditorClosed();
}

@end

// =============================================================================
// C-linkage entry points consumed by host_process/main.cpp
// =============================================================================

extern "C" void sirius_gui_set_state (ida::PluginGuiState* state) noexcept
{
    g_guiState = state;
}

extern "C" void sirius_appkit_init (void) noexcept
{
    static bool inited = false;
    if (inited) return;
    inited = true;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
    }
}

extern "C" void sirius_appkit_drain_events (void) noexcept
{
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask: NSEventMaskAny
                                           untilDate: [NSDate distantPast]
                                              inMode: NSDefaultRunLoopMode
                                             dequeue: YES]) != nil)
        {
            [NSApp sendEvent: event];
        }
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
        // Already open — just resize (and refocus).
        if (gui->set_size != nullptr
            && (width != g_editor.width || height != g_editor.height))
        {
            gui->set_size (plugin, width, height);
        }
        if (g_editor.window != nil)
        {
            [g_editor.window setContentSize: NSMakeSize (width, height)];
            [g_editor.window makeKeyAndOrderFront: nil];
        }
        g_editor.width  = width;
        g_editor.height = height;
        return kEditorOpenMarker;
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

    // Top-level window owned by THIS process. macOS's window-server
    // composites it like any other application's window; no cross-
    // process pixel handoff needed.
    const NSRect contentRect = NSMakeRect (120, 120, width, height);
    const NSUInteger style   = NSWindowStyleMaskTitled
                             | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskResizable;

    NSWindow* window = [[NSWindow alloc]
        initWithContentRect: contentRect
                  styleMask: style
                    backing: NSBackingStoreBuffered
                      defer: NO];
    window.title              = @"Plug-in editor";
    window.releasedWhenClosed = NO;       // we manage releases via teardownEditor

    SiriusPluginWindowDelegate* delegate = [[SiriusPluginWindowDelegate alloc] init];
    window.delegate = delegate;

    NSView* content = window.contentView;
    content.wantsLayer = YES;

    if (gui->set_parent != nullptr)
    {
        clap_window_t cw {};
        cw.api   = CLAP_WINDOW_API_COCOA;
        cw.cocoa = content;
        if (! gui->set_parent (plugin, &cw))
        {
            window.delegate = nil;
            [window release];
            [delegate release];
            if (gui->destroy != nullptr)
                gui->destroy (plugin);
            return 0;
        }
    }
    if (gui->show != nullptr)
        gui->show (plugin);

    [window makeKeyAndOrderFront: nil];
    [NSApp activateIgnoringOtherApps: YES];

    g_editor.window   = window;
    g_editor.delegate = delegate;
    g_editor.plugin   = plugin;
    g_editor.width    = width;
    g_editor.height   = height;
    g_editor.created  = true;
    return kEditorOpenMarker;
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

    if (g_editor.window != nil)
        [g_editor.window setContentSize: NSMakeSize (width, height)];

    g_editor.width  = width;
    g_editor.height = height;
    return kEditorOpenMarker;
}
