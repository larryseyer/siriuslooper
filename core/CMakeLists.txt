# =============================================================================
# SiriusCore — the conceptual-time engine core
# =============================================================================
# Deliberately JUCE-free and real-time-free (white paper Appendix D): the
# symbolic time model is a pure library so it can be exercised exhaustively in
# a plain unit-test harness, and so "exact by construction" is directly
# checkable. Do NOT add JUCE or any audio-framework dependency to this target.
# =============================================================================

add_library(SiriusCore STATIC
    src/Rational.cpp
    src/Meter.cpp
    src/TempoMap.cpp
    src/Position.cpp
    src/TimeDomain.cpp
    src/Constituent.cpp
    src/RepetitionRules.cpp)

target_include_directories(SiriusCore PUBLIC include)

target_compile_features(SiriusCore PUBLIC cxx_std_20)

add_library(Sirius::Core ALIAS SiriusCore)
