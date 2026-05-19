# =============================================================================
# Sirius Looper — test fixtures
# =============================================================================
# Test-only artifacts that the SiriusTests binary depends on at run time.
# Currently a single fixture: SyntheticTestPlugin, a minimal CLAP plug-in
# used by the M7 S2+ round-trip tests for sirius_plugin_host.
#
# Bundle pattern lifted from external/clap/CMakeLists.txt's reference build
# of clap-plugin-template — same SUFFIX/BUNDLE/symbol-list shape, so a CLAP
# format change rebases cleanly across both sites.
# =============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_library(SyntheticTestPlugin MODULE
        SyntheticTestPlugin.cpp
        SyntheticTestPluginGui.mm)
    target_link_libraries(SyntheticTestPlugin PRIVATE
        clap
        "-framework AppKit"
        "-framework QuartzCore")
    # ARC disabled in the .mm because the synthetic plug-in uses manual
    # retain/release for the editor NSView so its lifetime is bracketed by
    # the CLAP create/destroy callbacks. AppKit objects allocated here are
    # released in guiDestroy.
    set_source_files_properties(SyntheticTestPluginGui.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc")
else()
    add_library(SyntheticTestPlugin MODULE
        SyntheticTestPlugin.cpp)
    target_link_libraries(SyntheticTestPlugin PRIVATE clap)
endif()

target_compile_features(SyntheticTestPlugin PRIVATE cxx_std_20)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS CLAP plug-ins are .clap bundles with an Info.plist; only the
    # `_clap_entry` symbol is exported so the host's dlsym lookup stays
    # unambiguous.
    target_link_options(SyntheticTestPlugin PRIVATE
        -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/SyntheticTestPlugin.symbols.txt)

    set_target_properties(SyntheticTestPlugin PROPERTIES
        BUNDLE                          TRUE
        BUNDLE_EXTENSION                clap
        MACOSX_BUNDLE_GUI_IDENTIFIER    com.sirius.synthetic.identity
        MACOSX_BUNDLE_BUNDLE_NAME       SyntheticTestPlugin
        MACOSX_BUNDLE_BUNDLE_VERSION    "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1"
        MACOSX_BUNDLE_INFO_PLIST        ${CMAKE_CURRENT_SOURCE_DIR}/SyntheticTestPlugin.plist.in)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(SyntheticTestPlugin PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/SyntheticTestPlugin.version
        -Wl,-z,defs)
    set_target_properties(SyntheticTestPlugin PROPERTIES
        SUFFIX ".clap"
        PREFIX "")
endif()

# =============================================================================
# StatefulSynthFixture — second synthetic CLAP for M8 S2 state-IPC tests.
# =============================================================================
# Shaped like a real third-party plug-in: distinct id + version, two params,
# per-instance internal state, and a real clap_plugin_state extension. No GUI
# .mm — audio + params + state only. Same bundle pattern as SyntheticTestPlugin.
add_library(StatefulSynthFixture MODULE StatefulSynthFixture.cpp)
target_link_libraries(StatefulSynthFixture PRIVATE clap)

target_compile_features(StatefulSynthFixture PRIVATE cxx_std_20)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_options(StatefulSynthFixture PRIVATE
        -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/StatefulSynthFixture.symbols.txt)
    set_target_properties(StatefulSynthFixture PROPERTIES
        BUNDLE                          TRUE
        BUNDLE_EXTENSION                clap
        MACOSX_BUNDLE_GUI_IDENTIFIER    com.sirius.synthetic.statefulsynth
        MACOSX_BUNDLE_BUNDLE_NAME       StatefulSynthFixture
        MACOSX_BUNDLE_BUNDLE_VERSION    "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1"
        MACOSX_BUNDLE_INFO_PLIST        ${CMAKE_CURRENT_SOURCE_DIR}/StatefulSynthFixture.plist.in)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(StatefulSynthFixture PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/StatefulSynthFixture.version
        -Wl,-z,defs)
    set_target_properties(StatefulSynthFixture PROPERTIES
        SUFFIX ".clap" PREFIX "")
endif()
