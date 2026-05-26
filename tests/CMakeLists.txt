add_executable(IdaTests
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
    TapePoolTests.cpp
    TapePoolMirrorTests.cpp
    PromotionTests.cpp
    FileInputDescriptorTests.cpp
    FileInputPlaylistTests.cpp
    FileInputRegistryTests.cpp
    FileInputSourceTests.cpp
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
    InternalFxIdTests.cpp
    MixerGraphStateTests.cpp
    MixerGraphPersistenceTests.cpp
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
    InputMixerTests.cpp
    OutputMixerTests.cpp
    BusTests.cpp
    BusProcessedTapTests.cpp
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
    OutOfProcessEffectChainHostInternalDispatchTests.cpp
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
    MixerGraphTests.cpp
    FlacTapeSinkTests.cpp
    # P7 T3a (internal-FX adapters — EQ first): adapter wrapper + factory.
    # EqAdapter lives in engine/src/fx/ (private to the engine target);
    # the include path below lets the test reach it.
    EqAdapterTests.cpp
    InternalFxFactoryTests.cpp
    # P7 T3b — CmpAdapter (PlayerCompressor wrapper). Same include-path
    # pattern as EqAdapterTests above.
    CmpAdapterTests.cpp
    # P7 T3c — DlyAdapter (PlayerDelay wrapper). Same include-path
    # pattern as EqAdapterTests above.
    DlyAdapterTests.cpp
    # P7 T3d — RvbAdapter (PlayerIRConvolution wrapper, the last of four
    # internal-FX adapters). Same include-path pattern as EqAdapterTests
    # above. Case #5 (IR-loaded smoke) polls the async load with a 5 s
    # timeout and SKIPs cleanly when assets are unavailable so CI stays
    # green without bundled OTTO assets.
    RvbAdapterTests.cpp
    # TAPECOLOR — 5th internal FX adapter (lsfx::TapeColorProcessor wrapper).
    # Slice 1 of the TAPECOLOR placement (2026-05-24 design lock): insert-
    # anywhere plumbing only; default-OFF semantics; param surface lands in
    # a follow-on slice. Same include-path pattern as EqAdapterTests above.
    TapeColorAdapterTests.cpp
    # TAPECOLOR Slice 2 — per-tape routing decorator. Sits between the
    # InputMixer's per-tape sum and the FlacTapeSink, applying TAPECOLOR
    # to tapes whose mode is BeforeWrite. AfterRead is downstream-only;
    # this decorator forwards it bit-identical on the write path.
    TapeColoringSinkTests.cpp
    # P7 T3a-C — end-to-end Bus → host → adapter dispatch test.
    BusInternalFxEndToEndTests.cpp
    # P7 T3a I-2 — end-to-end InputMixer → Bus → host → adapter dispatch.
    InputMixerInternalFxEndToEndTests.cpp
    # P7 T5 slice 4 — InsertChainPopup (ui/) callback-driven coverage.
    # JUCE Component is constructed inside ScopedJuceInitialiser_GUI; the
    # public simulate* test seams drive the callback contract without
    # painting pixels (pixel rendering is operator-eyes-on at slice 5).
    InsertChainPopupTests.cpp
    # V9 monitor slice — per-channel MON (Off/On) two-state toggle.
    #   MonitorModeLifecycleTests — Off ↔ On transitions create and remove
    #     the auto-minted OutputMixer channel that reads the input's post-
    #     strip stereo buffer (engine-level lifecycle).
    #   SessionFormatMonitorModeTests — round-trip + back-compat (missing
    #     field reads as Off, unknown token throws).
    InputMixerMonitorModeLifecycleTests.cpp
    SessionFormatMonitorModeTests.cpp
    # V9 conformance Slice 2 — per-channel post-strip buffer is the stable
    # pointer Slice 3 hands to OutputMixer::setChannelAudioSource(...) when
    # MON goes on. Pins: non-null while channel exists, null after remove,
    # pointer-stable across renderInputGraph blocks.
    InputMixerPostStripBufferTests.cpp
    # V9 conformance Slice 3 — MON on auto-creates an OutputMixer channel
    # whose audio source reads this input's post-strip buffer; MON off
    # removes it; idempotent re-entry; removeChannel cleans up too.
    InputMixerMonOutputChannelTests.cpp
    # Input-bus MON T8 — full JSON round-trip E2E sanity. Proves the
    # engine + persistence chain composes correctly under the actual
    # disk-shaped path MainComponent invokes at save/load: export →
    # serialize → deserialize → import. Mirror of the OutputMixer aux-bus
    # JSON round-trip added in commit 2da5459.
    InputMixerBusMonitorPersistenceTests.cpp
    # 2026-05-24 — pins the whitepaper-correct silence rule: an Output Mixer
    # phrase channel does NOT pipe live device input to master. Caught the
    # "M5 proxy" leak that bypassed the Input Mixer's Monitor button.
    OutputMixerPhraseChannelSilenceTests.cpp
    # V9 conformance Slice 6 — Constituent::withEffectChainClonedFrom is the
    # API surface for the whitepaper V9 §6.3.2 dry-tap FX-migration rule.
    # Pins value semantics (deep copy, empty source clears the chain).
    # Capture-path call site lands in M6+.
    ConstituentEffectChainCloneTests.cpp)

# T3a: EqAdapterTests includes "fx/EqAdapter.h" (engine-private header) and
# EqAdapter.h transitively includes <otto/effects/PlayerEQ.h>. Both paths
# stay PRIVATE — the engine's public include surface remains JUCE-free /
# OTTO-free, and only the test target reaches into the engine internals.
target_include_directories(IdaTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-core/include)

target_compile_features(IdaTests PRIVATE cxx_std_20)

# Ida::Engine links Ida::Core publicly, so linking Engine here brings
# Core's headers and library along transitively — no need to list it twice.
target_link_libraries(IdaTests PRIVATE
    Ida::Engine
    Ida::Audio
    Ida::AppCore
    Ida::Persistence
    Ida::Host
    Ida::Ui
    Ida::Net
    Ida::Video
    # T3a: EqAdapterTests transitively includes <otto/effects/PlayerEQ.h>
    # through "fx/EqAdapter.h", which pulls juce_dsp + juce_audio_basics.
    # Engine links them PRIVATE so they don't propagate; the test target
    # picks them up here.
    juce::juce_audio_basics
    juce::juce_dsp
    # TAPECOLOR Slice 1: TapeColorAdapterTests includes "fx/TapeColorAdapter.h"
    # (engine-private), which transitively #includes <lsfx_tapecolor/
    # lsfx_tapecolor.h>. Engine links lsfx_tapecolor PRIVATE — test target
    # picks it up here, same pattern as the OTTO includes above.
    lsfx::lsfx_tapecolor
    Catch2::Catch2WithMain
    Threads::Threads)

# Test fixtures — the SyntheticTestPlugin CLAP bundle that the M7 S2+
# round-trip tests load through ida_plugin_host.
add_subdirectory(fixtures)

# M7 S1: OutOfProcessPluginTests needs the path of the ida_plugin_host
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
target_compile_definitions(IdaTests PRIVATE
    IDA_PLUGIN_HOST_PATH="$<TARGET_FILE:ida_plugin_host>"
    # M8 S2: ClapBundleLoaderTests' malformed-bundle case dlopens a real
    # Mach-O that lacks a `clap_entry` symbol. The bare ida_plugin_host
    # executable ($<TARGET_FILE:...>) is exactly that — not the .app copy.
    IDA_HOST_BINARY_PATH="$<TARGET_FILE:ida_plugin_host>"
    $<$<PLATFORM_ID:Darwin>:IDA_SYNTHETIC_CLAP_PATH="$<TARGET_BUNDLE_DIR:SyntheticTestPlugin>">
    $<$<NOT:$<PLATFORM_ID:Darwin>>:IDA_SYNTHETIC_CLAP_PATH="$<TARGET_FILE:SyntheticTestPlugin>">
    # M8 S2: StatefulSynthFixture — second synthetic CLAP shaped like a real
    # third-party plug-in (params + real clap_plugin_state). Same generator-
    # expression trick: bundle dir on macOS, shared library file elsewhere.
    $<$<PLATFORM_ID:Darwin>:IDA_STATEFUL_SYNTH_CLAP_PATH="$<TARGET_BUNDLE_DIR:StatefulSynthFixture>">
    $<$<NOT:$<PLATFORM_ID:Darwin>>:IDA_STATEFUL_SYNTH_CLAP_PATH="$<TARGET_FILE:StatefulSynthFixture>">)
add_dependencies(IdaTests ida_plugin_host SyntheticTestPlugin StatefulSynthFixture)

# M8 S2: optional third-party CLAP integration test. Compiled into
# IdaTests only when the operator supplies a real CLAP bundle path
# (FabFilter, Surge XT, etc.) at configure time. When unset (CI, default
# dev builds) the source is not compiled and the [third-party-clap] cases
# do not exist — silently absent. Mirrors the IDA_SYNTHETIC_CLAP_PATH
# fixture convention above.
if(DEFINED IDA_THIRDPARTY_CLAP_PATH)
    target_sources(IdaTests PRIVATE
        ThirdPartyClapIntegrationTests.cpp)
    target_compile_definitions(IdaTests PRIVATE
        IDA_THIRDPARTY_CLAP_PATH="${IDA_THIRDPARTY_CLAP_PATH}")
endif()

include(Catch)
catch_discover_tests(IdaTests)

# =============================================================================
# MainComponentPluginEditorTests — separate executable (M7 S7)
# =============================================================================
# MainComponent.cpp is only compiled into the IDA GUI app target.
# Testing the MainComponent ↔ OutOfProcessEffectChainHost lifecycle requires
# compiling MainComponent.cpp again in a dedicated headless test binary so
# the test can construct a MainComponent without linking against IDA.
if(APPLE)
    add_executable(MainComponentPluginEditorTests
        MainComponentPluginEditorTests.cpp
        ${CMAKE_SOURCE_DIR}/app/MainComponent.cpp)

    target_compile_features(MainComponentPluginEditorTests PRIVATE cxx_std_20)

    target_compile_definitions(MainComponentPluginEditorTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        IDA_PLUGIN_HOST_PATH="$<TARGET_FILE:ida_plugin_host>"
        IDA_SYNTHETIC_CLAP_PATH="$<TARGET_BUNDLE_DIR:SyntheticTestPlugin>")

    target_include_directories(MainComponentPluginEditorTests PRIVATE
        ${CMAKE_SOURCE_DIR}/app)

    target_link_libraries(MainComponentPluginEditorTests PRIVATE
        Ida::Engine
        Ida::Audio
        Ida::AppCore
        Ida::Persistence
        Ida::Host
        Ida::Ui
        Ida::Net
        Ida::Video
        juce::juce_audio_utils
        juce::juce_gui_extra
        Catch2::Catch2WithMain
        Threads::Threads)

    add_dependencies(MainComponentPluginEditorTests
        ida_plugin_host SyntheticTestPlugin)

    catch_discover_tests(MainComponentPluginEditorTests)
endif()
