# =============================================================================
# IdaAudio — the JUCE audio-device bridge into the engine
# =============================================================================
# The engine layer (Ida::Engine) is deliberately JUCE-free so its real-time
# primitives stay exhaustively unit-testable from the headless harness. This
# library is the thin layer the engine comment in `engine/CMakeLists.txt`
# references: JUCE's AudioIODeviceCallback turned into one class
# (`ida::AudioCallback`) that drives the engine, gated by an explicit
# monitoring toggle and bound to a single EngineConfig.
#
# Anything that needs to touch the audio thread from inside the standalone
# app — the app shell, future automated GUI tests — links Ida::Audio.
# The engine library never links juce_audio_devices.
# =============================================================================

add_library(IdaAudio STATIC
    src/AudioCallback.cpp
    src/FileInputRegistry.cpp
    src/FileInputSource.cpp
    src/FlacTapeSink.cpp)

target_include_directories(IdaAudio PUBLIC include)

target_compile_features(IdaAudio PUBLIC cxx_std_20)

# JUCE's audio-device headers are part of the public surface (AudioCallback.h
# inherits juce::AudioIODeviceCallback), so juce_audio_devices is PUBLIC.
target_link_libraries(IdaAudio
    PUBLIC  Ida::Engine
            juce::juce_audio_devices
            juce::juce_audio_formats)

add_library(Ida::Audio ALIAS IdaAudio)
