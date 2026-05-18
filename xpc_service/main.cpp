// =============================================================================
// sirius_gui_bridge — XPC service binary (M7 S6).
// =============================================================================
// Bundled inside SiriusLooper.app/Contents/XPCServices/. launchd brings
// this up on demand when the engine or a sirius_plugin_host child opens
// an XPC connection to com.larryseyer.siriuslooper.gui-bridge.
//
// **Stateless Mach-port broker.** Holds one cached mach_port_t — the
// engine's CARemoteLayerServer.sharedServer.serverPort. Two ops:
//   - "set_server_port"  payload: { "port": <mach_send> }
//                        reply:   { "ok":   <bool>      }
//   - "get_server_port"  payload: {}
//                        reply:   { "port": <mach_send> }
//                                  OR { "error": "not_registered" }
//
// **Lifetime.** Mach send-rights are reference-counted by the kernel;
// the broker holds one extra send-right while caching, releases via
// mach_port_deallocate when replaced. The kernel reclaims any send-
// rights held in transit through a cancelled connection automatically.
// =============================================================================

#include <xpc/xpc.h>
#include <mach/mach.h>

#include <cstring>
#include <dispatch/dispatch.h>

namespace
{
    /// The cached engine serverPort. Accessed only from the serial queue.
    mach_port_t           g_cachedServerPort = MACH_PORT_NULL;
    dispatch_queue_t      g_serialQueue      = nullptr;

    void handleSetServerPort (xpc_object_t req, xpc_object_t reply)
    {
        const mach_port_t port = xpc_dictionary_copy_mach_send (req, "port");
        if (port == MACH_PORT_NULL)
        {
            xpc_dictionary_set_bool (reply, "ok", false);
            return;
        }

        dispatch_sync (g_serialQueue, ^{
            if (g_cachedServerPort != MACH_PORT_NULL)
                mach_port_deallocate (mach_task_self(), g_cachedServerPort);
            g_cachedServerPort = port;
        });

        xpc_dictionary_set_bool (reply, "ok", true);
    }

    void handleGetServerPort (xpc_object_t /*req*/, xpc_object_t reply)
    {
        __block mach_port_t snap = MACH_PORT_NULL;
        dispatch_sync (g_serialQueue, ^{
            snap = g_cachedServerPort;
        });

        if (snap == MACH_PORT_NULL)
        {
            xpc_dictionary_set_string (reply, "error", "not_registered");
            return;
        }
        xpc_dictionary_set_mach_send (reply, "port", snap);
    }

    void handleMessage (xpc_object_t req, xpc_connection_t peer)
    {
        if (xpc_get_type (req) != XPC_TYPE_DICTIONARY)
            return;
        const char* op = xpc_dictionary_get_string (req, "op");
        if (op == nullptr)
            return;

        xpc_object_t reply = xpc_dictionary_create_reply (req);
        if (reply == nullptr)
            return;

        if (std::strcmp (op, "set_server_port") == 0)
            handleSetServerPort (req, reply);
        else if (std::strcmp (op, "get_server_port") == 0)
            handleGetServerPort (req, reply);
        else
            xpc_dictionary_set_string (reply, "error", "unknown_op");

        xpc_connection_send_message (peer, reply);
        xpc_release (reply);
    }

    void connectionHandler (xpc_connection_t peer)
    {
        xpc_connection_set_event_handler (peer, ^(xpc_object_t event) {
            const auto type = xpc_get_type (event);
            if (type == XPC_TYPE_ERROR)
                return; // peer went away; kernel reclaims any in-transit rights
            if (type == XPC_TYPE_DICTIONARY)
                handleMessage (event, peer);
        });
        xpc_connection_resume (peer);
    }
}

int main (int /*argc*/, const char* /*argv*/[])
{
    g_serialQueue = dispatch_queue_create (
        "com.larryseyer.siriuslooper.gui-bridge.serial",
        DISPATCH_QUEUE_SERIAL);

    xpc_main (connectionHandler);
    return 0; // unreachable; xpc_main never returns
}
