# =============================================================================
# SiriusEngine — the real-time engine layer
# =============================================================================
# Public API is JUCE-free, deliberately: the real-time-safe primitives (the
# lock-free tape-write path), the retroactive ring, and the LMC's clock model
# expose pure C++ interfaces so they stay exhaustively unit-testable. juce_core
# is a PRIVATE implementation detail of TapeWriter.cpp (filesystem streams,
# logging) and does not leak into engine headers.
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
    src/Bus.cpp
    src/DirectLayer.cpp
    src/InputMixer.cpp
    src/OutputMixer.cpp
    src/ProcessingChain.cpp
    src/TapeWriter.cpp)

target_include_directories(SiriusEngine PUBLIC include)

target_compile_features(SiriusEngine PUBLIC cxx_std_20)

# Sirius::Core is public (its types appear in the engine's headers); soxr and
# Sirius::Persistence are private (implementation details of .cpp files that
# never leak into engine public headers — Persistence is used by
# InputMixer.cpp for the finalizeChannel → TapeStore::store flow).
target_link_libraries(SiriusEngine
    PUBLIC  Sirius::Core
    PRIVATE juce::juce_core
            Sirius::Persistence
            soxr)

add_library(Sirius::Engine ALIAS SiriusEngine)
