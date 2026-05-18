# macOS Sandbox + Distribution Signing — Planned Diff

**Last updated:** 2026-05-18 (M7 S6 — NOT YET APPLIED)

## Context

Sirius Looper is currently ad-hoc signed by CMake's default Ninja/Make path and Developer-ID signed (without notarization) by the Xcode-generator path. Neither is fully distribution-ready. The CI signing workflow (`.github/workflows/ci-macos-signed.yml`) is operator-pending (carryover from M6).

When that workflow lands AND sandbox is enabled, the M7 S6 XPC bridge + embedded helpers will need entitlement + re-sign work to keep working. This file documents the diff so the operator session that enables the sandbox can apply it without re-deriving.

## Required entitlements (when sandbox is enabled)

Add to `app/SiriusLooper.macos.entitlements`:

```xml
<!-- Allow the engine to look up the bundled XPC service by Mach name -->
<key>com.apple.security.temporary-exception.mach-lookup.global-name</key>
<array>
    <string>com.larryseyer.siriuslooper.gui-bridge</string>
</array>
```

The `mach-lookup.global-name` exception is the narrowest scope — only the bridge's bundle ID can be looked up. CARemoteLayer itself doesn't need a special entitlement; the Mach port is transferred via the already-allowed XPC connection.

The bridge's own entitlements file (does not yet exist; needs creation) should declare:

```xml
<key>com.apple.security.app-sandbox</key>
<true/>
<key>com.apple.security.application-groups</key>
<array>
    <string>RR5DY39W4Q.com.larryseyer.siriuslooper</string>
</array>
```

The bridge does not need network, file, microphone, or any other capability — it only brokers a Mach port.

## Required signing pipeline changes (independent of sandbox)

Surfaced by the M7 S6 Task 4 code review (commit `aa64394` note):

1. **`sirius_plugin_host` is not a MACOSX_BUNDLE.** CMake's auto-ad-hoc-sign only fires for bundle targets, so under Ninja the helper ships unsigned. Even ad-hoc signing (`codesign -s -`) would let the parent's seal verify. Add a POST_BUILD `codesign` step on `sirius_plugin_host` itself.

2. **The parent `SiriusLooper.app` is never re-signed after the POST_BUILD copies.** The Xcode generator signs the parent BEFORE the POST_BUILD custom commands run, so `Contents/MacOS/sirius_plugin_host` and `Contents/XPCServices/sirius_gui_bridge.xpc/` are not in the parent's CodeResources seal. Gatekeeper will reject the bundle with `resource added that is not in the seal` once distribution is attempted. The fix is a final POST_BUILD step (after the two helper copies) that runs:

   ```
   codesign --force --sign "Developer ID Application: <identity>" \
            --options runtime \
            --entitlements app/SiriusLooper.macos.entitlements \
            "$<TARGET_BUNDLE_DIR:SiriusLooper>"
   ```

   Without `--deep` (Apple has deprecated it for new code) — sign embedded helpers individually first via their own target signing, then re-seal the parent.

3. **Notarization (`notarytool submit --wait`) currently in `app/CMakeLists.txt`** will fail at the resource-seal check until #2 lands.

## What to test after applying

1. `[plugin-editor-xpc]` test tag still green from inside the sandbox.
2. Operator eyes-on procedure (`docs/operator/m7-eyes-on.md`) still shows synthetic plug-in NSView content (not the placeholder).
3. Console.app shows no `sandbox` blocked messages from `SiriusLooper`, `sirius_plugin_host`, or `sirius_gui_bridge`.
4. `spctl -a -vv "Sirius Looper.app"` returns `accepted, source=Notarized Developer ID`.
5. `codesign --verify --strict --deep "Sirius Looper.app"` exits 0.

## Why deferred

The CI signing handoff is operator-pending (M6 carryover). Adding sandbox entitlements before signing exists would block the dev loop (sandbox prevents many things that ad-hoc-signed builds rely on). The re-sign pipeline depends on having a real signing identity in CI. All four items above land in the same operator session.

## Cross-references

- M7 S6 spec: `docs/superpowers/specs/2026-05-18-m7-s6-design.md` (commit `fff90c6`)
- Bundle copy + signing: `app/CMakeLists.txt` (M7 S6 step 4, commits `fb870e9` + `aa64394`)
- XPC service signing: `xpc_service/CMakeLists.txt` (M7 S6 step 2, commit `739e4ea`)
- Engine ↔ bridge connection: `host/src/PluginGuiBridge.cpp` + `.mm` (M7 S6 steps 1 + 3)
- Child ↔ bridge connection: `host_process/main.cpp` (M7 S6 step 6)
