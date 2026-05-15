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
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

add_library(Sirius::Ui ALIAS SiriusUi)
