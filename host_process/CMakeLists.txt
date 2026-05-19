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

# M8 S2 — the CLAP load sequence (dlopen + entry init + factory walk) now
# lives in ClapBundleLoader, shared with the JUCE-dependent scanner via
# SiriusHost. ClapBundleLoader itself is JUCE-free (PluginDescriptor + clap
# + dlfcn only), so we compile its .cpp directly into this deliberately
# JUCE-free child binary rather than linking the JUCE-heavy SiriusHost lib.
set(sirius_clap_loader_src ${CMAKE_SOURCE_DIR}/host/src/ClapBundleLoader.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(sirius_plugin_host
        main.cpp
        ${sirius_clap_loader_src}
        gui_cocoa.mm)
    target_link_libraries(sirius_plugin_host PRIVATE
        Sirius::Core
        clap
        "-framework AppKit"
        "-framework Foundation")
    # M7 S9 — child owns its own top-level NSWindow; manual retain/release
    # in the .mm for the window + delegate lifetimes (no ARC, see the
    # docblock in gui_cocoa.mm).
    set_source_files_properties(gui_cocoa.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc")
else()
    add_executable(sirius_plugin_host
        main.cpp
        ${sirius_clap_loader_src})
    target_link_libraries(sirius_plugin_host PRIVATE Sirius::Core clap)
endif()

# ClapBundleLoader.h lives under host/include; the child needs it on the
# include path (Sirius::Core supplies PluginDescriptor.h transitively).
target_include_directories(sirius_plugin_host PRIVATE
    ${CMAKE_SOURCE_DIR}/host/include)

target_compile_features(sirius_plugin_host PRIVATE cxx_std_20)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sirius_plugin_host PRIVATE dl)
endif()
