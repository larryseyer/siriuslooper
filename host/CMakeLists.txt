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
    src/ClapBundleLoader.cpp
    src/ClapScanner.cpp
    src/PluginScanner.cpp
    src/GenericParameterView.cpp
    src/OutOfProcessPluginInstance.cpp
    src/OutOfProcessEffectChainHost.cpp)

# M7 S9 (Reaper-style separate plug-in windows): the engine no longer
# embeds the child's editor pixels via CARemoteLayer; each
# sirius_plugin_host child opens its own top-level NSWindow. No
# engine-side AppKit shim is needed — the entire cross-process editor
# view scaffolding from M7 S5-S8 (OutOfProcessEditorView, PluginGuiBridge,
# IGuiBridge, xpc_service/) is gone.

target_include_directories(SiriusHost PUBLIC include)

target_compile_features(SiriusHost PUBLIC cxx_std_20)

# JUCE_PLUGINHOST_VST3 / _AU are off by default; turn them on so the format
# manager registers them when the platform supports them.
target_compile_definitions(SiriusHost PUBLIC
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)

target_link_libraries(SiriusHost
    PUBLIC  Sirius::Core
            clap
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# JUCE's AU hosting code (juce_audio_processors.mm) references AUGenericView,
# which lives in CoreAudioKit. juce_add_gui_app would add this framework on
# our behalf; a plain static library does not, so we add it explicitly when
# linking against this lib on Apple platforms.
if(APPLE)
    target_link_libraries(SiriusHost PUBLIC
        "-framework CoreAudioKit")
endif()

add_library(Sirius::Host ALIAS SiriusHost)
