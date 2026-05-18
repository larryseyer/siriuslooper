# =============================================================================
# SiriusHost — plugin hosting and the generic parameter view
# =============================================================================
# This is the runtime half of M5: the JUCE-dependent wrappers that scan and
# instantiate VST3/AU plugins (PluginScanner) and the minimal plugin-format-
# agnostic UI (GenericParameterView) the milestone test needs in order to
# confirm parameter automation round-trips through a parameter tape.
#
# The structural side of M5 — PluginDescriptor, EffectChain, ParameterEvent —
# is in core/, JUCE-free. This library depends on it via Sirius::Core.
# =============================================================================

add_library(SiriusHost STATIC
    src/PluginScanner.cpp
    src/GenericParameterView.cpp
    src/OutOfProcessPluginInstance.cpp
    src/OutOfProcessEffectChainHost.cpp
    src/OutOfProcessEditorView.cpp
    src/PluginGuiBridge.cpp)

# M7 S5 — macOS GUI embedding bridge. AppKit / QuartzCore lives behind a
# C-linkage shim in OutOfProcessEditorView.mm so the .cpp body stays plain
# C++. Manual retain/release for the NSView + CALayerHost (-fno-objc-arc)
# matches the editor-lifecycle bracketing in OutOfProcessEditorView.cpp.
if(APPLE)
    target_sources(SiriusHost PRIVATE src/OutOfProcessEditorView.mm)
    set_source_files_properties(src/OutOfProcessEditorView.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc")
endif()

target_include_directories(SiriusHost PUBLIC include)

target_compile_features(SiriusHost PUBLIC cxx_std_20)

# JUCE_PLUGINHOST_VST3 / _AU are off by default; turn them on so the format
# manager registers them when the platform supports them.
target_compile_definitions(SiriusHost PUBLIC
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)

target_link_libraries(SiriusHost
    PUBLIC  Sirius::Core
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# JUCE's AU hosting code (juce_audio_processors.mm) references AUGenericView,
# which lives in CoreAudioKit. juce_add_gui_app would add this framework on
# our behalf; a plain static library does not, so we add it explicitly when
# linking against this lib on Apple platforms. M7 S5 adds QuartzCore for
# the CALayerHost-based cross-process editor embedding.
if(APPLE)
    target_link_libraries(SiriusHost PUBLIC
        "-framework CoreAudioKit"
        "-framework QuartzCore")
endif()

add_library(Sirius::Host ALIAS SiriusHost)
