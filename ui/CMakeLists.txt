# =============================================================================
# IdaUi — the performer's instrument (white paper Part XIV)
# =============================================================================
# Two layers under one umbrella:
#
#   * The structural half — UndoStack, LatencyBudget, the view-state selectors
#     that compute display data from a Constituent tree + playhead — is
#     deliberately JUCE-free, so the white paper's claims about sacred undo,
#     the <30 ms latency budget, and what each view actually shows can be
#     exercised exhaustively in the headless harness.
#
#   * The presentational half — the small JUCE Components in PerformanceView
#     and PreparationView — consumes those state selectors and renders them.
#     The component code links JUCE GUI modules and is operator-verified.
# =============================================================================

# -----------------------------------------------------------------------------
# IdaLookAndFeel — OTTO's LookAndFeel, consumed from the OTTO submodule
# -----------------------------------------------------------------------------
# Sources live in the OTTO git submodule at external/OTTO/src/otto-plugin/ui
# (see .gitmodules). IDA links these source files directly so that bug
# fixes in OTTO propagate via `git submodule update --remote external/OTTO`
# without a byte-faithful copy to keep in sync. IDA never edits OTTO
# source — see project memory `project_otto_is_a_submodule_now`. Kept in its
# own target and compiled WITHOUT IDA's strict warning flags: this is
# third-party code held byte-faithful to OTTO rather than reformatted to
# IDA conventions.
juce_add_binary_data(IdaBinaryData
    HEADER_NAME "OTTOBinaryData.h"
    NAMESPACE OTTOBinaryData
    SOURCES
        "${CMAKE_SOURCE_DIR}/assets/GUI/Fader Knob.png"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Roboto/Roboto-VariableFont_wdth,wght.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Roboto_Condensed/RobotoCondensed-VariableFont_wght.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Orbitron/Orbitron-VariableFont_wght.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Bricolage_Grotesque/BricolageGrotesque-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Bricolage_Grotesque/BricolageGrotesque-Medium.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Bricolage_Grotesque/BricolageGrotesque-SemiBold.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Bricolage_Grotesque/BricolageGrotesque-Bold.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/Bricolage_Grotesque/BricolageGrotesque-ExtraBold.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/JetBrains_Mono/JetBrainsMono-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/JetBrains_Mono/JetBrainsMono-Medium.ttf"
        "${CMAKE_SOURCE_DIR}/assets/Fonts/JetBrains_Mono/JetBrainsMono-SemiBold.ttf"
        # OTTOEngine-required OTTOBinaryData symbols (S1, 2026-05-27): OTTO's
        # PluginEditor.cpp + UI .cpps reference logo + grain-texture + the OFL
        # license blob unconditionally. IDA's own assets/ tree does not bundle
        # the OTTO-specific items (copyright + OTTO-only branding); they live
        # under the operator's working OTTO checkout. IDA_OTTO_WORKING_ROOT
        # is exported by cmake/Dependencies.cmake's TFLite block; falls back
        # to the same default path if not set (matches the dev-time location).
        "${IDA_OTTO_WORKING_ROOT}/assets/GUI/OTTO App Logo.png"
        "${IDA_OTTO_WORKING_ROOT}/assets/GUI/OTTO IOS NB.png"
        "${IDA_OTTO_WORKING_ROOT}/assets/GUI/grain-160.png"
        "${IDA_OTTO_WORKING_ROOT}/assets/Fonts/OFL.txt")

# Source layout (T0b, 2026-05-22): all L&F sources consumed from the OTTO
# submodule at external/OTTO/src/otto-plugin/ui. Bug fixes in OTTO
# propagate via `git submodule update --remote external/OTTO` followed by
# `git add external/OTTO && git commit` to record the new SHA. IDA
# never edits the submodule.
#
# OTTO_OUTPUT_COMBO_AS_JUCE_COMBOBOX (defined below) tells OTTO's
# CompactFaderStrip to substitute juce::ComboBox for OTTO's TouchComboBox
# at compile time — IDA is desktop-first and does not link OTTO's
# iOS touch-menu stack. The default OTTO behavior is unchanged.
set(OTTO_UI_DIR ${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-plugin/ui)

add_library(IdaLookAndFeel STATIC
    ${OTTO_UI_DIR}/OTTOLookAndFeel.cpp
    ${OTTO_UI_DIR}/components/FaderMeter.cpp
    ${OTTO_UI_DIR}/components/CompactFaderStrip.cpp
    ${OTTO_UI_DIR}/components/ChannelDetailPanWidTab.cpp)

target_include_directories(IdaLookAndFeel PUBLIC
    ${OTTO_UI_DIR}
    ${OTTO_UI_DIR}/components)

target_compile_definitions(IdaLookAndFeel PUBLIC
    OTTO_OUTPUT_COMBO_AS_JUCE_COMBOBOX)

target_compile_features(IdaLookAndFeel PUBLIC cxx_std_20)

target_link_libraries(IdaLookAndFeel
    PUBLIC  juce::juce_gui_basics
    PRIVATE IdaBinaryData
            juce::juce_recommended_config_flags)

add_library(Ida::LookAndFeel ALIAS IdaLookAndFeel)

add_library(IdaUi STATIC
    src/UndoStack.cpp
    src/LatencyBudget.cpp
    src/PerformanceViewState.cpp
    src/PreparationViewState.cpp
    src/TimelineViewState.cpp
    src/PerformanceView.cpp
    src/PreparationView.cpp
    src/TimelineView.cpp
    src/InsertChainPopup.cpp
    src/ChannelDetail.cpp
    src/ChannelDetailEQTab.cpp
    src/ChannelDetailCMPTab.cpp
    src/EqCurveView.cpp
    src/CmpMeterView.cpp)

target_include_directories(IdaUi PUBLIC include)

target_compile_features(IdaUi PUBLIC cxx_std_20)

target_link_libraries(IdaUi
    PUBLIC  Ida::Core
            Ida::LookAndFeel
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

add_library(Ida::Ui ALIAS IdaUi)
