# =============================================================================
# SiriusEngine — the real-time engine layer
# =============================================================================
# Still JUCE-free at this layer, deliberately: the real-time-safe primitives
# (the lock-free tape-write path), the retroactive ring, and the LMC's clock
# model are pure C++ so they stay exhaustively unit-testable. The JUCE
# audio-device wiring and the membranes are a thin layer added on top in later
# steps of M2 — they depend on this foundation, not the other way around.
# =============================================================================

add_library(SiriusEngine STATIC
    src/MonotonicClock.cpp
    src/Lmc.cpp
    src/AudioDeviceCalibration.cpp
    src/LatencyTiming.cpp
    src/LoopRenderer.cpp
    src/Asrc.cpp
    src/RenderPipeline.cpp
    src/OverloadProtection.cpp
    src/Channel.cpp
    src/InputMixer.cpp
    src/OutputMixer.cpp)

target_include_directories(SiriusEngine PUBLIC include)

target_compile_features(SiriusEngine PUBLIC cxx_std_20)

# Sirius::Core is public (its types appear in the engine's headers); soxr is
# private (it is an implementation detail of Asrc.cpp — soxr.h never leaks into
# a SiriusEngine public header).
target_link_libraries(SiriusEngine
    PUBLIC  Sirius::Core
    PRIVATE soxr)

add_library(Sirius::Engine ALIAS SiriusEngine)
