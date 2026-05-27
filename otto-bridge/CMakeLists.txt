# =============================================================================
# IdaOttoBridge — the JUCE-coupled bridge layer between IDA and OTTO
# =============================================================================
# Public API is JUCE-free AND OTTO-free (pimpl), so app/engine code that links
# Ida::OttoBridge never transitively pulls JUCE or otto:: types into its own
# headers. The .cpp side owns one PlayerManager + TransportTracker per host
# instance and forwards lifecycle calls to OTTO.
#
# Layer placement decision (per docs/superpowers/specs/2026-05-26-otto-integration-
# scope-and-sequencing.md M-OTTO-2 risk note): this is a separate module rather
# than living in engine/ because engine's RT-safety contract (no allocation,
# no locks, no I/O) does not extend to OTTO — keeping the JUCE-coupled OTTO
# instance behind its own module localises the coupling to one .cpp file.
# =============================================================================

add_library(IdaOttoBridge STATIC
    src/OttoHost.cpp)

target_include_directories(IdaOttoBridge PUBLIC include)

target_compile_features(IdaOttoBridge PUBLIC cxx_std_20)

# otto::core gives access to PlayerManager + TransportTracker (header-only
# in OTTO's public headers; transitively pulls sfizz + Ableton::Link via
# otto-core's PUBLIC link surface). juce_audio_basics is needed at compile
# time because OTTO's PlayerManager.h transitively includes MasterBus.h
# which uses juce::AudioBuffer.
# M-OTTO-3 — Ida::Engine adds the `LockFreeSpscQueue<T>` public template header
# the OttoHost pimpl needs to marshal OTTO transport events from the audio
# thread to the message-thread drainer. Engine's PUBLIC headers are JUCE-free
# (juce_core is engine-PRIVATE per CLAUDE.md), so this link does not pollute
# OttoBridge's public surface. juce_events supplies juce::Timer for the
# drainer itself.
target_link_libraries(IdaOttoBridge
    # M-OTTO-4 slice 2: OttoHost now inherits from `ida::IOttoRenderSource`
    # (the audio-thread port the AudioCallback drives). The interface lives
    # in `core/` so AudioCallback can name it without depending on
    # OttoBridge. Inheritance forces the full type definition into
    # OttoHost.h, so Ida::Core moves from transitive-PRIVATE (via
    # Ida::Engine) to PUBLIC on IdaOttoBridge — consumers of OttoHost.h
    # need the IOttoRenderSource header on their include path.
    PUBLIC  Ida::Core
    PRIVATE otto::core
            juce::juce_audio_basics
            juce::juce_events
            lsfx::lsfx_tapecolor
            Ida::Engine)

add_library(Ida::OttoBridge ALIAS IdaOttoBridge)
