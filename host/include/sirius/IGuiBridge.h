#pragma once

#include <cstdint>

namespace sirius
{

/// Engine-side port for the cross-process GUI Mach-port bridge (M7 S6).
///
/// Pure-virtual seam so unit tests can drive PluginGuiBridge without
/// needing the bundled XPC service (which only resolves inside a real
/// .app bundle). Follows the S3 `IEffectChainHost` / S4
/// `INotificationSink` port convention locked in the M7-era decisions.
///
/// The concrete implementation (`PluginGuiBridgeImpl`) lives in
/// `host/src/PluginGuiBridge.cpp` / `.mm` and is what
/// `PluginGuiBridge::instance()` returns by default; tests inject a
/// `StubBridge` via `setInstanceForTesting`.
struct IGuiBridge
{
    virtual ~IGuiBridge() = default;

    /// True iff the XPC connection is open AND the engine has
    /// registered its CARemoteLayerServer.sharedServer.serverPort.
    /// Children should fall back to the S5 placeholder path when this
    /// returns false on the engine side.
    virtual bool isReady() const noexcept = 0;

    /// Send `set_server_port` to the bridge with the given Mach port
    /// send-right. Non-blocking; the XPC reply is consumed
    /// asynchronously by the connection's event handler.
    ///
    /// Takes `std::uint32_t` rather than `mach_port_t` to keep this
    /// header free of `<mach/mach.h>`. The concrete impl reinterprets
    /// at the .mm boundary where `mach_port_t == uint32_t` on macOS
    /// anyway (Mach port names are uint32_t).
    virtual void registerServerPort (std::uint32_t portName) noexcept = 0;
};

} // namespace sirius
