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
    VERSION ${PROJECT_VERSION})

target_sources(SiriusLooper PRIVATE
    Main.cpp
    DemoSession.cpp
    SessionInspector.cpp)

target_compile_definitions(SiriusLooper PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SiriusLooper,JUCE_VERSION>")

target_link_libraries(SiriusLooper PRIVATE
    Sirius::Engine
    Sirius::AppCore
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
