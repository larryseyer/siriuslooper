# M7 — Out-of-process plug-in hosting — Operator Eyes-on Procedure

**Last updated:** 2026-05-18 (M7 S7)

## Why this exists

ctest runs the engine headless from `build/`. CARemoteLayer's cross-process GPU compositing only works when the .app bundle is launched from Finder or `open`, because launchd's XPC `mach_service` lookup requires the `Contents/XPCServices/` layout. The integration tests in `tests/OutOfProcessEditorTests.cpp` verify the IPC + lifecycle + supervisor-restart contracts; they do NOT verify pixels reach the screen. This procedure does.

## Steps

1. Clean rebuild from source:

   ```bash
   rm -rf build
   cmake -B build -S .
   cmake --build build -j
   ```

   (The clean is required per the project's `feedback_clean_builds` rule before any GUI testing — CMake caches stale configurations on iOS-style builds, and the bundle copies can drift if intermediate state lingers.)

2. Confirm the bundle has both helpers + the XPC service:

   ```bash
   find "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app/Contents" \
       \( -name "sirius_plugin_host" -o -name "*.xpc" \) | sort
   ```

   Expected output:

   ```
   .../Contents/MacOS/sirius_plugin_host
   .../Contents/XPCServices/sirius_gui_bridge.xpc
   ```

   (The .xpc bundle is named `sirius_gui_bridge.xpc` per its `OUTPUT_NAME` in `xpc_service/CMakeLists.txt`. launchd dispatches the XPC service by `CFBundleIdentifier` = `com.larryseyer.siriuslooper.gui-bridge`, NOT by the on-disk directory name.)

3. Launch the app:

   ```bash
   open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
   ```

4. Open the app's **Plugins** tab. If no descriptors are listed, click **"Scan a plugin folder..."** and pick a folder containing a CLAP plug-in (the synthetic test plug-in builds at `build/tests/fixtures/SyntheticTestPlugin.clap` — use the `build/tests/fixtures/` folder for the smoke).

5. Click the **Open editor** button on a descriptor row. A floating window appears with the plug-in's editor. The colour the operator sees tells the story:

   - **A flat coloured rectangle (purple-ish, hue shifts per restart)** — bridge is missing, child fell back to the S5 placeholder. Open Console.app and filter for `sirius_plugin_host` — the line `XPC bridge timeout (250 ms); falling back to S5 placeholder editor surface` should be visible.
   - **The synthetic plug-in's actual NSView contents** — success. CARemoteLayer is composing the child's CALayer tree into the engine's window via the window-server.

6. Verify the supervisor-restart re-publication path (optional):

   - Open the synthetic plug-in's editor (you should see real content).
   - In another terminal: `killall sirius_plugin_host`.
   - Within ~5 seconds (`kConsecutiveMissThreshold × kSupervisorPollMs`), a new child spawns and re-publishes. The editor's CAContext rebinds to the new clientId; the synthetic plug-in's content returns (possibly with a brief placeholder flash during the restart window).

## Failure modes

| Symptom | Likely cause | Fix |
|---|---|---|
| App launches but synthetic plug-in editor stays purple forever | Bridge XPC service not bundled — check `Contents/XPCServices/` exists | Re-build with the POST_BUILD copy from `app/CMakeLists.txt` |
| Console.app shows `XPC bridge timeout` | Bridge crashed at launch or never connected | Check Console.app for `sirius_gui_bridge` crashes; check launchd registered the service (`launchctl print user/$UID/com.larryseyer.siriuslooper.gui-bridge` if running as user agent, or `launchctl list \| grep gui-bridge` to confirm registration) |
| Editor shows purple on first open, real content after force-quit + relaunch | First-show race — engine hadn't registered serverPort yet | Confirm `OutOfProcessEffectChainHost::configureBus` calls `PluginGuiBridge::instance()` (`host/src/OutOfProcessEffectChainHost.cpp`, M7 S6 step 5) |
| Engine crashes when opening plug-in editor | Likely Mach send-right lifetime bug | Run under `lldb`; check `mach_port_deallocate` pairing in `xpc_service/main.cpp` + `host_process/main.cpp` |
| `spctl -a -vv` rejects the bundle | Embedded helpers not in the parent's CodeResources seal | Operator-pending CI signing handoff — see `docs/operator/macos-sandbox.md`. Ad-hoc dev-loop builds are unaffected. |

## Quick smoke without a real plug-in

If a real CLAP plug-in isn't handy, the synthetic test plug-in built by `tests/fixtures/SyntheticTestPlugin/` exposes the same `clap_gui_cocoa` shim. Path:

```
build/tests/fixtures/SyntheticTestPlugin.clap
```

The integration tests (`tests/OutOfProcessEditorTests.cpp`) load it via `SIRIUS_SYNTHETIC_CLAP_PATH`. Point the Plugins tab's **"Scan a plugin folder..."** at `build/tests/fixtures/` to use it in the eyes-on flow above. Pixels-on-screen compositing requires the operator launch procedure; the headless tests cover the IPC + lifecycle contracts only.
