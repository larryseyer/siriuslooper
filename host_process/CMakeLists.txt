# =============================================================================
# sirius_plugin_host — the out-of-process plug-in host executable (M7 S1+)
# =============================================================================
# A standalone POSIX binary the engine spawns once per plug-in instance.
# Deliberately JUCE-free so its startup cost stays low and its link-time
# surface stays auditable: the engine launches one of these per hosted
# plug-in, so any link-time weight here multiplies.
#
# S1: identity-mode pass-through over stdin/stdout.
# S2: introduces `--mode clap --plugin-path <.clap>` which dlopens a CLAP
#     bundle. CLAP is header-only — the only link-time addition is `dl`
#     on Linux (`dlopen` is in libc on macOS).
# =============================================================================

add_executable(sirius_plugin_host
    main.cpp)

target_compile_features(sirius_plugin_host PRIVATE cxx_std_20)

target_link_libraries(sirius_plugin_host PRIVATE Sirius::Core clap)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sirius_plugin_host PRIVATE dl)
endif()
