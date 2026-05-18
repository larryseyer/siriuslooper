# =============================================================================
# sirius_plugin_host — the out-of-process plug-in host executable (M7 S1)
# =============================================================================
# A standalone POSIX binary the engine spawns once per plug-in instance.
# Deliberately JUCE-free + dependency-free so its startup cost stays low
# and its link-time surface stays auditable: the engine launches one of
# these per hosted plug-in, so any link-time weight here multiplies.
#
# S1: identity-mode pass-through over stdin/stdout. No CLAP SDK yet (S2).
# S2: replaces stdin/stdout with POSIX shared-memory + SPSC rings.
# =============================================================================

add_executable(sirius_plugin_host
    main.cpp)

target_compile_features(sirius_plugin_host PRIVATE cxx_std_20)
