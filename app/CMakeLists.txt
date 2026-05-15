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
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
