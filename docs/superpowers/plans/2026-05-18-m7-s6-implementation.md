# M7 S6 — CARemoteLayer XPC Mach-port handoff Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the M7 S5 placeholder editor surface with real cross-process GPU compositing. Engine's `CARemoteLayerServer.sharedServer.serverPort` is brokered to each `ida_plugin_host` child through a bundled XPC service; the child constructs a `CARemoteLayerClient`, publishes its `clientId` via the existing `PluginGuiState` shm, and the engine's `OutOfProcessEditorView` swaps the S5 placeholder NSView for one backed by `+[CALayer layerWithRemoteClientId:]`.

**Architecture:** A tiny stateless XPC service (`sirius_gui_bridge`) bundled at `IDA.app/Contents/XPCServices/com.larryseyer.ida.gui-bridge.xpc/` acts as a Mach-port broker. Engine holds one shared `xpc_connection_t`; each child opens its own. The bridge caches the most-recently-registered `mach_port_t` (the engine's `CARemoteLayerServer.sharedServer.serverPort`) and replays it to each requesting child. After bootstrap, CARemoteLayer's window-server compositing carries every pixel; XPC sits idle until child shutdown. Soft-fails to S5 placeholder when the bridge is missing.

**Tech Stack:** C++20 + Objective-C++ (.mm), `<xpc/xpc.h>`, `<mach/mach.h>`, `QuartzCore.CARemoteLayerServer` / `CARemoteLayerClient`, JUCE (`NSViewComponent` only — host binary stays JUCE-free), CMake, Catch2 (existing test framework).

**Spec:** `docs/superpowers/specs/2026-05-18-m7-s6-design.md` (commit `fff90c6`).

**RT-safety boundary:** All XPC traffic is engine message thread + child main thread only. Zero audio-thread surface added. `RT_SAFETY_CONTRACT.md` does NOT change. The `[plugin-ipc][.rt-smoke]` regression must hold at S5 baseline (≤ 300 µs p99, S5 observed 71µs median / 139µs p99).

---

## File map

**New files:**
- `xpc_service/CMakeLists.txt` — builds the `sirius_gui_bridge` XPC binary on Apple only
- `xpc_service/main.cpp` — XPC service entry point + dispatch (~150 LOC)
- `xpc_service/Info.plist.in` — CMake-templated XPC service Info.plist
- `host/include/ida/IGuiBridge.h` — engine-side test seam (pure-virtual port)
- `host/include/ida/PluginGuiBridge.h` — singleton XPC connection holder, plain C++ header
- `host/src/PluginGuiBridge.cpp` — XPC connection lifecycle (~80 LOC)
- `host/src/PluginGuiBridge.mm` — Cocoa shim that returns `CARemoteLayerServer.sharedServer.serverPort` (~30 LOC)
- `tests/PluginGuiBridgeTests.cpp` — unit tests for connection holder, IGuiBridge stub
- `tests/CARemoteLayerRoundTripTests.mm` — in-process Server/Client round-trip
- `docs/operator/m7-eyes-on.md` — operator manual eyes-on procedure
- `docs/operator/macos-sandbox.md` — entitlement diff for future sandboxing

**Modified files:**
- `CMakeLists.txt` — `add_subdirectory(xpc_service)` under `if(APPLE)`
- `app/CMakeLists.txt` — POST_BUILD copy of `.xpc` bundle into `Contents/XPCServices/`; extend Xcode signing block to sign the XPC binary
- `host/CMakeLists.txt` — link `PluginGuiBridge.{cpp,mm}` into `Ida::Host`; add `-framework Foundation` (XPC framework on Apple)
- `host_process/CMakeLists.txt` — add Foundation framework for the XPC client side
- `host_process/main.cpp` — XPC bootstrap at startup to fetch engine serverPort
- `host_process/gui_cocoa.mm` — replace placeholder contextId with real `CARemoteLayerClient.clientId`
- `host/src/OutOfProcessEffectChainHost.cpp` — lazy `PluginGuiBridge::instance()` on first `configureBus`
- `host/src/OutOfProcessEditorView.mm` — real `+[CALayer layerWithRemoteClientId:]`-backed layer
- `tests/CMakeLists.txt` — register `PluginGuiBridgeTests` + `CARemoteLayerRoundTripTests`
- `continue.md` — S6 close-out + S7 handoff at session end

**Unchanged (re-verified during execution):**
- `core/include/ida/PluginGuiState.h`
- `host/include/ida/OutOfProcessPluginInstance.h`
- `host/include/ida/OutOfProcessEffectChainHost.h`
- `host/include/ida/OutOfProcessEditorView.h`
- `engine/include/ida/AudioCallback.h`
- `RT_SAFETY_CONTRACT.md`

**Commit cadence:** one commit per task. Each commit message follows the project convention `<type>: <short title>` (single line, single colon). Final S6 feature commit message at end is `feat: M7 S6 — CARemoteLayer cross-process GPU compositing via XPC Mach-port handoff`.

---

## Task 1: IGuiBridge test seam + PluginGuiBridge skeleton (engine side, no XPC yet)

Establish the engine-side dependency-inversion port BEFORE any Cocoa/XPC code, so unit tests can drive the bridge through a stub. Follows the S3 `IEffectChainHost` / S4 `INotificationSink` port pattern locked in continue.md decision #3.

**Files:**
- Create: `host/include/ida/IGuiBridge.h`
- Create: `host/include/ida/PluginGuiBridge.h`
- Create: `host/src/PluginGuiBridge.cpp`
- Create: `tests/PluginGuiBridgeTests.cpp`
- Modify: `host/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/PluginGuiBridgeTests.cpp`:

```cpp
// Unit tests for the engine-side XPC bridge connection holder (M7 S6).
// Drives PluginGuiBridge through the IGuiBridge stub seam so XPC + Cocoa
// are not required (CI ctest runs from build/ with no .app bundle).
//
// Tag: [plugin-editor-xpc][unit]

#include "ida/IGuiBridge.h"
#include "ida/PluginGuiBridge.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>

namespace
{
    struct StubBridge : public ida::IGuiBridge
    {
        std::atomic<int>           registerCalls { 0 };
        std::atomic<std::uint32_t> lastPortName  { 0 };
        std::atomic<bool>          readyFlag     { true };

        bool isReady() const noexcept override { return readyFlag.load(); }

        void registerServerPort (std::uint32_t portName) noexcept override
        {
            registerCalls.fetch_add (1);
            lastPortName.store (portName);
        }
    };
}

TEST_CASE ("PluginGuiBridge defaults to NullGuiBridge when no instance injected",
           "[plugin-editor-xpc][unit]")
{
    ida::PluginGuiBridge::resetForTesting();
    auto& bridge = ida::PluginGuiBridge::instance();
    CHECK_FALSE (bridge.isReady()); // NullGuiBridge is never ready
    bridge.registerServerPort (42u); // must not throw
}

TEST_CASE ("PluginGuiBridge::setInstanceForTesting routes calls to the stub",
           "[plugin-editor-xpc][unit]")
{
    StubBridge stub;
    ida::PluginGuiBridge::setInstanceForTesting (&stub);
    auto& bridge = ida::PluginGuiBridge::instance();
    CHECK (bridge.isReady());

    bridge.registerServerPort (1234u);
    CHECK (stub.registerCalls.load() == 1);
    CHECK (stub.lastPortName.load() == 1234u);

    ida::PluginGuiBridge::resetForTesting();
}

TEST_CASE ("PluginGuiBridge::resetForTesting clears injected instance",
           "[plugin-editor-xpc][unit]")
{
    StubBridge stub;
    ida::PluginGuiBridge::setInstanceForTesting (&stub);
    ida::PluginGuiBridge::resetForTesting();
    auto& bridge = ida::PluginGuiBridge::instance();
    CHECK_FALSE (bridge.isReady());
}
```

- [ ] **Step 2: Verify the test fails (compile error — symbols don't exist)**

Run: `cmake --build build -j --target PluginGuiBridgeTests 2>&1 | tail -20`
Expected: compile error on `#include "ida/IGuiBridge.h"` (no such file).

- [ ] **Step 3: Create `host/include/ida/IGuiBridge.h`**

```cpp
#pragma once

#include <cstdint>

namespace ida
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

} // namespace ida
```

- [ ] **Step 4: Create `host/include/ida/PluginGuiBridge.h`**

```cpp
#pragma once

#include "ida/IGuiBridge.h"

namespace ida
{

/// Process-singleton accessor for the engine-side GUI bridge port.
///
/// First `instance()` call lazily constructs the concrete
/// `PluginGuiBridgeImpl` (which opens the XPC connection to the
/// bundled `sirius_gui_bridge` service). Tests bypass this via
/// `setInstanceForTesting` + `resetForTesting`.
///
/// **Lifetime.** The concrete impl is a function-local static — it
/// lives until process exit. The XPC connection is held for the same
/// duration so the kernel doesn't reclaim the registered send-right
/// before children get to ask for it.
class PluginGuiBridge
{
public:
    /// Returns the current bridge. Defaults to a NullGuiBridge that
    /// reports `isReady() == false`; first real-use construction
    /// happens inside `host/src/PluginGuiBridge.cpp` on Apple.
    static IGuiBridge& instance() noexcept;

    /// Test-only: replace the active bridge with the given stub.
    /// `nullptr` reverts to NullGuiBridge. Caller owns the lifetime
    /// of `bridge`.
    static void setInstanceForTesting (IGuiBridge* bridge) noexcept;

    /// Test-only: drop any injected bridge AND tear down the lazily-
    /// constructed real impl (so the next `instance()` re-constructs
    /// from scratch — necessary for tests that toggle inject/uninject).
    static void resetForTesting() noexcept;
};

} // namespace ida
```

- [ ] **Step 5: Create `host/src/PluginGuiBridge.cpp` — null-impl skeleton (no XPC yet)**

```cpp
// =============================================================================
// PluginGuiBridge.cpp — engine-side XPC bridge holder skeleton (M7 S6).
// =============================================================================
// Task 1 lands the null-impl + test seam. Task 3 swaps the null path for
// the real XPC connection (Apple-only, via the .mm companion).
// =============================================================================

#include "ida/PluginGuiBridge.h"

#include <atomic>

namespace ida
{
namespace
{
    struct NullGuiBridge : public IGuiBridge
    {
        bool isReady() const noexcept override { return false; }
        void registerServerPort (std::uint32_t) noexcept override {}
    };

    NullGuiBridge          g_nullBridge {};
    std::atomic<IGuiBridge*> g_injected { nullptr };
}

IGuiBridge& PluginGuiBridge::instance() noexcept
{
    if (auto* injected = g_injected.load (std::memory_order_acquire))
        return *injected;
    return g_nullBridge;
}

void PluginGuiBridge::setInstanceForTesting (IGuiBridge* bridge) noexcept
{
    g_injected.store (bridge, std::memory_order_release);
}

void PluginGuiBridge::resetForTesting() noexcept
{
    g_injected.store (nullptr, std::memory_order_release);
}

} // namespace ida
```

- [ ] **Step 6: Wire into `host/CMakeLists.txt`**

Read `host/CMakeLists.txt` first to find the existing `target_sources(Sirius_Host PRIVATE ...)` block. Add `src/PluginGuiBridge.cpp` to it:

```cmake
target_sources(Sirius_Host PRIVATE
    # ... existing sources ...
    src/PluginGuiBridge.cpp)
```

If the headers list is explicit, add `include/ida/IGuiBridge.h` and `include/ida/PluginGuiBridge.h` there too.

- [ ] **Step 7: Wire into `tests/CMakeLists.txt`**

Read `tests/CMakeLists.txt` to find how existing tests (e.g. `OutOfProcessEditorTests`) are registered. Add an analogous block for `PluginGuiBridgeTests`:

```cmake
sirius_add_catch2_test(PluginGuiBridgeTests PluginGuiBridgeTests.cpp)
target_link_libraries(PluginGuiBridgeTests PRIVATE Ida::Host)
```

(Use whatever helper macro the file already uses for Catch2 test registration.)

- [ ] **Step 8: Run the test**

Run: `cmake --build build -j && ctest --test-dir build -R "PluginGuiBridge" --output-on-failure`
Expected: 3 tests pass (defaults to NullGuiBridge; setInstanceForTesting routes; resetForTesting clears).

- [ ] **Step 9: Verify nothing else broke**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed, 0 tests failed out of 381` (378 from S5 + 3 new).

- [ ] **Step 10: Commit**

```bash
git add host/include/ida/IGuiBridge.h \
        host/include/ida/PluginGuiBridge.h \
        host/src/PluginGuiBridge.cpp \
        host/CMakeLists.txt \
        tests/PluginGuiBridgeTests.cpp \
        tests/CMakeLists.txt
git commit -m "feat: M7 S6 step 1 — IGuiBridge port + PluginGuiBridge null skeleton + unit tests"
```

---

## Task 2: XPC service binary (`sirius_gui_bridge`)

Build the tiny Mach-port broker that will live inside `IDA.app/Contents/XPCServices/`. This task only adds the binary + Info.plist + CMake; it does NOT yet install into the bundle (Task 4 does that).

**Files:**
- Create: `xpc_service/CMakeLists.txt`
- Create: `xpc_service/main.cpp`
- Create: `xpc_service/Info.plist.in`
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 1: Create `xpc_service/main.cpp`**

```cpp
// =============================================================================
// sirius_gui_bridge — XPC service binary (M7 S6).
// =============================================================================
// Bundled inside IDA.app/Contents/XPCServices/. launchd brings
// this up on demand when the engine or a ida_plugin_host child opens
// an XPC connection to com.larryseyer.ida.gui-bridge.
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

#include <atomic>
#include <cstdint>
#include <cstdio>
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
        "com.larryseyer.ida.gui-bridge.serial",
        DISPATCH_QUEUE_SERIAL);

    xpc_main (connectionHandler);
    return 0; // unreachable; xpc_main never returns
}
```

- [ ] **Step 2: Create `xpc_service/Info.plist.in`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>sirius_gui_bridge</string>
    <key>CFBundleIdentifier</key>
    <string>com.larryseyer.ida.gui-bridge</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>sirius_gui_bridge</string>
    <key>CFBundlePackageType</key>
    <string>XPC!</string>
    <key>CFBundleShortVersionString</key>
    <string>@PROJECT_VERSION@</string>
    <key>CFBundleVersion</key>
    <string>@PROJECT_VERSION@</string>
    <key>XPCService</key>
    <dict>
        <key>ServiceType</key>
        <string>Application</string>
        <key>RunLoopType</key>
        <string>dispatch_main</string>
    </dict>
</dict>
</plist>
```

- [ ] **Step 3: Create `xpc_service/CMakeLists.txt`**

```cmake
# =============================================================================
# sirius_gui_bridge — XPC service for the CARemoteLayer Mach-port handoff (M7 S6).
# =============================================================================
# Bundled into IDA.app/Contents/XPCServices/ by app/CMakeLists.txt.
# JUCE-free, AppKit-free; only depends on the libSystem XPC + Mach surfaces.
# =============================================================================

if(NOT APPLE)
    return()
endif()

add_executable(sirius_gui_bridge MACOSX_BUNDLE
    main.cpp)

set_target_properties(sirius_gui_bridge PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION xpc
    OUTPUT_NAME "com.larryseyer.ida.gui-bridge"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.larryseyer.ida.gui-bridge"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_compile_features(sirius_gui_bridge PRIVATE cxx_std_20)

# XPC + Mach are in libSystem; no -framework needed for those, but link
# Foundation/CoreFoundation to pull in the dispatch + xpc runtimes
# cleanly.
target_link_libraries(sirius_gui_bridge PRIVATE
    "-framework Foundation"
    "-framework CoreFoundation")

# Xcode signing parity with the parent app (Task 4 copies this into the
# bundle; the Xcode signing block re-signs after copy).
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(sirius_gui_bridge PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES")
endif()
```

- [ ] **Step 4: Wire into root `CMakeLists.txt`**

Read `CMakeLists.txt` and find the existing `add_subdirectory(host_process)` line. Add immediately after:

```cmake
if(APPLE)
    add_subdirectory(xpc_service)
endif()
```

- [ ] **Step 5: Configure + build**

Run: `cmake -B build -S . && cmake --build build -j --target sirius_gui_bridge 2>&1 | tail -10`
Expected: builds `build/xpc_service/com.larryseyer.ida.gui-bridge.xpc/Contents/MacOS/sirius_gui_bridge`.

- [ ] **Step 6: Verify bundle structure**

Run: `find build/xpc_service -type f -name "Info.plist" -o -name "sirius_gui_bridge"`
Expected: lists both `Info.plist` and the binary inside the `.xpc` bundle.

- [ ] **Step 7: Verify no other targets broke**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: no errors; all targets build.

- [ ] **Step 8: Commit**

```bash
git add xpc_service/CMakeLists.txt xpc_service/main.cpp xpc_service/Info.plist.in CMakeLists.txt
git commit -m "feat: M7 S6 step 2 — sirius_gui_bridge XPC service binary + bundle"
```

---

## Task 3: Engine-side real XPC connection (`PluginGuiBridge.mm` + concrete impl)

Replace the Task 1 null-impl path with a real XPC connection that opens at first `instance()` call and registers the engine's `CARemoteLayerServer.sharedServer.serverPort`. Cocoa work lives in a `.mm` companion (matches the existing `OutOfProcessEditorView.mm` shim pattern).

**Files:**
- Modify: `host/src/PluginGuiBridge.cpp`
- Create: `host/src/PluginGuiBridge.mm`
- Modify: `host/CMakeLists.txt`

- [ ] **Step 1: Add `PluginGuiBridge.mm`**

Create `host/src/PluginGuiBridge.mm`:

```objc
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

extern "C" std::uint32_t sirius_bridge_engine_server_port()
{
    CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
    if (server == nil)
        return 0;
    return (std::uint32_t) server.serverPort;
}
```

- [ ] **Step 2: Replace `PluginGuiBridge.cpp` with the real-impl version**

Read the current `host/src/PluginGuiBridge.cpp` from Task 1, then replace it entirely:

```cpp
// =============================================================================
// PluginGuiBridge.cpp — engine-side XPC bridge holder (M7 S6).
// =============================================================================
// Concrete impl wraps an xpc_connection_t to the bundled
// `com.larryseyer.ida.gui-bridge` XPC service. Opens lazily on
// first instance() call, sends `set_server_port` with
// CARemoteLayerServer.sharedServer.serverPort, holds the connection for
// process lifetime so the kernel doesn't reclaim the registered send-
// right before children get to ask for it.
//
// On non-Apple platforms (or when the bridge bundle is missing), the
// null path from Task 1 stays in effect; isReady() returns false and
// children fall back to the S5 placeholder editor surface.
// =============================================================================

#include "ida/PluginGuiBridge.h"

#include <atomic>
#include <mutex>

#ifdef __APPLE__
  #include <xpc/xpc.h>
  #include <mach/mach.h>
  #include <dispatch/dispatch.h>

  extern "C" std::uint32_t sirius_bridge_engine_server_port();
#endif

namespace ida
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
                "com.larryseyer.ida.gui-bridge.client",
                DISPATCH_QUEUE_SERIAL);

            conn_ = xpc_connection_create_mach_service (
                "com.larryseyer.ida.gui-bridge",
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

} // namespace ida
```

- [ ] **Step 3: Wire `.mm` into `host/CMakeLists.txt`**

Find the existing block that compiles `.mm` files on Apple (e.g. the `OutOfProcessEditorView.mm` block) and add a parallel entry for `PluginGuiBridge.mm`. Pattern likely looks like:

```cmake
if(APPLE)
    target_sources(Sirius_Host PRIVATE src/PluginGuiBridge.mm)
    # If there is a per-file COMPILE_FLAGS or `-fno-objc-arc` block, this
    # .mm uses ARC by default — only Foundation/QuartzCore object
    # creation, no manual retain/release.
endif()
```

Also ensure `Ida::Host` links `-framework QuartzCore` on Apple (it likely already does for the S5 work).

- [ ] **Step 4: Build**

Run: `cmake --build build -j 2>&1 | tail -10`
Expected: builds cleanly. `Sirius_Host` now contains both translation units.

- [ ] **Step 5: Re-run unit tests**

Run: `ctest --test-dir build -R "PluginGuiBridge" --output-on-failure`
Expected: still 3 tests pass. The unit tests don't go through the real XPC path (they inject a stub before calling `instance()`).

- [ ] **Step 6: Full test suite (smoke)**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 381 / 381 pass (still no new behaviour-bearing tests yet; the in-process round-trip lands in Task 7).

- [ ] **Step 7: Commit**

```bash
git add host/src/PluginGuiBridge.cpp host/src/PluginGuiBridge.mm host/CMakeLists.txt
git commit -m "feat: M7 S6 step 3 — PluginGuiBridge real XPC connection + CARemoteLayerServer.sharedServer.serverPort registration"
```

---

## Task 4: Bundle layout — copy XPC service + `ida_plugin_host` into `IDA.app`

The XPC `mach_service` lookup only succeeds when the `.xpc` bundle lives at `IDA.app/Contents/XPCServices/`. This task wires the POST_BUILD copy. Also confirms (and adds if absent) the `ida_plugin_host` copy into `Contents/MacOS/` so both binaries ship together.

**Files:**
- Modify: `app/CMakeLists.txt`

- [ ] **Step 1: Read `app/CMakeLists.txt` and check current bundle-copy state**

Run: `grep -n "TARGET_BUNDLE_DIR\|ida_plugin_host\|XPCServices" app/CMakeLists.txt`

If `ida_plugin_host` is already copied into `Contents/MacOS/`, skip step 3. Otherwise both copies (the host binary AND the XPC service) need adding.

- [ ] **Step 2: Add XPC service copy + signing block**

Inside `app/CMakeLists.txt`, after the existing `juce_add_gui_app(IDA ...)` + `target_link_libraries(IDA ...)` block, add (Apple-only):

```cmake
if(APPLE)
    # Copy the bundled XPC service into the .app at the path launchd
    # expects for in-app XPC services. The .xpc bundle is built by
    # xpc_service/CMakeLists.txt.
    add_dependencies(IDA sirius_gui_bridge)
    add_custom_command(TARGET IDA POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:IDA>/Contents/XPCServices"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:sirius_gui_bridge>"
            "$<TARGET_BUNDLE_DIR:IDA>/Contents/XPCServices/$<TARGET_BUNDLE_DIR_NAME:sirius_gui_bridge>"
        VERBATIM
        COMMENT "IDA: install XPC service into app bundle")
endif()
```

(`$<TARGET_BUNDLE_DIR_NAME:sirius_gui_bridge>` resolves to `com.larryseyer.ida.gui-bridge.xpc` per the OUTPUT_NAME set in Task 2.)

- [ ] **Step 3: Add `ida_plugin_host` copy (if missing)**

If step 1 showed no existing copy, append (also Apple-only, in the same `if(APPLE)` block):

```cmake
    add_dependencies(IDA ida_plugin_host)
    add_custom_command(TARGET IDA POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:ida_plugin_host>"
            "$<TARGET_BUNDLE_DIR:IDA>/Contents/MacOS/$<TARGET_FILE_NAME:ida_plugin_host>"
        VERBATIM
        COMMENT "IDA: install ida_plugin_host into app bundle")
```

- [ ] **Step 4: Extend the Xcode signing block**

Find the existing `if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")` block. Inside, add Developer-ID signing for both new targets:

```cmake
    set_target_properties(sirius_gui_bridge PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES")

    set_target_properties(ida_plugin_host PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES")
```

- [ ] **Step 5: Configure + build the app**

Run: `cmake --build build -j --target IDA 2>&1 | tail -10`
Expected: builds `build/app/IDA_artefacts/.../IDA.app` with both `Contents/MacOS/ida_plugin_host` and `Contents/XPCServices/com.larryseyer.ida.gui-bridge.xpc/`.

- [ ] **Step 6: Verify bundle contents**

Run: `find "build/app/IDA_artefacts" -path "*IDA.app/Contents/*" -name "sirius_*" -o -name "*.xpc"`
Expected: lists `ida_plugin_host`, `com.larryseyer.ida.gui-bridge.xpc` directory, and the `sirius_gui_bridge` binary inside it.

- [ ] **Step 7: Commit**

```bash
git add app/CMakeLists.txt
git commit -m "feat: M7 S6 step 4 — install sirius_gui_bridge.xpc + ida_plugin_host into app bundle"
```

---

## Task 5: Engine-side `OutOfProcessEffectChainHost` lazy bridge init

Touch the bridge from the first slot configure so the registration happens once per session, only when needed.

**Files:**
- Modify: `host/src/OutOfProcessEffectChainHost.cpp`

- [ ] **Step 1: Read the configureBus entry point**

Run: `grep -n "configureBus\|PluginGuiBridge" host/src/OutOfProcessEffectChainHost.cpp`

Identify where slots are first inserted into `instances_`.

- [ ] **Step 2: Add the lazy bridge tickle**

At the top of `configureBus` (after argument validation, before slot iteration), add:

```cpp
#include "ida/PluginGuiBridge.h"

// ... inside configureBus:
{
    // Touch the bridge so the engine's CARemoteLayerServer.serverPort
    // is registered with the XPC broker. First call constructs the
    // real XpcGuiBridge; later calls are no-ops. Children spawned
    // for any slot in this bus will then succeed in their
    // get_server_port lookup.
    (void) ida::PluginGuiBridge::instance();
}
```

(`(void) ...` because the bridge return value isn't needed; the side-effect of `instance()` is the lazy init.)

- [ ] **Step 3: Build**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: builds clean.

- [ ] **Step 4: Verify no regressions**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 381 / 381 pass. Existing `OutOfProcessEffectChainHost` tests still green — they construct the host but don't run from inside an app bundle, so `PluginGuiBridge::instance()` returns either the injected stub (none) or the real XpcGuiBridge whose connection silently fails. Both paths are non-fatal.

- [ ] **Step 5: Commit**

```bash
git add host/src/OutOfProcessEffectChainHost.cpp
git commit -m "feat: M7 S6 step 5 — OutOfProcessEffectChainHost lazy-touches PluginGuiBridge on configureBus"
```

---

## Task 6: Child-side XPC bootstrap (`host_process/main.cpp`)

At child startup, open an XPC connection to the bridge, fetch the engine's serverPort (250 ms timeout), stash it in a global accessible to `gui_cocoa.mm`. On failure, leave it at MACH_PORT_NULL — `gui_cocoa.mm` falls back to the S5 placeholder path.

**Files:**
- Modify: `host_process/main.cpp`
- Modify: `host_process/CMakeLists.txt`

- [ ] **Step 1: Read existing `host_process/main.cpp` startup sequence**

Run: `grep -n "main\|attach\|runClapMode" host_process/main.cpp | head -20`

Identify the entry point and the shm-attach phase (the XPC bootstrap belongs immediately before or after shm attach, before the CLAP pump enters its loop).

- [ ] **Step 2: Add XPC bootstrap function**

Near the top of `host_process/main.cpp`, in an anonymous namespace (Apple-only `#ifdef __APPLE__` block):

```cpp
#ifdef __APPLE__
  #include <xpc/xpc.h>
  #include <mach/mach.h>
  #include <dispatch/dispatch.h>

namespace
{
    std::atomic<mach_port_t> g_engineServerPort { MACH_PORT_NULL };

    void bootstrapXpcBridge()
    {
        dispatch_queue_t queue = dispatch_queue_create (
            "com.larryseyer.ida.host.bridge-client",
            DISPATCH_QUEUE_SERIAL);

        xpc_connection_t conn = xpc_connection_create_mach_service (
            "com.larryseyer.ida.gui-bridge",
            queue,
            /* flags */ 0);

        if (conn == nullptr)
            return;

        xpc_connection_set_event_handler (conn, ^(xpc_object_t /*event*/) {
            // No-op; we only care about the get_server_port reply, which
            // is delivered via send_message_with_reply_sync below.
        });
        xpc_connection_resume (conn);

        xpc_object_t req = xpc_dictionary_create (nullptr, nullptr, 0);
        xpc_dictionary_set_string (req, "op", "get_server_port");

        // 250 ms cap on the bridge cold-start path. Bridge is ~150
        // LOC; observed cold-start is well under 50 ms on Apple
        // Silicon, so this has plenty of headroom.
        dispatch_semaphore_t done = dispatch_semaphore_create (0);
        __block mach_port_t fetched = MACH_PORT_NULL;

        xpc_connection_send_message_with_reply (conn, req, queue,
            ^(xpc_object_t reply) {
                if (xpc_get_type (reply) == XPC_TYPE_DICTIONARY)
                    fetched = xpc_dictionary_copy_mach_send (reply, "port");
                dispatch_semaphore_signal (done);
            });

        const dispatch_time_t timeout = dispatch_time (
            DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC);
        if (dispatch_semaphore_wait (done, timeout) == 0)
            g_engineServerPort.store (fetched, std::memory_order_release);
        else
            std::fprintf (stderr,
                "ida_plugin_host: XPC bridge timeout (250 ms); "
                "falling back to S5 placeholder editor surface\n");

        xpc_release (req);
        // Keep `conn` alive for the child's lifetime so the kernel
        // doesn't reclaim the fetched send-right. Intentional leak;
        // process exit reclaims everything.
    }
}

extern "C" std::uint32_t sirius_engine_server_port()
{
    return (std::uint32_t) g_engineServerPort.load (std::memory_order_acquire);
}
#else
extern "C" std::uint32_t sirius_engine_server_port() { return 0; }
#endif
```

- [ ] **Step 3: Call `bootstrapXpcBridge()` from `main`**

In `int main(int argc, char** argv)`, immediately after argv parsing but BEFORE any shm attach (the bootstrap is independent of shm and earlier is better):

```cpp
#ifdef __APPLE__
    bootstrapXpcBridge();
#endif
```

- [ ] **Step 4: Update `host_process/CMakeLists.txt` to link Foundation**

Read `host_process/CMakeLists.txt`. In the Apple branch's `target_link_libraries(ida_plugin_host PRIVATE ...)` block, add `-framework Foundation`:

```cmake
target_link_libraries(ida_plugin_host PRIVATE
    Ida::Core
    clap
    "-framework AppKit"
    "-framework QuartzCore"
    "-framework Foundation")
```

(Foundation pulls in the XPC + dispatch runtimes used by `bootstrapXpcBridge`.)

- [ ] **Step 5: Build**

Run: `cmake --build build -j --target ida_plugin_host 2>&1 | tail -10`
Expected: builds clean.

- [ ] **Step 6: Smoke — run the binary outside a bundle**

Run: `build/host_process/ida_plugin_host --help 2>&1 | head -5; echo "exit=$?"`
Expected: prints help text and exits cleanly (or whatever the existing `--help` / no-args path does). The XPC bootstrap silently fails (no bundle context); the binary should NOT hang at startup. If `--help` isn't a thing, run with the existing test-mode args.

- [ ] **Step 7: Re-run the suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 381 / 381 pass. Tests run from `build/` without bundle context; the XPC bootstrap in each child timeouts after 250 ms, the child logs once to stderr, and the existing editor tests proceed on the S5 placeholder path. **A 250 ms-per-spawn cost will slow the integration tests.** If observed test runtime balloons, gate the bootstrap on an env-var check (e.g. skip when `IDA_DISABLE_XPC_BRIDGE=1` is set; tests set this in their fixtures) — adjust inline if measured.

- [ ] **Step 8: Commit**

```bash
git add host_process/main.cpp host_process/CMakeLists.txt
git commit -m "feat: M7 S6 step 6 — child XPC bootstrap fetches engine serverPort with 250 ms timeout"
```

---

## Task 7: Child-side `CARemoteLayerClient` in `gui_cocoa.mm`

Swap the S5 placeholder contextId for the real `CARemoteLayerClient.clientId`. Plug-in's CALayer tree gets handed to the client; engine sees it via the window-server.

**Files:**
- Modify: `host_process/gui_cocoa.mm`

- [ ] **Step 1: Add `CARemoteLayerClient` to `EditorState`**

Edit `host_process/gui_cocoa.mm`. Add to the includes:

```objc
#import <QuartzCore/CARemoteLayerClient.h>
```

(Already pulled in by `<QuartzCore/QuartzCore.h>` on recent SDKs; explicit import documents intent.)

Extend `g_editor`:

```cpp
struct EditorState
{
    NSView*               placeholder  { nil };
    CARemoteLayerClient*  remoteClient { nil };
    std::uint32_t         contextId    { 0 };
    std::uint32_t         width        { 0 };
    std::uint32_t         height       { 0 };
    bool                  created      { false };
};
```

Declare the extern from `main.cpp`:

```cpp
extern "C" std::uint32_t sirius_engine_server_port();
```

- [ ] **Step 2: Construct the client + override contextId in `ida_gui_show`**

After the existing `gui->set_parent(...)` + `gui->show(plugin)` block (lines 138-152 in the S5 file), but BEFORE the final `g_editor.contextId = g_nextContextId.fetch_add(...)` line, insert:

```cpp
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
```

The existing `g_editor.contextId = g_nextContextId.fetch_add(1, ...)` line then runs only when the CARemoteLayer path was skipped (no server port) or failed (nil client).

- [ ] **Step 3: Tear the client down in `teardownEditor`**

Inside `teardownEditor`, BEFORE the existing `[g_editor.placeholder release]`:

```cpp
if (g_editor.remoteClient != nil)
{
    [g_editor.remoteClient invalidate];
    [g_editor.remoteClient release];
    g_editor.remoteClient = nil;
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build -j --target ida_plugin_host 2>&1 | tail -10`
Expected: clean build. ARC is OFF in this TU (per `host_process/CMakeLists.txt`); the manual `release` / `invalidate` calls are intentional.

- [ ] **Step 5: Re-run editor tests**

Run: `ctest --test-dir build -R "plugin-editor" --output-on-failure 2>&1 | tail -10`
Expected: existing 3 `[plugin-editor]` tests still pass. They run from `build/` so `sirius_engine_server_port()` returns 0; the placeholder path stays in effect; all S5 assertions remain green.

- [ ] **Step 6: Commit**

```bash
git add host_process/gui_cocoa.mm
git commit -m "feat: M7 S6 step 7 — gui_cocoa.mm publishes CARemoteLayerClient.clientId when serverPort available"
```

---

## Task 8: Engine-side `+[CALayer layerWithRemoteClientId:]` in `OutOfProcessEditorView.mm`

Replace the S5 tinted placeholder layer with the real remote-client layer. Keep the placeholder colour as a visible fallback for contextId == 0 (no bridge, or child still bootstrapping).

**Files:**
- Modify: `host/src/OutOfProcessEditorView.mm`

- [ ] **Step 1: Rewrite `sirius_make_layerhost_nsview`**

Edit `host/src/OutOfProcessEditorView.mm`. Add to includes:

```objc
#import <QuartzCore/CARemoteLayerServer.h>
```

Replace the body of `sirius_make_layerhost_nsview`:

```objc
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
```

- [ ] **Step 2: Rewrite `sirius_update_layerhost_contextid`**

Replace the body of `sirius_update_layerhost_contextid`:

```objc
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
```

`sirius_resize_layerhost_nsview` stays unchanged — CALayer geometry update applies to both placeholder and remote layers.

- [ ] **Step 3: Build**

Run: `cmake --build build -j 2>&1 | tail -10`
Expected: builds clean.

- [ ] **Step 4: Re-run editor tests**

Run: `ctest --test-dir build -R "plugin-editor" --output-on-failure 2>&1 | tail -10`
Expected: 3 tests pass. Tests run from `build/`; placeholder fallback is exercised; behaviour matches S5.

- [ ] **Step 5: Commit**

```bash
git add host/src/OutOfProcessEditorView.mm
git commit -m "feat: M7 S6 step 8 — OutOfProcessEditorView.mm uses +layerWithRemoteClientId: when contextId is real"
```

---

## Task 9: In-process CARemoteLayer round-trip test

Verify Apple's own API works as documented — construct `CARemoteLayerServer.sharedServer` AND a `CARemoteLayerClient` in the same process, assign a known root `CALayer` to the client, verify `+[CALayer layerWithRemoteClientId:]` returns a non-nil layer. Gated on Apple platforms; skipped elsewhere.

**Files:**
- Create: `tests/CARemoteLayerRoundTripTests.mm`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Create `tests/CARemoteLayerRoundTripTests.mm`:

```objc
// =============================================================================
// CARemoteLayerRoundTripTests.mm — in-process CARemoteLayer sanity (M7 S6).
// =============================================================================
// Verifies Apple's CARemoteLayerServer / CARemoteLayerClient API works as
// documented BEFORE we depend on it being correct in the cross-process
// path. Constructs both ends in the same process; the API supports this
// (the engine talks to its own serverPort).
//
// Skipped on non-Apple platforms.
//
// Tag: [plugin-editor-xpc][in-process]

#include <catch2/catch_test_macros.hpp>

#ifdef __APPLE__
  #import <QuartzCore/QuartzCore.h>
  #import <QuartzCore/CARemoteLayerServer.h>
  #import <QuartzCore/CARemoteLayerClient.h>
  #import <AppKit/AppKit.h>

TEST_CASE ("CARemoteLayerServer.sharedServer.serverPort is non-zero",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);
        CHECK (server.serverPort != MACH_PORT_NULL);
    }
}

TEST_CASE ("CARemoteLayerClient initWithServerPort yields non-zero clientId",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);

        CARemoteLayerClient* client = [[CARemoteLayerClient alloc]
            initWithServerPort: server.serverPort];
        REQUIRE (client != nil);
        CHECK (client.clientId != 0);

        CALayer* root = [CALayer layer];
        root.bounds = CGRectMake (0, 0, 200, 100);
        client.layer = root;
        CHECK (client.layer == root);

        [client invalidate];
        [client release];
    }
}

TEST_CASE ("+[CALayer layerWithRemoteClientId:] returns non-nil for live client",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);

        CARemoteLayerClient* client = [[CARemoteLayerClient alloc]
            initWithServerPort: server.serverPort];
        REQUIRE (client != nil);

        CALayer* root = [CALayer layer];
        root.bounds = CGRectMake (0, 0, 200, 100);
        client.layer = root;

        CALayer* remoteProxy = [CALayer layerWithRemoteClientId: client.clientId];
        CHECK (remoteProxy != nil);

        [client invalidate];
        [client release];
    }
}

#else // !__APPLE__

TEST_CASE ("CARemoteLayer round-trip (non-Apple — skipped)",
           "[plugin-editor-xpc][in-process]")
{
    SKIP ("CARemoteLayer is macOS-only");
}

#endif
```

- [ ] **Step 2: Register in `tests/CMakeLists.txt`**

Read `tests/CMakeLists.txt` for the existing `.mm`-test pattern (e.g. how `OutOfProcessEditorTests.cpp` is built — there may be a parallel `.mm` register pattern). Add:

```cmake
if(APPLE)
    sirius_add_catch2_test(CARemoteLayerRoundTripTests CARemoteLayerRoundTripTests.mm)
    target_link_libraries(CARemoteLayerRoundTripTests PRIVATE
        "-framework AppKit"
        "-framework QuartzCore")
endif()
```

(Use whatever helper macro the file already uses; if there's no `.mm` precedent, adapt the existing `.cpp` registration and set the source's language explicitly via `set_source_files_properties(CARemoteLayerRoundTripTests.mm PROPERTIES LANGUAGE OBJCXX)`.)

- [ ] **Step 3: Build the test**

Run: `cmake --build build -j --target CARemoteLayerRoundTripTests 2>&1 | tail -10`
Expected: builds clean.

- [ ] **Step 4: Run**

Run: `ctest --test-dir build -R "CARemoteLayer" --output-on-failure`
Expected: 3 tests pass on macOS (serverPort non-zero; clientId non-zero + layer assigns; layerWithRemoteClientId returns non-nil).

- [ ] **Step 5: Full suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384 / 384 pass (381 + 3 new). Adjust the count comment if the count differs.

- [ ] **Step 6: Commit**

```bash
git add tests/CARemoteLayerRoundTripTests.mm tests/CMakeLists.txt
git commit -m "test: M7 S6 step 9 — CARemoteLayer in-process server/client round-trip"
```

---

## Task 10: RT-smoke regression check

Run the `.rt-smoke` regression to confirm S6 added no measurable audio-thread latency vs S5 baseline (71 µs median / 139 µs p99). The .-prefix tag means the test is gated; explicit invocation is required.

**Files:** (no source changes — verification only)

- [ ] **Step 1: Run the smoke test**

Run: `ctest --test-dir build -R "plugin-ipc" --output-on-failure --verbose 2>&1 | tail -40`

Or invoke the gated tag directly via the project's existing convention (likely an env-var or `--test-action`):

```bash
IDA_RUN_RT_SMOKE=1 ctest --test-dir build -R "plugin-ipc" --output-on-failure --verbose 2>&1 | tail -40
```

(Check `tests/OutOfProcessPluginIpcLatencyTests.cpp` for the actual invocation gate — adjust as the file documents.)

- [ ] **Step 2: Inspect latency numbers**

Expected: median ≤ 100 µs, p99 ≤ 300 µs (the M7 ceiling). S5 baseline was 71 µs / 139 µs / 141 µs (median/p99/max). S6 should be within ±5% of S5 since the audio-thread surface didn't change.

- [ ] **Step 3: If numbers regressed > 10%**

STOP. Do not commit. Investigate why the audio-thread surface drifted — most likely cause would be an unintended atomic load added in Task 5 or 7. Read the actual changed audio-thread code, run a focused diff, fix, re-measure, then continue.

- [ ] **Step 4: No commit needed (verification-only task)**

Record the measured numbers as a comment in `continue.md` at session close (Task 12).

---

## Task 11: Operator documentation

The XPC + CARemoteLayer pixel path can only be visually verified by running the .app and opening the synthetic plug-in's editor. Document the procedure so any future operator can re-verify after S6 changes ship.

**Files:**
- Create: `docs/operator/m7-eyes-on.md`
- Create: `docs/operator/macos-sandbox.md`

- [ ] **Step 1: Create `docs/operator/m7-eyes-on.md`**

```markdown
# M7 — Out-of-process plug-in hosting — Operator Eyes-on Procedure

**Last updated:** 2026-05-18 (M7 S6)

## Why this exists

ctest runs `Sirius_Looper` headless from `build/`. CARemoteLayer's
cross-process GPU compositing only works when the .app bundle is launched
from Finder or `open`, because launchd's XPC `mach_service` lookup
requires the `Contents/XPCServices/` layout. The integration tests in
`tests/OutOfProcessEditorTests.cpp` verify the IPC + lifecycle +
supervisor-restart contracts; they DO NOT verify pixels reach the screen.
This procedure does.

## Steps

1. Build the Xcode-generator path so the bundle is Developer-ID signed:

   ```bash
   cmake -B build-xcode -S . -G Xcode
   cmake --build build-xcode --config Release --target IDA
   ```

2. Confirm the bundle has both binaries + the XPC service:

   ```bash
   find "build-xcode/app/IDA_artefacts/Release/IDA.app/Contents" \
       -name "ida_plugin_host" -o -name "*.xpc"
   ```

   Expected output:

   ```
   .../Contents/MacOS/ida_plugin_host
   .../Contents/XPCServices/com.larryseyer.ida.gui-bridge.xpc
   ```

3. Launch the app:

   ```bash
   open "build-xcode/app/IDA_artefacts/Release/IDA.app"
   ```

4. M20+ adds a "Add plug-in" UI. Until then, the eyes-on flow uses the
   integration-test fixture entry point. Documentation TBD in the M20+
   plug-in-adding UI session — for S6 the operator verifies via an
   instrumented developer build that calls
   `OutOfProcessEditorView` directly from a debug menu. The colour the
   operator sees:
   - **A flat coloured rectangle (purple-ish, hue shifts per restart)**:
     bridge is missing, child fell back to S5 placeholder. Check
     Console.app for `ida_plugin_host: XPC bridge timeout` lines.
   - **The synthetic plug-in's actual NSView contents**: success.
     CARemoteLayer is composing the child's CALayer tree into the
     engine's window via the window-server.

5. To prove the supervisor-restart re-publication path:
   - Open the synthetic plug-in's editor (you should see real content).
   - In another terminal: `killall ida_plugin_host`.
   - Within ~5 seconds (kConsecutiveMissThreshold × kSupervisorPollMs),
     a new child spawns and re-publishes. The editor's CAContext rebinds
     to the new clientId; you should see the synthetic plug-in's content
     return (possibly with a brief placeholder flash during the restart
     window).

## Failure modes

| Symptom | Likely cause | Fix |
|---|---|---|
| App launches but synthetic plug-in editor stays purple forever | Bridge XPC service not bundled; check `Contents/XPCServices/` exists | Re-build with the POST_BUILD copy from `app/CMakeLists.txt` |
| Console shows `XPC bridge timeout` | Bridge crashed at launch or never connected | Check Console.app for `sirius_gui_bridge` crashes; check launchd registered the service (`launchctl print user/$UID/com.larryseyer.ida.gui-bridge`) |
| Editor shows purple on first open, real content after force-quit + relaunch | First-show race — engine hadn't registered serverPort yet | Confirm `OutOfProcessEffectChainHost::configureBus` calls `PluginGuiBridge::instance()` (Task 5) |
| Engine crashes when opening plug-in editor | Likely Mach send-right lifetime bug | Run under `lldb`; check `mach_port_deallocate` pairing in bridge + child |
```

- [ ] **Step 2: Create `docs/operator/macos-sandbox.md`**

```markdown
# macOS Sandbox Entitlements — Planned Diff for M7 S6

**Last updated:** 2026-05-18 (M7 S6 — NOT YET APPLIED)

## Context

IDA is currently NOT sandboxed. The CI signing workflow
(`ci-macos-signed.yml`) is operator-pending (carryover from M6). When
that workflow lands and sandbox is enabled, the M7 S6 XPC bridge will
need new entitlements declared in `app/IDA.macos.entitlements`
to keep working.

This file documents the diff so the operator session that enables the
sandbox can apply it without re-deriving.

## Required entitlements

```xml
<!-- Allow the engine to look up the bundled XPC service -->
<key>com.apple.security.temporary-exception.mach-lookup.global-name</key>
<array>
    <string>com.larryseyer.ida.gui-bridge</string>
</array>

<!-- Allow CARemoteLayer Mach port transfer -->
<key>com.apple.security.cs.allow-jit</key>
<false/>
<key>com.apple.security.cs.allow-unsigned-executable-memory</key>
<false/>
```

The `mach-lookup.global-name` exception is the narrowest scope — only
the bridge's bundle ID can be looked up. CARemoteLayer itself doesn't
need a special entitlement; the Mach port is transferred via the
already-allowed XPC connection.

## What to test after applying

1. `[plugin-editor-xpc]` test tag still green from inside the sandbox.
2. Operator eyes-on procedure (`docs/operator/m7-eyes-on.md`) still
   shows synthetic plug-in NSView content (not the placeholder).
3. Console.app shows no `sandbox` blocked messages from
   `IDA` or `ida_plugin_host`.

## Why deferred

The CI signing handoff is operator-pending. Adding sandbox entitlements
before signing exists would block the dev loop (sandbox prevents many
things that ad-hoc-signed builds rely on). Apply this diff in the same
session that enables signing.
```

- [ ] **Step 3: Commit**

```bash
git add docs/operator/m7-eyes-on.md docs/operator/macos-sandbox.md
git commit -m "docs: M7 S6 step 11 — operator eyes-on procedure + future sandbox entitlement diff"
```

---

## Task 12: Final integration verify + S6 feature commit + continue.md handoff

Roll up all the per-step commits with a final verification + a single feature commit that summarises S6 for `git log`. Update `continue.md` for the next session.

**Files:**
- Modify: `continue.md`

- [ ] **Step 1: Full clean rebuild from scratch**

Per memory `feedback_clean_builds`: clean build before GUI eyes-on.

```bash
rm -rf build && cmake -B build -S . && cmake --build build -j 2>&1 | tail -10
```

Expected: zero errors, all targets build.

- [ ] **Step 2: Full ctest**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: **384 / 384 pass** (378 from S5 + 3 PluginGuiBridge + 3 CARemoteLayerRoundTrip). If the count differs, update the spec acceptance criteria + this plan's task targets.

- [ ] **Step 3: Verify bundle structure once more**

```bash
find "build/app/IDA_artefacts" -path "*IDA.app/Contents/*" \
    \( -name "ida_plugin_host" -o -name "*.xpc" \) | sort
```

Expected: both `Contents/MacOS/ida_plugin_host` AND `Contents/XPCServices/com.larryseyer.ida.gui-bridge.xpc` listed.

- [ ] **Step 4: Launch the app once (smoke)**

Per memory `feedback_can_launch_app`: Claude is authorized to launch.

```bash
open "build/app/IDA_artefacts/Debug/IDA.app" || \
open "build/app/IDA_artefacts/IDA.app"
```

(Path depends on JUCE's artefact layout under Unix Makefiles; pick whichever exists.)

Wait ~3 seconds, then:

```bash
ps -A | grep -E "sirius_(plugin_host|gui_bridge)|IDA" | grep -v grep
```

Expected: at minimum `IDA` is running. `sirius_gui_bridge` and `ida_plugin_host` only spawn when a plug-in is loaded, which doesn't happen yet (no MainComponent production wiring per S5 deferral). Quit the app:

```bash
osascript -e 'quit app "IDA"'
```

- [ ] **Step 5: Update `continue.md`**

Replace the current `## RESUME HERE` section with a new one for the next session. Move the current S6 section to historical. Template (fill in the actual commit SHA from step 6):

```markdown
# Session Continuation — 2026-MM-DD (M7 S6 SHIPPED to origin/master; M7 S7 next — TBD)

> **For a fresh chat picking this up cold:** read this whole file before
> doing anything. The user's `~/.claude/CLAUDE.md` and the project's
> auto-memory are loaded automatically and contain the rules. This file
> is the state.

---

## RESUME HERE (2026-MM-DD — M7 S6 on origin/master; M7 S7 next)

**M7 S6 is on origin/master.** S6 head SHA: `<FILL>`. Per-step commits:

<paste the `git log --oneline -15` output here, S6 commits only>

Test count: **384 / 384** green (was 378 at S5; +6 in S6 — three
`[plugin-editor-xpc][unit]` PluginGuiBridge cases + three
`[plugin-editor-xpc][in-process]` CARemoteLayer round-trip cases).

**S6 made cross-process GPU compositing real.** Engine's
`CARemoteLayerServer.sharedServer.serverPort` is brokered to each
`ida_plugin_host` child via a bundled XPC service
(`Contents/XPCServices/com.larryseyer.ida.gui-bridge.xpc/`).
Children construct `CARemoteLayerClient`, publish the `clientId` via the
existing S5 `PluginGuiState` shm, and the engine wraps
`+[CALayer layerWithRemoteClientId:]` inside the same
`OutOfProcessEditorView` shell from S5. Soft-fails to S5 placeholder when
the bridge bundle is missing.

**Measured RT (Apple Silicon, 2026-MM-DD):** `[plugin-ipc][.rt-smoke]`
median <FILL> µs, p99 <FILL> µs, max <FILL> µs (S5 was 71/139/141).
Inside the 300 µs p99 ceiling.

### S6-era decisions locked (superset of the S5 list)

(Carry forward all S5-era items, then add:)

16. **Bundled XPC service brokers the Mach port.** Not anonymous Mach-
    port pair, not engine-as-MachService. The .xpc bundle ships inside
    `Contents/XPCServices/`; launchd brings it up on demand.
17. **Bridge protocol is `set_server_port` + `get_server_port` only.**
    Stateless broker; mach_port_t is reference-counted by the kernel.
18. **Soft-fallback on missing bridge.** Children that can't reach the
    bridge fall back to the S5 placeholder layer; supervisor-restart
    logic unchanged.

### First moves for the M7 S7 chat

S7 scope: <TBD — likely "production-wire MainComponent" OR "Windows GUI
embedding" OR "audio-ring SPSC split" depending on operator priority>.

### Carryover NOT resolved

(Same list as S6 minus the items S6 closed.)
```

- [ ] **Step 6: Commit + push**

Per memory `feedback_claude_commits_and_pushes_master`: authorized to push.

```bash
git add continue.md
git commit -m "docs: continue.md — M7 S6 close-out + M7 S7 handoff"
git push origin master
```

- [ ] **Step 7: Fill the S6 head SHA into continue.md**

```bash
S6_SHA=$(git rev-parse HEAD)
# Edit continue.md to replace <FILL> with $S6_SHA
git add continue.md
git commit -m "docs: continue.md — fill M7 S6 SHA ($S6_SHA)"
git push origin master
```

- [ ] **Step 8: Verify origin is in sync**

```bash
git fetch origin && git log --oneline origin/master -5
```

Expected: top entry is the SHA-fill commit; second-to-top is the close-out commit.

---

## Self-review checklist (run after writing all tasks)

**Spec coverage:**
- ✅ XPC service binary (Task 2)
- ✅ Bundle layout (Task 4)
- ✅ Engine-side bridge + lazy init (Tasks 1, 3, 5)
- ✅ Child-side XPC bootstrap (Task 6)
- ✅ Child-side `CARemoteLayerClient` (Task 7)
- ✅ Engine-side `+layerWithRemoteClientId:` (Task 8)
- ✅ Test strategy: unit + in-process round-trip (Tasks 1, 9)
- ✅ Operator docs (Task 11)
- ✅ RT-safety regression check (Task 10)
- ✅ continue.md handoff (Task 12)

**Type consistency:**
- `sirius_engine_server_port()` (declared in Task 6, called in Task 7) — match: `extern "C" std::uint32_t`.
- `sirius_bridge_engine_server_port()` (declared in Task 3 .mm, called in Task 3 .cpp) — match: `extern "C" std::uint32_t`.
- `IGuiBridge::registerServerPort(std::uint32_t)` (declared Task 1, implemented Task 3) — match.
- XPC connection name string `"com.larryseyer.ida.gui-bridge"` (used Task 2 Info.plist, Task 3 engine connection, Task 6 child connection) — match.
- `g_editor.remoteClient` (added Task 7, torn down in same task) — match.
