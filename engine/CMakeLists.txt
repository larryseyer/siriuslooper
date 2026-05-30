# =============================================================================
# IdaEngine — the real-time engine layer
# =============================================================================
# Public API is JUCE-free, deliberately: the real-time-safe primitives (the
# lock-free tape-write path), the retroactive ring, and the LMC's clock model
# expose pure C++ interfaces so they stay exhaustively unit-testable. juce_core
# is a PRIVATE implementation detail of TapeWriter.cpp (filesystem streams,
# logging) and does not leak into engine headers.
# =============================================================================

add_library(IdaEngine STATIC
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
    src/InputMixer.cpp
    src/OutputMixer.cpp
    src/ProcessingChain.cpp
    src/TapeWriter.cpp
    # TAPECOLOR Slice 2 — per-tape routing decorator. Sits between
    # InputMixer's ITapeSink boundary and the downstream sink (FlacTapeSink
    # in the app), applying TAPECOLOR to tapes whose mode is BeforeWrite.
    src/TapeColoringSink.cpp
    src/WetCaptureWriter.cpp
    src/NotificationBus.cpp
    src/SessionSnapshot.cpp
    src/ConstituentValidator.cpp
    src/MixerGraph.cpp
    # Internal-FX adapters — T3a of the P7 internal-FX umbrella. EqAdapter
    # wraps OTTO's header-only PlayerEQ; the factory resolves an InternalFxId
    # to a freshly-constructed adapter. CMP/DLY/RVB land in T3b/c/d.
    #
    # T3a-B: the factory's declaration lives in core/include/ida/
    # (InternalFxFactory.h) so the host can call into it without taking a
    # host→engine link dependency (engine already PRIVATE-links host via
    # SessionSnapshot — adding host→engine would cycle). The impl stays
    # here because it needs the engine-private EqAdapter (and CMP/DLY/RVB
    # adapters once they ship). The factory symbol is provided by Ida::
    # Engine and resolved at final-link by every binary that already pulls
    # both libraries (app, IdaTests) — see comment in host/CMakeLists.txt
    # for the symmetric note.
    src/InternalFxFactory.cpp
    src/fx/EqAdapter.cpp
    src/fx/CmpAdapter.cpp
    src/fx/DlyAdapter.cpp
    src/fx/RvbAdapter.cpp
    src/fx/TapeColorAdapter.cpp
    # S3a T2 — MasterMeter: lock-free per-block peak/RMS snapshot at the
    # master mix point. publish() is audio-thread-safe (alloc-free, lock-free);
    # snapshot() is any-thread atomic load.
    src/MasterMeter.cpp
    # S3a T4 — MasterSpectrum: lock-free per-bin FFT magnitude snapshot at the
    # master mix point. publish() is audio-thread-safe (Hann-windowed FFT into
    # pre-allocated scratch, per-bin std::atomic<float> stores); binDb() is
    # any-thread atomic load. Mirror of MasterMeter's shape; raw-pointer public
    # API keeps juce_audio_basics out of the engine's public include surface
    # (juce_dsp::FFT is forward-declared in the header).
    src/MasterSpectrum.cpp
    # T0a Task 2 — IPayloadCodec interface + TapeCodecRegistry: the media-agnostic
    # codec seam. No JUCE dependency; FLAC/PCM codecs land in Task 3 (audio layer).
    src/IPayloadCodec.cpp)

target_include_directories(IdaEngine PUBLIC include)

# OTTO header-only Player FX (PlayerEQ, PlayerCompressor, PlayerDelay,
# PlayerIRConvolution + PlayerEffectsConfig) consumed from the submodule.
# PRIVATE — only the adapter .cpp files in src/fx/ include these headers;
# the engine's public include surface stays JUCE-free / OTTO-free per the
# layering rule in CLAUDE.md.
target_include_directories(IdaEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-core/include)

target_compile_features(IdaEngine PUBLIC cxx_std_20)

# OTTO asset bundle path (T3d) — only RvbAdapter.cpp consumes the macro to
# resolve a default IR path; the engine's public surface stays asset-path-free.
target_compile_definitions(IdaEngine PRIVATE
    IDA_OTTO_ASSETS_DIR="${OTTO_ASSETS_DIR}")

# Ida::Core is public (its types appear in the engine's headers); soxr and
# Ida::Persistence are private (implementation details of .cpp files that
# never leak into engine public headers — Persistence is used by
# InputMixer.cpp for the finalizeChannel → TapeStore::store flow).
# juce_audio_basics + juce_dsp are private: EqAdapter.cpp (T3a) drives
# OTTO's PlayerEQ via juce::AudioBuffer + juce::dsp::ProcessContextReplacing.
# They stay out of the engine's PUBLIC surface.
target_link_libraries(IdaEngine
    PUBLIC  Ida::Core
    PRIVATE juce::juce_core
            juce::juce_audio_basics
            juce::juce_dsp
            Ida::Persistence
            Ida::Host
            soxr
            lsfx::lsfx_tapecolor)

add_library(Ida::Engine ALIAS IdaEngine)
