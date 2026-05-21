# =============================================================================
# SiriusAudio — the JUCE audio-device bridge into the engine
# =============================================================================
# The engine layer (Sirius::Engine) is deliberately JUCE-free so its real-time
# primitives stay exhaustively unit-testable from the headless harness. This
# library is the thin layer the engine comment in `engine/CMakeLists.txt`
# references: JUCE's AudioIODeviceCallback turned into one class
# (`sirius::AudioCallback`) that drives the engine, gated by an explicit
# monitoring toggle and bound to a single EngineConfig.
#
# Anything that needs to touch the audio thread from inside the standalone
# app — the app shell, future automated GUI tests — links Sirius::Audio.
# The engine library never links juce_audio_devices.
# =============================================================================

add_library(SiriusAudio STATIC
    src/AudioCallback.cpp)

target_include_directories(SiriusAudio PUBLIC include)

target_compile_features(SiriusAudio PUBLIC cxx_std_20)

# JUCE's audio-device headers are part of the public surface (AudioCallback.h
# inherits juce::AudioIODeviceCallback), so juce_audio_devices is PUBLIC.
target_link_libraries(SiriusAudio
    PUBLIC  Sirius::Engine
            juce::juce_audio_devices
            juce::juce_audio_formats)

add_library(Sirius::Audio ALIAS SiriusAudio)
