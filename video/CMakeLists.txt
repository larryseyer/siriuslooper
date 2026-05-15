# =============================================================================
# SiriusVideo — the video membrane and tape model (white paper Parts 5.3, 6.2)
# =============================================================================
# Two halves:
#
#   * The headless half — VideoFrame metadata, the `VideoTape` alias, the
#     `FrameMembrane` rate-conversion math — is the M6 data model. JUCE-free,
#     exact-rational arithmetic, exhaustively unit-testable.
#
#   * The presentational half — VideoPreview — is a small JUCE component that
#     paints a juce::Image letterboxed. Decoding tape bytes into that image
#     is the FFmpeg-bound work catalogued against the M0 spike in todo.md;
#     until that work lands, the preview surface is exercised by the operator
#     against any source of frames they choose.
# =============================================================================

add_library(SiriusVideo STATIC
    src/FrameMembrane.cpp
    src/VideoPreview.cpp)

target_include_directories(SiriusVideo PUBLIC include)

target_compile_features(SiriusVideo PUBLIC cxx_std_20)

target_link_libraries(SiriusVideo
    PUBLIC  Sirius::Core
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

add_library(Sirius::Video ALIAS SiriusVideo)
