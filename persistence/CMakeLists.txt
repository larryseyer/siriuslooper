# =============================================================================
# SiriusPersistence — session serialization and content-addressed tape store
# =============================================================================
# This layer reads and writes session directories (white paper Part 7.8):
# kilobytes-of-JSON for the structure layer (SessionFormat), content-addressed
# blob files for the data layer (TapeStore). It depends on Sirius::Core for the
# domain types and on juce_core only — never the audio or GUI modules — so it
# stays exercisable in the headless test harness with file I/O, JSON, and
# cryptographic hashing all coming from juce_core.
# =============================================================================

add_library(SiriusPersistence STATIC
    src/SessionFormat.cpp
    src/TapeStore.cpp)

target_include_directories(SiriusPersistence PUBLIC include)

target_compile_features(SiriusPersistence PUBLIC cxx_std_20)

target_link_libraries(SiriusPersistence
    PUBLIC  Sirius::Core
            juce::juce_core
            juce::juce_cryptography
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

add_library(Sirius::Persistence ALIAS SiriusPersistence)
