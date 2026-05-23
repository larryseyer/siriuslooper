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
    src/CalibrationStore.cpp
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
    src/TapeWriter.cpp
    src/WetCaptureWriter.cpp
    src/NotificationBus.cpp
    src/SessionSnapshot.cpp
    src/ConstituentValidator.cpp
    src/MixerGraph.cpp
    # Internal-FX adapters — T3a of the P7 internal-FX umbrella. EqAdapter
    # wraps OTTO's header-only PlayerEQ; the factory resolves an InternalFxId
    # to a freshly-constructed adapter. CMP/DLY/RVB land in T3b/c/d.
    #
    # T3a-B: the factory's declaration lives in core/include/sirius/
    # (InternalFxFactory.h) so the host can call into it without taking a
    # host→engine link dependency (engine already PRIVATE-links host via
    # SessionSnapshot — adding host→engine would cycle). The impl stays
    # here because it needs the engine-private EqAdapter (and CMP/DLY/RVB
    # adapters once they ship). The factory symbol is provided by Sirius::
    # Engine and resolved at final-link by every binary that already pulls
    # both libraries (app, SiriusTests) — see comment in host/CMakeLists.txt
    # for the symmetric note.
    src/InternalFxFactory.cpp
    src/fx/EqAdapter.cpp
    src/fx/CmpAdapter.cpp
    src/fx/DlyAdapter.cpp)

target_include_directories(SiriusEngine PUBLIC include)

# OTTO header-only Player FX (PlayerEQ, PlayerCompressor, PlayerDelay,
# PlayerIRConvolution + PlayerEffectsConfig) consumed from the submodule.
# PRIVATE — only the adapter .cpp files in src/fx/ include these headers;
# the engine's public include surface stays JUCE-free / OTTO-free per the
# layering rule in CLAUDE.md.
target_include_directories(SiriusEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-core/include)

target_compile_features(SiriusEngine PUBLIC cxx_std_20)

# Sirius::Core is public (its types appear in the engine's headers); soxr and
# Sirius::Persistence are private (implementation details of .cpp files that
# never leak into engine public headers — Persistence is used by
# InputMixer.cpp for the finalizeChannel → TapeStore::store flow).
# juce_audio_basics + juce_dsp are private: EqAdapter.cpp (T3a) drives
# OTTO's PlayerEQ via juce::AudioBuffer + juce::dsp::ProcessContextReplacing.
# They stay out of the engine's PUBLIC surface.
target_link_libraries(SiriusEngine
    PUBLIC  Sirius::Core
    PRIVATE juce::juce_core
            juce::juce_audio_basics
            juce::juce_dsp
            Sirius::Persistence
            Sirius::Host
            soxr)

add_library(Sirius::Engine ALIAS SiriusEngine)
