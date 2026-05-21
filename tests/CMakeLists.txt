add_executable(SiriusTests
    RationalTests.cpp
    MeterTests.cpp
    TempoMapTests.cpp
    PositionTests.cpp
    TimeDomainTests.cpp
    ConstituentTests.cpp
    ConstituentStateTests.cpp
    ArrangementTests.cpp
    RoleResolverTests.cpp
    CaptureSessionTests.cpp
    PromotionTests.cpp
    InputDescriptorTests.cpp
    RepetitionRulesTests.cpp
    PhraseTests.cpp
    TapeTests.cpp
    LockFreeSpscQueueTests.cpp
    RetroactiveRingTests.cpp
    LmcTests.cpp
    SampleClockTests.cpp
    AudioDeviceCalibrationTests.cpp
    CalibrationRecoveryTests.cpp
    LatencyTimingTests.cpp
    LoopRendererTests.cpp
    AsrcTests.cpp
    RenderPipelineTests.cpp
    CapabilityTierTests.cpp
    DemoSessionTests.cpp
    OverloadProtectionTests.cpp
    SessionFormatTests.cpp
    TapeStoreTests.cpp
    WetCaptureWriterTests.cpp
    EffectChainTests.cpp
    ParameterAutomationTests.cpp
    PluginScannerTests.cpp
    UndoStackTests.cpp
    LatencyBudgetTests.cpp
    PerformanceViewStateTests.cpp
    PreparationViewStateTests.cpp
    TimelineViewStateTests.cpp
    LmcElectionTests.cpp
    SessionMergeTests.cpp
    TransportTests.cpp
    VideoTapeTests.cpp
    FrameMembraneTests.cpp
    AudioCallbackTests.cpp
    SignalTypeTests.cpp
    ChannelTests.cpp
    ChannelStripTests.cpp
    DirectLayerTests.cpp
    InputMixerTests.cpp
    OutputMixerTests.cpp
    BusTests.cpp
    ProcessingChainTests.cpp
    TapeWriterTests.cpp
    NotificationBusTests.cpp
    NotificationBusIntegrationTests.cpp
    OutOfProcessPluginTests.cpp
    OutOfProcessPluginIpcLatencyTests.cpp
    OutOfProcessPluginInstanceAudioThreadTests.cpp
    OutOfProcessEffectChainHostSlotStateTests.cpp
    OutOfProcessEffectChainHostSupervisorTests.cpp
    OutOfProcessEffectChainHostStateTests.cpp
    OutOfProcessEditorTests.cpp
    OutputMixerPluginHostIntegrationTests.cpp
    SharedMemorySpscQueueTests.cpp
    SharedMemoryRegionTests.cpp
    ArchivalModeTests.cpp
    SessionSnapshotLiveStateTests.cpp
    ClapBundleLoaderTests.cpp
    ClapScannerTests.cpp
    PluginStateRegionTests.cpp
    PluginStateExtensionTests.cpp
    StatefulSynthFixtureTests.cpp
    MixerGraphTests.cpp)

target_compile_features(SiriusTests PRIVATE cxx_std_20)

# Sirius::Engine links Sirius::Core publicly, so linking Engine here brings
# Core's headers and library along transitively — no need to list it twice.
target_link_libraries(SiriusTests PRIVATE
    Sirius::Engine
    Sirius::Audio
    Sirius::AppCore
    Sirius::Persistence
    Sirius::Host
    Sirius::Ui
    Sirius::Net
    Sirius::Video
    Catch2::Catch2WithMain
    Threads::Threads)

# Test fixtures — the SyntheticTestPlugin CLAP bundle that the M7 S2+
# round-trip tests load through sirius_plugin_host.
add_subdirectory(fixtures)

# M7 S1: OutOfProcessPluginTests needs the path of the sirius_plugin_host
# binary. CMake's $<TARGET_FILE:...> generator expression resolves to the
# binary's full path after the build configures it, so the test can find
# the binary regardless of multi-config-generator output-dir variation.
# The add_dependencies() ensures the host binary is built before the test
# executable runs, removing the "the test was run before the binary was
# built" race that would otherwise force every CI run to SKIP.
#
# M7 S2: same generator-expression trick for the SyntheticTestPlugin CLAP
# bundle. On macOS the target is a bundle ($<TARGET_BUNDLE_DIR:...> resolves
# to the .clap directory); on Linux $<TARGET_FILE:...> resolves to the
# SyntheticTestPlugin.clap shared library file.
target_compile_definitions(SiriusTests PRIVATE
    SIRIUS_PLUGIN_HOST_PATH="$<TARGET_FILE:sirius_plugin_host>"
    # M8 S2: ClapBundleLoaderTests' malformed-bundle case dlopens a real
    # Mach-O that lacks a `clap_entry` symbol. The bare sirius_plugin_host
    # executable ($<TARGET_FILE:...>) is exactly that — not the .app copy.
    SIRIUS_HOST_BINARY_PATH="$<TARGET_FILE:sirius_plugin_host>"
    $<$<PLATFORM_ID:Darwin>:SIRIUS_SYNTHETIC_CLAP_PATH="$<TARGET_BUNDLE_DIR:SyntheticTestPlugin>">
    $<$<NOT:$<PLATFORM_ID:Darwin>>:SIRIUS_SYNTHETIC_CLAP_PATH="$<TARGET_FILE:SyntheticTestPlugin>">
    # M8 S2: StatefulSynthFixture — second synthetic CLAP shaped like a real
    # third-party plug-in (params + real clap_plugin_state). Same generator-
    # expression trick: bundle dir on macOS, shared library file elsewhere.
    $<$<PLATFORM_ID:Darwin>:SIRIUS_STATEFUL_SYNTH_CLAP_PATH="$<TARGET_BUNDLE_DIR:StatefulSynthFixture>">
    $<$<NOT:$<PLATFORM_ID:Darwin>>:SIRIUS_STATEFUL_SYNTH_CLAP_PATH="$<TARGET_FILE:StatefulSynthFixture>">)
add_dependencies(SiriusTests sirius_plugin_host SyntheticTestPlugin StatefulSynthFixture)

# M8 S2: optional third-party CLAP integration test. Compiled into
# SiriusTests only when the operator supplies a real CLAP bundle path
# (FabFilter, Surge XT, etc.) at configure time. When unset (CI, default
# dev builds) the source is not compiled and the [third-party-clap] cases
# do not exist — silently absent. Mirrors the SIRIUS_SYNTHETIC_CLAP_PATH
# fixture convention above.
if(DEFINED SIRIUS_THIRDPARTY_CLAP_PATH)
    target_sources(SiriusTests PRIVATE
        ThirdPartyClapIntegrationTests.cpp)
    target_compile_definitions(SiriusTests PRIVATE
        SIRIUS_THIRDPARTY_CLAP_PATH="${SIRIUS_THIRDPARTY_CLAP_PATH}")
endif()

include(Catch)
catch_discover_tests(SiriusTests)

# =============================================================================
# MainComponentPluginEditorTests — separate executable (M7 S7)
# =============================================================================
# MainComponent.cpp is only compiled into the SiriusLooper GUI app target.
# Testing the MainComponent ↔ OutOfProcessEffectChainHost lifecycle requires
# compiling MainComponent.cpp again in a dedicated headless test binary so
# the test can construct a MainComponent without linking against SiriusLooper.
if(APPLE)
    add_executable(MainComponentPluginEditorTests
        MainComponentPluginEditorTests.cpp
        ${CMAKE_SOURCE_DIR}/app/MainComponent.cpp)

    target_compile_features(MainComponentPluginEditorTests PRIVATE cxx_std_20)

    target_compile_definitions(MainComponentPluginEditorTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        SIRIUS_PLUGIN_HOST_PATH="$<TARGET_FILE:sirius_plugin_host>"
        SIRIUS_SYNTHETIC_CLAP_PATH="$<TARGET_BUNDLE_DIR:SyntheticTestPlugin>")

    target_include_directories(MainComponentPluginEditorTests PRIVATE
        ${CMAKE_SOURCE_DIR}/app)

    target_link_libraries(MainComponentPluginEditorTests PRIVATE
        Sirius::Engine
        Sirius::Audio
        Sirius::AppCore
        Sirius::Persistence
        Sirius::Host
        Sirius::Ui
        Sirius::Net
        Sirius::Video
        juce::juce_audio_utils
        juce::juce_gui_extra
        Catch2::Catch2WithMain
        Threads::Threads)

    add_dependencies(MainComponentPluginEditorTests
        sirius_plugin_host SyntheticTestPlugin)

    catch_discover_tests(MainComponentPluginEditorTests)
endif()
