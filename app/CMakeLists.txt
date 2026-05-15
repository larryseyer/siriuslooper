# -----------------------------------------------------------------------------
# SiriusAppCore — JUCE-free application logic
# -----------------------------------------------------------------------------
# The startup capability assessment is a pure decision from measured hardware
# to a locked tier (white paper Part 13.1). Keeping it JUCE-free here — the
# platform-specific hardware probing is the operator-run half of M4 — means the
# assessment ladder is exhaustively unit-testable in the headless harness.
add_library(SiriusAppCore STATIC
    CapabilityTier.cpp)

target_include_directories(SiriusAppCore PUBLIC .)

target_compile_features(SiriusAppCore PUBLIC cxx_std_20)

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
    DemoSession.cpp
    MainComponent.cpp)

target_compile_definitions(SiriusLooper PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_VERSION>")

target_link_libraries(SiriusLooper PRIVATE
    Sirius::Engine
    Sirius::AppCore
    Sirius::Ui
    Sirius::Host
    Sirius::Video
    Sirius::Persistence
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
