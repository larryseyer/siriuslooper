// =============================================================================
// PluginGuiBridge.mm — Cocoa half of the engine-side XPC bridge (M7 S6).
// =============================================================================
// Companion to PluginGuiBridge.cpp. Only this TU sees AppKit / QuartzCore.
// The plain C++ side reaches in via `extern "C" sirius_bridge_*` shims.
//
// The engine creates exactly one CARemoteLayerServer per process (it's a
// process-wide AppKit singleton). The shim returns its `serverPort`
// (mach_port_t == uint32_t on macOS) so the C++ side can forward it via
// XPC without including <QuartzCore/QuartzCore.h>.
// =============================================================================

#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CARemoteLayerServer.h>

#include <cstdint>

extern "C" std::uint32_t sirius_bridge_engine_server_port();

extern "C" std::uint32_t sirius_bridge_engine_server_port()
{
    CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
    if (server == nil)
        return 0;
    return (std::uint32_t) server.serverPort;
}
