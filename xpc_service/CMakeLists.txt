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
