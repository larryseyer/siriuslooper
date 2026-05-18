// =============================================================================
// PluginGuiBridge.cpp — engine-side XPC bridge holder (M7 S6).
// =============================================================================
// Concrete impl wraps an xpc_connection_t to the bundled
// `com.larryseyer.siriuslooper.gui-bridge` XPC service. Opens lazily on
// first instance() call, sends `set_server_port` with
// CARemoteLayerServer.sharedServer.serverPort, holds the connection for
// process lifetime so the kernel doesn't reclaim the registered send-
// right before children get to ask for it.
//
// On non-Apple platforms (or when the bridge bundle is missing), the
// null path from Task 1 stays in effect; isReady() returns false and
// children fall back to the S5 placeholder editor surface.
// =============================================================================

#include "sirius/PluginGuiBridge.h"

#include <atomic>
#include <mutex>

#ifdef __APPLE__
  #include <xpc/xpc.h>
  #include <mach/mach.h>
  #include <dispatch/dispatch.h>

  extern "C" std::uint32_t sirius_bridge_engine_server_port();
#endif

namespace sirius
{
namespace
{
    struct NullGuiBridge : public IGuiBridge
    {
        bool isReady() const noexcept override { return false; }
        void registerServerPort (std::uint32_t) noexcept override {}
    };

   #ifdef __APPLE__
    struct XpcGuiBridge : public IGuiBridge
    {
        XpcGuiBridge()
        {
            queue_ = dispatch_queue_create (
                "com.larryseyer.siriuslooper.gui-bridge.client",
                DISPATCH_QUEUE_SERIAL);

            conn_ = xpc_connection_create_mach_service (
                "com.larryseyer.siriuslooper.gui-bridge",
                queue_,
                /* flags */ 0);

            if (conn_ == nullptr)
                return;

            xpc_connection_set_event_handler (conn_, ^(xpc_object_t event) {
                if (xpc_get_type (event) == XPC_TYPE_ERROR)
                    ready_.store (false, std::memory_order_release);
            });
            xpc_connection_resume (conn_);

            // Lazy register on first construction. CARemoteLayerServer
            // creation requires the AppKit runtime (guaranteed live on
            // the engine since JUCE is up).
            const std::uint32_t port = sirius_bridge_engine_server_port();
            if (port != MACH_PORT_NULL)
                registerServerPort (port);
        }

        ~XpcGuiBridge() override
        {
            if (conn_ != nullptr)
            {
                xpc_connection_cancel (conn_);
                xpc_release (conn_);
                conn_ = nullptr;
            }
        }

        bool isReady() const noexcept override
        {
            return ready_.load (std::memory_order_acquire);
        }

        void registerServerPort (std::uint32_t portName) noexcept override
        {
            if (conn_ == nullptr || portName == MACH_PORT_NULL)
                return;
            xpc_object_t msg = xpc_dictionary_create (nullptr, nullptr, 0);
            xpc_dictionary_set_string (msg, "op", "set_server_port");
            xpc_dictionary_set_mach_send (msg, "port", (mach_port_t) portName);

            xpc_connection_send_message_with_reply (
                conn_, msg, queue_, ^(xpc_object_t reply) {
                    if (xpc_get_type (reply) == XPC_TYPE_DICTIONARY
                        && xpc_dictionary_get_bool (reply, "ok"))
                    {
                        ready_.store (true, std::memory_order_release);
                    }
                });
            xpc_release (msg);
        }

    private:
        dispatch_queue_t     queue_ { nullptr };
        xpc_connection_t     conn_  { nullptr };
        std::atomic<bool>    ready_ { false };
    };
   #endif

    NullGuiBridge                      g_nullBridge {};
    std::atomic<IGuiBridge*>           g_injected   { nullptr };

    std::once_flag                     g_realInitFlag;
    IGuiBridge*                        g_realBridge { nullptr };
    std::mutex                         g_realLifecycleMutex;

    IGuiBridge* getOrCreateRealBridge() noexcept
    {
        std::call_once (g_realInitFlag, []() {
           #ifdef __APPLE__
            std::lock_guard<std::mutex> lock (g_realLifecycleMutex);
            g_realBridge = new XpcGuiBridge();
           #endif
        });
        return g_realBridge;
    }

    void teardownRealBridgeForTesting() noexcept
    {
       #ifdef __APPLE__
        std::lock_guard<std::mutex> lock (g_realLifecycleMutex);
        delete g_realBridge;
        g_realBridge = nullptr;
        // Re-arm the once_flag by replacing it. The standard way is to
        // hold a flag pointer; here we accept that resetForTesting only
        // tears the real bridge down (next instance() call will use the
        // null bridge unless a stub is injected first). Tests that need
        // a fresh real bridge must run in their own process — out of
        // scope for ctest.
       #endif
    }
}

IGuiBridge& PluginGuiBridge::instance() noexcept
{
    if (auto* injected = g_injected.load (std::memory_order_acquire))
        return *injected;
   #ifdef __APPLE__
    if (auto* real = getOrCreateRealBridge())
        return *real;
   #endif
    return g_nullBridge;
}

void PluginGuiBridge::setInstanceForTesting (IGuiBridge* bridge) noexcept
{
    g_injected.store (bridge, std::memory_order_release);
}

void PluginGuiBridge::resetForTesting() noexcept
{
    g_injected.store (nullptr, std::memory_order_release);
    teardownRealBridgeForTesting();
}

} // namespace sirius
