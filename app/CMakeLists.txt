# -----------------------------------------------------------------------------
# SiriusAppCore — JUCE-free application logic
# -----------------------------------------------------------------------------
# The startup capability assessment is a pure decision from measured hardware
# to a locked tier (white paper Part 13.1). Keeping it JUCE-free here — the
# platform-specific hardware probing is the operator-run half of M4 — means the
# assessment ladder is exhaustively unit-testable in the headless harness.
add_library(SiriusAppCore STATIC
    CapabilityTier.cpp
    DemoSession.cpp)

target_include_directories(SiriusAppCore PUBLIC .)

target_compile_features(SiriusAppCore PUBLIC cxx_std_20)

# DemoSession's deps (Constituent, Arrangement, TempoMap, InputDescriptor) all
# live in Sirius::Core. Public so headers travel with the library.
target_link_libraries(SiriusAppCore PUBLIC Sirius::Core)

add_library(Sirius::AppCore ALIAS SiriusAppCore)

# -----------------------------------------------------------------------------
# SiriusLooper — the standalone application
# -----------------------------------------------------------------------------
juce_add_gui_app(SiriusLooper
    PRODUCT_NAME "Sirius Looper"
    COMPANY_NAME "Larry Seyer"
    BUNDLE_ID com.larryseyer.siriuslooper
    VERSION ${PROJECT_VERSION}
    # macOS TCC: without these usage-description keys the system silently
    # greys out files in the user's Downloads / Documents / Desktop folders
    # and on removable volumes, even with NSOpenPanel.
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict>\
<key>NSDownloadsFolderUsageDescription</key>\
<string>Sirius Looper needs access to your Downloads folder so you can load and save session files there.</string>\
<key>NSDocumentsFolderUsageDescription</key>\
<string>Sirius Looper needs access to your Documents folder so you can load and save session files there.</string>\
<key>NSDesktopFolderUsageDescription</key>\
<string>Sirius Looper needs access to your Desktop so you can load and save session files there.</string>\
<key>NSRemovableVolumesUsageDescription</key>\
<string>Sirius Looper needs access to removable volumes so you can load and save session files on external drives.</string>\
</dict></plist>")

target_sources(SiriusLooper PRIVATE
    Main.cpp
    MainComponent.cpp)

target_compile_definitions(SiriusLooper PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_VERSION>"
    # M7 S7 debug — compile-time path to the synthetic CLAP fixture so
    # the "Open synthetic test plug-in (debug)" button can bypass the
    # scanner (which doesn't register CLAP). Resolves to the build-tree
    # location; remove when production CLAP support lands in the
    # scanner OR move to a relative-to-bundle resource.
    SIRIUS_SYNTHETIC_CLAP_PATH="${CMAKE_BINARY_DIR}/tests/fixtures/SyntheticTestPlugin.clap")

target_link_libraries(SiriusLooper PRIVATE
    Sirius::Engine
    Sirius::Audio
    Sirius::AppCore
    Sirius::Ui
    Sirius::LookAndFeel
    Sirius::Host
    Sirius::Video
    Sirius::Persistence
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Developer ID signing for macOS Xcode-generator builds. Xcode attributes are
# silently ignored under Unix Makefiles (the default dev loop), so the
# `cmake -B build` build remains ad-hoc-signed and fast; `cmake -B build-xcode
# -G Xcode` produces a Developer-ID-signed bundle with hardened runtime.
# Team RR5DY39W4Q + audio-input entitlement match the white-paper Part VI
# always-running-tape requirement.
if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(SiriusLooper PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_CURRENT_SOURCE_DIR}/SiriusLooper.macos.entitlements")

    # sirius_gui_bridge signing is already set in xpc_service/CMakeLists.txt
    # for the Xcode generator — omitted here to avoid duplication.
    set_target_properties(sirius_plugin_host PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES")
endif()

# Copy the plug-in host helper into the app bundle. M7 S9 deleted the
# bundled XPC service entirely; each sirius_plugin_host child owns its
# own NSWindow (Reaper-style separate plug-in windows), no cross-process
# pixel transport needed.
if(APPLE)
    add_dependencies(SiriusLooper sirius_plugin_host)

    add_custom_command(TARGET SiriusLooper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:sirius_plugin_host>"
            "$<TARGET_BUNDLE_DIR:SiriusLooper>/Contents/MacOS/$<TARGET_FILE_NAME:sirius_plugin_host>"
        VERBATIM
        COMMENT "Sirius: install sirius_plugin_host into app bundle")

    # M7 S8 — Developer-ID re-sign the embedded sirius_plugin_host. It
    # is NOT a MACOSX_BUNDLE target, so CMake's automatic linker-adhoc-
    # sign covers only the binary on its own; once copied into the
    # parent's Contents/MacOS/, the parent's re-sign below needs a
    # coherent inner signature to seal cleanly. Ad-hoc is rejected by
    # macOS 14+ AMFI for embedded executables loaded out-of-process —
    # see the matching comment in xpc_service/CMakeLists.txt for the
    # full rationale. The Xcode-generator path signs this via Xcode
    # attributes (see set_target_properties above); this command targets
    # the Unix-Makefiles / Ninja dev-loop default.
    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        add_custom_command(TARGET SiriusLooper POST_BUILD
            COMMAND codesign --force --sign "Developer ID Application: Larry Seyer (RR5DY39W4Q)"
                    --identifier "com.larryseyer.siriuslooper.host"
                    --options runtime
                    --entitlements "${CMAKE_SOURCE_DIR}/host_process/sirius_plugin_host.entitlements"
                    --generate-entitlement-der
                    "$<TARGET_BUNDLE_DIR:SiriusLooper>/Contents/MacOS/$<TARGET_FILE_NAME:sirius_plugin_host>"
            VERBATIM
            COMMENT "Sirius: Developer-ID re-sign embedded sirius_plugin_host")
    endif()

    # M7 S8 — final re-sign of the parent app AFTER the helper copies.
    # The earlier CMake auto-sign of SiriusLooper.app runs BEFORE these
    # POST_BUILD commands, so without this final step the parent's
    # CodeResources seal does not cover the copied
    # Contents/MacOS/sirius_plugin_host. M7 S9 simplified this — with the
    # XPC bridge deleted, the parent re-sign only seals the helper
    # binary; --deep is still avoided to keep semantic parity with the
    # multi-stage sign pipeline above (which only signs one inner thing
    # now anyway, but the convention costs nothing).
    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        add_custom_command(TARGET SiriusLooper POST_BUILD
            COMMAND codesign --force --sign "Developer ID Application: Larry Seyer (RR5DY39W4Q)"
                    --identifier "com.larryseyer.siriuslooper"
                    --options runtime
                    --generate-entitlement-der
                    "$<TARGET_BUNDLE_DIR:SiriusLooper>"
            VERBATIM
            COMMENT "Sirius: Developer-ID re-sign parent .app after helper copies (no --deep)")
    endif()
endif()

# Notarization (opt-in, slow). Requires a Developer-ID-signed bundle, so it
# only makes sense alongside the Xcode-generator signing block above. The
# `notarytool submit --wait` step blocks for the full Apple round-trip (often
# minutes), then `stapler staple` writes the ticket into the bundle so
# Gatekeeper accepts offline. `spctl` verifies the final result.
# One-time setup: `xcrun notarytool store-credentials sirius-notary
# --apple-id itunes@larryseyer.com --team-id RR5DY39W4Q`.
option(SIRIUS_NOTARIZE "Notarize and staple the Developer ID bundle (slow)" OFF)
if(SIRIUS_NOTARIZE AND APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    set(SIRIUS_NOTARY_PROFILE "sirius-notary"
        CACHE STRING "notarytool keychain profile name")
    add_custom_command(TARGET SiriusLooper POST_BUILD
        COMMAND xcrun notarytool submit
                "$<TARGET_BUNDLE_DIR:SiriusLooper>"
                --keychain-profile "${SIRIUS_NOTARY_PROFILE}"
                --wait
        COMMAND xcrun stapler staple
                "$<TARGET_BUNDLE_DIR:SiriusLooper>"
        COMMAND spctl -a -t exec -vv
                "$<TARGET_BUNDLE_DIR:SiriusLooper>"
        VERBATIM
        COMMENT "Sirius: notarize + staple + verify")
endif()
