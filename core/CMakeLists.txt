# =============================================================================
# IdaCore — the conceptual-time engine core
# =============================================================================
# Deliberately JUCE-free and real-time-free (white paper Appendix D): the
# symbolic time model is a pure library so it can be exercised exhaustively in
# a plain unit-test harness, and so "exact by construction" is directly
# checkable. Do NOT add JUCE or any audio-framework dependency to this target.
# =============================================================================

add_library(IdaCore STATIC
    src/Rational.cpp
    src/Meter.cpp
    src/TempoMap.cpp
    src/Position.cpp
    src/TimeDomain.cpp
    src/Constituent.cpp
    src/RepetitionRules.cpp
    src/Arrangement.cpp
    src/RoleResolver.cpp
    src/CaptureSession.cpp
    src/Promotion.cpp
    src/EffectChain.cpp
    src/SharedMemoryRegion.cpp
    src/Sha256.cpp
    src/VersionPinningRecord.cpp
    src/TapePool.cpp
    src/TapeRecord.cpp
    src/IdaProject.cpp)

target_include_directories(IdaCore PUBLIC include)

target_compile_features(IdaCore PUBLIC cxx_std_20)

add_library(Ida::Core ALIAS IdaCore)
