# =============================================================================
# SiriusUi — the performer's instrument (white paper Part XIV)
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
# SiriusLookAndFeel — OTTO's LookAndFeel, vendored for sister-app visual parity
# -----------------------------------------------------------------------------
# Copied verbatim from OTTO (src/otto-plugin/ui) with identical class names and
# binary-data namespace, so re-syncing is a straight file copy and the .cpp
# needs zero edits. Kept in its own target and compiled WITHOUT Sirius's strict
# warning flags: this is vendored third-party code, held byte-faithful to OTTO
# rather than reformatted to Sirius conventions. The proper long-term form is a
# shared submodule (a one-time extraction out of OTTO), tracked separately.
juce_add_binary_data(SiriusBinaryData
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
        "${CMAKE_SOURCE_DIR}/assets/Fonts/JetBrains_Mono/JetBrainsMono-SemiBold.ttf")

add_library(SiriusLookAndFeel STATIC
    lookandfeel/OTTOLookAndFeel.cpp)

target_include_directories(SiriusLookAndFeel PUBLIC lookandfeel)

target_compile_features(SiriusLookAndFeel PUBLIC cxx_std_20)

target_link_libraries(SiriusLookAndFeel
    PUBLIC  juce::juce_gui_basics
    PRIVATE SiriusBinaryData
            juce::juce_recommended_config_flags)

add_library(Sirius::LookAndFeel ALIAS SiriusLookAndFeel)

add_library(SiriusUi STATIC
    src/UndoStack.cpp
    src/LatencyBudget.cpp
    src/PerformanceViewState.cpp
    src/PreparationViewState.cpp
    src/TimelineViewState.cpp
    src/PerformanceView.cpp
    src/PreparationView.cpp
    src/TimelineView.cpp)

target_include_directories(SiriusUi PUBLIC include)

target_compile_features(SiriusUi PUBLIC cxx_std_20)

target_link_libraries(SiriusUi
    PUBLIC  Sirius::Core
            Sirius::LookAndFeel
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

add_library(Sirius::Ui ALIAS SiriusUi)
