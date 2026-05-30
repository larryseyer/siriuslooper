# TAPECOLOR A/B — standalone test plugin (VST3 / AU / Standalone) wrapping the
# shared lsfx::tapecolor DSP, so the operator can A/B asinh vs Jiles-Atherton on
# real music in Reaper. Opt-in: -DIDA_BUILD_TAPECOLOR_PLUGIN=ON. Not part of the
# shipping product; a throwaway experiment harness.

juce_add_plugin(TapeColorTestPlugin
    PRODUCT_NAME              "TAPECOLOR AB"
    COMPANY_NAME              "Larry Seyer"
    PLUGIN_MANUFACTURER_CODE  Lsyr
    PLUGIN_CODE               Tcab
    FORMATS                   VST3 AU Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    VST3_CATEGORIES           Fx
    AU_MAIN_TYPE              kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD   TRUE)

target_sources(TapeColorTestPlugin PRIVATE PluginProcessor.cpp)

target_compile_features(TapeColorTestPlugin PRIVATE cxx_std_20)

target_compile_definitions(TapeColorTestPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TapeColorTestPlugin PRIVATE
    lsfx::lsfx_tapecolor
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
