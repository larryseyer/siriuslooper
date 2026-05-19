# =============================================================================
# sirius_gui_bridge — XPC service for the CARemoteLayer Mach-port handoff (M7 S6).
# =============================================================================
# Bundled into SiriusLooper.app/Contents/XPCServices/ by app/CMakeLists.txt.
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
    OUTPUT_NAME "sirius_gui_bridge"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.larryseyer.siriuslooper.gui-bridge"
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

# M7 S8 — POST_BUILD Developer-ID re-sign with the explicit CFBundleIdentifier.
# Two reasons this is mandatory, not optional:
#   (1) `codesign --sign -` (ad-hoc) derives the code-directory identifier
#       from the binary name (`sirius_gui_bridge`), which mismatches the
#       Info.plist's CFBundleIdentifier (`com.larryseyer.siriuslooper.gui-bridge`).
#       launchd refuses to register the in-app XPC service when those
#       differ.
#   (2) macOS 14+ AMFI rejects ad-hoc-signed embedded XPC services even
#       after registration succeeds — `amfid` logs Error Code=-423
#       "The file is adhoc signed or signed by an unknown certificate
#       chain", launchd reports "Connection invalid" to the engine, and
#       the bridge binary never actually execs. Developer ID is the
#       minimum trust level macOS 14+ accepts for in-app XPC services
#       loaded by another process.
# The Xcode-generator branch above sets the same identity via Xcode
# attributes; this POST_BUILD targets the Unix-Makefiles / Ninja
# generators that are the dev-loop default. `--options runtime` aligns
# the dev binaries with the hardened-runtime flag the Developer-ID
# distribution path needs — harmless for dev.
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET sirius_gui_bridge POST_BUILD
        COMMAND codesign --force --sign "Developer ID Application: Larry Seyer (RR5DY39W4Q)"
                --identifier "com.larryseyer.siriuslooper.gui-bridge"
                --options runtime
                --generate-entitlement-der
                "$<TARGET_BUNDLE_DIR:sirius_gui_bridge>"
        VERBATIM
        COMMENT "Sirius: Developer-ID re-sign sirius_gui_bridge.xpc with explicit identifier")
endif()
