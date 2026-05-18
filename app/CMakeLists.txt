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
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_VERSION>")

target_link_libraries(SiriusLooper PRIVATE
    Sirius::Engine
    Sirius::Audio
    Sirius::AppCore
    Sirius::Ui
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

# Copy the XPC service and the plug-in host helper into the app bundle.
# launchd dispatches the XPC service by CFBundleIdentifier
# (com.larryseyer.siriuslooper.gui-bridge); the on-disk directory name
# sirius_gui_bridge.xpc (OUTPUT_NAME set in xpc_service/CMakeLists.txt)
# does not affect service lookup.
if(APPLE)
    add_dependencies(SiriusLooper sirius_gui_bridge)
    add_custom_command(TARGET SiriusLooper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:SiriusLooper>/Contents/XPCServices"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:sirius_gui_bridge>"
            "$<TARGET_BUNDLE_DIR:SiriusLooper>/Contents/XPCServices/$<TARGET_BUNDLE_DIR_NAME:sirius_gui_bridge>"
        VERBATIM
        COMMENT "Sirius: install XPC service into app bundle")

    add_dependencies(SiriusLooper sirius_plugin_host)
    add_custom_command(TARGET SiriusLooper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:sirius_plugin_host>"
            "$<TARGET_BUNDLE_DIR:SiriusLooper>/Contents/MacOS/$<TARGET_FILE_NAME:sirius_plugin_host>"
        VERBATIM
        COMMENT "Sirius: install sirius_plugin_host into app bundle")
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
