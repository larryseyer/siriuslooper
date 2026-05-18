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

add_library(SyntheticTestPlugin MODULE
    SyntheticTestPlugin.cpp)

target_link_libraries(SyntheticTestPlugin PRIVATE clap)

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
